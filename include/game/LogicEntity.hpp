#pragma once

#include <glm/glm.hpp>
#include <unordered_map>
#include <memory>
#include <mutex>

#include "config/ConfigManager.hpp"
#include "logging/Logger.hpp"
#include "network/BinaryProtocol.hpp"
#include "network/ConnectionManager.hpp"

#include "game/NPCSystem.hpp"
#include "game/MobSystem.hpp"
#include "game/EntityManager.hpp"
#include "game/CollisionSystem.hpp"
#include "game/LootTableManager.hpp"

class LogicEntity {
public:
    static LogicEntity& GetInstance();

    // Initialization
    void Initialize();
    void Shutdown();

    // NPC management
    uint64_t SpawnNPC(NPCType type, const glm::vec3& position, uint64_t ownerId = 0);
    void DespawnNPC(uint64_t npcId);
    void UpdateNPCs(float deltaTime);
    NPCEntity* GetNPCEntity(uint64_t npcId);

    // Entity management
    GameEntity* GetEntity(uint64_t entityId);

    // Collision management
    CollisionResult CheckCollision(const glm::vec3& position, float radius, uint64_t excludeEntityId = 0);
    bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit);
    void UpdateCollisions(float deltaTime);

    // Loot management
    void CreateLootEntity(const glm::vec3& position, std::shared_ptr<LootItem> item, int quantity);

    // Statistics
    int GetActiveNPCCount() const { return npcManager_->GetNPCs().size(); }

private:
    LogicEntity();
    ~LogicEntity();

    static std::mutex instanceMutex_;
    static LogicEntity* instance_;

    std::unique_ptr<NPCManager> npcManager_;
    std::mutex npcMutex_;

    MobSystem& mobSystem_;
    EntityManager& entityManager_;
    std::unique_ptr<CollisionSystem> collisionSystem_;

    // Loot systems
    std::unique_ptr<LootTableManager> lootTableManager_;
    //std::unique_ptr<InventorySystem> inventorySystem_;

    void InitializeNPCSystem();
    void InitializeMobSystem();
    void InitializeCollisionSystem();
};
