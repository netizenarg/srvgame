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
#include "database/CitusClient.hpp"

// Forward declarations for missing dependencies
class GameSession;
class ConnectionManager;
class PlayerManager;

std::atomic<bool> g_shutdown(false);

void SignalHandler(int signal) {
    Logger::Info("Received signal {}, initiating shutdown...", signal);
    g_shutdown.store(true);
}

void WorkerMain(int workerId) {
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

    // Initialize database client
    auto& dbClient = CitusClient::GetInstance();
    
    // Build connection string
    std::string connInfo =
        "host=" + config.GetDatabaseHost() +
        " port=" + std::to_string(config.GetDatabasePort()) +
        " dbname=" + config.GetDatabaseName() +
        " user=" + config.GetDatabaseUser() +
        " password=" + config.GetDatabasePassword();
    
    // Get worker nodes for Citus (empty for PostgreSQL)
    std::vector<std::string> workerNodes = config.GetCitusWorkerNodes();
    
    if (!dbClient.Initialize(connInfo, workerNodes)) {
        Logger::Error("Worker {} failed to initialize database", workerId);
        return;
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
    gameLogic.Initialize();

    // Preload world data if configured
    if (config.ShouldPreloadWorld()) {
        Logger::Info("Worker {} preloading world data...", workerId);
        gameLogic.PreloadWorldData(config.GetWorldPreloadRadius());
    }

    // Create game server
    GameServer server(config);

    // Set session factory - using lambda with necessary captures
    server.SetSessionFactory([workerId](asio::ip::tcp::socket socket) {
        auto session = std::make_shared<GameSession>(std::move(socket));

        Logger::Debug("Worker {} created new game session {}",
                     workerId, session->GetSessionId());

        // Message handler - simplified for demonstration
        session->SetMessageHandler([session, workerId](const nlohmann::json& msg) {
            try {
                std::string msgType = msg.value("type", "");
                Logger::Debug("Worker {} processing message type: {}", workerId, msgType);
                
                // Delegate to game logic
                GameLogic::GetInstance().HandleMessage(session->GetSessionId(), msg);
            } catch (const std::exception& e) {
                Logger::Error("Worker {} error processing message: {}", workerId, e.what());
                session->SendError("Internal server error", 500);
            }
        });

        // Close handler
        session->SetCloseHandler([session, workerId]() {
            Logger::Info("Worker {} session {} closing", workerId, session->GetSessionId());
            
            // These would need to be properly defined
            // ConnectionManager::GetInstance().Stop(session);
            // PlayerManager::GetInstance().PlayerDisconnected(sessionId);
            
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
        std::thread worldMaintenanceThread([&gameLogic, &worldMaintenanceRunning, workerId]() {
            Logger::Info("Worker {} starting world maintenance thread", workerId);

            auto lastCleanupTime = std::chrono::steady_clock::now();

            while (worldMaintenanceRunning && !g_shutdown.load()) {
                auto currentTime = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastCleanupTime);

                if (elapsed.count() >= 30) {
                    Logger::Debug("Worker {} performing periodic world maintenance", workerId);
                    gameLogic.PerformMaintenance();  // Assuming this method exists
                    lastCleanupTime = currentTime;
                }

                std::this_thread::sleep_for(std::chrono::seconds(5));
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

    processPool.SetWorkerMain(WorkerMain);

    // Initialize as master process
    Logger::Info("Starting {} worker processes for 3D world", processCount);
    processPool.Run();

    // Wait for shutdown signal
    Logger::Info("Master process waiting for shutdown signal...");
    while (!g_shutdown.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Shutdown process pool gracefully
    Logger::Info("Initiating graceful shutdown...");
    processPool.Shutdown();

    Logger::Info("3D Game Server shutdown complete");
    return 0;
}
