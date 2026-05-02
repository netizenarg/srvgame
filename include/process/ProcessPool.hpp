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
#include <nlohmann/json.hpp>

#include "logging/Logger.hpp"
#include "config/ConfigManager.hpp"

class ProcessWorker : public std::enable_shared_from_this<ProcessWorker> {
public:
    ProcessWorker(asio::io_context& io, int globalId, const WorkerGroupConfig& cfg);
    ~ProcessWorker();
    void Start();
    void Send(const std::string& message);
    void Shutdown();
    int GetId() const;
    pid_t GetPid() const;
    int GetMasterReadFd() const;

private:
    int workerId_;
    WorkerGroupConfig config_;
    asio::io_context& io_;
    pid_t pid_;
    int masterReadFd_;
    int masterWriteFd_;
    asio::posix::stream_descriptor writeStream_;
    std::mutex writeMutex_;
};

class ProcessPool : public std::enable_shared_from_this<ProcessPool> {
public:
    ProcessPool(asio::io_context& io, const std::vector<WorkerGroupConfig>& groups);
    void Initialize();
    void Run();
    void Shutdown();
    void SetWorkerMain(std::function<void(int, const WorkerGroupConfig&, int)> func);
    bool SendToWorker(int workerId, const std::string& message);
    void BroadcastToOtherWorkers(const nlohmann::json& msg, int senderId);
    void BroadcastToAllWorkers(const nlohmann::json& msg);
    size_t GetTotalWorkerCount() const;
    bool IsWorkerAlive(int workerId) const;
    bool IsWorkersReady() const;
    void WaitForWorkers();

private:
    asio::io_context& io_;
    std::vector<WorkerGroupConfig> groups_;
    std::vector<std::shared_ptr<ProcessWorker>> workers_;
    std::function<void(int, const WorkerGroupConfig&, int)> workerMain_;
    asio::signal_set signals_;
    int workerId_ = -1;
    bool running_ = false;
    std::atomic<bool> ready_{false};
    void doSpawnWorkers();
    void handleSignal(const asio::error_code& ec, int signo);
};
