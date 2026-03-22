#include "process/ProcessPool.hpp"

extern std::atomic<bool> g_shutdown;

ProcessPool::ProcessPool(const std::vector<WorkerGroupConfig>& groups)
    : groups_(groups)
{
    // Compute total number of workers
    totalWorkers_ = 0;
    for (const auto& g : groups_) {
        totalWorkers_ += g.count;
    }

    // Pre-allocate vectors
    workers_.resize(totalWorkers_);
    workerPipes_.resize(totalWorkers_ * 2, -1);
}

ProcessPool::~ProcessPool() {
    Shutdown();
}

bool ProcessPool::Initialize() {
    if (totalWorkers_ <= 0) {
        Logger::Error("No workers configured");
        return false;
    }

    masterPid_ = getpid();
    SetupSignalHandlers();
    running_.store(true);
    shutdownRequested_.store(false);

    // Create pipes for each worker
    for (int i = 0; i < totalWorkers_; ++i) {
        CreateWorkerPipe(i);
    }

    return true;
}

void ProcessPool::CreateWorkerPipe(int globalWorkerId) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        Logger::Error("Failed to create pipe for worker {}: {}", globalWorkerId, strerror(errno));
        return;
    }

    int read_idx = globalWorkerId * 2;
    int write_idx = globalWorkerId * 2 + 1;

    if (workerPipes_[read_idx] != -1) close(workerPipes_[read_idx]);
    if (workerPipes_[write_idx] != -1) close(workerPipes_[write_idx]);

    workerPipes_[read_idx] = pipefd[0];
    workerPipes_[write_idx] = pipefd[1];

    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    fcntl(pipefd[1], F_SETFL, O_NONBLOCK);
}

void ProcessPool::Run() {
    if (!Initialize()) {
        Logger::Critical("Failed to initialize process pool");
        return;
    }

    if (getpid() == masterPid_) {
        masterThread_ = std::thread(&ProcessPool::MasterProcess, this);
    } else {
        // Worker process: we need to know which global worker ID and group config we have.
        // This is set in the child before calling Run(), but here we need to retrieve it.
        // We'll set a member variable in the child branch before calling Run.
        // However, the constructor only runs in the master; for workers, we need to
        // set these after fork. The child branch of MasterProcess will set them.
        // So this branch should never be executed directly because we call WorkerProcess
        // explicitly from the child after fork.
        Logger::Error("Worker process started without proper initialization");
    }
}

