#pragma once

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "game/GameEntity.hpp"
#include "game/NPCEntity.hpp"
#include "game/PlayerEntity.hpp"
#include "logging/Logger.hpp"

class EntityManager {
public:
    static EntityManager& GetInstance();

    // Entity lifecycle
    uint64_t CreateEntity(EntityType type, const glm::vec3& position);
    void DestroyEntity(uint64_t entityId);

    // Entity access
    GameEntity*   GetEntity(uint64_t entityId);
    PlayerEntity* GetPlayerEntity(uint64_t playerId);
    NPCEntity*    GetNPCEntity(uint64_t npcId);

    // Spatial queries
    std::vector<uint64_t> GetEntitiesInRadius(const glm::vec3& position,
                                              float radius,
                                              EntityType filter = EntityType::ANY);
    std::vector<uint64_t> GetEntitiesInChunk(int chunkX, int chunkZ);

    // Updates
    void Update(float deltaTime);
    void UpdateEntityPosition(uint64_t entityId, const glm::vec3& newPosition);

    // Serialization
    nlohmann::json SerializeEntity(uint64_t entityId) const;
    nlohmann::json SerializeEntitiesInRadius(const glm::vec3& position,
                                             float radius) const;

    // Ownership
    void SetEntityOwner(uint64_t entityId, uint64_t ownerId);
    std::vector<uint64_t> GetOwnedEntities(uint64_t ownerId);

    // Statistics
    size_t GetTotalEntities() const;
    size_t GetPlayerCount() const;
    size_t GetNPCCount() const;
    size_t GetPendingDestructionCount() const;

    // Debugging
    void DumpEntityStats() const;
    const char* EntityTypeToString(EntityType type) const;

    // Advanced queries
    std::vector<uint64_t> FindEntitiesByCriteria(
        const std::function<bool(const GameEntity&)>& predicate) const;
    std::vector<uint64_t> FindEntitiesInBox(const glm::vec3& minBounds,
                                            const glm::vec3& maxBounds,
                                            EntityType filter = EntityType::ANY) const;

    // Entity pooling
    void PreallocateEntityPool(EntityType type, size_t count);
    uint64_t ActivatePooledEntity(EntityType type, const glm::vec3& position);
    void DeactivateEntity(uint64_t entityId);

private:
    // Constructor is now defined in the .cpp file (no '= default')
    EntityManager();
    ~EntityManager() = default;

    // Non-copyable
    EntityManager(const EntityManager&) = delete;
    EntityManager& operator=(const EntityManager&) = delete;

    // Helper struct for delayed destruction
    struct PendingDestruction {
        uint64_t entityId;
        EntityType type;
        std::chrono::steady_clock::time_point destructionTime;
    };

    // Core containers
    std::unordered_map<uint64_t, std::unique_ptr<GameEntity>> entities_;
    std::unordered_map<uint64_t, PlayerEntity*> playerEntities_;
    std::unordered_map<uint64_t, NPCEntity*> npcEntities_;

    // Ownership mapping
    std::unordered_map<uint64_t, std::vector<uint64_t>> ownership_;

    // Object pool
    std::unordered_map<uint64_t, std::unique_ptr<GameEntity>> inactiveEntities_;

    // Destruction queue
    std::vector<PendingDestruction> pendingDestruction_;

    uint64_t nextEntityId_;
    mutable std::mutex mutex_;

    // Cleanup helpers
    void CleanupDestroyedEntities();
    void CleanupStaleOwnershipReferences();
};
