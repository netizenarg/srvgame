#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <atomic>

#include <asio.hpp>
#include <asio/ssl.hpp>

#include "logging/Logger.hpp"
#include "config/ConfigManager.hpp"
#include "network/ConnectionManager.hpp"
#include "network/GameSession.hpp"
#include "network/WebSocketProtocol.hpp"
#include "network/WebSocketSession.hpp"

class GameServer {
public:
    GameServer(const WorkerGroupConfig& groupConfig, const ConfigManager& globalConfig);
    ~GameServer();

    bool Initialize();
    void Run();
    void Shutdown();

    // For binary protocol
    using SessionFactory = std::function<std::shared_ptr<GameSession>(asio::ip::tcp::socket, std::shared_ptr<asio::ssl::context>)>;
    void SetSessionFactory(SessionFactory factory);

    // For WebSocket protocol
    using WebSocketFactory = std::function<WebSocketProtocol::WebSocketConnection::Pointer(asio::ip::tcp::socket, std::shared_ptr<asio::ssl::context>)>;
    void SetWebSocketConnectionFactory(WebSocketFactory factory);

private:
    void DoAccept();
    void StartWorkerThreads();
    void SetupSignalHandlers();

    asio::io_context ioContext_;
    asio::ip::tcp::acceptor acceptor_;
    asio::signal_set signals_;

    WorkerGroupConfig groupConfig_;
    const ConfigManager& globalConfig_;

    std::string host_;
    uint16_t port_;
    bool reusePort_;
    int ioThreads_;

    std::vector<std::thread> workerThreads_;
    std::atomic<bool> running_{false};

    std::shared_ptr<asio::ssl::context> sslContext_;   // optional, set if SSL is configured

    SessionFactory sessionFactory_;
    WebSocketFactory webSocketFactory_;
};
