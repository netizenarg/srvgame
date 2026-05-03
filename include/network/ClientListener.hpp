#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <unordered_map>

#include <asio.hpp>
#include <asio/ssl.hpp>

#include "logging/Logger.hpp"
#include "config/ConfigManager.hpp"
#include "network/IConnection.hpp"
#include "network/ConnectionManager.hpp"
#include "network/BinarySession.hpp"
#include "network/WebSocketProtocol.hpp"
#include "network/WebSocketSession.hpp"
#include "network/BinaryProtocol.hpp"
#include "game/GameData.hpp"

class ClientListener {
public:
    ClientListener(const WorkerGroupConfig& groupConfig, const ConfigManager& config);
    ~ClientListener();

    bool Initialize();
    void Run();
    void Shutdown();

    void SetMasterSender(std::function<void(const std::vector<uint8_t>&)> sender);
    void OnMasterReply(uint32_t correlationId, const std::vector<uint8_t>& reply);

    asio::io_context& GetIoContext();

    static std::vector<uint8_t> PackIPCEnvelope(uint32_t correlationId, uint64_t sessionId,
                                                uint16_t messageType, const std::vector<uint8_t>& body);

    void InitSessionFactory(int workerId);

private:
    struct PendingEntry {
        uint64_t sessionId;
        uint16_t messageType;
    };

    std::function<void(const std::vector<uint8_t>&)> sendToMaster_;
    std::unordered_map<uint32_t, PendingEntry> pendingReplies_;
    std::atomic<uint32_t> nextCorrelationId_{1};

    asio::io_context ioContext_;
    asio::ip::tcp::acceptor acceptor_;

    WorkerGroupConfig groupConfig_;
    const ConfigManager& config_;

    std::string host_;
    uint16_t port_;
    bool reuse_;
    int ioThreads_;

    std::vector<std::thread> workerThreads_;
    std::atomic<bool> running_{false};

    std::shared_ptr<asio::ssl::context> sslContext_;

    std::function<std::shared_ptr<IConnection>(asio::ip::tcp::socket, std::shared_ptr<asio::ssl::context>)> sessionFactory_;
    std::function<std::shared_ptr<IConnection>(asio::ip::tcp::socket, std::shared_ptr<asio::ssl::context>)> webSocketFactory_;

    std::optional<asio::executor_work_guard<asio::io_context::executor_type>> work_guard_;

    void DoAccept();
    void StartWorkerThreads();

    static uint16_t JsonMsgStringToType(const std::string& msg);
    static nlohmann::json BinaryToJson(uint16_t type, const std::vector<uint8_t>& body);
};
