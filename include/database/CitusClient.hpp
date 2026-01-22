#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <memory>

#include "../include/database/DatabaseBackend.hpp"

// Compatibility wrapper for existing code
class CitusClient {
public:
    static CitusClient& GetInstance();

    // Initialize with backend type detection
    bool Initialize(const std::string& coordinatorConnInfo,
                    const std::vector<std::string>& workerNodes = {});

    // Get the actual backend (for advanced use)
    std::shared_ptr<DatabaseBackend> GetBackend() { return backend_; }

    // Shard management (delegated to backend)
    bool CreateDistributedTable(const std::string& tableName,
                                const std::string& distributionColumn,
                                const std::string& distributionType = "hash") {
        return backend_->CreateDistributedTable(tableName, distributionColumn, distributionType);
    }

    bool CreateReferenceTable(const std::string& tableName) {
        return backend_->CreateReferenceTable(tableName);
    }

    // Query routing
    nlohmann::json QueryShard(int shardId, const std::string& query) {
        return backend_->QueryShard(shardId, query);
    }

    nlohmann::json QueryAllShards(const std::string& query) {
        return backend_->QueryAllShards(query);
    }

    // Player data management
    bool CreatePlayer(const nlohmann::json& playerData) {
        return backend_->CreatePlayer(playerData);
    }

    nlohmann::json GetPlayer(int64_t playerId) {
        return backend_->GetPlayer(playerId);
    }

    bool UpdatePlayer(int64_t playerId, const nlohmann::json& updates) {
        return backend_->UpdatePlayer(playerId, updates);
    }

    bool DeletePlayer(int64_t playerId) {
        return backend_->DeletePlayer(playerId);
    }

    // Game state management
    bool SaveGameState(int64_t gameId, const nlohmann::json& gameState) {
        return backend_->SaveGameState(gameId, gameState);
    }

    nlohmann::json LoadGameState(int64_t gameId) {
        return backend_->LoadGameState(gameId);
    }

    // Analytics queries
    nlohmann::json GetPlayerStats(int64_t playerId) {
        return backend_->GetPlayerStats(playerId);
    }

    nlohmann::json GetGameAnalytics(int64_t gameId) {
        return backend_->GetGameAnalytics(gameId);
    }

    // Utility methods for existing code
    bool IsOnline(int64_t playerId);
    bool SetOnlineStatus(int64_t playerId, bool online,
                        const std::string& sessionId = "",
                        const std::string& ipAddress = "") {
        return backend_->SetOnlineStatus(playerId, online, sessionId, ipAddress);
    }

    bool UpdateHeartbeat(int64_t playerId) {
        return backend_->UpdateHeartbeat(playerId);
    }

    nlohmann::json GetOnlinePlayers() {
        return backend_->GetOnlinePlayers();
    }

    bool UpdatePlayerPosition(int64_t playerId, float x, float y, float z) {
        return backend_->UpdatePlayerPosition(playerId, x, y, z);
    }

    nlohmann::json GetNearbyPlayers(int64_t playerId, float radius) {
        return backend_->GetNearbyPlayers(playerId, radius);
    }

    bool AddPlayerItem(int64_t playerId, int itemDefId,
                      int quantity, const nlohmann::json& attributes) {
        return backend_->AddPlayerItem(playerId, itemDefId, quantity, attributes);
    }

    nlohmann::json GetPlayerItems(int64_t playerId) {
        return backend_->GetPlayerItems(playerId);
    }

    bool LogGameEvent(int64_t playerId, int64_t gameId,
                     const std::string& eventType,
                     const nlohmann::json& eventData) {
        return backend_->LogGameEvent(playerId, gameId, eventType, eventData);
    }

    // Maintenance methods
    bool VacuumTables() {
        return backend_->VacuumTables();
    }

    bool RebalanceShards() {
        return backend_->RebalanceShards();
    }

    nlohmann::json GetClusterStatus() {
        return backend_->GetClusterStatus();
    }

    nlohmann::json GetPerformanceMetrics() {
        return backend_->GetPerformanceMetrics();
    }

    void ReconnectAll() {
        backend_->ReconnectAll();
    }

    bool CheckClusterHealth() {
        return backend_->CheckHealth();
    }

private:
    CitusClient() = default;
    ~CitusClient() = default;

    std::shared_ptr<DatabaseBackend> backend_;
    std::string backendType_;
};