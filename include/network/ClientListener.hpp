#pragma once
#include <functional>
#include <memory>
#include <thread>
#include <atomic>

#include <asio.hpp>

#include "config/ConfigManager.hpp"
#include "network/BinaryProtocol.hpp"
#include "network/ConnectionManager.hpp"
#include "process/IPCChannel.hpp"

class ClientListener {
public:
    ClientListener(const WorkerGroupConfig& groupConfig, int masterFd, int workerId);
    ~ClientListener();
    void Start();
    void Shutdown();
private:
    int workerId_;
    asio::io_context io_;
    std::shared_ptr<IPCChannel> channel_;
    std::thread ioThread_;
    std::atomic<bool> stopping_{false};
    std::shared_ptr<ConnectionManager> manager_;

    void onMasterMessage(const IPCEnvelope& env);
    void sendToMaster(uint32_t correlationId, uint64_t sessionId, uint16_t messageType, const std::vector<uint8_t>& body);
};