void ProcessPool::MasterProcess() {
    Logger::Info("Master process started (PID: {})", getpid());

    sigset_t oldset;
    BlockSignals(&oldset);

    // Spawn workers in order of groups
    int globalWorkerId = 0;
    for (size_t gidx = 0; gidx < groups_.size(); ++gidx) {
        const auto& group = groups_[gidx];
        for (int w = 0; w < group.count; ++w, ++globalWorkerId) {
            pid_t pid = fork();

            if (pid == 0) {
                // Child process
                signal(SIGINT, SIG_IGN);
                // SIGTERM handler: sets the global shutdown flag (async‑safe)
                signal(SIGTERM, [](int) { g_shutdown.store(true, std::memory_order_relaxed); });

                UnblockSignals(&oldset);

                // Permanently block SIGINT only (SIGTERM remains unblocked)
                sigset_t block_int;
                sigemptyset(&block_int);
                sigaddset(&block_int, SIGINT);
                pthread_sigmask(SIG_BLOCK, &block_int, nullptr);

                // Close all pipe ends except the one for this worker
                for (int i = 0; i < totalWorkers_ * 2; ++i) {
                    if (i != globalWorkerId * 2) {
                        if (workerPipes_[i] != -1) close(workerPipes_[i]);
                    }
                }

                // Set worker process name
                std::string processName = "game_worker_" + std::to_string(globalWorkerId);
                prctl(PR_SET_NAME, processName.c_str(), 0, 0, 0);

                // Store config for this worker
                groupConfig_ = group;

                Logger::Info("Worker {} started (PID: {}) for group {} ({}:{})",
                             globalWorkerId, getpid(), gidx, group.protocol, group.port);

                WorkerProcess(globalWorkerId, group);

                // Cleanup: close read end of pipe
                if (workerPipes_[globalWorkerId * 2] != -1)
                    close(workerPipes_[globalWorkerId * 2]);
                _exit(0);

            } else if (pid > 0) {
                // Master: record worker info
                workers_[globalWorkerId] = {pid, static_cast<int>(gidx), w, group};
                workerPids_[globalWorkerId] = pid; // we don't have workerPids_ anymore? Actually we use workers_ vector.

                // Close the read end in master
                if (workerPipes_[globalWorkerId * 2] != -1) {
                    close(workerPipes_[globalWorkerId * 2]);
                    workerPipes_[globalWorkerId * 2] = -1;
                }

                // Health tracking
                {
                    std::lock_guard<std::mutex> lock(healthMutex_);
                    workerHealth_[globalWorkerId] = {pid, time(nullptr)};
                }

                Logger::Info("Worker {} started with PID: {}", globalWorkerId, pid);
            } else {
                Logger::Error("Failed to fork worker {}: {}", globalWorkerId, strerror(errno));
            }
        }
    }

    UnblockSignals(&oldset);

    // Main loop: monitor workers and check for shutdown
    while (running_.load() && !shutdownRequested_.load()) {
        CleanupDeadWorkers();
        for (int i = 0; i < 10 && running_.load() && !shutdownRequested_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // Send SIGTERM to all workers
    for (int i = 0; i < totalWorkers_; ++i) {
        if (workers_[i].pid > 0) {
            Logger::Info("Terminating worker {} (PID: {})", i, workers_[i].pid);
            kill(workers_[i].pid, SIGTERM);
        }
    }

    // Wait for all workers to exit
    int status;
    for (int i = 0; i < totalWorkers_; ++i) {
        if (workers_[i].pid > 0) {
            waitpid(workers_[i].pid, &status, 0);
            Logger::Info("Worker {} exited with status: {}", i, status);
        }
    }

    CloseAllPipes();
    Logger::Info("Master process shutdown complete");
}

void ProcessPool::WorkerProcess(int globalWorkerId, const WorkerGroupConfig& config) {
    this->workerId_ = globalWorkerId;
    this->groupConfig_ = config;

    if (workerMainFunc_) {
        workerMainFunc_(globalWorkerId, config);
    }
}

void ProcessPool::CleanupDeadWorkers() {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < totalWorkers_; ++i) {
            if (workers_[i].pid == pid) {
                Logger::Warn("Worker {} (PID: {}) died with status {}, {}...",
                             i, pid, WEXITSTATUS(status),
                             shutdownRequested_.load() ? "not restarting (shutdown)" : "restarting");

                int write_idx = i * 2 + 1;
                if (workerPipes_[write_idx] != -1) {
                    close(workerPipes_[write_idx]);
                    workerPipes_[write_idx] = -1;
                }

                if (!shutdownRequested_.load()) {
                    RestartWorker(i);
                } else {
                    std::lock_guard<std::mutex> lock(healthMutex_);
                    workerHealth_.erase(i);
                }
                break;
            }
        }
    }
}

