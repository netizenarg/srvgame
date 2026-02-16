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

#include "logging/Logger.hpp"
#include "game/GameEntity.hpp"

// Python integration
#include <Python.h>

class EntityManager {
public:
    virtual ~EntityManager();

    // Singleton access (optional, can be removed if you prefer dependency injection)
    static EntityManager& GetInstance();

    // ----- Entity lifecycle -----
    virtual uint64_t CreateEntity(EntityType type, const glm::vec3& position) = 0;
    virtual void DestroyEntity(uint64_t entityId) = 0;

    // ----- Entity access -----
    virtual GameEntity* GetEntity(uint64_t entityId) = 0;
    virtual const GameEntity* GetEntity(uint64_t entityId) const = 0;

    // ----- Spatial queries -----
    virtual std::vector<uint64_t> GetEntitiesInRadius(const glm::vec3& position,
                                                      float radius,
                                                      EntityType filter = EntityType::ANY) const = 0;
    virtual std::vector<uint64_t> GetEntitiesInChunk(int chunkX, int chunkZ) const = 0;

    // ----- Updates -----
    virtual void Update(float deltaTime) = 0;
    virtual void UpdateEntityPosition(uint64_t entityId, const glm::vec3& newPosition) = 0;

    // ----- Serialization -----
    virtual nlohmann::json SerializeEntity(uint64_t entityId) const = 0;
    virtual nlohmann::json SerializeEntitiesInRadius(const glm::vec3& position,
                                                     float radius) const = 0;

    // ----- Ownership (generic) -----
    virtual void SetEntityOwner(uint64_t entityId, uint64_t ownerId) = 0;
    virtual std::vector<uint64_t> GetOwnedEntities(uint64_t ownerId) const = 0;

    // ----- Statistics -----
    virtual size_t GetTotalEntities() const = 0;
    virtual size_t GetPendingDestructionCount() const = 0;

    // ----- Debugging -----
    virtual void DumpEntityStats() const = 0;
    virtual const char* EntityTypeToString(EntityType type) const = 0;

    // ----- Advanced queries -----
    virtual std::vector<uint64_t> FindEntitiesByCriteria(
        const std::function<bool(const GameEntity&)>& predicate) const = 0;
    virtual std::vector<uint64_t> FindEntitiesInBox(const glm::vec3& minBounds,
                                                    const glm::vec3& maxBounds,
                                                    EntityType filter = EntityType::ANY) const = 0;

    // ----- Entity pooling -----
    virtual void PreallocateEntityPool(EntityType type, size_t count) = 0;
    virtual uint64_t ActivatePooledEntity(EntityType type, const glm::vec3& position) = 0;
    virtual void DeactivateEntity(uint64_t entityId) = 0;

    // ----- Python scripting -----
    // Initializes the Python interpreter (call once at startup)
    virtual bool InitializePython() = 0;
    // Shuts down the Python interpreter
    virtual void ShutdownPython() = 0;

    // Attach a Python script to an entity. The script should define a class
    // with the same name as the file (without .py) and implement:
    // - __init__(self, entity_id)
    // - on_create(self)
    // - on_update(self, dt)
    // - on_destroy(self)
    // - on_collision(self, other_id)
    // - etc.
    virtual bool AttachScript(uint64_t entityId, const std::string& scriptPath) = 0;

    // Call a specific method on the entity's script (if attached).
    // Returns true if the method exists and was called.
    virtual bool CallScriptMethod(uint64_t entityId, const std::string& methodName,
                                  PyObject* args = nullptr, PyObject* kwargs = nullptr) = 0;

    // Detach and release the script object for an entity.
    virtual void DetachScript(uint64_t entityId) = 0;

    // Check if an entity has a script attached.
    virtual bool HasScript(uint64_t entityId) const = 0;

protected:
    EntityManager() = default;

    // Non-copyable
    EntityManager(const EntityManager&) = delete;
    EntityManager& operator=(const EntityManager&) = delete;
};