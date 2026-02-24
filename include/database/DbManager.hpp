#pragma once

#include <atomic>
#include <fstream>
#include <functional>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "logging/Logger.hpp"
#include "config/ConfigManager.hpp"

#ifdef USE_CITUS
#include "database/CitusClient.hpp"
class CitusClient;
#else
#include "database/PostgreSqlClient.hpp"
class PostgreSqlClient;
#endif


/**
 * @brief Database Manager Singleton
 *
 * Manages database connections and provides access to the appropriate
 * database backend based on configuration.
 */
class DbManager {
public:
    // Database types
    enum DatabaseType {
        POSTGRESQL,
        CITUS,
        INVALID
    };

    static DbManager& GetInstance();

    // Lifecycle Management
    bool Initialize();
    void Shutdown();
    bool IsInitialized() const { return initialized_; }

    static bool EnsureDatabaseExists(const nlohmann::json& config);

    std::string EscapeString(const std::string& input);

    // Backend Management
    bool SaveGameState(const std::string& key, const nlohmann::json& state);
    bool SetBackend(DatabaseType type, const nlohmann::json& config);
    DatabaseBackend* GetBackend() const { return backend_.get(); }
    DatabaseType GetCurrentType() const { return currentType_; }
    nlohmann::json Query(const std::string& sql) { return backend_->Query(sql); };
    nlohmann::json GetPlayer(uint64_t playerId){ return backend_->GetPlayer(playerId); };

    bool UpdatePlayerPosition(uint64_t playerId, float x, float y, float z) {
        if (backend_) {
            return backend_->UpdatePlayerPosition(playerId, x, y, z);
        }
        return false;
    }

    // Configuration
    bool LoadConfiguration(const std::string& configPath = "");
    nlohmann::json GetConfiguration() const { return config_; }

    // Connection Management
    bool Connect();
    bool Reconnect();
    void Disconnect();
    bool IsConnected() const;

    // Shard Mapping
    int GetShardId(uint64_t entityId) const;
    int GetTotalShards() const;

    // Statistics
    nlohmann::json GetStatistics() const;
    void PrintStatistics() const;

    // Migration Management
    bool RunMigrations();
    bool CheckMigrationStatus();
    bool RollbackMigration(int version);

private:
    DbManager();
    ~DbManager();

    DbManager(const DbManager&) = delete;
    DbManager& operator=(const DbManager&) = delete;

    static std::mutex instanceMutex_;
    static DbManager* instance_;

    std::unique_ptr<DatabaseBackend> backend_;
    DatabaseType currentType_;
    nlohmann::json config_;
    std::atomic<bool> initialized_;
    std::atomic<bool> connected_;

    // Statistics
    struct Statistics {
        std::atomic<int64_t> queriesExecuted{0};
        std::atomic<int64_t> queriesFailed{0};
        std::atomic<int64_t> transactionsCommitted{0};
        std::atomic<int64_t> transactionsRolledBack{0};
        std::atomic<int64_t> bytesTransferred{0};
        std::chrono::steady_clock::time_point startTime;
    };
    mutable Statistics stats_;

    // Helper methods
    bool ValidateConfiguration(const nlohmann::json& config) const;
    DatabaseType ParseDatabaseType(const std::string& typeStr) const;
    std::string DatabaseTypeToString(DatabaseType type) const;
};
