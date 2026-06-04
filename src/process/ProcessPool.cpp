#include "process/ProcessPool.hpp"

ProcessWorker::ProcessWorker(asio::io_context& io, int globalId, const WorkerGroupConfig& cfg, WorkerType type)
    : workerId_(globalId), config_(cfg), type_(type), io_(io), pid_(-1) {}

ProcessWorker::~ProcessWorker() { Shutdown(); }

int ProcessWorker::GetId() const { return workerId_; }
pid_t ProcessWorker::GetPid() const { return pid_; }
WorkerType ProcessWorker::GetType() const { return type_; }

void ProcessWorker::Start() {
    asio::local::stream_protocol::socket socket0(io_);
    asio::local::stream_protocol::socket socket1(io_);
    asio::local::connect_pair(socket0, socket1);
    pid_ = fork();
    if (pid_ == 0) {
        socket0.close();
        masterFd_ = socket1.native_handle();
        socket1.release();
        io_.notify_fork(asio::execution_context::fork_event::fork_child);
        throw std::runtime_error("worker_function");
    } else if (pid_ > 0) {
        socket1.close();
        masterFd_ = socket0.native_handle();
        socket0.release();
        channel_ = std::make_shared<IPCChannel>(io_, masterFd_);
        io_.notify_fork(asio::execution_context::fork_event::fork_parent);
    } else {
        socket0.close();
        socket1.close();
        throw std::runtime_error("fork failed");
    }
}

