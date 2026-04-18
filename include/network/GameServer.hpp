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

#include "game/GameLogic.hpp"

class GameServer {
public:
    GameServer(const WorkerGroupConfig& groupConfig, const ConfigManager& globalConfig);
    ~GameServer();

    bool Initialize();
    void Run();
    void Shutdown();

    asio::io_context& GetIoContext() { return ioContext_; }

    using SessionFactory = std::function<std::shared_ptr<GameSession>(asio::ip::tcp::socket, std::shared_ptr<asio::ssl::context>)>;
    void SetSessionFactory(SessionFactory factory);

    using WebSocketFactory = std::function<WebSocketProtocol::WebSocketConnection::Pointer(asio::ip::tcp::socket, std::shared_ptr<asio::ssl::context>)>;
    void SetWebSocketConnectionFactory(WebSocketFactory factory);

private:
    void DoAccept();
    void StartWorkerThreads();

    asio::io_context ioContext_;
    asio::ip::tcp::acceptor acceptor_;

    WorkerGroupConfig groupConfig_;
    const ConfigManager& globalConfig_;

    std::string host_;
    uint16_t port_;
    bool reuse_;
    int ioThreads_;

    std::vector<std::thread> workerThreads_;
    std::atomic<bool> running_{false};

    std::shared_ptr<asio::ssl::context> sslContext_;

    SessionFactory sessionFactory_;
    WebSocketFactory webSocketFactory_;
};
