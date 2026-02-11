/**
 * @brief Abstract Database Backend Interface
 *
 * Provides a unified interface for database operations with support for
 * both PostgreSQL and Citus (distributed PostgreSQL) backends.
 */
class DatabaseBackend {
public:
    virtual ~DatabaseBackend() = default;

    // Connection Management
    virtual bool Connect() = 0;
    virtual bool Reconnect() = 0;
    virtual void Disconnect() = 0;
    virtual bool IsConnected() const = 0;
    virtual bool CheckHealth() = 0;
    virtual void ReconnectAll() = 0;

    // Player Data Operations
    virtual bool SavePlayerData(uint64_t playerId, const nlohmann::json& data) = 0;
    virtual nlohmann::json LoadPlayerData(uint64_t playerId) = 0;
    virtual bool UpdatePlayer(uint64_t playerId, const nlohmann::json& updates) = 0;
    virtual bool DeletePlayer(uint64_t playerId) = 0;
    virtual bool UpdatePlayerPosition(uint64_t playerId, float x, float y, float z) = 0;
    virtual bool PlayerExists(uint64_t playerId) = 0;
    virtual nlohmann::json GetPlayerStats(uint64_t playerId) = 0;
    virtual bool UpdatePlayerStats(uint64_t playerId, const nlohmann::json& stats) = 0;
    virtual nlohmann::json GetPlayer(uint64_t playerId) = 0;

    // Game State Operations
    virtual bool SaveGameState(const std::string& key, const nlohmann::json& state) = 0;
    virtual nlohmann::json LoadGameState(const std::string& key) = 0;
    virtual bool DeleteGameState(const std::string& key) = 0;
    virtual std::vector<std::string> ListGameStates() = 0;

    // World Data Operations
    virtual bool SaveChunkData(int chunkX, int chunkZ, const nlohmann::json& chunkData) = 0;
    virtual nlohmann::json LoadChunkData(int chunkX, int chunkZ) = 0;
    virtual bool DeleteChunkData(int chunkX, int chunkZ) = 0;
    virtual std::vector<std::pair<int, int>> ListChunksInRange(int centerX, int centerZ, int radius) = 0;

    // Inventory Operations
    virtual bool SaveInventory(uint64_t playerId, const nlohmann::json& inventory) = 0;
    virtual nlohmann::json LoadInventory(uint64_t playerId) = 0;

    // Quest Operations
    virtual bool SaveQuestProgress(uint64_t playerId, const std::string& questId, const nlohmann::json& progress) = 0;
    virtual nlohmann::json LoadQuestProgress(uint64_t playerId, const std::string& questId) = 0;
    virtual std::vector<std::string> ListActiveQuests(uint64_t playerId) = 0;

    // Transaction Operations
    virtual bool BeginTransaction() = 0;
    virtual bool CommitTransaction() = 0;
    virtual bool RollbackTransaction() = 0;
    virtual bool ExecuteTransaction(const std::function<bool()>& operation) = 0;

    // Query Operations
    virtual nlohmann::json Query(const std::string& sql) = 0;
    virtual nlohmann::json QueryWithParams(const std::string& sql, const std::vector<std::string>& params) = 0;
    virtual bool Execute(const std::string& sql) = 0;
    virtual bool ExecuteWithParams(const std::string& sql, const std::vector<std::string>& params) = 0;

    // Shard Operations (for distributed databases)
    virtual nlohmann::json QueryShard(int shardId, const std::string& sql) = 0;
    virtual nlohmann::json QueryShardWithParams(int shardId, const std::string& sql, const std::vector<std::string>& params) = 0;
    virtual bool ExecuteShard(int shardId, const std::string& sql) = 0;
    virtual bool ExecuteShardWithParams(int shardId, const std::string& sql, const std::vector<std::string>& params) = 0;

    // Utility Methods
    virtual std::string EscapeString(const std::string& str) = 0;
    virtual int GetShardId(uint64_t entityId) const = 0;
    virtual int GetTotalShards() const = 0;
    virtual std::string GetConnectionInfo() const = 0;
    virtual int64_t GetLastInsertId() = 0;
    virtual int GetAffectedRows() = 0;

    // Statistics
    virtual nlohmann::json GetDatabaseStats() = 0;
    virtual void ResetStats() = 0;

    // Connection Pool Management
    virtual bool InitializeConnectionPool(size_t minConnections, size_t maxConnections) = 0;
    virtual void ReleaseConnectionPool() = 0;
    virtual size_t GetActiveConnections() const = 0;
    virtual size_t GetIdleConnections() const = 0;
};
