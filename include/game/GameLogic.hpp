#pragma once

#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <functional>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "database/DbService.hpp"
#include "network/PredictionSystem.hpp"
#include "scripting/PythonScripting.hpp"
#include "game/LogicCore.hpp"
#include "game/LogicWorld.hpp"
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

    //struct WorldConfig : public WorldConfig {};
    void SetWorldConfig(const WorldConfig& config);
    const WorldConfig& GetWorldConfig() const;

    void PerformMaintenance();

    std::shared_ptr<WorldChunk> GetOrCreateChunk(int chunkX, int chunkZ);
    void GenerateWorldAroundPlayer(uint64_t player_id, const glm::vec3& position);
    void PreloadWorldData(float radius);
    float GetTerrainHeight(float x, float z) const;
    BiomeType GetBiomeAt(float x, float z) const;

    uint64_t SpawnNPC(NPCType type, const glm::vec3& position, uint64_t ownerId = 0);
    void DespawnNPC(uint64_t npcId);
    NPCEntity* GetNPCEntity(uint64_t npcId);
    GameEntity* GetEntity(uint64_t entityId);
    std::shared_ptr<Player> GetPlayer(uint64_t player_id);

    CollisionResult CheckCollision(const glm::vec3& position, float radius, uint64_t excludeEntityId = 0);
    bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit);

    void CreateLootEntity(const glm::vec3& position, std::shared_ptr<LootItem> item, int quantity);

    std::vector<uint64_t> GetSessionsInRadius(glm::vec3 position);

    void SendAuthentication(uint64_t session_id, const std::string& message, uint64_t player_id);
    void SendAuthenticationFailure(uint64_t session_id, const std::string& message);

    void BroadcastRemotePlayerSpawn(const PlayerSpawnData& data);
    void BroadcastRemotePlayerDespawn(uint64_t player_id);
    void BroadcastPlayerSpawn(uint64_t session_id, uint64_t player_id);
    void BroadcastPlayerDespawn(uint64_t session_id, uint64_t player_id);
    void OnPlayerConnected(uint64_t session_id, uint64_t player_id) override;
    void OnPlayerDisconnected(uint64_t session_id) override;

    void SyncNearbyEntitiesToPlayer(uint64_t session_id, const glm::vec3& position);
    void SendPositionCorrection(uint64_t session_id, const glm::vec3& position, const glm::vec3& velocity);

    void SetSendReplyCallback(std::function<void(uint64_t sessionId, const std::vector<uint8_t>& data)> cb);
    void SetGetSessionIdsInRadiusCallback(std::function<std::vector<uint64_t>(const glm::vec3&, float)> cb);

    void SetSendAuthenticationResponseCallback(std::function<void(uint64_t session_id, const std::string& message, uint64_t player_id)> cb);
    void SetSendChunkParamsCallback(std::function<void(uint64_t session_id, const ChunkParams&)> cb);
    void SetSendChunkCallback(std::function<void(uint64_t session_id, const ChunkData&)> cb);
    void SetSendCollisionResponseCallback(std::function<void(uint64_t session_id, const CollisionResult& result)> cb);

    void SetPlayerStateCallback(std::function<void(const PlayerStateData&)> cb);
    void SetBroadcastPlayerPositionCallback(std::function<void(const PlayerPositionData&, float radius)> cb);
    void SetSendPlayerUpdateCallback(std::function<void(uint64_t session_id, const PlayerUpdateData& data)> cb);
    void SetSendPlayersUpdateCallback(std::function<void(uint64_t session_id, const PlayerUpdateData& data)> cb);
    void SetSendPlayerSpawnCallback(std::function<void(uint64_t session_id, const PlayerSpawnData& data)> cb);
    void SetSendPlayerDespawnCallback(std::function<void(uint64_t session_id, const PlayerDespawnData& data)> cb);

    void SetSendNPCInteractionResponseCallback(std::function<void(uint64_t session_id, const NpcData& response)> cb);
    void SetSendFamiliarCommandResponseCallback(std::function<void(uint64_t session_id, const FamiliarData& response)> cb);
    void SetSendEntitySpawnResponseCallback(std::function<void(uint64_t session_id, const EntitySpawnData& response)> cb);
    void SetSendLootPickupResponseCallback(std::function<void(uint64_t session_id, const LootPickupData& response)> cb);
    void SetSendInventoryResponseCallback(std::function<void(uint64_t session_id, const InventoryData& response)> cb);

    void OnAuthentication(const AuthenticationData& data);
    void OnChunkParams(const ChunkParams& req);
    void OnChunkData(const ChunkData& data);
    void OnCollisionCheck(const CollisionData& data);
    void OnRemotePlayerPosition(const PlayerPositionData& data);
    void OnPlayerPosition(const PlayerPositionData& data);
    void OnPlayerState(const PlayerStateData& data);
    void OnPlayerUpdate(const PlayerUpdateData& data);
    void OnPlayersUpdate(const PlayerUpdateData& data);
    void OnNPCInteraction(const NpcData& data);
    void OnFamiliarCommand(const FamiliarData& data);
    void OnEntitySpawnRequest(const EntitySpawnData& data);
    void OnLootPickup(const LootPickupData& data);
    void OnInventory(const InventoryData& data);

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

    void SetDatabaseService(DatabaseService* dbService);
    void SetDatabaseBackend(std::unique_ptr<DatabaseBackend> backend);
    DatabaseBackend* GetDatabaseBackend() const;

