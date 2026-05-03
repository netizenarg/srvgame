#include <atomic>
#include <csignal>
#include <iostream>

#include "logging/Logger.hpp"
#include "config/ConfigManager.hpp"
#include "database/DbManager.hpp"
#include "database/DbService.hpp"
#include "network/MasterServer.hpp"
#include "game/GameLogic.hpp"

std::atomic<bool> g_shutdown(false);

void SignalHandler(int) {
    g_shutdown.store(true);
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
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
    if (!dbManager.EnsureDatabaseExists(conf_path)) {
        Logger::Critical("Failed to ensure database existence. Exiting.");
        return 1;
    }
    dbManager.Disconnect();
    dbManager.Shutdown();

    Logger::Info("Starting Game Server");
    Logger::Info("Database Backend: {}", config.GetDatabaseBackend());
    Logger::Info("World Seed: {}", config.GetWorldSeed());

    auto groups = config.GetWorkerGroups();
    if (groups.empty()) {
        Logger::Critical("No worker groups configured");
        return 1;
    }

    asio::io_context io;
    GameLogic& gameLogic = GameLogic::GetInstance();
    DatabaseService dbService(io, config.GetInt("database.pool.threads", 2));
    MasterServer master(io, groups, config, gameLogic, dbService, conf_path);

    master.Initialize();
    master.Run();

    Logger::Info("Game Server shutdown complete");
    return 0;
}
