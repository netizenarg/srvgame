#include "process/ProcessPool.hpp"

extern std::atomic<bool> g_shutdown;

ProcessWorker::ProcessWorker(asio::io_context& io, int globalId, const WorkerGroupConfig& cfg)
    : workerId_(globalId), config_(cfg), io_(io), pid_(-1),
      masterReadFd_(-1), masterWriteFd_(-1),
      writeStream_(io) {}

ProcessWorker::~ProcessWorker() { Shutdown(); }

int ProcessWorker::GetId() const { return workerId_; }
pid_t ProcessWorker::GetPid() const { return pid_; }
int ProcessWorker::GetMasterReadFd() const { return masterReadFd_; }

void ProcessWorker::Start() {
    int pipeParentToChild[2];
    if (pipe(pipeParentToChild) == -1) throw std::runtime_error("pipe failed");
    if (fcntl(pipeParentToChild[0], F_SETFL, O_NONBLOCK) < 0 ||
        fcntl(pipeParentToChild[1], F_SETFL, O_NONBLOCK) < 0) {
        close(pipeParentToChild[0]); close(pipeParentToChild[1]);
        throw std::runtime_error("fcntl failed");
    }
    pid_ = fork();
    if (pid_ == 0) {
        close(pipeParentToChild[1]);
        masterReadFd_ = pipeParentToChild[0];
        prctl(PR_SET_NAME, ("game_worker_"+std::to_string(workerId_)).c_str());
        io_.notify_fork(asio::execution_context::fork_event::fork_child);
        throw std::runtime_error("worker_function");
    } else if (pid_ > 0) {
        close(pipeParentToChild[0]);
        masterWriteFd_ = pipeParentToChild[1];
        writeStream_.assign(masterWriteFd_);
    } else {
        close(pipeParentToChild[0]); close(pipeParentToChild[1]);
        throw std::runtime_error("fork failed");
    }
}

void ProcessWorker::Send(const std::string& message) {
    std::lock_guard<std::mutex> lock(writeMutex_);
    uint32_t len = htonl(static_cast<uint32_t>(message.size()));
    std::vector<asio::const_buffer> buffers;
    buffers.emplace_back(&len, sizeof(len));
    buffers.emplace_back(message.data(), message.size());
    asio::error_code ec;
    asio::write(writeStream_, buffers, ec);
    if (ec) Logger::Error("Worker write error {}", ec.message());
}

void ProcessWorker::Shutdown() {
    if (pid_ > 0) {
        kill(pid_, SIGTERM);
        waitpid(pid_, nullptr, 0);
    }
    if (masterReadFd_ != -1) close(masterReadFd_);
    if (masterWriteFd_ != -1) close(masterWriteFd_);
    masterReadFd_ = masterWriteFd_ = -1;
    pid_ = -1;
}

ProcessPool::ProcessPool(asio::io_context& io, const std::vector<WorkerGroupConfig>& groups)
    : io_(io), groups_(groups), signals_(io) {}

void ProcessPool::Initialize() {
    signals_.add(SIGINT);
    signals_.add(SIGTERM);
    signals_.async_wait([this](const asio::error_code& ec, int signo) {
        handleSignal(ec, signo);
    });
    doSpawnWorkers();
    ready_ = true;
    running_ = true;
}

void ProcessPool::Run() { io_.run(); }

void ProcessPool::Shutdown() {
    if (!running_) return;
    running_ = false;
    signals_.cancel();
    for (auto& w : workers_) w->Shutdown();
}

void ProcessPool::SetWorkerMain(std::function<void(int, const WorkerGroupConfig&, int)> func) {
    workerMain_ = std::move(func);
}

bool ProcessPool::SendToWorker(int workerId, const std::string& message) {
    if (workerId >= 0 && workerId < static_cast<int>(workers_.size())) {
        workers_[workerId]->Send(message);
        return true;
    }
    return false;
}

void ProcessPool::BroadcastToOtherWorkers(const nlohmann::json& msg, int senderId) {
    std::string serialized = msg.dump();
    for (auto& w : workers_)
        if (w->GetId() != senderId) w->Send(serialized);
}

void ProcessPool::BroadcastToAllWorkers(const nlohmann::json& msg) {
    std::string serialized = msg.dump();
    for (auto& w : workers_) w->Send(serialized);
}

void ProcessPool::doSpawnWorkers() {
    int globalId = 0;
    for (size_t gi = 0; gi < groups_.size(); ++gi) {
        for (int i = 0; i < groups_[gi].count; ++i, ++globalId) {
            auto worker = std::make_shared<ProcessWorker>(io_, globalId, groups_[gi]);
            try {
                worker->Start();
                workers_.push_back(worker);
            } catch (const std::exception& e) {
                if (std::string(e.what()) == "worker_function") {
                    int fd = worker->GetMasterReadFd();
                    workerMain_(globalId, groups_[gi], fd);
                    _exit(0);
                } else {
                    Logger::Error("Worker start failed: {}", e.what());
                }
            }
        }
    }
}

void ProcessPool::handleSignal(const asio::error_code& ec, int signo) {
    if (ec) return;
    Logger::Info("Master received signal {}", signo);
    g_shutdown.store(true);
    Shutdown();
    io_.stop();
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
