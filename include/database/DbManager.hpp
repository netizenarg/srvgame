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

#include "config/ConfigManager.hpp"
#include "database/Backend.hpp"

#ifdef USE_CITUS
#include "database/CitusClient.hpp"
#else
#ifdef USE_POSTGRESQL
#include "database/PostgreSqlClient.hpp"
#endif
#endif

#ifdef USE_SQLITE
#include "database/SQLiteClient.hpp"
#endif

enum BackendType {
    SQLITE,
    POSTGRESQL,
    CITUS,
    INVALID
};

class DbManager {
public:
    static DbManager& GetInstance();

    bool Initialize(const std::string& configPath = "");
    void Shutdown();
    bool IsInitialized() const;

    const SQLProvider& GetSQLProvider() const;
    bool LoadSQLForBackend();

    bool EnsureDatabaseExists(const std::string& configPath = "");

    std::string EscapeString(const std::string& input);

    bool SaveGameState(const std::string& key, const nlohmann::json& state);
    bool SetBackend(BackendType type, const nlohmann::json& config);
    DatabaseBackend* GetBackend() const;
    BackendType GetCurrentType() const;
    nlohmann::json GetPlayer(uint64_t playerId);

    nlohmann::json Query(const std::string& sql);
    nlohmann::json QueryWithParams(const std::string& sql, const std::vector<std::string>& params);
    bool Execute(const std::string& sql);
    bool ExecuteWithParams(const std::string& sql, const std::vector<std::string>& params);

    bool UpdatePlayerPosition(uint64_t playerId, float x, float y, float z);

    bool LoadConfiguration(const std::string& configPath = "");
    nlohmann::json GetConfiguration() const;

    bool Connect();
    bool Reconnect();
    void Disconnect();
    bool IsConnected() const;

    int GetShardId(uint64_t entityId) const;
    int GetTotalShards() const;

    nlohmann::json GetStatistics() const;
    void PrintStatistics() const;

    bool RunMigrations();
    bool CheckMigrationStatus();
    bool RollbackMigration(int version);

    bool CreateDefaultTablesIfNotExist();
    bool CheckDefaultTablesExist();

private:
    DbManager();
    ~DbManager();

    DbManager(const DbManager&) = delete;
    DbManager& operator=(const DbManager&) = delete;

    static std::mutex instanceMutex_;
    static DbManager* instance_;
    SQLProvider sqlProvider_;
    std::unique_ptr<DatabaseBackend> backend_;
    BackendType currentType_;
    nlohmann::json config_;
    std::atomic<bool> running_;
    std::atomic<bool> connected_;

    struct Statistics {
        std::atomic<int64_t> queriesExecuted{0};
        std::atomic<int64_t> queriesFailed{0};
        std::atomic<int64_t> transactionsCommitted{0};
        std::atomic<int64_t> transactionsRolledBack{0};
        std::atomic<int64_t> bytesTransferred{0};
        std::chrono::steady_clock::time_point startTime;
    };
    mutable Statistics stats_;

    bool ValidateConfiguration(const nlohmann::json& config) const;
    BackendType ParseBackendType(const std::string& typeStr) const;
    std::string BackendTypeToString(BackendType type) const;

    bool ExecuteCreateTable(const std::string& tableName, const std::string& createSql);
    bool TableExists(const std::string& tableName);

};
