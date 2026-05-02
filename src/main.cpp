#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include "logging/Logger.hpp"
#include "config/ConfigManager.hpp"
#include "database/DbManager.hpp"
#include "database/DbService.hpp"
#include "process/ProcessPool.hpp"
#include "network/GameServer.hpp"
#include "game/GameLogic.hpp"

std::atomic<bool> g_shutdown(false);

void SignalHandler(int signal) {
    (void)signal;
    g_shutdown.store(true);
}

void worker(int workerId, const WorkerGroupConfig& groupConfig,
            int masterReadFd, const std::string& path_config = "config.json")
{
    try
    {
        asio::io_context ipc_io;
        asio::signal_set signals(ipc_io, SIGINT, SIGTERM);
        signals.async_wait([&](const asio::error_code& ec, int signo) {
            if (!ec) {
                Logger::Info("Worker {} received signal {}", workerId, signo);
                g_shutdown.store(true);
                ipc_io.stop();
            }
        });
        asio::posix::stream_descriptor masterPipe(ipc_io);
        if (masterReadFd != -1)
            masterPipe.assign(masterReadFd);
        std::thread ipc_thread([&]() { ipc_io.run(); });

        auto& config = ConfigManager::GetInstance();
        Logger::InitializeWithWorkerId(workerId);

        Logger::Info("Worker {} starting game world system for group: {} ({}:{})",
                     workerId, groupConfig.protocol, groupConfig.host, groupConfig.port);

        if (!config.LoadConfig(path_config)) {
            Logger::Critical("Worker {} failed to load configuration", workerId);
            return;
        }

        auto& dbManager = DbManager::GetInstance();
        nlohmann::json dbConfig;
        dbConfig["host"] = config.GetDatabaseHost();
        dbConfig["port"] = config.GetDatabasePort();
        dbConfig["name"] = config.GetDatabaseName();
        dbConfig["user"] = config.GetDatabaseUser();
        dbConfig["password"] = config.GetDatabasePassword();
        dbConfig["max_connections"] = config.GetInt("database.pool.max_connections", 20);
        dbConfig["min_connections"] = config.GetInt("database.pool.min_connections", 5);
        dbConfig["connection_timeout_ms"] = config.GetInt("database.pool.connection_timeout_ms", 5000);

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
        server.InitSessionFactory(workerId, nullptr, gameLogic);
        server.RegisterCallbacks(groupConfig.protocol, gameLogic);

        gameLogic.SetConnectionManager(ConnectionManager::GetInstancePtr());
        gameLogic.Initialize();

        DatabaseService dbService(server.GetIoContext(), config.GetInt("database.pool.threads", 2));
        gameLogic.SetDatabaseService(&dbService);

        if (config.ShouldPreloadWorld()) {
            Logger::Info("Worker {} preloading world data...", workerId);
            gameLogic.PreloadWorldData(config.GetWorldPreloadRadius());
        }

        if (server.Initialize()) {
            Logger::Info("Worker {} game server initialized on {}:{} (protocol: {})",
                         workerId, groupConfig.host, groupConfig.port, groupConfig.protocol);

            std::thread shutdown_trigger([&server]() {
                while (!g_shutdown.load())
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                server.Shutdown();
            });
            shutdown_trigger.detach();

            std::thread watchdog([workerId]() {
                while (!g_shutdown.load())
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::this_thread::sleep_for(std::chrono::seconds(35));
                Logger::Error("Worker {} watchdog triggered – forcing exit", workerId);
                _exit(1);
            });
            watchdog.detach();

            Logger::Info("Worker {} entering server.Run()", workerId);
            server.Run();
        } else {
            Logger::Critical("Worker {} failed to initialize server", workerId);
        }

        ipc_io.stop();
        if (ipc_thread.joinable()) ipc_thread.join();

        Logger::Info("Worker {} beginning cleanup...", workerId);
        gameLogic.Shutdown();
        dbService.shutdown();
        dbManager.Disconnect();
        dbManager.Shutdown();

        Logger::Info("Worker {} shutdown complete", workerId);
    }
    catch (const std::exception& err) {
        Logger::Critical("Worker {} caught unhandled exception: {}", workerId, err.what());
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

    Logger::InitializeDefaults();
    std::string conf_path = "config/core.json";
    auto& config = ConfigManager::GetInstance();
    if (!config.LoadConfig(conf_path)) {
        Logger::Critical("Failed to load configuration.");
        return 1;
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

    Logger::Info("Starting Game Server");
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

    asio::io_context io;
    ProcessPool processPool(io, groups);

    processPool.SetWorkerMain(
    [&conf_path](int workerId, const WorkerGroupConfig& groupConfig, int masterReadFd) {
        worker(workerId, groupConfig, masterReadFd, conf_path);
    });

    processPool.Initialize();
    Logger::Info("Starting {} worker processes", processPool.GetTotalWorkerCount());

    Logger::Info("Master entering ASIO run loop...");
    io.run();
    processPool.Shutdown();
    Logger::Info("Game Server shutdown complete");
    return 0;
}
