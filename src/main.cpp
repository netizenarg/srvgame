#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

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

// New worker main signature: receives group config
void WorkerMain(int workerId, const WorkerGroupConfig& groupConfig, ProcessPool* processPool = nullptr, const std::string& path_config = "config.json") {
    try {
        // Initialize logging with worker-specific configuration
        auto& config = ConfigManager::GetInstance();

        // Use worker-specific logger initialization
        Logger::InitializeWithWorkerId(workerId);
        Logger::Info("Worker {} starting game world system for group: {} ({}:{})",
                     workerId, groupConfig.protocol, groupConfig.host, groupConfig.port);

        // Initialize configuration
        if (!config.LoadConfig(path_config)) {
            Logger::Critical("Worker {} failed to load configuration", workerId);
            return;
        }

        // Configure ProcessPool message protocol if processPool is provided
        if (processPool) {
            // Set message protocol configuration from config
            uint32_t maxMessageSize = config.GetInt("process.max_message_size", 1048576); // 1MB default
            uint32_t receiveTimeout = config.GetInt("process.receive_timeout_ms", 1000); // 1 second default

            processPool->SetMaxMessageSize(maxMessageSize);
            processPool->SetReceiveTimeout(receiveTimeout);

            Logger::Info("Worker {} using max message size: {} bytes, timeout: {}ms",
                         workerId, maxMessageSize, receiveTimeout);
        } else {
            Logger::Warn("Worker {}: ProcessPool not available for configuration", workerId);
        }

        // Initialize database using the new DbManager interface
        auto& dbManager = DbManager::GetInstance();

        // Create configuration JSON object
        nlohmann::json dbConfig;
        dbConfig["host"] = config.GetDatabaseHost();
        dbConfig["port"] = config.GetDatabasePort();
        dbConfig["name"] = config.GetDatabaseName();
        dbConfig["user"] = config.GetDatabaseUser();
        dbConfig["password"] = config.GetDatabasePassword();

        // Add connection pool settings
        dbConfig["max_connections"] = config.GetInt("database.max_connections", 20);
        dbConfig["min_connections"] = config.GetInt("database.min_connections", 5);
        dbConfig["connection_timeout_ms"] = config.GetInt("database.connection_timeout_ms", 5000);

        if (!dbManager.Initialize(path_config)) {
            Logger::Error("Worker {} failed to initialize database", workerId);
            return;
        }

        // Connect to the database
        if (!dbManager.Connect()) {
            Logger::Error("Worker {} failed to connect to database", workerId);
            return;
        }

        Logger::Info("Worker {} database connection established successfully using backend: {}",
                     workerId, config.GetDatabaseBackend());

        // Initialize game logic with 3D world system
        auto& gameLogic = GameLogic::GetInstance();

        // Configure world settings using the newly added methods
        GameLogic::WorldConfig worldConfig;
        worldConfig.seed = config.GetWorldSeed() + workerId;
        worldConfig.viewDistance = config.GetViewDistance();
        worldConfig.chunkSize = config.GetChunkSize();
        worldConfig.maxActiveChunks = config.GetMaxActiveChunks();
        worldConfig.terrainScale = config.GetTerrainScale();
        worldConfig.maxTerrainHeight = config.GetMaxTerrainHeight();
        worldConfig.waterLevel = config.GetWaterLevel();

        gameLogic.SetWorldConfig(worldConfig);

        // Initialize and run server with group configuration
        // Note: GameServer constructor must be updated to accept WorkerGroupConfig
        GameServer server(groupConfig, config);   // Pass group config and global config

        // Set session factory - using lambda with necessary captures
        if (groupConfig.protocol == "binary") {
            server.SetSessionFactory([workerId, processPool, &groupConfig]
                                     (asio::ip::tcp::socket socket,
                                      std::shared_ptr<asio::ssl::context> sslCtx)
            {
                auto session = std::make_shared<GameSession>(std::move(socket), sslCtx);
                Logger::Debug("Worker {} created new game session {}",
                            workerId, session->GetSessionId());

                // Message handler - simplified for demonstration
                session->SetMessageHandler([session, workerId, processPool](const nlohmann::json& msg) {
                    try {
                        std::string msgType = msg.value("type", "");
                        Logger::Debug("Worker {} processing message type: {}", workerId, msgType);
                        // Check if message is for inter-process communication
                        if (msgType == "ipc_message" && processPool) { // Extract IPC message details
                            if (msg.contains("target_worker") && msg.contains("payload")) {
                                int targetWorker = msg["target_worker"];
                                std::string payload = msg["payload"].dump();
                                // Send to another worker via master using new protocol
                                if (processPool->SendToWorker(targetWorker, payload)) {
                                    Logger::Debug("Worker {} sent IPC message to worker {}",
                                                workerId, targetWorker);
                                } else {
                                    Logger::Error("Worker {} failed to send IPC message to worker {}",
                                                workerId, targetWorker);
                                }
                            }
                        } else { // Regular game message - delegate to game logic
                            GameLogic::GetInstance().HandleMessage(session->GetSessionId(), msg);
                        }
                    } catch (const std::exception& e) {
                        Logger::Error("Worker {} error processing message: {}", workerId, e.what());
                        session->SendError("Internal server error", 500);
                    }
                });

                // --- Binary message handler ---
                session->SetDefaultBinaryMessageHandler([session, workerId](uint16_t type,
                                                        const std::vector<uint8_t>& data) {
                    GameLogic::GetInstance().HandleBinaryMessage(session->GetSessionId(), type, data);
                });

                session->SetCloseHandler([session, workerId]() { // Close handler
                    Logger::Info("Worker {} session {} closing", workerId, session->GetSessionId());
                    GameLogic::GetInstance().OnPlayerDisconnected(session->GetSessionId());
                    Logger::Debug("Worker {} session {} cleanup complete", workerId, session->GetSessionId());
                });
                return session;
            });
        } else if (groupConfig.protocol == "websocket") {
            server.SetWebSocketConnectionFactory([workerId, processPool, &groupConfig](asio::ip::tcp::socket socket, std::shared_ptr<asio::ssl::context> sslCtx) {
                // Create a WebSocketConnection (which handles the upgrade)
                auto wsConn = std::make_shared<WebSocketProtocol::WebSocketConnection>(std::move(socket));
                if (sslCtx) {
                    // For wss, we need to do SSL handshake before WebSocket upgrade.
                    // This is more complex; we might need a separate SSL stream.
                    // For now, assume SSL is handled by the acceptor (e.g., using asio::ssl::stream).
                    // We'll need to extend WebSocketConnection to accept an SSL stream.
                    // This is a more advanced integration; for simplicity, we can require SSL to be handled by the listener (i.e., wss://) which will already have an SSL stream.
                    // The current WebSocketConnection doesn't support SSL; we may need to create an SSL version.
                    // As a simplification, we can defer SSL WebSocket support.
                }
                return wsConn;
            });
        }

        // Pass connection manager to game logic before initialization
        gameLogic.SetConnectionManager(ConnectionManager::GetInstancePtr());
        gameLogic.Initialize();

        // Preload world data if configured
        if (config.ShouldPreloadWorld()) {
            Logger::Info("Worker {} preloading world data...", workerId);
            gameLogic.PreloadWorldData(config.GetWorldPreloadRadius());
        }

        // Initialize and run server
        if (server.Initialize()) {
            Logger::Info("Worker {} game server initialized on {}:{} (protocol: {})",
                         workerId, groupConfig.host, groupConfig.port, groupConfig.protocol);

            // Start background world maintenance thread
            std::atomic<bool> worldMaintenanceRunning{true};
            std::thread worldMaintenanceThread([&gameLogic, &worldMaintenanceRunning, workerId, processPool]() {
                Logger::Info("Worker {} starting world maintenance thread", workerId);

                auto lastCleanupTime = std::chrono::steady_clock::now();
                auto lastIPCCheckTime = std::chrono::steady_clock::now();

                while (worldMaintenanceRunning && !g_shutdown.load()) {
                    auto currentTime = std::chrono::steady_clock::now();

                    // World maintenance every 30 seconds
                    auto elapsedWorld = std::chrono::duration_cast<std::chrono::seconds>(
                        currentTime - lastCleanupTime);

                    if (elapsedWorld.count() >= 30) {
                        Logger::Debug("Worker {} performing periodic world maintenance", workerId);
                        gameLogic.PerformMaintenance();
                        lastCleanupTime = currentTime;
                    }

                    // Check for IPC messages from master every 100ms
                    auto elapsedIPC = std::chrono::duration_cast<std::chrono::milliseconds>(
                        currentTime - lastIPCCheckTime);

                    if (elapsedIPC.count() >= 100 && processPool) {
                        // Use new ReceiveFromMaster with message length protocol
                        std::string message = processPool->ReceiveFromMaster();
                        if (!message.empty()) {
                            try {
                                // Parse JSON message
                                auto jsonMsg = nlohmann::json::parse(message);
                                Logger::Debug("Worker {} received IPC message: {}", workerId, jsonMsg.dump());

                                // Handle IPC message in game logic
                                gameLogic.HandleIPCMessage(jsonMsg);
                            } catch (const std::exception& e) {
                                Logger::Error("Worker {} failed to parse IPC message: {}", workerId, e.what());
                            }
                        }
                        lastIPCCheckTime = currentTime;
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

                Logger::Info("Worker {} world maintenance thread stopped", workerId);
            });

            // Start the server (blocks until shutdown)
            Logger::Info("Worker {} starting server loop", workerId);
            server.Run();

            // Stop maintenance thread
            worldMaintenanceRunning = false;
            if (worldMaintenanceThread.joinable()) {
                worldMaintenanceThread.join();
            }

        } else {
            Logger::Critical("Worker {} failed to initialize server", workerId);
        }

        // Cleanup
        Logger::Info("Worker {} beginning cleanup...", workerId);
        gameLogic.Shutdown();

        // Disconnect and shutdown database connections
        dbManager.Disconnect();
        dbManager.Shutdown();

        Logger::Info("Worker {} shutdown complete", workerId);
    }
    catch (const std::exception& e) {
        Logger::Critical("Worker {} caught unhandled exception: {}", workerId, e.what());
        // Allow logs to flush
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    catch (...) {
        Logger::Critical("Worker {} caught unknown exception", workerId);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}


int main(int argc, char* argv[]) {
    // Set up signal handlers
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    // Initialize a default logger (no config needed)
    Logger::InitializeDefaults();

    // Load configuration
    std::string conf_path = "config/core.json";
    auto& config = ConfigManager::GetInstance();
    if (!config.LoadConfig(conf_path)) {
        //std::cerr << "Failed to load configuration" << std::endl;
        Logger::Critical("Failed to load configuration.");
        return 1;
    }
    else {
        //std::cout << "Success to load configuration" << std::endl;
        Logger::Info("Success to load configuration.");
    }

    // Initialize logger with the actual config settings
    Logger::Initialize();

    // Ensure the target database exists (master process only)
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

    // Get worker groups from config
    auto groups = config.GetWorkerGroups();
    if (groups.empty()) {
        Logger::Critical("No worker groups configured");
        return 1;
    }

    // Create process pool with groups
    ProcessPool processPool(groups);

    // Configure process pool message protocol from config
    uint32_t maxMessageSize = config.GetInt("process.max_message_size", 1048576); // 1MB default
    uint32_t receiveTimeout = config.GetInt("process.receive_timeout_ms", 1000); // 1 second default

    processPool.SetMaxMessageSize(maxMessageSize);
    processPool.SetReceiveTimeout(receiveTimeout);

    Logger::Info("Process pool configured: max message size = {} bytes, timeout = {}ms",
                 maxMessageSize, receiveTimeout);

    // Create a lambda that captures processPool pointer and group configs (though group config will be passed by worker)
    // But we need to pass the global config path. The worker will load it itself.
    auto workerMainWithPool = [&processPool, &conf_path](int workerId, const WorkerGroupConfig& groupConfig) {
        WorkerMain(workerId, groupConfig, &processPool, conf_path);
    };

    // Set worker main function with the new signature
    processPool.SetWorkerMain(workerMainWithPool);

    // Initialize and run process pool (will fork workers)
    Logger::Info("Starting {} worker processes", processPool.GetTotalWorkerCount());
    processPool.Run();

    // Send test messages to workers using new protocol
    std::thread masterMessagingThread([&processPool]() {
        std::this_thread::sleep_for(std::chrono::seconds(3)); // Wait for workers to start

        Logger::Info("Master process starting IPC message test");

        // Send a test message to each worker
        int totalWorkers = processPool.GetTotalWorkerCount();
        for (int i = 0; i < totalWorkers; i++) {
            // Skip dead workers
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

    // Wait for shutdown signal
    Logger::Info("Master process waiting for shutdown signal...");
    while (!g_shutdown.load()) {
        // Periodically check worker health
        int totalWorkers = processPool.GetTotalWorkerCount();
        for (int i = 0; i < totalWorkers; i++) {
            if (!processPool.IsWorkerAlive(i)) {
                Logger::Warn("Master detected worker {} is not alive", i);
            }
        }

        // Short sleep to allow quick response to shutdown
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // If shutdown requested during sleep, exit loop immediately
        if (g_shutdown.load()) break;

        // Send periodic heartbeat to all workers
        static int heartbeatCount = 0;
        heartbeatCount++;

        for (int i = 0; i < totalWorkers; i++) {
            // Stop sending if shutdown requested
            if (g_shutdown.load()) break;

            // Skip dead workers
            if (!processPool.IsWorkerAlive(i)) {
                Logger::Debug("Worker {} is dead, skipping heartbeat", i);
                continue;
            }

            nlohmann::json heartbeat;
            heartbeat["type"] = "heartbeat";
            heartbeat["count"] = heartbeatCount;
            heartbeat["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();

            processPool.SendToWorker(i, heartbeat.dump());
        }

        // Broadcast server status to all players via workers
        static int statusUpdateCount = 0;
        statusUpdateCount++;

        if (statusUpdateCount % 10 == 0) { // Every 10 seconds (since sleep is 1 sec)
            nlohmann::json serverStatus;
            serverStatus["type"] = "server_status";
            serverStatus["online_workers"] = totalWorkers;
            serverStatus["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();

            // Send broadcast command to all workers
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

    // Stop messaging thread
    if (masterMessagingThread.joinable()) {
        masterMessagingThread.join();
    }

    // Shutdown process pool gracefully
    Logger::Info("Initiating graceful shutdown...");
    processPool.Shutdown();

    Logger::Info("Game Server shutdown complete");
    return 0;
}
