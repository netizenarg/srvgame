#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include <execinfo.h>
//#include <signal.h>

#include "logging/Logger.hpp"

#include "config/ConfigManager.hpp"

#include "network/GameServer.hpp"
#include "process/ProcessPool.hpp"

#include "database/DbManager.hpp"

#include "game/GameLogic.hpp"


std::atomic<bool> g_shutdown(false);

void SignalHandler(int signal) {
    (void)signal;
    g_shutdown.store(true);
}

void crash_handler(int sig) {
    void* array[20];
    size_t size = backtrace(array, 20);
    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    _exit(1);
}

// Worker main signature: receives group config
void WorkerMain(int workerId, const WorkerGroupConfig& groupConfig, ProcessPool* processPool = nullptr, const std::string& path_config = "config.json") {
    try {
        auto& config = ConfigManager::GetInstance();

        Logger::InitializeWithWorkerId(workerId);
        Logger::Info("Worker {} starting game world system for group: {} ({}:{})",
                     workerId, groupConfig.protocol, groupConfig.host, groupConfig.port);

        if (!config.LoadConfig(path_config)) {
            Logger::Critical("Worker {} failed to load configuration", workerId);
            return;
        }

        if (processPool) {
            uint32_t maxMessageSize = config.GetInt("process.max_message_size", 1048576);
            uint32_t receiveTimeout = config.GetInt("process.receive_timeout_ms", 1000);

            processPool->SetMaxMessageSize(maxMessageSize);
            processPool->SetReceiveTimeout(receiveTimeout);

            Logger::Info("Worker {} using max message size: {} bytes, timeout: {}ms",
                         workerId, maxMessageSize, receiveTimeout);
        } else {
            Logger::Warn("Worker {}: ProcessPool not available for configuration", workerId);
        }

        auto& dbManager = DbManager::GetInstance();

        nlohmann::json dbConfig;
        dbConfig["host"] = config.GetDatabaseHost();
        dbConfig["port"] = config.GetDatabasePort();
        dbConfig["name"] = config.GetDatabaseName();
        dbConfig["user"] = config.GetDatabaseUser();
        dbConfig["password"] = config.GetDatabasePassword();

        dbConfig["max_connections"] = config.GetInt("database.max_connections", 20);
        dbConfig["min_connections"] = config.GetInt("database.min_connections", 5);
        dbConfig["connection_timeout_ms"] = config.GetInt("database.connection_timeout_ms", 5000);

        if (!dbManager.Initialize(path_config)) {
            Logger::Error("Worker {} failed to initialize database", workerId);
            return;
        }

        if (!dbManager.Connect()) {
            Logger::Error("Worker {} failed to connect to database", workerId);
            return;
        }

        Logger::Info("Worker {} database connection established successfully using backend: {}",
                     workerId, config.GetDatabaseBackend());

        auto& gameLogic = GameLogic::GetInstance();

        GameLogic::WorldConfig worldConfig;
        worldConfig.seed = config.GetWorldSeed() + workerId;
        worldConfig.viewDistance = config.GetViewDistance();
        worldConfig.chunkSize = config.GetChunkSize();
        worldConfig.maxActiveChunks = config.GetMaxActiveChunks();
        worldConfig.terrainScale = config.GetTerrainScale();
        worldConfig.maxTerrainHeight = config.GetMaxTerrainHeight();
        worldConfig.waterLevel = config.GetWaterLevel();

        gameLogic.SetWorldConfig(worldConfig);

        GameServer server(groupConfig, config);

        if (groupConfig.protocol == "binary") {
            server.SetSessionFactory([workerId, processPool, &groupConfig]
                                     (asio::ip::tcp::socket socket,
                                      std::shared_ptr<asio::ssl::context> sslCtx)
            {
                auto session = std::make_shared<GameSession>(std::move(socket), sslCtx);
                Logger::Debug("Worker {} created new game session {}",
                            workerId, session->GetSessionId());

                session->SetMessageHandler([session, workerId, processPool](const nlohmann::json& msg) {
                    try {
                        std::string msgType = msg.value("type", "");
                        Logger::Debug("Worker {} processing message type: {}", workerId, msgType);
                        if (msgType == "ipc_message" && processPool) {
                            if (msg.contains("target_worker") && msg.contains("payload")) {
                                int targetWorker = msg["target_worker"];
                                std::string payload = msg["payload"].dump();
                                if (processPool->SendToWorker(targetWorker, payload)) {
                                    Logger::Debug("Worker {} sent IPC message to worker {}",
                                                workerId, targetWorker);
                                } else {
                                    Logger::Error("Worker {} failed to send IPC message to worker {}",
                                                workerId, targetWorker);
                                }
                            }
                        } else {
                            GameLogic::GetInstance().HandleMessage(session->GetSessionId(), msg);
                        }
                    } catch (const std::exception& e) {
                        Logger::Error("Worker {} error processing message: {}", workerId, e.what());
                        session->SendError("Internal server error", 500);
                    }
                });

                session->SetDefaultBinaryMessageHandler([session, workerId](uint16_t type,
                                                        const std::vector<uint8_t>& data) {
                    GameLogic::GetInstance().HandleBinaryMessage(session->GetSessionId(), type, data);
                });

                session->SetCloseHandler([session, workerId]() {
                    Logger::Info("Worker {} session {} closing", workerId, session->GetSessionId());
                    GameLogic::GetInstance().OnPlayerDisconnected(session->GetSessionId());
                    Logger::Debug("Worker {} session {} cleanup complete", workerId, session->GetSessionId());
                });
                return session;
            });
        } else if (groupConfig.protocol == "websocket") {
            server.SetWebSocketConnectionFactory([workerId, processPool, &groupConfig](asio::ip::tcp::socket socket, std::shared_ptr<asio::ssl::context> /*sslCtx*/) {
                auto wsConn = std::make_shared<WebSocketProtocol::WebSocketConnection>(std::move(socket));
                // SSL handling would be added later if needed
                return wsConn;
            });
        }

        gameLogic.SetConnectionManager(ConnectionManager::GetInstancePtr());
        gameLogic.Initialize();

        if (config.ShouldPreloadWorld()) {
            Logger::Info("Worker {} preloading world data...", workerId);
            gameLogic.PreloadWorldData(config.GetWorldPreloadRadius());
        }

        if (server.Initialize()) {
            Logger::Info("Worker {} game server initialized on {}:{} (protocol: {})",
                         workerId, groupConfig.host, groupConfig.port, groupConfig.protocol);

            std::atomic<bool> worldMaintenanceRunning{true};
            std::thread worldMaintenanceThread([&gameLogic, &worldMaintenanceRunning, workerId, processPool]() {
                Logger::Info("Worker {} starting world maintenance thread", workerId);

                auto lastCleanupTime = std::chrono::steady_clock::now();
                auto lastIPCCheckTime = std::chrono::steady_clock::now();

                while (worldMaintenanceRunning && !g_shutdown.load()) {
                    auto currentTime = std::chrono::steady_clock::now();

                    auto elapsedWorld = std::chrono::duration_cast<std::chrono::seconds>(
                        currentTime - lastCleanupTime);
                    if (elapsedWorld.count() >= 30) {
                        gameLogic.PerformMaintenance();
                        lastCleanupTime = currentTime;
                    }

                    // Check for IPC messages every 10 ms and drain all pending
                    auto elapsedIPC = std::chrono::duration_cast<std::chrono::milliseconds>(
                        currentTime - lastIPCCheckTime);
                    if (elapsedIPC.count() >= 10 && processPool) {
                        std::string message = processPool->ReceiveFromMaster();
                        while (!message.empty()) {
                            try {
                                auto jsonMsg = nlohmann::json::parse(message);
                                gameLogic.HandleIPCMessage(jsonMsg);
                            } catch (const std::exception& e) {
                                Logger::Error("Worker {} failed to parse IPC message: {}", workerId, e.what());
                            }
                            message = processPool->ReceiveFromMaster();
                        }
                        lastIPCCheckTime = currentTime;
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

                Logger::Info("Worker {} world maintenance thread stopped", workerId);
            });

            Logger::Info("Worker {} starting server loop", workerId);
            server.Run();

            worldMaintenanceRunning = false;
            if (worldMaintenanceThread.joinable()) {
                worldMaintenanceThread.join();
            }

        } else {
            Logger::Critical("Worker {} failed to initialize server", workerId);
        }

        Logger::Info("Worker {} beginning cleanup...", workerId);
        gameLogic.Shutdown();

        dbManager.Disconnect();
        dbManager.Shutdown();

        Logger::Info("Worker {} shutdown complete", workerId);
    }
    catch (const std::exception& e) {
        Logger::Critical("Worker {} caught unhandled exception: {}", workerId, e.what());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    catch (...) {
        Logger::Critical("Worker {} caught unknown exception", workerId);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}


int main(int argc, char* argv[]) {
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    //std::signal(SIGSEGV, crash_handler);
    //std::signal(SIGABRT, crash_handler);

    Logger::InitializeDefaults();

    std::string conf_path = "config/core.json";
    auto& config = ConfigManager::GetInstance();
    if (!config.LoadConfig(conf_path)) {
        Logger::Critical("Failed to load configuration.");
        return 1;
    }
    else {
        Logger::Info("Success to load configuration.");
    }

    Logger::Initialize();

    DbManager& dbManager = DbManager::GetInstance();
    bool db_check = dbManager.EnsureDatabaseExists(conf_path);
    dbManager.Disconnect();
    dbManager.Shutdown();
    if (!db_check) {
        Logger::Critical("Failed to ensure database existence. Exiting.");
        return 1;
    }

    Logger::Info("Starting Game Server v0.0.1 with LogicCore System");
    Logger::Info("Database Backend: {}", config.GetDatabaseBackend());
    Logger::Info("World Seed: {}", config.GetWorldSeed());
    Logger::Info("View Distance: {} chunks", config.GetViewDistance());
    Logger::Info("Chunk Size: {} units", config.GetChunkSize());
    std::string cmdline;
    for (int i = 0; i < argc; ++i) {
        if (i > 0) cmdline += " ";
        cmdline += argv[i];
    }
    Logger::Info("{} commands ({})", argc, cmdline);

    auto groups = config.GetWorkerGroups();
    if (groups.empty()) {
        Logger::Critical("No worker groups configured");
        return 1;
    }

    ProcessPool processPool(groups);

    uint32_t maxMessageSize = config.GetInt("process.max_message_size", 1048576);
    uint32_t receiveTimeout = config.GetInt("process.receive_timeout_ms", 1000);

    processPool.SetMaxMessageSize(maxMessageSize);
    processPool.SetReceiveTimeout(receiveTimeout);

    Logger::Info("Process pool configured: max message size = {} bytes, timeout = {}ms",
                 maxMessageSize, receiveTimeout);

    auto workerMainWithPool = [&processPool, &conf_path](int workerId, const WorkerGroupConfig& groupConfig) {
        WorkerMain(workerId, groupConfig, &processPool, conf_path);
    };

    processPool.SetWorkerMain(workerMainWithPool);

    Logger::Info("Starting {} worker processes", processPool.GetTotalWorkerCount());
    processPool.Run();

    // Master messaging thread – reduced frequency to avoid pipe overload
    std::thread masterMessagingThread([&processPool]() {
        std::this_thread::sleep_for(std::chrono::seconds(3)); // Wait for workers to start

        Logger::Info("Master process starting IPC message test");

        int totalWorkers = processPool.GetTotalWorkerCount();
        for (int i = 0; i < totalWorkers; i++) {
            if (!processPool.IsWorkerAlive(i)) {
                Logger::Warn("Master skipping welcome message to dead worker {}", i);
                continue;
            }

            nlohmann::json testMsg;
            testMsg["type"] = "welcome";
            testMsg["from"] = "master";
            testMsg["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
            testMsg["worker_id"] = i;
            testMsg["message"] = "Welcome to the game server!";

            std::string serialized = testMsg.dump();

            if (processPool.SendToWorker(i, serialized)) {
                Logger::Info("Master sent welcome message to worker {}", i);
            } else {
                Logger::Error("Master failed to send welcome message to worker {}", i);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        Logger::Info("Master process IPC message test complete");
    });

    Logger::Info("Master process waiting for shutdown signal...");
    while (!g_shutdown.load()) {
        int totalWorkers = processPool.GetTotalWorkerCount();
        for (int i = 0; i < totalWorkers; i++) {
            if (!processPool.IsWorkerAlive(i)) {
                Logger::Warn("Master detected worker {} is not alive", i);
            }
        }

        // Sleep longer (2 seconds) to reduce master message rate
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (g_shutdown.load()) break;

        // Send heartbeat every 10 seconds instead of every second
        static int heartbeatCount = 0;
        heartbeatCount++;
        if (heartbeatCount % 5 == 0) {  // every 10 seconds (since sleep 2 sec)
            for (int i = 0; i < totalWorkers; i++) {
                if (g_shutdown.load()) break;
                if (!processPool.IsWorkerAlive(i)) continue;

                nlohmann::json heartbeat;
                heartbeat["type"] = "heartbeat";
                heartbeat["count"] = heartbeatCount;
                heartbeat["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();

                processPool.SendToWorker(i, heartbeat.dump());
            }
        }

        // Broadcast server status every 30 seconds (reduced from 10)
        static int statusUpdateCount = 0;
        statusUpdateCount++;
        if (statusUpdateCount % 15 == 0) { // every 30 seconds
            nlohmann::json serverStatus;
            serverStatus["type"] = "server_status";
            serverStatus["online_workers"] = totalWorkers;
            serverStatus["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();

            nlohmann::json broadcastMsg;
            broadcastMsg["type"] = "broadcast";
            broadcastMsg["data"] = serverStatus;

            std::string broadcastSerialized = broadcastMsg.dump();
            for (int i = 0; i < totalWorkers; i++) {
                if (g_shutdown.load()) break;
                if (!processPool.IsWorkerAlive(i)) continue;
                processPool.SendToWorker(i, broadcastSerialized);
            }

            Logger::Info("Master broadcasted server status to all workers");
        }
    }

    if (masterMessagingThread.joinable()) {
        masterMessagingThread.join();
    }

    Logger::Info("Initiating graceful shutdown...");
    processPool.Shutdown();

    Logger::Info("Game Server shutdown complete");
    return 0;
}
