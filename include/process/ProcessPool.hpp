#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <unistd.h>

#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <asio/detached.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read_until.hpp>
#include <asio/redirect_error.hpp>
#include <asio/signal_set.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>
#include <asio/local/connect_pair.hpp>

using asio::ip::tcp;
using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::redirect_error;
using asio::use_awaitable;

#include <nlohmann/json.hpp>

#include "logging/Logger.hpp"
#include "config/ConfigManager.hpp"
#include "network/BinaryProtocol.hpp"


struct IPCEnvelope {
    uint32_t correlationId;
    uint64_t sessionId;
    uint16_t messageType;
    std::vector<uint8_t> payload;
};

class ProcessWorker : public std::enable_shared_from_this<ProcessWorker> {
public:
    ProcessWorker(asio::io_context& io, int globalId, const WorkerGroupConfig& cfg);
    ~ProcessWorker();
    void Start();
    void Send(const std::vector<uint8_t>& binaryData);
    void Shutdown();
    int GetId() const;
    pid_t GetPid() const;
    int GetMasterReadFd() const;
    using MasterMessageHandler = std::function<void(
        int workerId,
        uint32_t correlationId,
        uint64_t sessionId,
        uint16_t messageType,
        const std::vector<uint8_t>& body
    )>;
    asio::posix::stream_descriptor& GetMasterStream();
    void SendAsync(const std::vector<uint8_t>& binaryData);
    // void StartWriterThread();
    // void StopWriter();
    // void JoinWriterThread();
    //Delete writerLoop(), StartWriterThread(), StopWriter(), JoinWriterThread(), and the writerRunning_ flag

private:
    int workerId_;
    WorkerGroupConfig config_;
    asio::io_context& io_;
    pid_t pid_;
    int masterReadFd_;
    int masterWriteFd_;
    int masterFd_;
    asio::posix::stream_descriptor masterStream_;
    std::deque<std::vector<uint8_t>> write_queue_;
    std::mutex write_mutex_;
    std::condition_variable sendCv_;
    bool writing_ = false;
    //std::thread writerThread_;
    //bool writerRunning_{true};
    //void writerLoop();
    void doWrite();
};

class ProcessPool : public std::enable_shared_from_this<ProcessPool> {
public:
    ProcessPool(asio::io_context& io, const std::vector<WorkerGroupConfig>& groups);
    void Initialize();
    void Run();
    void Shutdown();
    void SetWorker(std::function<void(int, const WorkerGroupConfig&, int)> func);
    bool SendToWorker(int workerId, const std::vector<uint8_t>& message);
    void BroadcastToOtherWorkers(const std::vector<uint8_t>& msg, int owner_id);
    void BroadcastToAllWorkers(const std::vector<uint8_t>& msg);
    size_t GetTotalWorkerCount() const;
    bool IsWorkerAlive(int workerId) const;
    bool IsWorkersReady() const;
    void WaitForWorkers();
    void SetMasterMessageHandler(ProcessWorker::MasterMessageHandler handler);
    bool SendReplyToWorker(int workerId, uint32_t correlationId, const std::vector<uint8_t>& binaryData);
    bool PushToWorker(int workerId, uint64_t sessionId, const std::vector<uint8_t>& binaryData);

private:
    asio::io_context& io_;
    std::vector<WorkerGroupConfig> groups_;
    std::vector<std::shared_ptr<ProcessWorker>> workers_;
    std::function<void(int, const WorkerGroupConfig&, int)> worker_;
    int workerId_ = -1;
    std::atomic<bool> running_{false};
    std::atomic<bool> ready_{false};
    ProcessWorker::MasterMessageHandler masterHandler_;
    std::vector<std::thread> readerThreads_;
    std::unordered_map<int, std::shared_ptr<std::atomic<bool>>> readerRunningFlags_;
    void doSpawnWorkers();
    void StartReadingFromWorker(std::shared_ptr<ProcessWorker> worker);
};
