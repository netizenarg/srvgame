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
#include <asio/local/connect_pair.hpp>
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

private:
    int workerId_;
    WorkerGroupConfig config_;
    asio::io_context& io_;
    pid_t pid_;
    int masterReadFd_;
    int masterWriteFd_;
    int masterFd_;
    asio::posix::stream_descriptor masterStream_;
    std::mutex writeMutex_;
};

class ProcessPool : public std::enable_shared_from_this<ProcessPool> {
public:
    ProcessPool(asio::io_context& io, const std::vector<WorkerGroupConfig>& groups);
    void Initialize();
    void Run();
    void Shutdown();
    void SetWorker(std::function<void(int, const WorkerGroupConfig&, int)> func);
    bool SendToWorker(int workerId, const std::vector<uint8_t>& message);
    void BroadcastToOtherWorkers(const nlohmann::json& msg, int senderId);
    void BroadcastToAllWorkers(const nlohmann::json& msg);
    size_t GetTotalWorkerCount() const;
    bool IsWorkerAlive(int workerId) const;
    bool IsWorkersReady() const;
    void WaitForWorkers();
    void SetMasterMessageHandler(ProcessWorker::MasterMessageHandler handler);
    bool SendReplyToWorker(int workerId, uint32_t correlationId, const std::vector<uint8_t>& binaryData);

private:
    asio::io_context& io_;
    std::vector<WorkerGroupConfig> groups_;
    std::vector<std::shared_ptr<ProcessWorker>> workers_;
    std::function<void(int, const WorkerGroupConfig&, int)> worker_;
    asio::signal_set signals_;
    int workerId_ = -1;
    bool running_ = false;
    std::atomic<bool> ready_{false};
    ProcessWorker::MasterMessageHandler masterHandler_;
    void doSpawnWorkers();
    void handleSignal(const asio::error_code& ec, int signo);
    void StartReadingFromWorker(std::shared_ptr<ProcessWorker> worker);
};
