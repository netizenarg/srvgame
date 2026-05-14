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
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        sendQueue_.push_back(std::move(frame));
    }
    sendCv_.notify_one();
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

void ProcessWorker::writerLoop() {
    while (true) {
        Logger::Trace("ProcessWorker::writerLoop ID {}", workerId_);
        std::vector<uint8_t> data;
        {
            std::unique_lock<std::mutex> lock(sendMutex_);
            sendCv_.wait(lock, [this] {return !writerRunning_ || !sendQueue_.empty();});
            if (!writerRunning_ && sendQueue_.empty()) return;
            data = std::move(sendQueue_.front());
            sendQueue_.pop_front();
        }
        Logger::Trace("ProcessWorker::writerLoop {} writing {} bytes", workerId_, data.size());
        std::error_code ec;
        asio::write(masterStream_, asio::buffer(data), ec);
        Logger::Trace("ProcessWorker::writerLoop {} write completed: ec = {}", workerId_, ec.message());
        if (ec) {
            Logger::Error("ProcessWorker::writerLoop for worker {} failed: {}", workerId_, ec.message());
            break;
        }
    }
}

void ProcessWorker::StartWriterThread() {
    writerThread_ = std::thread(&ProcessWorker::writerLoop, this);
}

void ProcessWorker::StopWriter() {
    writerRunning_ = false;
    sendCv_.notify_all();
}

void ProcessWorker::JoinWriterThread() {
    if (writerThread_.joinable())
        writerThread_.join();
}

ProcessPool::ProcessPool(asio::io_context& io, const std::vector<WorkerGroupConfig>& groups)
    : io_(io), groups_(groups) {}

void ProcessPool::Initialize() {
    doSpawnWorkers();
    for (auto& w : workers_) {
        StartReadingFromWorker(w);
    }
    ready_.store(true);
    running_.store(true);
}

void ProcessPool::Run() { io_.run(); }

void ProcessPool::Shutdown() {
    if (!running_.exchange(false)) return;
    Logger::Trace("ProcessPool::Shutdown: running...");
    for (auto& w : workers_) w->Shutdown();
    Logger::Trace("ProcessPool::Shutdown: workers_->Shutdown() finished");
    for (auto& w : workers_) w->StopWriter();
    Logger::Trace("ProcessPool::Shutdown: workers_->StopWriter() finished");
    for (auto& pair : readerRunningFlags_) *pair.second = false;
    Logger::Trace("ProcessPool::Shutdown: readerRunningFlags_ finished");
    io_.stop();
    Logger::Trace("ProcessPool::Shutdown: io_.stop finished");
    for (auto& w : workers_) w->JoinWriterThread();
    Logger::Trace("ProcessPool::Shutdown: workers_->JoinWriterThread() finished");
}

void ProcessPool::SetWorker(std::function<void(int, const WorkerGroupConfig&, int)> func) {
    worker_ = std::move(func);
}

void ProcessPool::SetMasterMessageHandler(ProcessWorker::MasterMessageHandler handler) {
    masterHandler_ = std::move(handler);
}

