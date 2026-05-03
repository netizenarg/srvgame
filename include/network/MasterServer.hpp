#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <asio.hpp>

#include "logging/Logger.hpp"
#include "config/ConfigManager.hpp"
#include "database/DbService.hpp"
#include "process/ProcessPool.hpp"
#include "network/BinaryProtocol.hpp"
#include "network/ClientListener.hpp"
#include "game/GameLogic.hpp"

class DatabaseService;

class MasterServer {
public:
    MasterServer(asio::io_context& io,
                 const std::vector<WorkerGroupConfig>& workerGroups,
                 const ConfigManager& config,
                 GameLogic& gameLogic,
                 DatabaseService& dbService,
                 const std::string& configPath);
    void Initialize();
    void Run();
    void Shutdown();

    static void WorkerClient(int workerId, const WorkerGroupConfig& groupConfig,
                           int masterReadFd, const std::string& configPath);

private:
    asio::io_context& io_;
    GameLogic& gameLogic_;
    ProcessPool processPool_;
    const ConfigManager& config_;
    std::string configPath_;

    std::unordered_map<uint64_t, uint64_t> sessionToVirtual_;
    std::function<void(uint64_t, const std::vector<uint8_t>&)> sendReplyCb_;
    std::function<uint64_t(uint64_t, int)> assignVirtualId_;
    std::atomic<uint32_t> nextPersistentId_{1};

    void WireCallbacks();
    void SendResponse(uint64_t sessionId, const std::vector<uint8_t>& buffer);
    void StartShutdownWatcher();
};
