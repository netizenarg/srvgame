#pragma once

#ifdef USE_CITUS

#include <algorithm>
#include <random>
#include <sstream>
#include <thread>

#include "database/PostgreSqlClient.hpp"

/**
 * @brief Citus Client Implementation
 *
 * Extends PostgreSQL client with Citus-specific distributed database features.
 * Provides sharding, distributed queries, and cluster management capabilities.
 */
class CitusClient : public PostgreSqlClient {
public:
    static CitusClient& GetInstance();

    CitusClient(const nlohmann::json& config);
    virtual ~CitusClient();

    // Citus-specific cluster management
    bool CreateDistributedTable(const std::string& tableName, const std::string& distributionColumn);
    bool CreateReferenceTable(const std::string& tableName);
    bool CreateDistributedFunction(const std::string& functionName, const std::string& functionDefinition);

    // Worker node management
    bool AddWorkerNode(const std::string& host, int port);
    bool RemoveWorkerNode(const std::string& host, int port);
    bool DisableWorkerNode(const std::string& host, int port);
    bool EnableWorkerNode(const std::string& host, int port);
    nlohmann::json GetWorkerNodes();
    nlohmann::json GetWorkerNodeStats();

    // Shard management
    bool RebalanceShards();
    bool MoveShard(int shardId, const std::string& sourceNode, const std::string& targetNode);
    bool IsolateShard(int shardId);
    nlohmann::json GetShardPlacements();
    nlohmann::json GetShardStatistics();

    // Override shard operations for actual Citus distribution
    nlohmann::json QueryShard(int shardId, const std::string& sql) override;
    nlohmann::json QueryShardWithParams(int shardId, const std::string& sql,
                                       const std::vector<std::string>& params) override;
    bool ExecuteShard(int shardId, const std::string& sql) override;
    bool ExecuteShardWithParams(int shardId, const std::string& sql,
                               const std::vector<std::string>& params) override;

    // Enhanced shard management
    int GetShardId(uint64_t entityId) const override;
    int GetTotalShards() const override;
    std::string GetShardConnectionInfo(int shardId) const;

    // Distributed transaction support
    bool BeginDistributedTransaction();
    bool CommitDistributedTransaction();
    bool RollbackDistributedTransaction();
    bool PrepareDistributedTransaction(const std::string& transactionId);
    bool CommitPreparedDistributedTransaction(const std::string& transactionId);
    bool RollbackPreparedDistributedTransaction(const std::string& transactionId);

    // Citus-specific statistics and monitoring
    nlohmann::json GetCitusStats();
    nlohmann::json GetQueryStats();
    nlohmann::json GetClusterStats();
    nlohmann::json GetShardQueryStats(int shardId = -1);

    // Performance tuning
    bool SetShardCount(int shardCount);
    bool SetReplicationFactor(int replicationFactor);
    bool EnableQueryMetrics(bool enabled);

    // Maintenance operations
    bool VacuumDistributedTables();
    bool AnalyzeDistributedTables();
    bool ReplicateReferenceTables();

    // Backward compatibility methods
    bool ExecuteDatabase(const std::string& sql) { return Execute(sql); }
    nlohmann::json QueryDatabase(const std::string& sql) { return Query(sql); }

    // Legacy singleton access (for backward compatibility)
    static CitusClient* GetInstancePtr() {
        std::lock_guard<std::mutex> lock(instanceMutex_);
        return instance_;
    }

    bool IsCitusEnabled() const { return citusEnabled_; }
    bool ConnectToDatabase(const std::string& dbname) override;

private:
    static std::mutex instanceMutex_;
    static CitusClient* instance_;

    // Citus-specific configuration
    bool citusEnabled_;
    int shardCount_;
    int replicationFactor_;
    std::string coordinatorNode_;

    // Worker node cache
    struct WorkerNode {
        std::string host;
        int port;
        std::string name;
        bool enabled;
        int shardCount;
    };
    std::unordered_map<std::string, WorkerNode> workerNodes_;
    mutable std::mutex workerNodesMutex_;

    // Shard placement cache
    struct ShardPlacement {
        int shardId;
        std::string nodeName;
        std::string host;
        int port;
        int placementId;
    };
    std::unordered_map<int, std::vector<ShardPlacement>> shardPlacements_;
    mutable std::mutex shardPlacementsMutex_;

    // Helper methods
    bool RefreshWorkerNodes();
    bool RefreshShardPlacements();
    PGconn* GetShardConnection(int shardId);
    void ReleaseShardConnection(PGconn* conn);
    bool ExecuteOnShard(int shardId, const std::string& sql,
                       const std::vector<const char*>& params = {});
    nlohmann::json QueryOnShard(int shardId, const std::string& sql,
                               const std::vector<const char*>& params = {});

    // Citus extension management
    bool CheckCitusExtension();
    bool EnableCitusExtension();

    // Connection pool for shards
    struct ShardConnection {
        PGconn* conn;
        bool inUse;
        std::chrono::steady_clock::time_point lastUsed;
        std::string connectionString;
    };
    std::unordered_map<int, std::vector<ShardConnection>> shardConnections_;
    mutable std::mutex shardConnectionsMutex_;
    size_t maxShardConnectionsPerNode_;

    PGconn* GetOrCreateShardConnection(int shardId);
    void MaintainShardConnections();
    void CloseAllShardConnections();

    // Statistics
    struct CitusStats {
        std::atomic<int64_t> distributedQueries{0};
        std::atomic<int64_t> shardQueries{0};
        std::atomic<int64_t> shardQueryFailures{0};
        std::atomic<int64_t> shardConnectionErrors{0};
        std::chrono::steady_clock::time_point startTime;
    };
    CitusStats citusStats_;
};

#endif // USE_CITUS
