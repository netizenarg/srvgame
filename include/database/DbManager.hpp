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

    // Backend Management
    bool SetBackend(DatabaseType type, const nlohmann::json& config);
    DatabaseBackend* GetBackend() const { return backend_.get(); }
    DatabaseType GetCurrentType() const { return currentType_; }

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

/**
 * @brief PostgreSQL Client Implementation
 *
 * Implements the DatabaseBackend interface for standard PostgreSQL.
 */
class PostgreSqlCli : public DatabaseBackend {
public:
    PostgreSqlCli(const nlohmann::json& config);
    virtual ~PostgreSqlCli();

    // Connection Management
    bool Connect() override;
    bool Reconnect() override;
    void Disconnect() override;
    bool IsConnected() const override;
    bool CheckHealth() override;
    void ReconnectAll() override;

    // Player Data Operations
    bool SavePlayerData(uint64_t playerId, const nlohmann::json& data) override;
    nlohmann::json LoadPlayerData(uint64_t playerId) override;
    bool UpdatePlayer(uint64_t playerId, const nlohmann::json& updates) override;
    bool DeletePlayer(uint64_t playerId) override;
    bool UpdatePlayerPosition(uint64_t playerId, float x, float y, float z) override;
    bool PlayerExists(uint64_t playerId) override;
    nlohmann::json GetPlayerStats(uint64_t playerId) override;
    bool UpdatePlayerStats(uint64_t playerId, const nlohmann::json& stats) override;

    // Game State Operations
    bool SaveGameState(const std::string& key, const nlohmann::json& state) override;
    nlohmann::json LoadGameState(const std::string& key) override;
    bool DeleteGameState(const std::string& key) override;
    std::vector<std::string> ListGameStates() override;

    // World Data Operations
    bool SaveChunkData(int chunkX, int chunkZ, const nlohmann::json& chunkData) override;
    nlohmann::json LoadChunkData(int chunkX, int chunkZ) override;
    bool DeleteChunkData(int chunkX, int chunkZ) override;
    std::vector<std::pair<int, int>> ListChunksInRange(int centerX, int centerZ, int radius) override;

    // Inventory Operations
    bool SaveInventory(uint64_t playerId, const nlohmann::json& inventory) override;
    nlohmann::json LoadInventory(uint64_t playerId) override;

    // Quest Operations
    bool SaveQuestProgress(uint64_t playerId, const std::string& questId, const nlohmann::json& progress) override;
    nlohmann::json LoadQuestProgress(uint64_t playerId, const std::string& questId) override;
    std::vector<std::string> ListActiveQuests(uint64_t playerId) override;

    // Transaction Operations
    bool BeginTransaction() override;
    bool CommitTransaction() override;
    bool RollbackTransaction() override;
    bool ExecuteTransaction(const std::function<bool()>& operation) override;

    // Query Operations
    nlohmann::json Query(const std::string& sql) override;
    nlohmann::json QueryWithParams(const std::string& sql, const std::vector<std::string>& params) override;
    bool Execute(const std::string& sql) override;
    bool ExecuteWithParams(const std::string& sql, const std::vector<std::string>& params) override;

    // Shard Operations (simulated for compatibility)
    nlohmann::json QueryShard(int shardId, const std::string& sql) override;
    nlohmann::json QueryShardWithParams(int shardId, const std::string& sql, const std::vector<std::string>& params) override;
    bool ExecuteShard(int shardId, const std::string& sql) override;
    bool ExecuteShardWithParams(int shardId, const std::string& sql, const std::vector<std::string>& params) override;

    // Utility Methods
    std::string EscapeString(const std::string& str) override;
    int GetShardId(uint64_t entityId) const override;
    int GetTotalShards() const override;
    std::string GetConnectionInfo() const override;
    int64_t GetLastInsertId() override;
    int GetAffectedRows() override;

    // Statistics
    nlohmann::json GetDatabaseStats() override;
    void ResetStats() override;

    // Connection Pool Management
    bool InitializeConnectionPool(size_t minConnections, size_t maxConnections) override;
    void ReleaseConnectionPool() override;
    size_t GetActiveConnections() const override;
    size_t GetIdleConnections() const override;

private:
    // PIMPL pattern to hide PostgreSQL implementation details
    class Impl;
    std::unique_ptr<Impl> impl_;

    nlohmann::json config_;
    std::atomic<bool> connected_;

    // Connection pool statistics
    struct PoolStats {
        size_t activeConnections;
        size_t idleConnections;
        size_t totalConnections;
    };
    PoolStats poolStats_;
};

#ifdef USE_CITUS
/**
 * @brief Citus Client Implementation
 *
 * Extends PostgreSQL client with Citus-specific distributed database features.
 */
class CitusCli : public PostgreSqlCli {
public:
    static CitusCli& GetInstance();

    CitusCli(const nlohmann::json& config);
    virtual ~CitusCli();

    // Citus-specific operations
    bool CreateDistributedTable(const std::string& tableName, const std::string& distributionColumn);
    bool CreateReferenceTable(const std::string& tableName);
    bool AddWorkerNode(const std::string& host, int port);
    bool RemoveWorkerNode(const std::string& host, int port);
    nlohmann::json GetWorkerNodes();
    bool RebalanceShards();

    // Override shard operations for actual Citus distribution
    nlohmann::json QueryShard(int shardId, const std::string& sql) override;
    nlohmann::json QueryShardWithParams(int shardId, const std::string& sql, const std::vector<std::string>& params) override;
    bool ExecuteShard(int shardId, const std::string& sql) override;
    bool ExecuteShardWithParams(int shardId, const std::string& sql, const std::vector<std::string>& params) override;

    // Enhanced shard management
    int GetShardId(uint64_t entityId) const override;
    int GetTotalShards() const override;
    bool MoveShard(int shardId, const std::string& sourceNode, const std::string& targetNode);
    nlohmann::json GetShardPlacements();

    // Citus-specific statistics
    nlohmann::json GetCitusStats();
    nlohmann::json GetQueryStats();

    // Backward compatibility methods
    bool ExecuteDatabase(const std::string& sql) { return Execute(sql); }
    nlohmann::json QueryDatabase(const std::string& sql) { return Query(sql); }

private:
    static std::mutex instanceMutex_;
    static CitusCli* instance_;

    // Citus-specific configuration
    int totalShards_;
    std::unordered_map<std::string, int> workerNodes_;
};
#endif