private:
    GameLogic();
    ~GameLogic();

    static std::mutex instanceMutex_;
    static GameLogic* instance_;

    std::unique_ptr<DatabaseBackend> databaseBackend_;
    std::unordered_map<uint64_t, PredictionSystem> playerPrediction_;
    std::mutex predictionMutex_;
    DatabaseService* dbService_ = nullptr;
    std::queue<std::pair<uint64_t, glm::vec3>> pendingWorldGeneration_;
    std::mutex world_generation_mutex_;

    std::function<void(uint64_t, const std::vector<uint8_t>&)> sendReplyCb_;
    std::function<std::vector<uint64_t>(const glm::vec3&, float)> getSessionIdsInRadiusCb_;

    std::function<void(uint64_t, const std::string&, uint64_t)> sendAuthResponseCb_;
    std::function<void(uint64_t, const ChunkParams&)> sendChunkParamsCb_;
    std::function<void(uint64_t, const ChunkData&)> sendChunkCb_;
    std::function<void(uint64_t, const CollisionResult&)> sendCollisionResponseCb_;

    std::function<void(const PlayerPositionData&, float)> broadcastPlayerPositionCb_;
    std::function<void(const PlayerStateData&)> playerStateCb_;
    std::function<void(uint64_t, const PlayerUpdateData&)> sendPlayerUpdateCb_;
    std::function<void(uint64_t, const PlayerUpdateData&)> sendPlayersUpdateCb_;
    std::function<void(uint64_t, const PlayerSpawnData&)> sendPlayerSpawnCb_;
    std::function<void(uint64_t, const PlayerDespawnData&)> sendPlayerDespawnCb_;

    std::function<void(uint64_t, const NpcData&)> sendNPCInteractionResponseCb_;
    std::function<void(uint64_t, const FamiliarData&)> sendFamiliarCommandResponseCb_;
    std::function<void(uint64_t, const EntitySpawnData&)> sendEntitySpawnResponseCb_;
    std::function<void(uint64_t, const LootPickupData&)> sendLootPickupResponseCb_;
    std::function<void(uint64_t, const InventoryData&)> sendInventoryResponseCb_;

    std::unordered_map<uint64_t, PlayerSpawnData> remotePlayers_;
    std::mutex remotePlayersMutex_;

    bool pythonEnabled_ = false;

    void GameLoop() override;
    void SpawnerLoop() override;
    void UpdateWorld(float deltaTime);
    bool LoadGameData();
    void SaveChunkData();
};
