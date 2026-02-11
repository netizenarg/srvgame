#pragma once

#include <cmath>
#include <memory>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "config/ConfigManager.hpp"
#include "logging/Logger.hpp"
#include "network/ConnectionManager.hpp"
#include "database/DbManager.hpp"

#include "game/LogicWorld.hpp"
#include "game/LogicEntity.hpp"
#include "game/ChunkLOD.hpp"
#include "game/CollisionSystem.hpp"

//class LogicCore;
//#include "game/LogicCore.hpp"

class GameLogic {
public:
    static GameLogic& GetInstance();

    //LogicCore& GetLogicCore() { return LogicCore::GetInstance(); }
    //LogicCore& GetLogicCore() { return logicCore_; }

    // Core lifecycle
    void Initialize();
    void Shutdown();

    // Set connection manager for broadcasting
    void SetConnectionManager(std::shared_ptr<ConnectionManager> connMgr) {
        connectionManager_ = connMgr;
        Logger::Info("ConnectionManager set for GameLogic");
    }

    // Database backend management
    void SetDatabaseBackend(std::unique_ptr<DatabaseBackend> backend) {
        databaseBackend_ = std::move(backend);
        Logger::Info("Database backend set for GameLogic");
    }
    
    DatabaseBackend* GetDatabaseBackend() const {
        return databaseBackend_.get();
    }

    // World configuration
    struct WorldConfig : public LogicWorld::WorldConfig {};
    void SetWorldConfig(const WorldConfig& config);
    const WorldConfig& GetWorldConfig() const;

    // World maintenance
    void PerformMaintenance();

    // IPC message handling
    void HandleIPCMessage(const nlohmann::json& message);

    // 3D World methods
    std::shared_ptr<WorldChunk> GetOrCreateChunk(int chunkX, int chunkZ);
    void GenerateWorldAroundPlayer(uint64_t playerId, const glm::vec3& position);
    void PreloadWorldData(float radius);
    float GetTerrainHeight(float x, float z) const;
    BiomeType GetBiomeAt(float x, float z) const;

    // Entity methods
    uint64_t SpawnNPC(NPCType type, const glm::vec3& position, uint64_t ownerId = 0);
    void DespawnNPC(uint64_t npcId);
    NPCEntity* GetNPCEntity(uint64_t npcId);
    GameEntity* GetEntity(uint64_t entityId);
    PlayerEntity* GetPlayerEntity(uint64_t playerId);

    // Collision methods
    CollisionResult CheckCollision(const glm::vec3& position, float radius, uint64_t excludeEntityId = 0);
    bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit);

    // Loot methods
    void CreateLootEntity(const glm::vec3& position, std::shared_ptr<LootItem> item, int quantity);
    void HandleLootPickup(uint64_t sessionId, const nlohmann::json& data);
    void HandleInventoryMove(uint64_t sessionId, const nlohmann::json& data);
    void HandleItemUse(uint64_t sessionId, const nlohmann::json& data);
    void HandleItemDrop(uint64_t sessionId, const nlohmann::json& data);
    void HandleTradeRequest(uint64_t sessionId, const nlohmann::json& data);
    void HandleGoldTransaction(uint64_t sessionId, const nlohmann::json& data);

    // 3D World message handlers
    void HandleWorldChunkRequest(uint64_t sessionId, const nlohmann::json& data);
    void HandlePlayerPositionUpdate(uint64_t sessionId, const nlohmann::json& data);
    void HandleNPCInteraction(uint64_t sessionId, const nlohmann::json& data);
    void HandleCollisionCheck(uint64_t sessionId, const nlohmann::json& data);
    void HandleEntitySpawnRequest(uint64_t sessionId, const nlohmann::json& data);
    void HandleFamiliarCommand(uint64_t sessionId, const nlohmann::json& data);

    // Message handling (inherited from LogicCore)
    void HandleMessage(uint64_t sessionId, const nlohmann::json& message) {
        // Delegate to base class or implement here
        HandleMessage(sessionId, message);
    }

    // Player connection/disconnection
    void OnPlayerConnected(uint64_t sessionId, uint64_t playerId) {
        OnPlayerConnected(sessionId, playerId);
    }

    void OnPlayerDisconnected(uint64_t sessionId) {
        OnPlayerDisconnected(sessionId);
    }

    // Broadcasting
    void BroadcastBinaryToNearbyPlayers(const glm::vec3& position, uint16_t messageType, 
                                        const std::vector<uint8_t>& data, float radius = 50.0f);
    void BroadcastToNearbyPlayers(const glm::vec3& position, const nlohmann::json& message, float radius = 50.0f);
    void SyncNearbyEntitiesToPlayer(uint64_t sessionId, const glm::vec3& position);

    // Helper broadcast methods
    void BroadcastToAllPlayers(const nlohmann::json& message);
    void BroadcastToAllPlayersBinary(uint16_t messageType, const std::vector<uint8_t>& data);
    void BroadcastToPlayers(const std::vector<uint64_t>& sessionIds, const nlohmann::json& message);

private:
    GameLogic();
    ~GameLogic();

    static std::mutex instanceMutex_;
    static GameLogic* instance_;
    //LogicCore& logicCore_;

    // Component systems
    LogicWorld worldLogic_;
    LogicEntity entityLogic_;
    
    // Database backend
    std::unique_ptr<DatabaseBackend> databaseBackend_;
    
    // Connection manager for broadcasting
    std::shared_ptr<ConnectionManager> connectionManager_;

    // Thread functions
    void GameLoop();
    void SpawnerLoop();
    void SaveLoop();

    // Game tick processing
    void ProcessGameTick(float deltaTime);
    void UpdateWorld(float deltaTime);

    // Helper methods
    void RegisterWorldHandlers();
    void LoadGameData();
    void SaveGameState();
    void SaveChunkData();
    void CleanupOldData();
};
