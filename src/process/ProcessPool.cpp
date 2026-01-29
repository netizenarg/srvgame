#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <chrono>
#include <array>
#include <vector>

#include "process/ProcessPool.hpp"
#include "logging/Logger.hpp"

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

    // Create pipes for IPC with workers
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
    
    // Close existing pipes if any
    int read_idx = workerId * 2;
    int write_idx = workerId * 2 + 1;
    
    if (workerPipes_[read_idx] != -1) close(workerPipes_[read_idx]);
    if (workerPipes_[write_idx] != -1) close(workerPipes_[write_idx]);
    
    workerPipes_[read_idx] = pipefd[0];     // Master reads from this
    workerPipes_[write_idx] = pipefd[1];    // Master writes to this

    // Set non-blocking
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

    // Fork worker processes
    for (int i = 0; i < numProcesses_; ++i) {
        pid_t pid = fork();

        if (pid == 0) {
            // Child process
            workerId_ = i;
            role_ = ProcessRole::WORKER;

            // Close all pipe ends except the one we need
            for (int j = 0; j < numProcesses_ * 2; ++j) {
                if (j != workerId_ * 2) { // Worker keeps read end from master
                    close(workerPipes_[j]);
                }
            }

            // Set process name
            std::string processName = "game_worker_" + std::to_string(i);
            prctl(PR_SET_NAME, processName.c_str(), 0, 0, 0);

            Logger::Info("Worker process {} started (PID: {})", i, getpid());

            if (workerMain_) {
                workerMain_(i);
            }

            // Cleanup before exit
            close(workerPipes_[workerId_ * 2]);
            _exit(0);

        } else if (pid > 0) {
            // Parent process
            workerPids_[i] = pid;

            // Close worker's read end (we don't need it in master)
            close(workerPipes_[i * 2]);
            
            // Record worker health
            {
                std::lock_guard<std::mutex> lock(healthMutex_);
                workerHealth_[i] = {pid, time(nullptr)};
            }

            Logger::Info("Worker {} started with PID: {}", i, pid);
        } else {
            Logger::Error("Failed to fork worker {}: {}", i, strerror(errno));
        }
    }

    // Monitor worker processes
    while (running_.load() && !shutdownRequested_.load()) {
        CleanupDeadWorkers();
        
        // Check for shutdown every second
        for (int i = 0; i < 10 && running_.load() && !shutdownRequested_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // Send SIGTERM to all workers
    for (int i = 0; i < numProcesses_; ++i) {
        if (workerPids_[i] > 0) {
            Logger::Info("Terminating worker {} (PID: {})", i, workerPids_[i]);
            kill(workerPids_[i], SIGTERM);
        }
    }

    // Wait for workers to exit
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
    for (int i = 0; i < workerPipes_.size(); ++i) {
        if (workerPipes_[i] != -1) {
            close(workerPipes_[i]);
            workerPipes_[i] = -1;
        }
    }
}

void ProcessPool::WorkerProcess(int workerId) {
    this->workerId_ = workerId;

    // Set up signal handlers for worker
    signal(SIGTERM, [](int) { 
        Logger::Info("Worker received SIGTERM, exiting...");
        _exit(0); 
    });
    signal(SIGINT, SIG_IGN);

    if (workerMain_) {
        workerMain_(workerId);
    }
}

void ProcessPool::CleanupDeadWorkers() {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        // Find which worker died
        for (int i = 0; i < numProcesses_; ++i) {
            if (workerPids_[i] == pid) {
                Logger::Warn("Worker {} (PID: {}) died with status {}, restarting...", 
                           i, pid, WEXITSTATUS(status));
                
                // Clean up old pipe
                int write_idx = i * 2 + 1;
                if (workerPipes_[write_idx] != -1) {
                    close(workerPipes_[write_idx]);
                    workerPipes_[write_idx] = -1;
                }

                // Restart worker
                RestartWorker(i);
                break;
            }
        }
    }
}

