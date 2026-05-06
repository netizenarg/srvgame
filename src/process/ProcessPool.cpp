#include "process/ProcessPool.hpp"

//extern std::atomic<bool> g_shutdown;

ProcessWorker::ProcessWorker(asio::io_context& io, int globalId, const WorkerGroupConfig& cfg)
    : workerId_(globalId), config_(cfg), io_(io), pid_(-1),
      masterReadFd_(-1), masterWriteFd_(-1), masterStream_(io_) {}

ProcessWorker::~ProcessWorker() { Shutdown(); }

int ProcessWorker::GetId() const { return workerId_; }
pid_t ProcessWorker::GetPid() const { return pid_; }
int ProcessWorker::GetMasterReadFd() const { return masterReadFd_; }
asio::posix::stream_descriptor& ProcessWorker::GetMasterStream() { return masterStream_; }

void ProcessWorker::Start() {
    asio::local::stream_protocol::socket socket0(io_);
    asio::local::stream_protocol::socket socket1(io_);
    asio::local::connect_pair(socket0, socket1);
    pid_ = fork();
    if (pid_ == 0) {
        socket0.close();
        masterFd_ = socket1.native_handle();
        socket1.release();
        masterReadFd_ = masterFd_;
        io_.notify_fork(asio::execution_context::fork_event::fork_child);
        throw std::runtime_error("worker_function");
    } else if (pid_ > 0) {
        socket1.close();
        masterFd_ = socket0.native_handle();
        socket0.release();
        masterStream_.assign(masterFd_);
        io_.notify_fork(asio::execution_context::fork_event::fork_parent);
    } else {
        socket0.close();
        socket1.close();
        throw std::runtime_error("fork failed");
    }
}

void ProcessWorker::Send(const std::vector<uint8_t>& binaryData) {
    uint32_t len = htonl(static_cast<uint32_t>(binaryData.size()));
    std::vector<asio::const_buffer> buffers;
    buffers.emplace_back(&len, sizeof(len));
    buffers.emplace_back(binaryData.data(), binaryData.size());
    asio::write(masterStream_, buffers);
}

void ProcessWorker::SendAsync(const std::vector<uint8_t>& binaryData) {
    uint32_t len = htonl(static_cast<uint32_t>(binaryData.size()));
    std::vector<uint8_t> frame(sizeof(len) + binaryData.size());
    memcpy(frame.data(), &len, sizeof(len));
    memcpy(frame.data() + sizeof(len), binaryData.data(), binaryData.size());
    bool startWriting = false;
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        sendQueue_.push_back(std::move(frame));
        if (!writing_) {
            writing_ = true;
            startWriting = true;
        }
    }
    if (startWriting) {
        doWrite();
    }
}

void ProcessWorker::doWrite() {
    auto self = shared_from_this();
    std::vector<uint8_t> data;
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        if (sendQueue_.empty()) {
            writing_ = false;
            return;
        }
        data = std::move(sendQueue_.front());
        sendQueue_.pop_front();
    }
    asio::async_write(masterStream_, asio::buffer(data),
    [self, data](std::error_code ec, size_t /*bytes*/) {
        if (ec) {
            Logger::Error("Master async_write to worker {} failed: {}",
                            self->workerId_, ec.message());
        }
        self->doWrite();
    });
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
    if (masterReadFd_ != -1) close(masterReadFd_);
    if (masterWriteFd_ != -1) close(masterWriteFd_);
    masterReadFd_ = masterWriteFd_ = -1;
    pid_ = -1;
}

ProcessPool::ProcessPool(asio::io_context& io, const std::vector<WorkerGroupConfig>& groups)
    : io_(io), groups_(groups) {}

void ProcessPool::Initialize() {
    doSpawnWorkers();
    for (auto& w : workers_) {
        StartReadingFromWorker(w);
    }
    ready_ = true;
    running_ = true;
}

void ProcessPool::Run() { io_.run(); }

void ProcessPool::Shutdown() {
    if (!running_) return;
    running_ = false;
    for (auto& w : workers_) w->Shutdown();
}

void ProcessPool::SetWorker(std::function<void(int, const WorkerGroupConfig&, int)> func) {
    worker_ = std::move(func);
}

void ProcessPool::SetMasterMessageHandler(ProcessWorker::MasterMessageHandler handler) {
    masterHandler_ = std::move(handler);
}

void ProcessPool::StartReadingFromWorker(std::shared_ptr<ProcessWorker> worker) {
    auto& stream = worker->GetMasterStream();
    auto header = std::make_shared<std::vector<uint8_t>>(14);
    asio::async_read(stream, asio::buffer(*header), [this, worker, header, &stream](std::error_code ec, size_t) {
        if (!ec) {
            BinaryProtocol::BinaryReader r(header->data(), header->size());
            uint32_t corrId = r.ReadUInt32();
            uint64_t sessionId = r.ReadUInt64();
            uint16_t msgType = r.ReadUInt16();
            uint32_t bodyLen = r.ReadUInt32();
            if (bodyLen > 0) {
                auto body = std::make_shared<std::vector<uint8_t>>(bodyLen);
                asio::async_read(stream, asio::buffer(*body), [this, worker, corrId, sessionId, msgType, body, &stream](std::error_code ec2, size_t) {
                    if (!ec2 && masterHandler_)
                        masterHandler_(worker->GetId(), corrId, sessionId, msgType, std::move(*body));
                    StartReadingFromWorker(worker);
                });
            } else {
                if (masterHandler_)
                    masterHandler_(worker->GetId(), corrId, sessionId, msgType, {});
                StartReadingFromWorker(worker);
            }
        } else {
            if (ec != asio::error::operation_aborted)
                Logger::Error("Error reading from worker {}: {}", worker->GetId(), ec.message());
        }
    });
}

bool ProcessPool::SendToWorker(int workerId, const std::vector<uint8_t>& message) {
    if (workerId >= 0 && workerId < static_cast<int>(workers_.size())) {
        workers_[workerId]->SendAsync(message);
        return true;
    }
    return false;
}

void ProcessPool::BroadcastToOtherWorkers(const nlohmann::json& msg, int senderId) {
    std::string serialized = msg.dump();
    std::vector<uint8_t> data(serialized.begin(), serialized.end());
    for (auto& w : workers_)
        if (w->GetId() != senderId) w->SendAsync(data);
}

void ProcessPool::BroadcastToAllWorkers(const nlohmann::json& msg) {
    std::string serialized = msg.dump();
    std::vector<uint8_t> data(serialized.begin(), serialized.end());
    for (auto& w : workers_) w->SendAsync(data);
}

void ProcessPool::doSpawnWorkers() {
    int globalId = 0;
    for (size_t gi = 0; gi < groups_.size(); ++gi) {
        for (int i = 0; i < groups_[gi].count; ++i, ++globalId) {
            auto worker = std::make_shared<ProcessWorker>(io_, globalId, groups_[gi]);
            try {
                worker->Start();
                workers_.push_back(worker);
            } catch (const std::exception& err) {
                if (std::string(err.what()) == "worker_function") {
                    int fd = worker->GetMasterReadFd();
                    worker_(globalId, groups_[gi], fd);
                    _exit(0);
                } else {
                    Logger::Error("Worker start failed: {}", err.what());
                }
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
    BinaryProtocol::BinaryWriter w;
    w.WriteUInt32(correlationId);
    w.WriteBytes(binaryData.data(), binaryData.size());
    return SendToWorker(workerId, w.GetBuffer());
}
