// main.cpp - Updated with ProcessPool message protocol configuration

#include <iostream>
#include <csignal>
#include <thread>
#include <chrono>
#include <atomic>

#include "config/ConfigManager.hpp"
#include "logging/Logger.hpp"
#include "network/GameServer.hpp"
#include "process/ProcessPool.hpp"
#include "game/GameLogic.hpp"

// Conditional include for database backend
#include "database/Backend.hpp"
#include "database/PostgreSQLBackend.hpp"

#ifdef USE_CITUS
#include "database/CitusClient.hpp"
#endif

// Forward declarations for missing dependencies
class GameSession;
class ConnectionManager;
class PlayerManager;

std::atomic<bool> g_shutdown(false);

// Global ProcessPool instance (or you can make it a singleton)
// For this example, we'll modify WorkerMain to accept ProcessPool as parameter
// Since we can't easily pass it, we'll use an alternative approach

void SignalHandler(int signal) {
    Logger::Info("Received signal {}, initiating shutdown...", signal);
    g_shutdown.store(true);
}

void WorkerMain(int workerId, ProcessPool* processPool = nullptr) {
    // Initialize logging with worker-specific configuration
    auto& config = ConfigManager::GetInstance();
    
    // Use worker-specific logger initialization
    Logger::Initialize("Worker" + std::to_string(workerId));
    Logger::Info("Worker {} starting 3D game world system", workerId);

    // Initialize configuration
    if (!config.LoadConfig("config/server_config.json")) {
        Logger::Critical("Worker {} failed to load configuration", workerId);
        return;
    }

    // Configure ProcessPool message protocol if processPool is provided
    if (processPool) {
        // Set message protocol configuration from config
        uint32_t maxMessageSize = config.GetUInt32("process.max_message_size", 1048576); // 1MB default
        uint32_t receiveTimeout = config.GetUInt32("process.receive_timeout_ms", 1000); // 1 second default
        
        processPool->SetMaxMessageSize(maxMessageSize);
        processPool->SetReceiveTimeout(receiveTimeout);
        
        Logger::Info("Worker {} using max message size: {} bytes, timeout: {}ms", 
                    workerId, maxMessageSize, receiveTimeout);
    } else {
        Logger::Warn("Worker {}: ProcessPool not available for configuration", workerId);
    }

    // Initialize database backend based on configuration
    std::unique_ptr<DatabaseBackend> databaseBackend;
    
    std::string backendType = config.GetDatabaseBackend();
    Logger::Info("Worker {} using database backend: {}", workerId, backendType);
    
    if (backendType == "citus") {
#ifdef USE_CITUS
        // Initialize Citus client
        auto& dbClient = CitusClient::GetInstance();
        
        // Build connection string
        std::string connInfo =
            "host=" + config.GetDatabaseHost() +
            " port=" + std::to_string(config.GetDatabasePort()) +
            " dbname=" + config.GetDatabaseName() +
            " user=" + config.GetDatabaseUser() +
            " password=" + config.GetDatabasePassword();
        
        // Get worker nodes for Citus
        std::vector<std::string> workerNodes = config.GetCitusWorkerNodes();
        
        if (!dbClient.Initialize(connInfo, workerNodes)) {
            Logger::Error("Worker {} failed to initialize Citus database", workerId);
            return;
        }
        
        // Store backend for later use
        databaseBackend = DatabaseBackend::CreateBackend("citus");
#else
        Logger::Error("Citus backend requested but not compiled with Citus support");
        Logger::Error("Falling back to PostgreSQL backend");
        databaseBackend = DatabaseBackend::CreateBackend("postgresql");
#endif
    } else {
        // Use PostgreSQL backend
        databaseBackend = DatabaseBackend::CreateBackend("postgresql");
        
        // Build connection string
        std::string connInfo =
            "host=" + config.GetDatabaseHost() +
            " port=" + std::to_string(config.GetDatabasePort()) +
            " dbname=" + config.GetDatabaseName() +
            " user=" + config.GetDatabaseUser() +
            " password=" + config.GetDatabasePassword();
        
        // Empty worker nodes for PostgreSQL
        std::vector<std::string> workerNodes;
        
        if (!databaseBackend->Initialize(connInfo, workerNodes)) {
            Logger::Error("Worker {} failed to initialize PostgreSQL database", workerId);
            return;
        }
    }

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
    
    // Pass database backend to game logic
    gameLogic.SetDatabaseBackend(std::move(databaseBackend));
    gameLogic.Initialize();

    // Preload world data if configured
    if (config.ShouldPreloadWorld()) {
        Logger::Info("Worker {} preloading world data...", workerId);
        gameLogic.PreloadWorldData(config.GetWorldPreloadRadius());
    }

    // Create game server
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

    // Initialize and run server
    if (server.Initialize()) {
        Logger::Info("Worker {} 3D game server initialized on port {}", 
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
    Logger::Info("Worker {} shutdown complete", workerId);
}

int main(int argc, char* argv[]) {
    // Set up signal handlers
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    // Load configuration
    auto& config = ConfigManager::GetInstance();
    if (!config.LoadConfig("config/server_config.json")) {
        std::cerr << "Failed to load configuration" << std::endl;
        return 1;
    }

    // Initialize logging
    Logger::Initialize();

    Logger::Info("Starting 3D Game Server v2.0.0 with LogicCore System");
    Logger::Info("Database Backend: {}", config.GetDatabaseBackend());
    Logger::Info("World Seed: {}", config.GetWorldSeed());
    Logger::Info("View Distance: {} chunks", config.GetViewDistance());
    Logger::Info("Chunk Size: {} units", config.GetChunkSize());

    // Create process pool
    int processCount = config.GetProcessCount();
    ProcessPool processPool(processCount);

    // Configure process pool message protocol from config
    uint32_t maxMessageSize = config.GetUInt32("process.max_message_size", 1048576); // 1MB default
    uint32_t receiveTimeout = config.GetUInt32("process.receive_timeout_ms", 1000); // 1 second default
    
    processPool.SetMaxMessageSize(maxMessageSize);
    processPool.SetReceiveTimeout(receiveTimeout);
    
    Logger::Info("Process pool configured: max message size = {} bytes, timeout = {}ms",
                maxMessageSize, receiveTimeout);

    // Create a lambda to capture processPool pointer for WorkerMain
    // Note: This is a workaround since WorkerMain signature can't be changed easily
    // A better approach would be to redesign ProcessPool to pass context to workers
    auto workerMainWithPool = [&processPool](int workerId) {
        WorkerMain(workerId, &processPool);
    };

    // Set worker main function with process pool context
    processPool.SetWorkerMain(workerMainWithPool);

    // Initialize as master process
    Logger::Info("Starting {} worker processes for 3D world", processCount);
    processPool.Run();

    // Example: Send test messages to workers using new protocol
    std::thread masterMessagingThread([&processPool, processCount]() {
        std::this_thread::sleep_for(std::chrono::seconds(3)); // Wait for workers to start
        
        Logger::Info("Master process starting IPC message test");
        
        // Send a test message to each worker
        for (int i = 0; i < processCount; i++) {
            nlohmann::json testMsg;
            testMsg["type"] = "welcome";
            testMsg["from"] = "master";
            testMsg["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
            testMsg["worker_id"] = i;
            testMsg["message"] = "Welcome to the 3D game server!";
            
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
        // Example: Periodically check worker health
        for (int i = 0; i < processCount; i++) {
            if (!processPool.IsWorkerAlive(i)) {
                Logger::Warn("Master detected worker {} is not alive", i);
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // Send periodic heartbeat to all workers
        static int heartbeatCount = 0;
        heartbeatCount++;
        
        for (int i = 0; i < processCount; i++) {
            nlohmann::json heartbeat;
            heartbeat["type"] = "heartbeat";
            heartbeat["count"] = heartbeatCount;
            heartbeat["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
            
            processPool.SendToWorker(i, heartbeat.dump());
        }
    }

    // Stop messaging thread
    if (masterMessagingThread.joinable()) {
        masterMessagingThread.join();
    }

    // Shutdown process pool gracefully
    Logger::Info("Initiating graceful shutdown...");
    processPool.Shutdown();

    Logger::Info("3D Game Server shutdown complete");
    return 0;
}