#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "../../include/game/LogicCore.hpp"
#include "../../include/game/LogicWorld.hpp"
#include "../../include/game/LogicEntity.hpp"

class GameLogic : public LogicCore {
public:
    static GameLogic& GetInstance();

    // Core lifecycle
    void Initialize() override;
    void Shutdown() override;

    // World configuration
    struct WorldConfig : public WorldLogic::WorldConfig {};
    void SetWorldConfig(const WorldConfig& config);
    const WorldConfig& GetWorldConfig() const;

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

    // Broadcasting
    void BroadcastBinaryToNearbyPlayers(const glm::vec3& position, uint16_t messageType, 
                                        const std::vector<uint8_t>& data, float radius = 50.0f);
    void BroadcastToNearbyPlayers(const glm::vec3& position, const nlohmann::json& message, float radius = 50.0f);
    void SyncNearbyEntitiesToPlayer(uint64_t sessionId, const glm::vec3& position);

private:
    GameLogic();
    ~GameLogic();

    static std::mutex instanceMutex_;
    static GameLogic* instance_;

    // Component systems
    LogicWorld worldLogic_;
    LogicEntity entityLogic_;

    // Thread functions override
    void GameLoop() override;
    void SpawnerLoop() override;
    void SaveLoop() override;

    // Game tick processing
    void ProcessGameTick(float deltaTime) override;
    void UpdateWorld(float deltaTime);

    // Helper methods
    void RegisterWorldHandlers();
    void LoadGameData();
    void SaveGameState() override;
    void SaveChunkData();
    void CleanupOldData() override;
};