void ProcessPool::RestartWorker(int globalWorkerId) {
    // Find which group this worker belongs to
    const auto& oldInfo = workers_[globalWorkerId];
    const auto& group = oldInfo.config;

    // Create new pipe for this worker
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        Logger::Error("Failed to create pipe for restarting worker {}: {}", globalWorkerId, strerror(errno));
        return;
    }

    int old_read = workerPipes_[globalWorkerId * 2];
    int old_write = workerPipes_[globalWorkerId * 2 + 1];
    workerPipes_[globalWorkerId * 2] = pipefd[0];
    workerPipes_[globalWorkerId * 2 + 1] = pipefd[1];

    sigset_t oldset;
    BlockSignals(&oldset);

    pid_t pid = fork();

    if (pid == 0) {
        // Child
        signal(SIGINT, SIG_IGN);
        signal(SIGTERM, [](int) { g_shutdown.store(true, std::memory_order_relaxed); });

        UnblockSignals(&oldset);

        // Block SIGINT permanently
        sigset_t block_int;
        sigemptyset(&block_int);
        sigaddset(&block_int, SIGINT);
        pthread_sigmask(SIG_BLOCK, &block_int, nullptr);

        // Close all pipe ends except this worker's read end
        for (int i = 0; i < totalWorkers_ * 2; ++i) {
            if (i != globalWorkerId * 2) {
                if (workerPipes_[i] != -1) close(workerPipes_[i]);
            }
        }

        std::string processName = "game_worker_" + std::to_string(globalWorkerId);
        prctl(PR_SET_NAME, processName.c_str(), 0, 0, 0);

        Logger::Info("Restarted worker {} (PID: {})", globalWorkerId, getpid());

        WorkerProcess(globalWorkerId, group);

        if (workerPipes_[globalWorkerId * 2] != -1)
            close(workerPipes_[globalWorkerId * 2]);
        _exit(0);

    } else if (pid > 0) {
        // Master
        workers_[globalWorkerId].pid = pid;

        // Close read end in master
        if (workerPipes_[globalWorkerId * 2] != -1) {
            close(workerPipes_[globalWorkerId * 2]);
            workerPipes_[globalWorkerId * 2] = -1;
        }

        // Set non‑blocking for write end
        fcntl(workerPipes_[globalWorkerId * 2 + 1], F_SETFL, O_NONBLOCK);

        {
            std::lock_guard<std::mutex> lock(healthMutex_);
            workerHealth_[globalWorkerId] = {pid, time(nullptr)};
        }

        Logger::Info("Worker {} restarted with PID: {}", globalWorkerId, pid);
    } else {
        Logger::Error("Failed to restart worker {}: {}", globalWorkerId, strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        workerPipes_[globalWorkerId * 2] = old_read;
        workerPipes_[globalWorkerId * 2 + 1] = old_write;
    }

    UnblockSignals(&oldset);
}

void ProcessPool::Shutdown() {
    if (!running_.load()) return;
    shutdownRequested_.store(true);
    Stop();
}

void ProcessPool::Stop() {
    shutdownRequested_.store(true);
    running_.store(false);
    if (masterThread_.joinable()) {
        masterThread_.join();
    }
    CloseAllPipes();
}

bool ProcessPool::WriteAll(int fd, const void* buffer, size_t count) {
    const char* ptr = static_cast<const char*>(buffer);
    size_t remaining = count;
    auto startTime = std::chrono::steady_clock::now();

    while (remaining > 0) {
        ssize_t written = write(fd, ptr, remaining);
        if (written > 0) {
            ptr += written;
            remaining -= written;
        } else if (written == 0) {
            Logger::Error("Write reached EOF unexpectedly");
            return false;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                auto currentTime = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count();
                if (elapsed > 5000) {
                    Logger::Error("Write timeout after {}ms", elapsed);
                    return false;
                }
                continue;
            } else {
                Logger::Error("Write error: {}", strerror(errno));
                return false;
            }
        }
    }
    return true;
}

bool ProcessPool::ReadAll(int fd, void* buffer, size_t count, bool nonBlocking) {
    char* ptr = static_cast<char*>(buffer);
    size_t remaining = count;
    auto startTime = std::chrono::steady_clock::now();
    uint32_t timeoutMs = nonBlocking ? 100 : receiveTimeoutMs_;

    while (remaining > 0) {
        ssize_t bytes_read = read(fd, ptr, remaining);
        if (bytes_read > 0) {
            ptr += bytes_read;
            remaining -= bytes_read;
        } else if (bytes_read == 0) {
            Logger::Error("Read reached EOF unexpectedly");
            return false;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (nonBlocking) {
                    return (count - remaining) > 0;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                auto currentTime = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count();
                if (elapsed > timeoutMs) {
                    Logger::Error("Read timeout after {}ms", elapsed);
                    return false;
                }
                continue;
            } else {
                Logger::Error("Read error: {}", strerror(errno));
                return false;
            }
        }
    }
    return true;
}

