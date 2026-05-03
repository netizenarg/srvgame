#include "database/DbService.hpp"

std::vector<DatabaseService*> DatabaseService::instances_;
std::mutex DatabaseService::instancesMutex_;

DatabaseService::DatabaseService(asio::io_context& main_io, size_t num_threads)
    : main_io_(main_io), db_pool_(num_threads)
{
    std::lock_guard<std::mutex> lock(instancesMutex_);
    work_guard_ = std::make_unique<asio::executor_work_guard<asio::thread_pool::executor_type>>(
        asio::make_work_guard(db_pool_));
    instances_.push_back(this);
    Logger::Info("DatabaseService started with threads count {}", num_threads);
}

DatabaseService::~DatabaseService() {
    {
        std::lock_guard<std::mutex> lock(instancesMutex_);
        auto it = std::find(instances_.begin(), instances_.end(), this);
        if (it != instances_.end()) instances_.erase(it);
    }
    shutdown();
    Logger::Warn("DatabaseService destroyed");
}

void DatabaseService::shutdown() {
    work_guard_.reset(); // Allow pool to run out of work
    db_pool_.join(); // Wait for all tasks to complete
    Logger::Info("DatabaseService::shutdown");
}

void DatabaseService::ShutdownAll() {
    std::lock_guard<std::mutex> lock(instancesMutex_);
    for (auto* db : instances_) {
        db->shutdown();
    }
    instances_.clear();
}

void DatabaseService::asyncGetPlayer(uint64_t playerId,
                                     std::function<void(nlohmann::json)> callback) {
    asio::post(db_pool_, [this, playerId, cb = std::move(callback)]() {
        auto result = DbManager::GetInstance().GetPlayer(playerId);
        asio::post(main_io_, [cb = std::move(cb), res = std::move(result)]() {
            cb(res);
        });
    });
}

void DatabaseService::asyncSavePlayerData(uint64_t playerId, const nlohmann::json& data,
                                          std::function<void(bool)> callback) {
    asio::post(db_pool_, [this, playerId, data, cb = std::move(callback)]() {
        bool success = DbManager::GetInstance().GetBackend()->SavePlayerData(playerId, data);
        asio::post(main_io_, [cb = std::move(cb), success]() { cb(success); });
    });
}

void DatabaseService::asyncPlayerExists(uint64_t playerId,
                                        std::function<void(bool)> callback) {
    asio::post(db_pool_, [this, playerId, cb = std::move(callback)]() {
        bool exists = DbManager::GetInstance().GetBackend()->PlayerExists(playerId);
        asio::post(main_io_, [cb = std::move(cb), exists]() { cb(exists); });
    });
}

void DatabaseService::asyncCreatePlayer(const std::string& username,
                                        std::function<void(std::shared_ptr<Player>)> callback) {
    asio::post(db_pool_, [this, username, cb = std::move(callback)]() {
        auto& pm = PlayerManager::GetInstance();
        auto player = pm.CreatePlayer(username);
        asio::post(main_io_, [cb = std::move(cb), player]() { cb(player); });
    });
}

void DatabaseService::asyncAuthenticatePlayer(const std::string& username, const std::string& password,
                                              std::function<void(bool)> callback) {
    asio::post(db_pool_, [this, username, password, cb = std::move(callback)]() {
        bool success = PlayerManager::GetInstance().AuthenticatePlayer(username, password);
        asio::post(main_io_, [cb = std::move(cb), success]() { cb(success); });
    });
}

void DatabaseService::asyncGetPlayerByName(const std::string& username,
                                           std::function<void(nlohmann::json)> callback) {
    asio::post(db_pool_, [this, username, cb = std::move(callback)]() {
        auto player = PlayerManager::GetInstance().GetPlayerByUsername(username);
        nlohmann::json result = player ? player->Serialize() : nlohmann::json();
        asio::post(main_io_, [cb = std::move(cb), res = std::move(result)]() { cb(res); });
    });
}

void DatabaseService::asyncPlayerExists(const std::string& username,
                                        std::function<void(bool)> callback) {
    asio::post(db_pool_, [this, username, cb = std::move(callback)]() {
        bool exists = PlayerManager::GetInstance().PlayerExists(username);
        asio::post(main_io_, [cb = std::move(cb), exists]() { cb(exists); });
    });
}

void DatabaseService::asyncSaveChunkData(int chunkX, int chunkZ, const nlohmann::json& data,
                                         std::function<void(bool)> callback) {
    asio::post(db_pool_, [this, chunkX, chunkZ, data, cb = std::move(callback)]() {
        bool success = DbManager::GetInstance().GetBackend()->SaveChunkData(chunkX, chunkZ, data);
        asio::post(main_io_, [cb = std::move(cb), success]() { cb(success); });
    });
}

void DatabaseService::asyncLoadChunkData(int chunkX, int chunkZ,
                                         std::function<void(nlohmann::json)> callback) {
    asio::post(db_pool_, [this, chunkX, chunkZ, cb = std::move(callback)]() {
        auto data = DbManager::GetInstance().GetBackend()->LoadChunkData(chunkX, chunkZ);
        asio::post(main_io_, [cb = std::move(cb), data = std::move(data)]() { cb(data); });
    });
}
