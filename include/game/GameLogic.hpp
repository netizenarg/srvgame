#pragma once

#include <memory>
#include <shared_mutex>
#include <unordered_map>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "database/DbService.hpp"

#include "network/PredictionSystem.hpp"
#include "network/IConnection.hpp"

#include "scripting/PythonScripting.hpp"

#include "game/LogicCore.hpp"
#include "game/PlayerManager.hpp"
#include "game/InventorySystem.hpp"
#include "game/LootTableManager.hpp"
#include "game/SkillSystem.hpp"
#include "game/QuestManager.hpp"
#include "game/EntityManager.hpp"
#include "game/GameData.hpp"

class DatabaseService;

class GameLogic : public LogicCore, public std::enable_shared_from_this<GameLogic>
{
public:
    static GameLogic& GetInstance();

    void Initialize() override;
    void Shutdown() override;

    struct WorldConfig : public LogicWorld::WorldConfig {};
    void SetWorldConfig(const WorldConfig& config);
    const WorldConfig& GetWorldConfig() const;

    void PerformMaintenance();

    std::shared_ptr<WorldChunk> GetOrCreateChunk(int chunkX, int chunkZ);
    void GenerateWorldAroundPlayer(uint64_t playerId, const glm::vec3& position);
    void PreloadWorldData(float radius);
    float GetTerrainHeight(float x, float z) const;
    BiomeType GetBiomeAt(float x, float z) const;

    uint64_t SpawnNPC(NPCType type, const glm::vec3& position, uint64_t ownerId = 0);
    void DespawnNPC(uint64_t npcId);
    NPCEntity* GetNPCEntity(uint64_t npcId);
    GameEntity* GetEntity(uint64_t entityId);
    std::shared_ptr<Player> GetPlayer(uint64_t playerId);

    CollisionResult CheckCollision(const glm::vec3& position, float radius, uint64_t excludeEntityId = 0);
    bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit);

    void CreateLootEntity(const glm::vec3& position, std::shared_ptr<LootItem> item, int quantity);

    void OnPlayerConnected(uint64_t sessionId, uint64_t playerId) override;
    void OnPlayerDisconnected(uint64_t sessionId) override;

    void BroadcastToNearbyPlayers(const glm::vec3& position, uint16_t messageType,
                                  const std::vector<uint8_t>& data, float radius = 50.0f);
    void BroadcastToNearbyOnlinePlayers(const glm::vec3& position, uint16_t messageType,
                                        const std::vector<uint8_t>& data, float radius = 50.0f);
    void SyncNearbyEntitiesToPlayer(uint64_t sessionId, const glm::vec3& position);
    void BroadcastToNearbyPlayersJson(const glm::vec3& position, const nlohmann::json& message, float radius);
    void BroadcastToAllPlayers(const nlohmann::json& message);
    void BroadcastToAllPlayersBinary(uint16_t messageType, const std::vector<uint8_t>& data);
    void BroadcastToPlayers(const std::vector<uint64_t>& sessionIds, const nlohmann::json& message);
    void BroadcastPlayerSpawn(uint64_t playerId);
    void BroadcastPlayerDespawn(uint64_t playerId, const glm::vec3& lastPosition);
    void BroadcastPlayerSpawnJson(uint64_t playerId);
    void BroadcastPlayerDespawnJson(uint64_t playerId, const glm::vec3& lastPosition);
    void BroadcastEntitySpawn(uint64_t entityId, EntityType type, const glm::vec3& position,
                              float yaw, const std::string& name);
    void SendPositionCorrection(uint64_t sessionId, const glm::vec3& position, const glm::vec3& velocity);
    void BroadcastEntityDespawn(uint64_t entityId, const glm::vec3& position);
    void SendAuthenticationSuccess(uint64_t sessionId, uint64_t playerId, const std::string& message);
    void SendAuthenticationFailure(uint64_t sessionId, const std::string& message);

    void SetSendAuthenticationResponseCallback(std::function<void(uint64_t sessionId, bool success, const std::string& message, uint64_t playerId)> cb);
    void SetSendChunkCallback(std::function<void(uint64_t sessionId, const ChunkData&)> cb);
    void SetSendCollisionResponseCallback(std::function<void(uint64_t session_id, const CollisionResult& result)> cb);
    void SetPlayerStateCallback(std::function<void(const PlayerStateData&)> cb);
    void SetBroadcastPlayerPositionCallback(std::function<void(const PlayerPositionData&, float radius)> cb);
    void SetSendNPCInteractionResponseCallback(std::function<void(uint64_t session_id, const NpcData& response)> cb);
    void SetSendFamiliarCommandResponseCallback(std::function<void(uint64_t session_id, const FamiliarData& response)> cb);
    void SetSendEntitySpawnResponseCallback(std::function<void(uint64_t session_id, const EntitySpawnData& response)> cb);
    void SetSendLootPickupResponseCallback(std::function<void(uint64_t session_id, const LootPickupData& response)> cb);
    void SetSendInventoryResponseCallback(std::function<void(uint64_t session_id, const InventoryData& response)> cb);

    void OnAuthentication(const AuthenticationData& data);
    void OnChunkRequest(const ChunkData& data);
    void OnCollisionCheck(const CollisionData& data);
    void OnPlayerPosition(const PlayerPositionData& data);
    void OnPlayerState(const PlayerStateData& data);
    void OnNPCInteraction(const NpcData& data);
    void OnFamiliarCommand(const FamiliarData& data);
    void OnEntitySpawnRequest(const EntitySpawnData& data);
    void OnLootPickup(const LootPickupData& data);
    void OnInventory(const InventoryData& data);

    // scripting
    void RegisterPythonEventHandlers() override;
    void FirePythonEvent(const std::string& eventName, const nlohmann::json& data) override;
    nlohmann::json CallPythonFunction(const std::string& moduleName, const std::string& functionName,
                                      const nlohmann::json& args) override;

    void SaveGameState() override;
    void CleanupOldData() override;
    void ProcessGameTick(float deltaTime) override;
    void SpawnEnemies() override;
    void RespawnNPCs() override;
    void SpawnResources() override;
    void SaveLoop() override;
    void HandleLogin(uint64_t sessionId, const nlohmann::json& data) override;
    void HandleChat(uint64_t sessionId, const nlohmann::json& data) override;
    void HandleCombat(uint64_t sessionId, const nlohmann::json& data) override;
    void HandleQuest(uint64_t sessionId, const nlohmann::json& data) override;

    void SetDatabaseService(DatabaseService* dbService);
    void SetConnectionManager(std::shared_ptr<ConnectionManager> connMgr);
    void SetDatabaseBackend(std::unique_ptr<DatabaseBackend> backend);
    DatabaseBackend* GetDatabaseBackend() const;

