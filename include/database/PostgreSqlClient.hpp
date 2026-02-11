#pragma once

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iomanip>
#include <memory>
#include <mutex>
#include <vector>
#include <queue>
#include <thread>
#include <sstream>

#include <libpq-fe.h>

#include "database/Backend.hpp"
//#include "database/DbManager.hpp"

/**
 * @brief PostgreSQL Client Implementation
 *
 * Provides a concrete implementation of DatabaseBackend for PostgreSQL.
 * Includes connection pooling, prepared statements, and transaction support.
 */
class PostgreSqlClient : public DatabaseBackend {
public:
    // Connection pool entry
    struct Connection {
        PGconn* conn;
        bool inUse;
        std::chrono::steady_clock::time_point lastUsed;
    };

    // Prepared statement
    struct PreparedStatement {
        std::string name;
        std::string sql;
        int paramCount;
    };

    PostgreSqlClient(const nlohmann::json& config);
    virtual ~PostgreSqlClient();

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
    nlohmann::json GetPlayer(uint64_t playerId) override;

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

    // Prepared Statements
    bool PrepareStatement(const std::string& name, const std::string& sql, int paramCount);
    bool ExecutePrepared(const std::string& name, const std::vector<std::string>& params);
    nlohmann::json QueryPrepared(const std::string& name, const std::vector<std::string>& params);

    // Backward compatibility methods
    bool ExecuteDatabase(const std::string& sql) { return Execute(sql); }
    nlohmann::json QueryDatabase(const std::string& sql) { return Query(sql); }

private:
    // Connection management
    PGconn* GetConnection();
    void ReleaseConnection(PGconn* conn);
    PGconn* CreateNewConnection();
    void CloseConnection(PGconn* conn);
    bool TestConnection(PGconn* conn);

    // Connection pool management
    void MaintainPool();
    void CleanupIdleConnections();

    // Query execution helpers
    nlohmann::json ExecuteQuery(PGconn* conn, const std::string& sql,
                                const std::vector<const char*>& params = {});
    bool ExecuteCommand(PGconn* conn, const std::string& sql,
                       const std::vector<const char*>& params = {});

    // Result processing
    nlohmann::json ResultToJson(PGresult* result) const;

    // Error handling
    void HandleSQLError(PGconn* conn, const std::string& operation);
    bool ShouldReconnect(PGconn* conn) const;

    // Configuration
    std::string BuildConnectionString() const;

    // Configuration
    nlohmann::json config_;
    std::string connectionString_;

    // Connection pool
    mutable std::mutex poolMutex_;
    std::condition_variable poolCV_;
    std::vector<Connection> connections_;
    size_t minConnections_;
    size_t maxConnections_;
    std::atomic<bool> poolInitialized_;
    std::atomic<bool> poolShuttingDown_;

    // Statistics
    struct DatabaseStats {
        std::atomic<int64_t> totalQueries{0};
        std::atomic<int64_t> failedQueries{0};
        std::atomic<int64_t> totalTransactions{0};
        std::atomic<int64_t> connectionErrors{0};
        std::atomic<int64_t> connectionPoolHits{0};
        std::atomic<int64_t> connectionPoolMisses{0};
        std::chrono::steady_clock::time_point startTime;
    };
    DatabaseStats stats_;

    // Prepared statements
    std::unordered_map<std::string, PreparedStatement> preparedStatements_;
    mutable std::mutex preparedStatementsMutex_;

    // Current transaction state (per connection)
    struct TransactionState {
        PGconn* conn;
        bool inTransaction;
    };
    std::unordered_map<PGconn*, TransactionState> transactionStates_;
    mutable std::mutex transactionMutex_;

    // Last operation results
    int64_t lastInsertId_;
    int affectedRows_;

    // Shard configuration (for compatibility)
    int totalShards_;
};
