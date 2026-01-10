#pragma once

#include <thread>
#include <functional>
#include <atomic>
#include <memory>
#include <vector>

class RAIIThread {
public:
    RAIIThread() = default;
    
    RAIIThread(std::function<void()> task) {
        Start(std::move(task));
    }
    
    RAIIThread(std::function<void(int)> task, int id) {
        StartWithId(std::move(task), id);
    }
    
    ~RAIIThread() {
        Stop();
    }
    
    // Non-copyable
    RAIIThread(const RAIIThread&) = delete;
    RAIIThread& operator=(const RAIIThread&) = delete;
    
    // Movable
    RAIIThread(RAIIThread&& other) noexcept {
        MoveFrom(std::move(other));
    }
    
    RAIIThread& operator=(RAIIThread&& other) noexcept {
        if (this != &other) {
            Stop();
            MoveFrom(std::move(other));
        }
        return *this;
    }
    
    void Start(std::function<void()> task) {
        Stop();
        shouldStop_ = false;
        thread_ = std::thread([this, task = std::move(task)]() {
            task();
        });
    }
    
    void StartWithId(std::function<void(int)> task, int id) {
        Stop();
        shouldStop_ = false;
        thread_ = std::thread([this, task = std::move(task), id]() {
            task(id);
        });
    }
    
    void Stop() {
        if (thread_.joinable()) {
            shouldStop_ = true;
            thread_.join();
        }
    }
    
    void RequestStop() {
        shouldStop_ = true;
    }
    
    bool ShouldStop() const {
        return shouldStop_;
    }
    
    bool IsRunning() const {
        return thread_.joinable();
    }
    
    void Join() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    
    std::thread::id GetId() const {
        return thread_.get_id();
    }
    
private:
    void MoveFrom(RAIIThread&& other) noexcept {
        thread_ = std::move(other.thread_);
        shouldStop_ = other.shouldStop_.load();
        other.shouldStop_ = false;
    }
    
    std::thread thread_;
    std::atomic<bool> shouldStop_{false};
};

class ThreadPool {
public:
    ThreadPool() = default;
    
    explicit ThreadPool(size_t size) {
        threads_.reserve(size);
    }
    
    ~ThreadPool() {
        StopAll();
    }
    
    // Non-copyable
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    
    // Movable
    ThreadPool(ThreadPool&& other) noexcept = default;
    ThreadPool& operator=(ThreadPool&& other) noexcept = default;
    
    void AddThread(std::function<void()> task) {
        threads_.emplace_back(std::move(task));
    }
    
    void AddThreadWithId(std::function<void(int)> task) {
        int id = static_cast<int>(threads_.size());
        threads_.emplace_back(std::move(task), id);
    }
    
    void StopAll() {
        for (auto& thread : threads_) {
            thread.RequestStop();
        }
        for (auto& thread : threads_) {
            thread.Join();
        }
        threads_.clear();
    }
    
    void JoinAll() {
        for (auto& thread : threads_) {
            thread.Join();
        }
    }
    
    size_t Size() const {
        return threads_.size();
    }
    
    bool Empty() const {
        return threads_.empty();
    }
    
private:
    std::vector<RAIIThread> threads_;
};