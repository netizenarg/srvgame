#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <memory>

class DatabaseBackend {
public:
    virtual ~DatabaseBackend() = default;

    // Initialization
    virtual bool Initialize(const std::string& connInfo,
                           const std::vector<std::string>& workerNodes = {}) = 0;
    
    virtual bool TestConnection() = 0;
    virtual void ReconnectAll() = 0;
    virtual bool CheckHealth() = 0;

    // Table management
    virtual bool CreateTable(const std::string& tableName,
                            const std::string& schema) = 0;
    
    virtual bool CreateDistributedTable(const std::string& tableName,
                                       const std::string& distributionColumn,
                                       const std::string& distributionType = "hash") = 0;
    
    virtual bool CreateReferenceTable(const std::string& tableName) = 0;
    
    virtual bool TableExists(const std::string& tableName) = 0;

    // Query execution
    virtual nlohmann::json Query(const std::string& query) = 0;
    virtual bool Execute(const std::string& query) = 0;
    
    virtual nlohmann::json QueryShard(int shardId, const std::string& query) = 0;
    virtual nlohmann::json QueryAllShards(const std::string& query) = 0;

    // Player data management
    virtual bool CreatePlayer(const nlohmann::json& playerData) = 0;
    virtual nlohmann::json GetPlayer(int64_t playerId) = 0;
    virtual bool UpdatePlayer(int64_t playerId, const nlohmann::json& updates) = 0;
    virtual bool DeletePlayer(int64_t playerId) = 0;

    // Game state management
    virtual bool SaveGameState(int64_t gameId, const nlohmann::json& gameState) = 0;
    virtual nlohmann::json LoadGameState(int64_t gameId) = 0;

    // Player session management
    virtual bool SetOnlineStatus(int64_t playerId, bool online,
                                const std::string& sessionId = "",
                                const std::string& ipAddress = "") = 0;
    
    virtual bool UpdateHeartbeat(int64_t playerId) = 0;
    virtual bool UpdatePlayerPosition(int64_t playerId, float x, float y, float z) = 0;
    
    virtual nlohmann::json GetOnlinePlayers() = 0;
    virtual nlohmann::json GetNearbyPlayers(int64_t playerId, float radius) = 0;

    // Inventory management
    virtual bool AddPlayerItem(int64_t playerId, int itemDefId,
                              int quantity, const nlohmann::json& attributes) = 0;
    
    virtual nlohmann::json GetPlayerItems(int64_t playerId) = 0;

    // Game events
    virtual bool LogGameEvent(int64_t playerId, int64_t gameId,
                             const std::string& eventType,
                             const nlohmann::json& eventData) = 0;

    // Analytics
    virtual nlohmann::json GetPlayerStats(int64_t playerId) = 0;
    virtual nlohmann::json GetGameAnalytics(int64_t gameId) = 0;

    // Maintenance
    virtual bool VacuumTables() = 0;
    virtual bool RebalanceShards() = 0;
    virtual nlohmann::json GetClusterStatus() = 0;
    virtual nlohmann::json GetPerformanceMetrics() = 0;

    // Utility
    virtual std::string EscapeString(const std::string& str) = 0;
    virtual nlohmann::json PGResultToJson(PGresult* res) = 0;

    // Factory method
    static std::unique_ptr<DatabaseBackend> CreateBackend(const std::string& type);
};

// Factory function
inline std::unique_ptr<DatabaseBackend> CreateDatabaseBackend(const std::string& type) {
    return DatabaseBackend::CreateBackend(type);
}