void ProcessPool::StartReadingFromWorker(std::shared_ptr<ProcessWorker> worker) {
    auto running = std::make_shared<std::atomic<bool>>(true);
    readerRunningFlags_[worker->GetId()] = running;
    asio::co_spawn(io_, [this, worker, running]() -> asio::awaitable<void> {
        auto& stream = worker->GetMasterStream();
        while (running->load())
        {
            uint32_t netLen = 0;
            std::error_code ec;
            co_await asio::async_read(stream,
                                      asio::buffer(&netLen, sizeof(netLen)),
                                      asio::redirect_error(asio::use_awaitable, ec));
            if (ec == asio::error::eof || ec == asio::error::connection_reset)
                break;
            if (ec) {
                Logger::Error("ProcessPool::StartReadingFromWorker: ID {} read error: {}", worker->GetId(), ec.message());
                break;
            }
            uint32_t msgLen = ntohl(netLen);
            if (msgLen == 0 || msgLen > BinaryProtocol::MAX_MESSAGE_SIZE)
                continue;
            std::vector<uint8_t> msg(msgLen);
            co_await asio::async_read(stream,
                                      asio::buffer(msg),
                                      asio::redirect_error(asio::use_awaitable, ec));
            if (ec) {
                Logger::Error("ProcessPool::StartReadingFromWorker: ID {} message read error: {}", worker->GetId(), ec.message());
                break;
            }
            BinaryProtocol::BinaryReader r(msg.data(), msg.size());
            uint32_t corrId   = r.ReadUInt32();
            uint64_t sessionId = r.ReadUInt64();
            uint16_t msgType  = r.ReadUInt16();
            uint32_t bodyLen  = r.ReadUInt32();
            std::vector<uint8_t> body;
            if (bodyLen > 0 && r.Remaining() >= bodyLen)
                body = r.ReadBytes(bodyLen);
            if (masterHandler_)
                masterHandler_(worker->GetId(), corrId, sessionId, msgType, std::move(body));
        }
        Logger::Trace("ProcessPool::StartReadingFromWorker coroutine finished ID {}", worker->GetId());
    }, asio::detached);
}

bool ProcessPool::SendToWorker(int workerId, const std::vector<uint8_t>& message) {
    if (workerId >= 0 && workerId < static_cast<int>(workers_.size())) {
        workers_[workerId]->SendAsync(message);
        return true;
    }
    return false;
}

void ProcessPool::BroadcastToOtherWorkers(const std::vector<uint8_t>& msg, int owner_id) {
    BinaryProtocol::BinaryWriter w;
    w.WriteUInt32(0); // correlationId = 0 (broadcast)
    w.WriteUInt64(0); // sessionId = 0 → worker‑side broadcast
    w.WriteUInt16(0);
    w.WriteUInt32(static_cast<uint32_t>(msg.size()));
    w.WriteRaw(msg.data(), msg.size());
    auto frame = w.GetBuffer();
    for (auto& w : workers_)
        if (w->GetId() != owner_id)
            w->SendAsync(frame);
}

void ProcessPool::BroadcastToAllWorkers(const std::vector<uint8_t>& msg) {
    BinaryProtocol::BinaryWriter w;
    w.WriteUInt32(0); // correlationId = 0 (broadcast)
    w.WriteUInt64(0); // sessionId = 0 → worker‑side broadcast
    w.WriteUInt16(0);
    w.WriteUInt32(static_cast<uint32_t>(msg.size()));
    w.WriteRaw(msg.data(), msg.size());
    auto frame = w.GetBuffer();
    for (auto& worker : workers_) {
        worker->SendAsync(frame);
    }
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
                    Logger::Trace("ProcessPool::doSpawnWorkers: worker {} exiting cleanly", globalId);
                    _exit(0);
                } else {
                    Logger::Error("ProcessPool::doSpawnWorkers: worker start failed: {}", err.what());
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
    w.WriteUInt64(0);
    w.WriteUInt16(0);
    w.WriteUInt32(static_cast<uint32_t>(binaryData.size()));
    w.WriteRaw(binaryData.data(), binaryData.size());
    return SendToWorker(workerId, w.GetBuffer());
}

bool ProcessPool::PushToWorker(int workerId, uint64_t sessionId, const std::vector<uint8_t>& binaryData) {
    if (workerId < 0 || workerId >= static_cast<int>(workers_.size()))
        return false;
    BinaryProtocol::BinaryWriter w;
    w.WriteUInt32(0);           // corrId = 0  → push marker
    w.WriteUInt64(sessionId);   // target session
    w.WriteUInt16(0);           // unused
    w.WriteUInt32(static_cast<uint32_t>(binaryData.size()));
    w.WriteRaw(binaryData.data(), binaryData.size());
    workers_[workerId]->SendAsync(w.GetBuffer());
    return true;
}
