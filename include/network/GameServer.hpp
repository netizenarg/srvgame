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
#include "process/ProcessPool.hpp"

#include "network/IConnection.hpp"
#include "network/ConnectionManager.hpp"
#include "network/BinarySession.hpp"
#include "network/WebSocketProtocol.hpp"
#include "network/WebSocketSession.hpp"

#include "game/GameData.hpp"

#include "game/GameLogic.hpp"

class GameServer {
public:
    GameServer(const WorkerGroupConfig& groupConfig, const ConfigManager& globalConfig);
    ~GameServer();

    bool Initialize();
    void Run();
    void Shutdown();

    asio::io_context& GetIoContext();
    void HandleIPCMessage(const nlohmann::json& data, GameLogic& game_logic);

    void InitSessionFactory(int workerId, ProcessPool* processPool, GameLogic& game_logic);
    void RegisterCallbacks(const std::string& protocol, GameLogic& game_logic);

private:
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

    std::function<std::shared_ptr<IConnection>(asio::ip::tcp::socket, std::shared_ptr<asio::ssl::context>)> sessionFactory_;
    std::function<std::shared_ptr<IConnection>(asio::ip::tcp::socket, std::shared_ptr<asio::ssl::context>)> webSocketFactory_;

    std::optional<asio::executor_work_guard<asio::io_context::executor_type>> work_guard_;

    void DoAccept();
    void StartWorkerThreads();
    std::vector<std::shared_ptr<IConnection>> GetSessionsInRadius(const glm::vec3& position, float radius);

};
