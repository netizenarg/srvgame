#pragma once

#include <vector>
#include <memory>
#include <atomic>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <cstdint>
#include <thread>

class ProcessPool {
public:
    enum class ProcessRole {
        MASTER,
        WORKER
    };

    ProcessPool(int numProcesses);
    ~ProcessPool();

    ProcessPool(const ProcessPool&) = delete;
    ProcessPool& operator=(const ProcessPool&) = delete;

    bool Initialize();
    void Run();
    void Shutdown();
    void Stop();

    ProcessRole GetRole() const { return role_; }
    int GetWorkerId() const { return workerId_; }
    pid_t GetMasterPid() const { return masterPid_; }

    // Callback for worker process
    void SetWorkerMain(std::function<void(int workerId)> workerMain);

    // Inter-process communication
    bool SendToWorker(int workerId, const std::string& message);
    std::string ReceiveFromMaster();

    // Process health monitoring
    bool IsWorkerAlive(int workerId) const;
    void RestartWorker(int workerId);

private:
    void MasterProcess();
    void WorkerProcess(int workerId);
    void SetupSignalHandlers();
    void CleanupDeadWorkers();
    void CloseAllPipes();
    void CreateWorkerPipe(int workerId);

    int numProcesses_;
    ProcessRole role_{ProcessRole::MASTER};
    int workerId_{-1};
    pid_t masterPid_{-1};
    
    std::thread masterThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdownRequested_{false};

    std::vector<pid_t> workerPids_;
    std::function<void(int workerId)> workerMain_;

    // IPC mechanisms
    std::vector<int> workerPipes_;  // [read_fd, write_fd] for each worker

    // Health monitoring
    mutable std::mutex healthMutex_;
    std::unordered_map<int, std::pair<pid_t, time_t>> workerHealth_;
};