#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <asio.hpp>
#include <asio/ssl.hpp>

#include "logging/Logger.hpp"
#include "config/ConfigManager.hpp"
#include "network/IConnection.hpp"
#include "network/BinaryProtocol.hpp"
#include "network/BinarySession.hpp"
#include "network/WebSocketProtocol.hpp"
#include "network/WebSocketSession.hpp"
#include "game/GameData.hpp"

class ConnectionManager {
public:
    using MasterSender = std::function<void(uint32_t correlationId, uint64_t sessionId, uint16_t messageType, const std::vector<uint8_t>& body)>;
    ConnectionManager(const WorkerGroupConfig& groupConfig, MasterSender masterSender);
    ~ConnectionManager();
    bool Start();
    void Shutdown();
    void OnMasterReply(uint32_t correlationId, const std::vector<uint8_t>& reply);

private:
    asio::io_context ioContext_;
    asio::ip::tcp::acceptor acceptor_;
    std::vector<std::thread> workerThreads_;
    std::atomic<bool> running_{false};
    std::shared_ptr<asio::ssl::context> sslContext_;
    MasterSender masterSender_;
    mutable std::shared_mutex sessionsMutex_;
    std::unordered_map<uint64_t, std::shared_ptr<IConnection>> sessions_;
    struct PendingEntry {
        uint64_t sessionId;
        uint16_t messageType;
    };
    std::unordered_map<uint32_t, PendingEntry> pendingReplies_;
    std::atomic<uint32_t> nextCorrelationId_{1};
    std::function<std::shared_ptr<IConnection>(asio::ip::tcp::socket, std::shared_ptr<asio::ssl::context>)> sessionFactory_;
    std::function<std::shared_ptr<IConnection>(asio::ip::tcp::socket, std::shared_ptr<asio::ssl::context>)> webSocketFactory_;
    WorkerGroupConfig groupConfig_;
    std::string host_;
    uint16_t port_;
    bool reuse_;
    void initSessionFactory();
    void doAccept();
    void onClientMessage(uint64_t sessionId, uint16_t type, const std::vector<uint8_t>& data);
    void onClientClose(uint64_t sessionId);
    static uint16_t jsonMsgType(const std::string& msg);
    static nlohmann::json binaryToJson(uint16_t type, const std::vector<uint8_t>& body);
};
