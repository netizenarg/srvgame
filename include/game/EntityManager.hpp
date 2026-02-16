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

class EntityManager {
public:
    virtual ~EntityManager();

    // Singleton access (optional, can be removed if you prefer dependency injection)
    static EntityManager& GetInstance();

    // ----- Entity lifecycle -----
    virtual uint64_t CreateEntity(EntityType type, const glm::vec3& position);
    virtual void DestroyEntity(uint64_t entityId);

    // ----- Entity access -----
    virtual GameEntity* GetEntity(uint64_t entityId);
    virtual const GameEntity* GetEntity(uint64_t entityId) const;

    // ----- Spatial queries -----
    virtual std::vector<uint64_t> GetEntitiesInRadius(const glm::vec3& position,
                                                      float radius,
                                                      EntityType filter = EntityType::ANY) const;
    virtual std::vector<uint64_t> GetEntitiesInChunk(int chunkX, int chunkZ) const;

    // ----- Updates -----
    virtual void Update(float deltaTime);
    virtual void UpdateEntityPosition(uint64_t entityId, const glm::vec3& newPosition);

    // ----- Serialization -----
    virtual nlohmann::json SerializeEntity(uint64_t entityId) const;
    virtual nlohmann::json SerializeEntitiesInRadius(const glm::vec3& position,
                                                     float radius) const;

    // ----- Ownership (generic) -----
    virtual void SetEntityOwner(uint64_t entityId, uint64_t ownerId);
    virtual std::vector<uint64_t> GetOwnedEntities(uint64_t ownerId) const;

    // ----- Statistics -----
    virtual size_t GetTotalEntities() const;
    virtual size_t GetPendingDestructionCount() const;

    // ----- Debugging -----
    virtual void DumpEntityStats() const;
    virtual const char* EntityTypeToString(EntityType type) const;

    // ----- Advanced queries -----
    virtual std::vector<uint64_t> FindEntitiesByCriteria(
        const std::function<bool(const GameEntity&)>& predicate) const;
    virtual std::vector<uint64_t> FindEntitiesInBox(const glm::vec3& minBounds,
                                                    const glm::vec3& maxBounds,
                                                    EntityType filter = EntityType::ANY) const;

    // ----- Entity pooling -----
    virtual void PreallocateEntityPool(EntityType type, size_t count);
    virtual uint64_t ActivatePooledEntity(EntityType type, const glm::vec3& position);
    virtual void DeactivateEntity(uint64_t entityId);

    // ----- Python scripting -----
    // Initializes the Python interpreter (call once at startup)
    virtual bool InitializePython();
    // Shuts down the Python interpreter
    virtual void ShutdownPython();

    // Attach a Python script to an entity. The script should define a class
    // with the same name as the file (without .py) and implement:
    // - __init__(self, entity_id)
    // - on_create(self)
    // - on_update(self, dt)
    // - on_destroy(self)
    // - on_collision(self, other_id)
    // - etc.
    virtual bool AttachScript(uint64_t entityId, const std::string& scriptPath);

    // Call a specific method on the entity's script (if attached).
    // Returns true if the method exists and was called.
    virtual bool CallScriptMethod(uint64_t entityId, const std::string& methodName,
                                  PyObject* args = nullptr, PyObject* kwargs = nullptr);

    // Detach and release the script object for an entity.
    virtual void DetachScript(uint64_t entityId);

    // Check if an entity has a script attached.
    virtual bool HasScript(uint64_t entityId) const;

protected:
    EntityManager();

private:
    // Non-copyable
    EntityManager(const EntityManager&) = delete;
    EntityManager& operator=(const EntityManager&) = delete;

    // Internal helper for delayed destruction
    struct PendingDestruction {
        uint64_t entityId;
        EntityType type;
        std::chrono::steady_clock::time_point destructionTime;
    };

    // Core containers
    std::unordered_map<uint64_t, std::unique_ptr<GameEntity>> entities_;
    std::unordered_map<uint64_t, std::vector<uint64_t>> ownership_;   // owner -> list of owned entities
    std::unordered_map<uint64_t, std::unique_ptr<GameEntity>> inactiveEntities_; // pool

    // Destruction queue
    std::vector<PendingDestruction> pendingDestruction_;

    // Python scripting
    bool pythonInitialized_ = false;
    // Maps entity ID to a Python object representing the script instance
    std::unordered_map<uint64_t, PyObject*> scriptInstances_;

    uint64_t nextEntityId_;
    mutable std::mutex mutex_;

    // Helpers
    void CleanupDestroyedEntities();
    void CleanupStaleOwnershipReferences();

    // Python helper: import a module from a file path
    PyObject* ImportModuleFromPath(const std::string& path);
};
