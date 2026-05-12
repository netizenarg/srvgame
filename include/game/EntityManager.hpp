#pragma once

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <Python.h>

#include "logging/Logger.hpp"
#include "game/GameEntity.hpp"
#include "game/WorldChunk.hpp"

class EntityManager {
public:
    virtual ~EntityManager();

    static EntityManager& GetInstance();

    virtual uint64_t CreateEntity(EntityType type, const glm::vec3& position);
    virtual void DestroyEntity(uint64_t entityId);

    virtual GameEntity* GetEntity(uint64_t entityId);
    virtual const GameEntity* GetEntity(uint64_t entityId) const;

    virtual std::vector<uint64_t> GetEntitiesInRadius(const glm::vec3& position,
                                                      float radius,
                                                      EntityType filter = EntityType::ANY) const;
    virtual std::vector<uint64_t> GetEntitiesInChunk(int chunkX, int chunkZ) const;

    virtual void Update(float deltaTime);
    virtual void UpdateEntityPosition(uint64_t entityId, const glm::vec3& newPosition);

    virtual nlohmann::json SerializeEntity(uint64_t entityId) const;
    virtual nlohmann::json SerializeEntitiesInRadius(const glm::vec3& position,
                                                     float radius) const;

    virtual void SetEntityOwner(uint64_t entityId, uint64_t ownerId);
    virtual std::vector<uint64_t> GetOwnedEntities(uint64_t ownerId) const;

    virtual size_t GetTotalEntities() const;
    virtual size_t GetPendingDestructionCount() const;

    virtual void DumpEntityStats() const;
    virtual const char* EntityTypeToString(EntityType type) const;

    virtual std::vector<uint64_t> FindEntitiesByCriteria(
        const std::function<bool(const GameEntity&)>& predicate) const;
    virtual std::vector<uint64_t> FindEntitiesInBox(const glm::vec3& minBounds,
                                                    const glm::vec3& maxBounds,
                                                    EntityType filter = EntityType::ANY) const;

    virtual void PreallocateEntityPool(EntityType type, size_t count);
    virtual uint64_t ActivatePooledEntity(EntityType type, const glm::vec3& position);
    virtual void DeactivateEntity(uint64_t entityId);

    virtual void InitializePython();
    virtual void ShutdownPython();
    virtual bool AttachScript(uint64_t entityId, const std::string& scriptPath);
    virtual bool CallScriptMethod(uint64_t entityId, const std::string& methodName,
                                  PyObject* args = nullptr, PyObject* kwargs = nullptr);
    virtual void DetachScript(uint64_t entityId);
    virtual bool HasScript(uint64_t entityId) const;

protected:
    EntityManager();

private:
    EntityManager(const EntityManager&) = delete;
    EntityManager& operator=(const EntityManager&) = delete;

    struct PendingDestruction {
        uint64_t entityId;
        EntityType type;
        std::chrono::steady_clock::time_point destructionTime;
    };

    std::unordered_map<uint64_t, std::unique_ptr<GameEntity>> entities_;
    std::unordered_map<uint64_t, std::vector<uint64_t>> ownership_;
    std::unordered_map<uint64_t, std::unique_ptr<GameEntity>> inactiveEntities_;

    std::vector<PendingDestruction> pendingDestruction_;

    bool pythonInitialized_ = false;
    std::unordered_map<uint64_t, PyObject*> scriptInstances_;

    uint64_t nextEntityId_;
    mutable std::mutex mutex_;

    void CleanupDestroyedEntities();
    void CleanupStaleOwnershipReferences();

    PyObject* ImportModuleFromPath(const std::string& path);
};
