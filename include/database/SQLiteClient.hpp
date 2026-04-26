#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <queue>
#include <sstream>
#include <thread>
#include <unordered_map>

#include <sqlite3.h>

#include "database/Backend.hpp"

class SQLiteClient : public DatabaseBackend {
public:
    explicit SQLiteClient(const nlohmann::json& config, const SQLProvider& sqlProvider);
    virtual ~SQLiteClient();

    bool Connect() override;
    bool ConnectToDatabase(const std::string& dbname) override;
    bool Reconnect() override;
    void Disconnect() override;
    bool IsConnected() const override;
    bool CheckHealth() override;
    void ReconnectAll() override;

    bool SavePlayerData(uint64_t playerId, const nlohmann::json& data) override;
    nlohmann::json LoadPlayerData(uint64_t playerId) override;
    bool UpdatePlayer(uint64_t playerId, const nlohmann::json& updates) override;
    bool DeletePlayer(uint64_t playerId) override;
    bool UpdatePlayerPosition(uint64_t playerId, float x, float y, float z) override;
    bool PlayerExists(uint64_t playerId) override;
    nlohmann::json GetPlayerStats(uint64_t playerId) override;
    bool UpdatePlayerStats(uint64_t playerId, const nlohmann::json& stats) override;
    nlohmann::json GetPlayer(uint64_t playerId) override;

    bool SaveGameState(const std::string& key, const nlohmann::json& state) override;
    nlohmann::json LoadGameState(const std::string& key) override;
    bool DeleteGameState(const std::string& key) override;
    std::vector<std::string> ListGameStates() override;

    bool SaveChunkData(int chunkX, int chunkZ, const nlohmann::json& chunkData) override;
    nlohmann::json LoadChunkData(int chunkX, int chunkZ) override;
    bool DeleteChunkData(int chunkX, int chunkZ) override;
    std::vector<std::pair<int, int>> ListChunksInRange(int centerX, int centerZ, int radius) override;

    bool SaveInventory(uint64_t playerId, const nlohmann::json& inventory) override;
    nlohmann::json LoadInventory(uint64_t playerId) override;

    bool SaveQuestProgress(uint64_t playerId, const std::string& questId, const nlohmann::json& progress) override;
    nlohmann::json LoadQuestProgress(uint64_t playerId, const std::string& questId) override;
    std::vector<std::string> ListActiveQuests(uint64_t playerId) override;

    bool BeginTransaction() override;
    bool CommitTransaction() override;
    bool RollbackTransaction() override;
    bool ExecuteTransaction(const std::function<bool()>& operation) override;

    nlohmann::json Query(const std::string& sql) override;
    nlohmann::json QueryWithParams(const std::string& sql, const std::vector<std::string>& params) override;
    bool Execute(const std::string& sql) override;
    bool ExecuteWithParams(const std::string& sql, const std::vector<std::string>& params) override;

    nlohmann::json QueryShard(int shardId, const std::string& sql) override;
    nlohmann::json QueryShardWithParams(int shardId, const std::string& sql,
                                       const std::vector<std::string>& params) override;
    bool ExecuteShard(int shardId, const std::string& sql) override;
    bool ExecuteShardWithParams(int shardId, const std::string& sql,
                               const std::vector<std::string>& params) override;

    std::string EscapeString(const std::string& str) override;
    int GetShardId(uint64_t entityId) const override;
    int GetTotalShards() const override;
    std::string GetConnectionInfo() const override;
    int64_t GetLastInsertId() override;
    int GetAffectedRows() override;

    nlohmann::json GetDatabaseStats() override;
    void ResetStats() override;

    bool InitializeConnectionPool(size_t minConnections, size_t maxConnections) override;
    void ReleaseConnectionPool() override;
    size_t GetActiveConnections() const override;
    size_t GetIdleConnections() const override;

private:
    sqlite3* db_;
    const SQLProvider& sqlProvider_;

    nlohmann::json config_;
    std::string dbPath_;

    mutable std::mutex dbMutex_;

    struct SQLiteStats {
        std::atomic<int64_t> totalQueries{0};
        std::atomic<int64_t> failedQueries{0};
        std::atomic<int64_t> totalTransactions{0};
        std::atomic<int64_t> connectionErrors{0};
        std::chrono::steady_clock::time_point startTime;
    };
    SQLiteStats stats_;

    int64_t lastInsertId_;
    int affectedRows_;

    int totalShards_;

    bool OpenDatabase(const std::string& path);
    void CloseDatabase();
    bool ExecuteSql(const std::string& sql, std::vector<std::vector<std::string>>* results = nullptr);
    nlohmann::json ResultSetToJson(const std::vector<std::vector<std::string>>& rows,
                                   const std::vector<std::string>& columnNames) const;
    std::string BuildCreateTableSql(const std::string& tableName, const std::string& columns) const;
    bool TableExists(const std::string& tableName);
};
