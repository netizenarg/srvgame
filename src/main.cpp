#include <atomic>
#include <csignal>
#include <iostream>

#include <asio.hpp>

#include "logging/Logger.hpp"
#include "config/ConfigManager.hpp"
#include "database/DbManager.hpp"
#include "database/DbService.hpp"
#include "network/MasterServer.hpp"
#include "game/GameLogic.hpp"

std::atomic<bool> g_shutdown(false);
int g_signal_pipe[2];

void SignalHandler(int signo) {
    char c = static_cast<char>(signo);
    ssize_t res = write(g_signal_pipe[1], &c, 1);
    (void)res;
    g_shutdown.store(true);
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    if (pipe(g_signal_pipe) == -1) {
        perror("pipe");
        return 1;
    }
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    std::string conf_path = "config/core.json";
    auto& config = ConfigManager::GetInstance();
    if (!config.LoadConfig(conf_path)) {
        std::cerr << "Failed to load configuration." << std::endl;
        return 1;
    }

    asio::io_context log_io;
    auto log_service = std::make_shared<LogService>(log_io, config.GetJson("logging"));
    std::thread logThread([&] {
        log_service->Start();
        log_io.run();
    });

    DbManager& dbManager = DbManager::GetInstance();
    if (!dbManager.EnsureDatabaseExists(conf_path)) {
        Logger::Critical("Failed to ensure database existence. Exiting.");
        return 1;
    }

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
    master.Shutdown();

    dbManager.Disconnect();
    dbManager.Shutdown();

    Logger::Info("Game Server shutdown complete");

    log_service->Stop();
    log_io.stop();
    if (logThread.joinable()) logThread.join();
    std::_Exit(0);
    return 0;
}
