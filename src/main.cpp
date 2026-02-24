#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include "config/ConfigManager.hpp"
#include "logging/Logger.hpp"
#include "network/GameServer.hpp"
#include "process/ProcessPool.hpp"
#include "database/DbManager.hpp"
#include "game/GameLogic.hpp"


std::atomic<bool> g_shutdown(false);

void SignalHandler(int signal) {
    (void)signal;
    //Logger::Info("Received signal {}, initiating shutdown...", signal);
    g_shutdown.store(true);
}

void WorkerMain(int workerId, ProcessPool* processPool = nullptr, const std::string& path_config = "config.json") {
    try {
        // Initialize logging with worker-specific configuration
        auto& config = ConfigManager::GetInstance();

        // Use worker-specific logger initialization
        Logger::InitializeWithWorkerId(workerId);
        Logger::Info("Worker {} starting game world system", workerId);

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

        std::string backendType = config.GetDatabaseBackend();
        Logger::Info("Worker {} using database backend: {}", workerId, backendType);

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

        // Set backend type
        if (backendType == "citus") {
            // Set backend type
            DbManager::DatabaseType dbType = DbManager::CITUS;

            // Add Citus worker nodes if configured
            std::vector<std::string> workerNodes = config.GetCitusWorkerNodes();
            if (!workerNodes.empty()) {
                nlohmann::json nodesArray = nlohmann::json::array();
                for (const auto& node : workerNodes) {
                    nodesArray.push_back(node);
                }
                dbConfig["worker_nodes"] = nodesArray;
            }

            // Set backend configuration
            if (!dbManager.SetBackend(dbType, dbConfig)) {
                Logger::Error("Worker {} failed to set Citus database backend", workerId);
                return;
            }
        } else {
            // Use PostgreSQL backend
            DbManager::DatabaseType dbType = DbManager::POSTGRESQL;

            if (!dbManager.SetBackend(dbType, dbConfig)) {
                Logger::Error("Worker {} failed to set PostgreSQL database backend", workerId);
                return;
            }
        }

        // Initialize the database manager
        if (!dbManager.Initialize()) {
            Logger::Error("Worker {} failed to initialize database", workerId);
            return;
        }

        // Connect to the database
        if (!dbManager.Connect()) {
            Logger::Error("Worker {} failed to connect to database", workerId);
            return;
        }

        Logger::Info("Worker {} database connection established successfully", workerId);

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

        // Game logic will now use DbManager singleton directly instead of being passed a backend

        // Initialize and run server
        GameServer server(config);

        // Set session factory - using lambda with necessary captures
        server.SetSessionFactory([workerId, processPool](asio::ip::tcp::socket socket) {
            auto session = std::make_shared<GameSession>(std::move(socket));

            Logger::Debug("Worker {} created new game session {}",
                          workerId, session->GetSessionId());

            // Message handler - simplified for demonstration
            session->SetMessageHandler([session, workerId, processPool](const nlohmann::json& msg) {
                try {
                    std::string msgType = msg.value("type", "");
                    Logger::Debug("Worker {} processing message type: {}", workerId, msgType);

                    // Check if message is for inter-process communication
                    if (msgType == "ipc_message" && processPool) {
                        // Extract IPC message details
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
                    } else {
                        // Regular game message - delegate to game logic
                        GameLogic::GetInstance().HandleMessage(session->GetSessionId(), msg);
                    }
                } catch (const std::exception& e) {
                    Logger::Error("Worker {} error processing message: {}", workerId, e.what());
                    session->SendError("Internal server error", 500);
                }
            });

            // Close handler
            session->SetCloseHandler([session, workerId]() {
                Logger::Info("Worker {} session {} closing", workerId, session->GetSessionId());

                GameLogic::GetInstance().OnPlayerDisconnected(session->GetSessionId());
                Logger::Debug("Worker {} session {} cleanup complete", workerId, session->GetSessionId());
            });

            return session;
        });

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
            Logger::Info("Worker {} game server initialized on port {}",
                         workerId, config.GetServerPort());

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

            // Start the server
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
        std::cerr << "Failed to load configuration" << std::endl;
        return 1;
    }
    else {
        std::cout << "Success to load configuration" << std::endl;
    }

    // Initialize logger with the actual config settings
    Logger::Initialize();

    // Ensure the target database exists (master process only)
    auto dbConfig = config.GetJson("database");
    if (!DbManager::EnsureDatabaseExists(dbConfig)) {
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

    // Create process pool
    int processCount = config.GetProcessCount();
    ProcessPool processPool(processCount);

    // Configure process pool message protocol from config
    uint32_t maxMessageSize = config.GetInt("process.max_message_size", 1048576); // 1MB default
    uint32_t receiveTimeout = config.GetInt("process.receive_timeout_ms", 1000); // 1 second default

    processPool.SetMaxMessageSize(maxMessageSize);
    processPool.SetReceiveTimeout(receiveTimeout);

    Logger::Info("Process pool configured: max message size = {} bytes, timeout = {}ms",
                 maxMessageSize, receiveTimeout);

    // Create a lambda to capture processPool pointer for WorkerMain
    auto workerMainWithPool = [&processPool, &conf_path](int workerId) {
        WorkerMain(workerId, &processPool, conf_path);
    };

    // Set worker main function with process pool context
    processPool.SetWorkerMain(workerMainWithPool);

    // Initialize as master process
    Logger::Info("Starting {} worker processes for world", processCount);
    processPool.Run();

    // Send test messages to workers using new protocol
    std::thread masterMessagingThread([&processPool, processCount]() {
        std::this_thread::sleep_for(std::chrono::seconds(3)); // Wait for workers to start

        Logger::Info("Master process starting IPC message test");

        // Send a test message to each worker
        for (int i = 0; i < processCount; i++) {
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
        for (int i = 0; i < processCount; i++) {
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

        for (int i = 0; i < processCount; i++) {
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
            serverStatus["online_workers"] = processCount;
            serverStatus["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();

            // Send broadcast command to all workers
            nlohmann::json broadcastMsg;
            broadcastMsg["type"] = "broadcast";
            broadcastMsg["data"] = serverStatus;

            std::string broadcastSerialized = broadcastMsg.dump();
            for (int i = 0; i < processCount; i++) {
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