void ProcessPool::RestartWorker(int workerId) {
    pid_t pid = fork();

    if (pid == 0) {
        // New worker process
        workerId_ = workerId;
        role_ = ProcessRole::WORKER;

        // Close all pipes except our read end
        for (int j = 0; j < numProcesses_ * 2; ++j) {
            if (j != workerId * 2) {
                if (workerPipes_[j] != -1) close(workerPipes_[j]);
            }
        }

        // Set process name
        std::string processName = "game_worker_" + std::to_string(workerId);
        prctl(PR_SET_NAME, processName.c_str(), 0, 0, 0);

        Logger::Info("Restarted worker {} (PID: {})", workerId, getpid());

        if (workerMain_) {
            workerMain_(workerId);
        }

        close(workerPipes_[workerId * 2]);
        _exit(0);

    } else if (pid > 0) {
        workerPids_[workerId] = pid;

        // Create new pipe for communication
        CreateWorkerPipe(workerId);
        
        // Close worker's read end in master
        close(workerPipes_[workerId * 2]);

        {
            std::lock_guard<std::mutex> lock(healthMutex_);
            workerHealth_[workerId] = {pid, time(nullptr)};
        }

        Logger::Info("Worker {} restarted with PID: {}", workerId, pid);
    } else {
        Logger::Error("Failed to restart worker {}: {}", workerId, strerror(errno));
    }
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

// Helper function: Write all data with retry logic for non-blocking I/O
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
            // EOF
            Logger::Error("Write reached EOF unexpectedly");
            return false;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Non-blocking mode, wait a bit and retry
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                
                // Check timeout
                auto currentTime = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    currentTime - startTime).count();
                    
                if (elapsed > 5000) { // 5 second timeout for writing
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

// Helper function: Read all data with timeout support
bool ProcessPool::ReadAll(int fd, void* buffer, size_t count, bool nonBlocking) {
    char* ptr = static_cast<char*>(buffer);
    size_t remaining = count;
    
    auto startTime = std::chrono::steady_clock::now();
    uint32_t timeoutMs = nonBlocking ? 100 : receiveTimeoutMs_; // Shorter timeout for non-blocking
    
    while (remaining > 0) {
        ssize_t bytes_read = read(fd, ptr, remaining);
        
        if (bytes_read > 0) {
            ptr += bytes_read;
            remaining -= bytes_read;
        } else if (bytes_read == 0) {
            // EOF
            Logger::Error("Read reached EOF unexpectedly");
            return false;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (nonBlocking) {
                    // Non-blocking mode, return what we have
                    return (count - remaining) > 0;
                }
                
                // Wait a bit and retry
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                
                // Check timeout
                auto currentTime = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    currentTime - startTime).count();
                    
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

// Helper function: Drain excess data from pipe
bool ProcessPool::DrainPipe(int fd, size_t bytesToDrain) {
    const size_t BUFFER_SIZE = 4096;
    std::vector<char> buffer(BUFFER_SIZE);
    size_t drained = 0;
    
    while (drained < bytesToDrain) {
        size_t toRead = std::min(BUFFER_SIZE, bytesToDrain - drained);
        ssize_t bytes_read = read(fd, buffer.data(), toRead);
        
        if (bytes_read > 0) {
            drained += bytes_read;
        } else if (bytes_read == 0) {
            break; // EOF
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // No more data
            }
            Logger::Error("Drain error: {}", strerror(errno));
            return false;
        }
    }
    
    Logger::Warn("Drained {} excess bytes from pipe", drained);
    return true;
}

// Send message with length prefix protocol
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
    
    // Check message size limit
    if (message.length() > maxMessageSize_) {
        Logger::Error("Message too large for worker {}: {} bytes (max: {})", 
                     workerId, message.length(), maxMessageSize_);
        return false;
    }
    
    // Send message length (network byte order for cross-platform compatibility)
    uint32_t msg_len = static_cast<uint32_t>(message.length());
    uint32_t net_len = htonl(msg_len);
    
    if (!WriteAll(write_fd, &net_len, sizeof(net_len))) {
        Logger::Error("Failed to send message length to worker {}", workerId);
        return false;
    }
    
    // Send message content
    if (!WriteAll(write_fd, message.c_str(), msg_len)) {
        Logger::Error("Failed to send message content to worker {}", workerId);
        return false;
    }
    
    Logger::Debug("Sent {} bytes to worker {}", message.length(), workerId);
    return true;
}

// Receive message with length prefix protocol
std::string ProcessPool::ReceiveFromMaster() {
    if (role_ != ProcessRole::WORKER) {
        Logger::Error("Only workers can receive from master");
        return "";
    }
    
    int read_fd = workerPipes_[workerId_ * 2];
    if (read_fd == -1) {
        return "";
    }
    
    // Read message length
    uint32_t net_len = 0;
    if (!ReadAll(read_fd, &net_len, sizeof(net_len), true)) {
        // Non-blocking read, no data available
        return "";
    }
    
    uint32_t msg_len = ntohl(net_len);
    
    // Validate message length
    if (msg_len == 0) {
        Logger::Warn("Received zero-length message");
        return "";
    }
    
    if (msg_len > maxMessageSize_) {
        Logger::Error("Message too large: {} bytes (max: {})", msg_len, maxMessageSize_);
        // Drain the oversized message from pipe
        DrainPipe(read_fd, msg_len);
        return "";
    }
    
    // Read message content
    std::vector<char> buffer(msg_len + 1); // +1 for null terminator
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
    
    // Check if process is still alive
    if (kill(it->second.first, 0) == 0) {
        return true;
    }
    
    return false;
}

void ProcessPool::SetupSignalHandlers() {
    signal(SIGCHLD, SIG_IGN);  // We'll handle child processes with waitpid
    signal(SIGPIPE, SIG_IGN);  // Ignore broken pipes
}