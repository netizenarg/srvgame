#pragma once

#include <functional>
#include <memory>
#include <asio.hpp>
#include <nlohmann/json.hpp>

//#include "logging/Logger.hpp"
//#include "database/DbManager.hpp"

#include "game/Player.hpp"
#include "game/PlayerManager.hpp"

class DatabaseService {
public:
    explicit DatabaseService(asio::io_context& main_io, size_t num_threads = 4);
    ~DatabaseService();

    void asyncGetPlayer(uint64_t playerId, 
                        std::function<void(nlohmann::json)> callback);
    void asyncSavePlayerData(uint64_t playerId, const nlohmann::json& data,
                             std::function<void(bool)> callback);
    void asyncPlayerExists(uint64_t playerId,
                           std::function<void(bool)> callback);
    void asyncPlayerExists(const std::string& username,
                           std::function<void(bool)> callback);
    void asyncCreatePlayer(const std::string& username,
                           std::function<void(std::shared_ptr<Player>)> callback);
    void asyncAuthenticatePlayer(const std::string& username, const std::string& password,
                                 std::function<void(bool)> callback);
    void asyncGetPlayerByName(const std::string& username,
                              std::function<void(nlohmann::json)> callback);
    void asyncSaveChunkData(int chunkX, int chunkZ, const nlohmann::json& data,
                            std::function<void(bool)> callback);
    void asyncLoadChunkData(int chunkX, int chunkZ,
                            std::function<void(nlohmann::json)> callback);

    void shutdown();

private:
    asio::io_context& main_io_;
    asio::thread_pool db_pool_;
    std::unique_ptr<asio::executor_work_guard<asio::thread_pool::executor_type>> work_guard_;
};
