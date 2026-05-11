#pragma once
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <deque>
#include <mutex>
#include <asio.hpp>
#include "config/ConfigManager.hpp"
#include "network/BinaryProtocol.hpp"
#include "network/ConnectionManager.hpp"

class ClientListener {
public:
    ClientListener(const WorkerGroupConfig& groupConfig, int masterFd);
    ~ClientListener();
    void Start();
    void Shutdown();
private:
    asio::io_context io_;
    asio::posix::stream_descriptor pipe_;
    std::thread ioThread_;
    std::atomic<bool> stopping_{false};
    std::shared_ptr<ConnectionManager> manager_;
    std::deque<std::vector<uint8_t>> writeQueue_;
    std::mutex writeMutex_;
    bool writing_ = false;
    void sendToMaster(uint32_t correlationId, uint64_t sessionId, uint16_t messageType, const std::vector<uint8_t>& body);
    void startWrite();
    void doWrite();
    void doRead();
};