bool ProcessPool::DrainPipe(int fd, size_t bytesToDrain) {
    const size_t BUFFER_SIZE = 4096;
    const size_t MAX_DRAIN_LIMIT = 1024 * 1024 * 100;
    std::vector<char> buffer(BUFFER_SIZE);
    size_t drained = 0;

    if (bytesToDrain > MAX_DRAIN_LIMIT) {
        Logger::Error("Drain request too large: {} bytes (max: {})", bytesToDrain, MAX_DRAIN_LIMIT);
        return false;
    }

    const size_t MAX_ITERATIONS = bytesToDrain / BUFFER_SIZE + 1000;
    size_t iteration = 0;

    while (drained < bytesToDrain) {
        if (iteration++ > MAX_ITERATIONS) {
            Logger::Error("DrainPipe: Exceeded maximum iterations ({})", MAX_ITERATIONS);
            return false;
        }
        size_t remaining = bytesToDrain - drained;
        size_t toRead = std::min(BUFFER_SIZE, remaining);
        ssize_t bytes_read = read(fd, buffer.data(), toRead);
        if (bytes_read > 0) {
            drained += bytes_read;
        } else if (bytes_read == 0) {
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            Logger::Error("Drain error: {}", strerror(errno));
            return false;
        }
        if (bytes_read < static_cast<ssize_t>(toRead)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    Logger::Warn("Drained {} of {} requested bytes from pipe", drained, bytesToDrain);
    return drained > 0;
}

bool ProcessPool::SendToWorker(int workerId, const std::string& message) {
    if (workerId < 0 || workerId >= totalWorkers_) {
        Logger::Error("Invalid worker ID: {}", workerId);
        return false;
    }
    int write_fd = workerPipes_[workerId * 2 + 1];
    if (write_fd == -1) {
        Logger::Error("No pipe available for worker {}", workerId);
        return false;
    }
    if (message.length() > maxMessageSize_) {
        Logger::Error("Message too large for worker {}: {} bytes (max: {})", workerId, message.length(), maxMessageSize_);
        return false;
    }
    uint32_t msg_len = static_cast<uint32_t>(message.length());
    uint32_t net_len = htonl(msg_len);
    if (!WriteAll(write_fd, &net_len, sizeof(net_len))) {
        Logger::Error("Failed to send message length to worker {}", workerId);
        return false;
    }
    if (!WriteAll(write_fd, message.c_str(), msg_len)) {
        Logger::Error("Failed to send message content to worker {}", workerId);
        return false;
    }
    Logger::Debug("Sent {} bytes to worker {}", message.length(), workerId);
    return true;
}

std::string ProcessPool::ReceiveFromMaster() {
    if (role_ != ProcessRole::WORKER) {
        Logger::Error("Only workers can receive from master");
        return "";
    }
    int read_fd = workerPipes_[workerId_ * 2];
    if (read_fd == -1) {
        return "";
    }
    uint32_t net_len = 0;
    if (!ReadAll(read_fd, &net_len, sizeof(net_len), true)) {
        return "";
    }
    uint32_t msg_len = ntohl(net_len);
    if (msg_len == 0) {
        Logger::Warn("Received zero-length message");
        return "";
    }
    if (msg_len > maxMessageSize_) {
        Logger::Error("Message too large: {} bytes (max: {})", msg_len, maxMessageSize_);
        DrainPipe(read_fd, msg_len);
        return "";
    }
    std::vector<char> buffer(msg_len + 1);
    if (!ReadAll(read_fd, buffer.data(), msg_len, false)) {
        Logger::Error("Failed to read complete message (expected {} bytes)", msg_len);
        return "";
    }
    buffer[msg_len] = '\0';
    std::string message(buffer.data(), msg_len);
    Logger::Debug("Received {} bytes from master", msg_len);
    return message;
}

bool ProcessPool::IsWorkerAlive(int workerId) const {
    if (workerId < 0 || workerId >= totalWorkers_) {
        return false;
    }
    std::lock_guard<std::mutex> lock(healthMutex_);
    auto it = workerHealth_.find(workerId);
    if (it == workerHealth_.end()) {
        return false;
    }
    if (kill(it->second.first, 0) == 0) {
        return true;
    }
    return false;
}

void ProcessPool::SetupSignalHandlers() {
    signal(SIGPIPE, SIG_IGN);
}

void ProcessPool::SetWorkerMain(WorkerMainFunc workerMainFunc) {
    workerMainFunc_ = std::move(workerMainFunc);
}

void ProcessPool::BlockSignals(sigset_t* oldset) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, oldset);
}

void ProcessPool::UnblockSignals(const sigset_t* oldset) {
    pthread_sigmask(SIG_SETMASK, oldset, nullptr);
}