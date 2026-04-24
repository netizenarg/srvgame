#pragma once

#include <vector>
#include <memory>
#include <atomic>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <string>
#include <unistd.h>
#include <cstdint>
#include <thread>
#include <fcntl.h>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <chrono>
#include <array>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <signal.h>

#include "logging/Logger.hpp"
#include "config/ConfigManager.hpp"

class ProcessPool {
public:
    enum class ProcessRole {
        MASTER,
        WORKER
    };

    ProcessPool(const std::vector<WorkerGroupConfig>& groups);
    ~ProcessPool();

    ProcessPool(const ProcessPool&) = delete;
    ProcessPool& operator=(const ProcessPool&) = delete;

    bool Initialize();
    void Run();
    void Shutdown();
    void Stop();

    ProcessRole GetRole() const { return role_; }
    int GetWorkerId() const { return workerId_; }
    const WorkerGroupConfig& GetWorkerGroupConfig() const { return groupConfig_; }
    pid_t GetMasterPid() const { return masterPid_; }
    int GetTotalWorkerCount() const { return totalWorkers_; }

    using WorkerMainFunc = std::function<void(int workerId, const WorkerGroupConfig& config)>;
    void SetWorkerMain(WorkerMainFunc workerMainFunc);

    bool SendToWorker(int workerId, const std::string& message);
    std::string ReceiveFromMaster();

    bool IsWorkerAlive(int workerId) const;
    void RestartWorker(int workerId);

    void SetMaxMessageSize(uint32_t maxSize) { maxMessageSize_ = maxSize; }
    uint32_t GetMaxMessageSize() const { return maxMessageSize_; }
    void SetReceiveTimeout(uint32_t timeoutMs) { receiveTimeoutMs_ = timeoutMs; }
    uint32_t GetReceiveTimeout() const { return receiveTimeoutMs_; }

    void BlockSignals(sigset_t* oldset);
    void UnblockSignals(const sigset_t* oldset);

private:
    struct WorkerInfo {
        pid_t pid;
        int groupIdx;
        int localWorkerId;
        WorkerGroupConfig config;
    };

    void MasterProcess();
    void WorkerProcess(int globalWorkerId, const WorkerGroupConfig& config);
    void SetupSignalHandlers();
    void CleanupDeadWorkers();
    void CloseAllPipes();
    void CreateWorkerPipe(int globalWorkerId);

    bool WriteAll(int fd, const void* buffer, size_t count);
    bool ReadAll(int fd, void* buffer, size_t count, bool nonBlocking = false);
    bool DrainPipe(int fd, size_t bytesToDrain);

    std::vector<WorkerGroupConfig> groups_;
    int totalWorkers_;

    ProcessRole role_{ProcessRole::MASTER};
    int workerId_{-1};
    WorkerGroupConfig groupConfig_;
    pid_t masterPid_{-1};

    std::thread masterThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdownRequested_{false};

    std::vector<WorkerInfo> workers_;
    std::vector<int> workerPipes_;

    WorkerMainFunc workerMainFunc_;

    mutable std::mutex healthMutex_;
    std::unordered_map<int, std::pair<pid_t, time_t>> workerHealth_;

    uint32_t maxMessageSize_{1024 * 1024};
    uint32_t receiveTimeoutMs_{1000};
};
