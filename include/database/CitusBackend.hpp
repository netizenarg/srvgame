#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "database/Backend.hpp"
#include "database/Pool.hpp"

struct ShardInfo {
    int shard_id;
    std::string node_name;
    int node_port;
    std::string table_name;
    std::string distribution_column;
};

class CitusBackend : public DatabaseBackend {
public:
    CitusBackend();
    ~CitusBackend() override;

    // DatabaseBackend interface implementation
    bool Initialize(const std::string& connInfo,
                   const std::vector<std::string>& workerNodes) override;
    
    bool TestConnection() override;
    void ReconnectAll() override;
    bool CheckHealth() override;

    // Table management
    bool CreateTable(const std::string& tableName,
                    const std::string& schema) override;
    
    bool CreateDistributedTable(const std::string& tableName,
                               const std::string& distributionColumn,
                               const std::string& distributionType = "hash") override;
    
    bool CreateReferenceTable(const std::string& tableName) override;
    bool TableExists(const std::string& tableName) override;

    // Query execution
    nlohmann::json Query(const std::string& query) override;
    bool Execute(const std::string& query) override;
    
    nlohmann::json QueryShard(int shardId, const std::string& query) override;
    nlohmann::json QueryAllShards(const std::string& query) override;

    // Player data management
    bool CreatePlayer(const nlohmann::json& playerData) override;
    nlohmann::json GetPlayer(int64_t playerId) override;
    bool UpdatePlayer(int64_t playerId, const nlohmann::json& updates) override;
    bool DeletePlayer(int64_t playerId) override;

    // Game state management
    bool SaveGameState(int64_t gameId, const nlohmann::json& gameState) override;
    nlohmann::json LoadGameState(int64_t gameId) override;

    // Player session management
    bool SetOnlineStatus(int64_t playerId, bool online,
                        const std::string& sessionId = "",
                        const std::string& ipAddress = "") override;
    
    bool UpdateHeartbeat(int64_t playerId) override;
    bool UpdatePlayerPosition(int64_t playerId, float x, float y, float z) override;
    
    nlohmann::json GetOnlinePlayers() override;
    nlohmann::json GetNearbyPlayers(int64_t playerId, float radius) override;

    // Inventory management
    bool AddPlayerItem(int64_t playerId, int itemDefId,
                      int quantity, const nlohmann::json& attributes) override;
    
    nlohmann::json GetPlayerItems(int64_t playerId) override;

    // Game events
    bool LogGameEvent(int64_t playerId, int64_t gameId,
                     const std::string& eventType,
                     const nlohmann::json& eventData) override;

    // Analytics
    nlohmann::json GetPlayerStats(int64_t playerId) override;
    nlohmann::json GetGameAnalytics(int64_t gameId) override;

    // Maintenance
    bool VacuumTables() override;
    bool RebalanceShards() override;
    nlohmann::json GetClusterStatus() override;
    nlohmann::json GetPerformanceMetrics() override;

    // Utility
    std::string EscapeString(const std::string& str) override;
    nlohmann::json PGResultToJson(PGresult* res) override;

private:
    DatabasePool coordinatorPool_;
    std::vector<ShardInfo> shards_;
    std::unordered_map<std::string, std::shared_ptr<DatabasePool>> workerPools_;
    std::mutex workerPoolsMutex_;
    std::mutex shardsMutex_;
    int shardCount_{32};
    bool initialized_{false};

    bool TestCoordinatorConnection();
    bool InitializeWorkerPools(const std::vector<std::string>& workerNodes);
    bool LoadShardInformation();
    bool CreateDefaultTables();
    void InsertDefaultConfig();
    
    int GetShardId(int64_t entityId) const;
    std::shared_ptr<DatabasePool> GetShardPool(int shardId);
    
    // Helper methods
    nlohmann::json ExecuteCoordinatorQuery(const std::string& query);
    bool ExecuteCoordinatorCommand(const std::string& command);
};