void ProcessWorker::Shutdown() {
    if (pid_ > 0) {
        kill(pid_, SIGTERM);
        int status = 0;
        pid_t result = 0;
        for (int i = 0; i < 40; ++i) {
            result = waitpid(pid_, &status, WNOHANG);
            if (result == pid_) break;
            if (result < 0 && errno != EINTR) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (result != pid_) {
            kill(pid_, SIGKILL);
            waitpid(pid_, nullptr, 0);
        }
    }
    if (channel_) channel_->Stop();
    if (masterFd_ != -1) close(masterFd_);
    masterFd_ = -1;
    pid_ = -1;
}

ProcessPool::ProcessPool(asio::io_context& io, const std::vector<WorkerGroupConfig>& groups)
    : io_(io), groups_(groups) {}

void ProcessPool::Initialize() {
    doSpawnWorkers();
    ready_.store(true);
    running_.store(true);
}

void ProcessPool::Run() { io_.run(); }

void ProcessPool::Shutdown() {
    if (!running_.exchange(false)) return;
    Logger::Trace("ProcessPool::Shutdown: running...");
    for (auto& w : workers_) w->Shutdown();
    Logger::Trace("ProcessPool::Shutdown: workers shutdown finished");
    io_.stop();
    Logger::Trace("ProcessPool::Shutdown: io_.stop finished");
}

void ProcessPool::SetWorker(std::function<void(int, const WorkerGroupConfig&, int)> func) {
    worker_ = std::move(func);
}

void ProcessPool::SetGameLogicWorker(std::function<void(int, int)> func) {
    gameLogicWorkerFunc_ = std::move(func);
}

void ProcessPool::SetMasterMessageHandler(ProcessWorker::MasterMessageHandler handler) {
    masterHandler_ = std::move(handler);
}

void ProcessPool::onWorkerMessage(int workerId, const IPCEnvelope& env) {
    if (masterHandler_) {
        masterHandler_(workerId, env.correlationId, env.sessionId, env.messageType, env.payload);
    }
}

void ProcessPool::onGameLogicMessage(const IPCEnvelope& env) {
    if (masterHandler_) {
        masterHandler_(gameLogicWorkerId_, env.correlationId, env.sessionId, env.messageType, env.payload);
    }
}

void ProcessPool::doSpawnWorkers() {
    int globalId = 0;
    for (size_t gi = 0; gi < groups_.size(); ++gi) {
        for (int i = 0; i < groups_[gi].count; ++i, ++globalId) {
            auto worker = std::make_shared<ProcessWorker>(io_, globalId, groups_[gi], WorkerType::Child);
            try {
                worker->Start();
                workers_.push_back(worker);
                worker->GetChannel()->Start([this, globalId](const IPCEnvelope& env) {
                    onWorkerMessage(globalId, env);
                });
            } catch (const std::exception& err) {
                if (std::string(err.what()) == "worker_function") {
                    int fd = worker->GetMasterFd();
                    worker_(globalId, groups_[gi], fd);
                    Logger::Trace("ProcessPool::doSpawnWorkers: worker {} exiting cleanly", globalId);
                    _exit(0);
                } else {
                    Logger::Error("ProcessPool::doSpawnWorkers: worker start failed: {}", err.what());
                }
            }
        }
    }
}

int ProcessPool::SpawnGameLogicWorker() {
    int globalId = static_cast<int>(workers_.size());
    WorkerGroupConfig emptyCfg;
    emptyCfg.protocol = "internal";
    emptyCfg.host = "127.0.0.1";
    emptyCfg.port = 0;
    emptyCfg.count = 1;
    emptyCfg.threads = 1;
    auto worker = std::make_shared<ProcessWorker>(io_, globalId, emptyCfg, WorkerType::GameLogic);
    try {
        worker->Start();
        workers_.push_back(worker);
        gameLogicWorkerId_ = globalId;
        worker->GetChannel()->Start([this](const IPCEnvelope& env) {
            onGameLogicMessage(env);
        });
        return globalId;
    } catch (const std::exception& err) {
        if (std::string(err.what()) == "worker_function") {
            int fd = worker->GetMasterFd();
            if (gameLogicWorkerFunc_) {
                gameLogicWorkerFunc_(globalId, fd);
            }
            _exit(0);
        } else {
            Logger::Error("ProcessPool::SpawnGameLogicWorker failed: {}", err.what());
        }
    }
    return -1;
}

bool ProcessPool::RespawnGameLogicWorker() {
    if (gameLogicWorkerId_ >= 0 && gameLogicWorkerId_ < static_cast<int>(workers_.size())) {
        auto& old = workers_[gameLogicWorkerId_];
        old->Shutdown();
    }
    int newId = SpawnGameLogicWorker();
    return newId >= 0;
}

bool ProcessPool::IsGameLogicWorkerAlive() const {
    if (gameLogicWorkerId_ < 0 || gameLogicWorkerId_ >= static_cast<int>(workers_.size())) return false;
    pid_t pid = workers_[gameLogicWorkerId_]->GetPid();
    if (pid <= 0) return false;
    return (kill(pid, 0) == 0);
}

std::shared_ptr<IPCChannel> ProcessPool::GetGameLogicChannel() const {
    if (gameLogicWorkerId_ >= 0 && gameLogicWorkerId_ < static_cast<int>(workers_.size())) {
        return workers_[gameLogicWorkerId_]->GetChannel();
    }
    return nullptr;
}

bool ProcessPool::SendToWorker(int workerId, const std::vector<uint8_t>& message) {
    if (workerId >= 0 && workerId < static_cast<int>(workers_.size())) {
        auto channel = workers_[workerId]->GetChannel();
        if (channel && channel->IsOpen()) {
            IPCEnvelope env;
            env.correlationId = 0;
            env.sessionId = 0;
            env.messageType = 0;
            env.payload = message;
            channel->SendAsync(env);
            return true;
        }
    }
    return false;
}

void ProcessPool::BroadcastToOtherWorkers(const std::vector<uint8_t>& msg, int owner_id) {
    IPCEnvelope env;
    env.correlationId = 0;
    env.sessionId = 0;
    env.messageType = 0;
    env.payload = msg;
    for (auto& w : workers_) {
        if (w->GetId() != owner_id && w->GetType() == WorkerType::Child) {
            auto channel = w->GetChannel();
            if (channel && channel->IsOpen()) {
                channel->SendAsync(env);
            }
        }
    }
}

void ProcessPool::BroadcastToAllWorkers(const std::vector<uint8_t>& msg) {
    IPCEnvelope env;
    env.correlationId = 0;
    env.sessionId = 0;
    env.messageType = 0;
    env.payload = msg;
    for (auto& w : workers_) {
        if (w->GetType() == WorkerType::Child) {
            auto channel = w->GetChannel();
            if (channel && channel->IsOpen()) {
                channel->SendAsync(env);
            }
        }
    }
}

size_t ProcessPool::GetTotalWorkerCount() const { return workers_.size(); }

bool ProcessPool::IsWorkerAlive(int workerId) const {
    if (workerId < 0 || workerId >= static_cast<int>(workers_.size())) return false;
    pid_t pid = workers_[workerId]->GetPid();
    if (pid <= 0) return false;
    return (kill(pid, 0) == 0);
}

bool ProcessPool::IsWorkersReady() const { return ready_; }
void ProcessPool::WaitForWorkers() {}

bool ProcessPool::SendReplyToWorker(int workerId, uint32_t correlationId, const std::vector<uint8_t>& binaryData) {
    if (workerId < 0 || workerId >= static_cast<int>(workers_.size())) return false;
    auto channel = workers_[workerId]->GetChannel();
    if (!channel || !channel->IsOpen()) return false;
    IPCEnvelope env;
    env.correlationId = correlationId;
    env.sessionId = 0;
    env.messageType = 0;
    env.payload = binaryData;
    channel->SendAsync(env);
    return true;
}

bool ProcessPool::PushToWorker(int workerId, uint64_t sessionId, const std::vector<uint8_t>& binaryData) {
    if (workerId < 0 || workerId >= static_cast<int>(workers_.size())) return false;
    auto channel = workers_[workerId]->GetChannel();
    if (!channel || !channel->IsOpen()) return false;
    IPCEnvelope env;
    env.correlationId = 0;
    env.sessionId = sessionId;
    env.messageType = 0;
    env.payload = binaryData;
    channel->SendAsync(env);
    return true;
}
