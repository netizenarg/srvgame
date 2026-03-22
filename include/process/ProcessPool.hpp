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
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <signal.h>

#include "logging/Logger.hpp"
#include "config/ConfigManager.hpp"  // for WorkerGroupConfig

class ProcessPool {
public:
    enum class ProcessRole {
        MASTER,
        WORKER
    };

    // New constructor: takes a list of worker groups
    ProcessPool(const std::vector<WorkerGroupConfig>& groups);
    ~ProcessPool();

    ProcessPool(const ProcessPool&) = delete;
    ProcessPool& operator=(const ProcessPool&) = delete;

    bool Initialize();
    void Run();
    void Shutdown();
    void Stop();

    ProcessRole GetRole() const { return role_; }
    int GetWorkerId() const { return workerId_; }           // global ID (0..total-1)
    const WorkerGroupConfig& GetWorkerGroupConfig() const { return groupConfig_; }
    pid_t GetMasterPid() const { return masterPid_; }

    // Callback for worker process – now receives both global ID and group config
    using WorkerMainFunc = std::function<void(int workerId, const WorkerGroupConfig& config)>;
    void SetWorkerMain(WorkerMainFunc workerMainFunc);

    // Inter-process communication with message length prefix
    bool SendToWorker(int workerId, const std::string& message);
    std::string ReceiveFromMaster();

    // Process health monitoring
    bool IsWorkerAlive(int workerId) const;
    void RestartWorker(int workerId);

    // Configuration methods
    void SetMaxMessageSize(uint32_t maxSize) { maxMessageSize_ = maxSize; }
    uint32_t GetMaxMessageSize() const { return maxMessageSize_; }
    void SetReceiveTimeout(uint32_t timeoutMs) { receiveTimeoutMs_ = timeoutMs; }
    uint32_t GetReceiveTimeout() const { return receiveTimeoutMs_; }

    void BlockSignals(sigset_t* oldset);
    void UnblockSignals(const sigset_t* oldset);

private:
    struct WorkerInfo {
        pid_t pid;
        int groupIdx;           // index in groups_ vector
        int localWorkerId;      // index within the group (0..group.count-1)
        WorkerGroupConfig config;
    };

    void MasterProcess();
    void WorkerProcess(int globalWorkerId, const WorkerGroupConfig& config);
    void SetupSignalHandlers();
    void CleanupDeadWorkers();
    void CloseAllPipes();
    void CreateWorkerPipe(int globalWorkerId);

    // Helper functions for message protocol
    bool WriteAll(int fd, const void* buffer, size_t count);
    bool ReadAll(int fd, void* buffer, size_t count, bool nonBlocking = false);
    bool DrainPipe(int fd, size_t bytesToDrain);

    // Group configuration
    std::vector<WorkerGroupConfig> groups_;
    int totalWorkers_;

    ProcessRole role_{ProcessRole::MASTER};
    int workerId_{-1};                     // global ID (for worker)
    WorkerGroupConfig groupConfig_;        // config for this worker (filled after fork)
    pid_t masterPid_{-1};

    std::thread masterThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdownRequested_{false};

    std::vector<WorkerInfo> workers_;      // indexed by global worker ID
    std::vector<int> workerPipes_;         // [read_fd, write_fd] for each global worker

    WorkerMainFunc workerMainFunc_;

    // Health monitoring (keyed by global worker ID)
    mutable std::mutex healthMutex_;
    std::unordered_map<int, std::pair<pid_t, time_t>> workerHealth_;

    // Message protocol configuration
    uint32_t maxMessageSize_{1024 * 1024};  // 1MB default
    uint32_t receiveTimeoutMs_{1000};       // 1 second default
};