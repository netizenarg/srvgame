#include "process/ProcessPool.hpp"

// Declare the global shutdown flag from main.cpp
extern std::atomic<bool> g_shutdown;

ProcessPool::ProcessPool(int numProcesses)
    : numProcesses_(numProcesses > 0 ? numProcesses : 1) {
    workerPids_.resize(numProcesses_, -1);
    workerPipes_.resize(numProcesses_ * 2, -1);
}

ProcessPool::~ProcessPool() {
    Shutdown();
}

bool ProcessPool::Initialize() {
    if (numProcesses_ <= 0) {
        Logger::Error("Invalid number of processes: {}", numProcesses_);
        return false;
    }

    masterPid_ = getpid();
    SetupSignalHandlers();
    running_.store(true);
    shutdownRequested_.store(false);

    for (int i = 0; i < numProcesses_; ++i) {
        CreateWorkerPipe(i);
    }

    return true;
}

void ProcessPool::CreateWorkerPipe(int workerId) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        Logger::Error("Failed to create pipe for worker {}: {}", workerId, strerror(errno));
        return;
    }

    int read_idx = workerId * 2;
    int write_idx = workerId * 2 + 1;

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
        WorkerProcess(workerId_);
    }
}

void ProcessPool::MasterProcess() {
    Logger::Info("Master process started (PID: {})", getpid());

    sigset_t oldset;
    BlockSignals(&oldset);

    for (int i = 0; i < numProcesses_; ++i) {
        pid_t pid = fork();

        if (pid == 0) {
            // Child
            signal(SIGINT, SIG_IGN);
            // SIGTERM handler: sets the global shutdown flag (async‑safe)
            signal(SIGTERM, [](int) { g_shutdown.store(true, std::memory_order_relaxed); });

            UnblockSignals(&oldset);

            // Permanently block SIGINT only (SIGTERM remains unblocked)
            sigset_t block_int;
            sigemptyset(&block_int);
            sigaddset(&block_int, SIGINT);
            pthread_sigmask(SIG_BLOCK, &block_int, nullptr);

            workerId_ = i;
            role_ = ProcessRole::WORKER;

            for (int j = 0; j < numProcesses_ * 2; ++j) {
                if (j != workerId_ * 2) {
                    if (workerPipes_[j] != -1) close(workerPipes_[j]);
                }
            }

            std::string processName = "game_worker_" + std::to_string(i);
            prctl(PR_SET_NAME, processName.c_str(), 0, 0, 0);

            Logger::Info("Worker process {} started (PID: {})", i, getpid());

            WorkerProcess(i);

            if (workerPipes_[workerId_ * 2] != -1)
                close(workerPipes_[workerId_ * 2]);
            _exit(0);

        } else if (pid > 0) {
            workerPids_[i] = pid;

            if (workerPipes_[i * 2] != -1) {
                close(workerPipes_[i * 2]);
                workerPipes_[i * 2] = -1;
            }

            {
                std::lock_guard<std::mutex> lock(healthMutex_);
                workerHealth_[i] = {pid, time(nullptr)};
            }

            Logger::Info("Worker {} started with PID: {}", i, pid);
        } else {
            Logger::Error("Failed to fork worker {}: {}", i, strerror(errno));
        }
    }

    UnblockSignals(&oldset);

    while (running_.load() && !shutdownRequested_.load()) {
        CleanupDeadWorkers();
        for (int i = 0; i < 10 && running_.load() && !shutdownRequested_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    for (int i = 0; i < numProcesses_; ++i) {
        if (workerPids_[i] > 0) {
            Logger::Info("Terminating worker {} (PID: {})", i, workerPids_[i]);
            kill(workerPids_[i], SIGTERM);
        }
    }

    int status;
    for (int i = 0; i < numProcesses_; ++i) {
        if (workerPids_[i] > 0) {
            waitpid(workerPids_[i], &status, 0);
            Logger::Info("Worker {} exited with status: {}", i, status);
        }
    }

    CloseAllPipes();
    Logger::Info("Master process shutdown complete");
}

void ProcessPool::CloseAllPipes() {
    for (size_t i = 0; i < workerPipes_.size(); ++i) {
        if (workerPipes_[i] != -1) {
            close(workerPipes_[i]);
            workerPipes_[i] = -1;
        }
    }
}

void ProcessPool::WorkerProcess(int workerId) {
    this->workerId_ = workerId;

    // No need to set signal handlers here – they are already set in the child branch.
    // The SIGTERM handler set earlier will remain active.

    if (workerMainFunc_) {
        workerMainFunc_(workerId);
    }
}

void ProcessPool::CleanupDeadWorkers() {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < numProcesses_; ++i) {
            if (workerPids_[i] == pid) {
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

void ProcessPool::RestartWorker(int workerId) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        Logger::Error("Failed to create pipe for worker {}: {}", workerId, strerror(errno));
        return;
    }

    int old_read = workerPipes_[workerId * 2];
    int old_write = workerPipes_[workerId * 2 + 1];
    workerPipes_[workerId * 2] = pipefd[0];
    workerPipes_[workerId * 2 + 1] = pipefd[1];

    sigset_t oldset;
    BlockSignals(&oldset);

    pid_t pid = fork();

    if (pid == 0) {
        signal(SIGINT, SIG_IGN);
        signal(SIGTERM, [](int) { g_shutdown.store(true, std::memory_order_relaxed); });

        UnblockSignals(&oldset);

        // Permanently block SIGINT only
        sigset_t block_int;
        sigemptyset(&block_int);
        sigaddset(&block_int, SIGINT);
        pthread_sigmask(SIG_BLOCK, &block_int, nullptr);

        workerId_ = workerId;
        role_ = ProcessRole::WORKER;

        for (int j = 0; j < numProcesses_ * 2; ++j) {
            if (j != workerId * 2) {
                if (workerPipes_[j] != -1) close(workerPipes_[j]);
            }
        }

        fcntl(workerPipes_[workerId * 2], F_SETFL, O_NONBLOCK);

        std::string processName = "game_worker_" + std::to_string(workerId);
        prctl(PR_SET_NAME, processName.c_str(), 0, 0, 0);

        Logger::Info("Restarted worker {} (PID: {})", workerId, getpid());

        WorkerProcess(workerId);

        if (workerPipes_[workerId * 2] != -1)
            close(workerPipes_[workerId * 2]);
        _exit(0);

    } else if (pid > 0) {
        workerPids_[workerId] = pid;

        close(workerPipes_[workerId * 2]);
        workerPipes_[workerId * 2] = -1;

        fcntl(workerPipes_[workerId * 2 + 1], F_SETFL, O_NONBLOCK);

        {
            std::lock_guard<std::mutex> lock(healthMutex_);
            workerHealth_[workerId] = {pid, time(nullptr)};
        }

        Logger::Info("Worker {} restarted with PID: {}", workerId, pid);
    } else {
        Logger::Error("Failed to restart worker {}: {}", workerId, strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        workerPipes_[workerId * 2] = old_read;
        workerPipes_[workerId * 2 + 1] = old_write;
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
    if (workerId < 0 || workerId >= numProcesses_) {
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
    if (workerId < 0 || workerId >= numProcesses_) {
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

void ProcessPool::SetWorkerMain(std::function<void(int)> workerMainFunc) {
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
