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
#include "process/IPCChannel.hpp"

enum class WorkerType { Child, GameLogic };

class ProcessWorker : public std::enable_shared_from_this<ProcessWorker> {
public:
    ProcessWorker(asio::io_context& io, int globalId, const WorkerGroupConfig& cfg, WorkerType type = WorkerType::Child);
    ~ProcessWorker();
    void Start();
    void Shutdown();
    int GetId() const;
    pid_t GetPid() const;
    WorkerType GetType() const;

    std::shared_ptr<IPCChannel> GetChannel() { return channel_; }
    int GetMasterFd() const { return masterFd_; }

    using MasterMessageHandler = std::function<void(
        int workerId,
        uint32_t correlationId,
        uint64_t sessionId,
        uint16_t messageType,
        const std::vector<uint8_t>& body
    )>;

private:
    int workerId_;
    WorkerGroupConfig config_;
    WorkerType type_;
    asio::io_context& io_;
    pid_t pid_;
    int masterFd_ = -1;
    std::shared_ptr<IPCChannel> channel_;
};

class ProcessPool {
public:
    ProcessPool(asio::io_context& io, const std::vector<WorkerGroupConfig>& groups);
    void Initialize();
    void Run();
    void Shutdown();

    void SetWorker(std::function<void(int, const WorkerGroupConfig&, int)> func);
    void SetGameLogicWorker(std::function<void(int, int)> func);

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

    int SpawnGameLogicWorker();
    bool RespawnGameLogicWorker();
    bool IsGameLogicWorkerAlive() const;
    std::shared_ptr<IPCChannel> GetGameLogicChannel() const;

private:
    asio::io_context& io_;
    std::vector<WorkerGroupConfig> groups_;
    std::vector<std::shared_ptr<ProcessWorker>> workers_;
    std::function<void(int, const WorkerGroupConfig&, int)> worker_;
    std::function<void(int, int)> gameLogicWorkerFunc_;
    int gameLogicWorkerId_ = -1;
    std::atomic<bool> running_{false};
    std::atomic<bool> ready_{false};
    ProcessWorker::MasterMessageHandler masterHandler_;

    void doSpawnWorkers();
    void onWorkerMessage(int workerId, const IPCEnvelope& env);
    void onGameLogicMessage(const IPCEnvelope& env);
};