private:
    GameLogic();
    ~GameLogic();

    bool initialized_ = false;
    static std::mutex instanceMutex_;
    static GameLogic* instance_;

    std::unique_ptr<DatabaseBackend> databaseBackend_;
    std::shared_ptr<ConnectionManager> connectionManager_;
    std::unordered_map<uint64_t, PredictionSystem> playerPrediction_;
    std::mutex predictionMutex_;
    DatabaseService* dbService_ = nullptr;

    std::function<void(uint64_t, bool, const std::string&, uint64_t)> sendAuthResponseCb_;
    std::function<void(uint64_t, const ChunkData&)> sendChunkCb_;
    std::function<void(uint64_t, const CollisionResult&)> sendCollisionResponseCb_;
    std::function<void(const PlayerPositionData&, float)> broadcastPlayerPositionCb_;
    std::function<void(const PlayerStateData&)> playerStateCb_;
    std::function<void(uint64_t, const NpcData&)> sendNPCInteractionResponseCb_;
    std::function<void(uint64_t, const FamiliarData&)> sendFamiliarCommandResponseCb_;
    std::function<void(uint64_t, const EntitySpawnData&)> sendEntitySpawnResponseCb_;
    std::function<void(uint64_t, const LootPickupData&)> sendLootPickupResponseCb_;
    std::function<void(uint64_t, const InventoryData&)> sendInventoryResponseCb_;

    bool pythonEnabled_ = false;

    void GameLoop() override;
    void SpawnerLoop() override;
    void UpdateWorld(float deltaTime);

    bool LoadGameData();
    void SaveChunkData();

    nlohmann::json PlayerUpdateToJson(uint64_t playerId, const glm::vec3& pos, float yaw,
                                      float health, float maxHealth, const std::string& name);
    nlohmann::json PlayerPositionToJson(const std::vector<uint8_t>& data);
    nlohmann::json PlayerUpdateToJson(const std::vector<uint8_t>& data);
    nlohmann::json EntitySpawnToJson(const std::vector<uint8_t>& data);
    nlohmann::json EntityUpdateToJson(const std::vector<uint8_t>& data);
    nlohmann::json EntityDespawnToJson(const std::vector<uint8_t>& data);
    nlohmann::json ChunkDataToJson(const std::vector<uint8_t>& data);
};
