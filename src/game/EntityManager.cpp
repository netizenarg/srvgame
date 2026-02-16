#include "game/EntityManager.hpp"

#include <mutex>
#include <chrono>
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>
#include <filesystem>
#include <cassert>

// ============================================================================
// Concrete implementation of EntityManager with Python scripting support
// ============================================================================

class EntityManagerImpl : public EntityManager {
public:
    EntityManagerImpl();
    ~EntityManagerImpl() override;

    // ----- Singleton access -----
    static EntityManagerImpl& GetInstance();

    // ----- Entity lifecycle -----
    uint64_t CreateEntity(EntityType type, const glm::vec3& position) override;
    void DestroyEntity(uint64_t entityId) override;

    // ----- Entity access -----
    GameEntity* GetEntity(uint64_t entityId) override;
    const GameEntity* GetEntity(uint64_t entityId) const override;

    // ----- Spatial queries -----
    std::vector<uint64_t> GetEntitiesInRadius(const glm::vec3& position,
                                              float radius,
                                              EntityType filter) const override;
    std::vector<uint64_t> GetEntitiesInChunk(int chunkX, int chunkZ) const override;

    // ----- Updates -----
    void Update(float deltaTime) override;
    void UpdateEntityPosition(uint64_t entityId, const glm::vec3& newPosition) override;

    // ----- Serialization -----
    nlohmann::json SerializeEntity(uint64_t entityId) const override;
    nlohmann::json SerializeEntitiesInRadius(const glm::vec3& position,
                                             float radius) const override;

    // ----- Ownership -----
    void SetEntityOwner(uint64_t entityId, uint64_t ownerId) override;
    std::vector<uint64_t> GetOwnedEntities(uint64_t ownerId) const override;

    // ----- Statistics -----
    size_t GetTotalEntities() const override;
    size_t GetPendingDestructionCount() const override;

    // ----- Debugging -----
    void DumpEntityStats() const override;
    const char* EntityTypeToString(EntityType type) const override;

    // ----- Advanced queries -----
    std::vector<uint64_t> FindEntitiesByCriteria(
        const std::function<bool(const GameEntity&)>& predicate) const override;
    std::vector<uint64_t> FindEntitiesInBox(const glm::vec3& minBounds,
                                            const glm::vec3& maxBounds,
                                            EntityType filter) const override;

    // ----- Entity pooling -----
    void PreallocateEntityPool(EntityType type, size_t count) override;
    uint64_t ActivatePooledEntity(EntityType type, const glm::vec3& position) override;
    void DeactivateEntity(uint64_t entityId) override;

    // ----- Python scripting -----
    bool InitializePython() override;
    void ShutdownPython() override;
    bool AttachScript(uint64_t entityId, const std::string& scriptPath) override;
    bool CallScriptMethod(uint64_t entityId, const std::string& methodName,
                          PyObject* args, PyObject* kwargs) override;
    void DetachScript(uint64_t entityId) override;
    bool HasScript(uint64_t entityId) const override;

private:
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

// ============================================================================
// Singleton implementation
// ============================================================================

EntityManagerImpl& EntityManagerImpl::GetInstance() {
    static EntityManagerImpl instance;
    return instance;
}

// For backward compatibility with existing code that calls EntityManager::GetInstance()
EntityManager& EntityManager::GetInstance() {
    return EntityManagerImpl::GetInstance();
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

EntityManagerImpl::EntityManagerImpl() : nextEntityId_(1) {
    Logger::Info("EntityManagerImpl created");
}

EntityManagerImpl::~EntityManagerImpl() {
    ShutdownPython();
}

EntityManager::~EntityManager() = default;

// ============================================================================
// Entity Lifecycle
// ============================================================================

uint64_t EntityManagerImpl::CreateEntity(EntityType type, const glm::vec3& position) {
    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t entityId = nextEntityId_++;
    auto entity = std::make_unique<GameEntity>(type, position);
    entity->SetId(entityId);
    entities_[entityId] = std::move(entity);

    Logger::Debug("Created entity {} of type {} at [{:.1f}, {:.1f}, {:.1f}]",
                  entityId, static_cast<int>(type),
                  position.x, position.y, position.z);

    return entityId;
}

void EntityManagerImpl::DestroyEntity(uint64_t entityId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = entities_.find(entityId);
    if (it == entities_.end()) {
        Logger::Warn("Attempted to destroy non-existent entity: {}", entityId);
        return;
    }

    EntityType type = it->second->GetType();
    pendingDestruction_.push_back({entityId, type, std::chrono::steady_clock::now()});

    // Remove from ownership maps (owner references will be cleaned later)
    ownership_.erase(entityId);
    for (auto& [owner, vec] : ownership_) {
        auto pos = std::find(vec.begin(), vec.end(), entityId);
        if (pos != vec.end()) {
            vec.erase(pos);
            break;
        }
    }

    // Detach any Python script
    DetachScript(entityId);

    Logger::Debug("Marked entity {} for destruction", entityId);
}

// ============================================================================
// Entity Access
// ============================================================================

GameEntity* EntityManagerImpl::GetEntity(uint64_t entityId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entities_.find(entityId);
    return it != entities_.end() ? it->second.get() : nullptr;
}

const GameEntity* EntityManagerImpl::GetEntity(uint64_t entityId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entities_.find(entityId);
    return it != entities_.end() ? it->second.get() : nullptr;
}

// ============================================================================
// Spatial Queries
// ============================================================================

std::vector<uint64_t> EntityManagerImpl::GetEntitiesInRadius(const glm::vec3& position,
                                                             float radius,
                                                             EntityType filter) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint64_t> result;
    float radiusSq = radius * radius;

    for (const auto& [id, entity] : entities_) {
        if (filter != EntityType::ANY && entity->GetType() != filter)
            continue;

        glm::vec3 diff = entity->GetPosition() - position;
        if (glm::dot(diff, diff) <= radiusSq)
            result.push_back(id);
    }
    return result;
}

std::vector<uint64_t> EntityManagerImpl::GetEntitiesInChunk(int chunkX, int chunkZ) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint64_t> result;
    const float CHUNK_SIZE = 32.0f;
    const float HALF_CHUNK = CHUNK_SIZE / 2.0f;

    float minX = chunkX * CHUNK_SIZE - HALF_CHUNK;
    float maxX = (chunkX + 1) * CHUNK_SIZE - HALF_CHUNK;
    float minZ = chunkZ * CHUNK_SIZE - HALF_CHUNK;
    float maxZ = (chunkZ + 1) * CHUNK_SIZE - HALF_CHUNK;

    for (const auto& [id, entity] : entities_) {
        glm::vec3 pos = entity->GetPosition();
        if (pos.x >= minX && pos.x <= maxX && pos.z >= minZ && pos.z <= maxZ)
            result.push_back(id);
    }
    return result;
}

// ============================================================================
// Updates
// ============================================================================

void EntityManagerImpl::Update(float deltaTime) {
    std::lock_guard<std::mutex> lock(mutex_);
    CleanupDestroyedEntities();

    for (auto& [id, entity] : entities_) {
        // Basic physics: integrate velocity
        glm::vec3 vel = entity->GetVelocity();
        if (vel.x != 0.0f || vel.y != 0.0f || vel.z != 0.0f) {
            entity->SetPosition(entity->GetPosition() + vel * deltaTime);
        }

        // Call script's on_update if attached
        if (HasScript(id)) {
            PyObject* args = Py_BuildValue("(f)", deltaTime);
            CallScriptMethod(id, "on_update", args, nullptr);
            Py_XDECREF(args);
        }

        // Let the entity itself update (C++ side)
        entity->Update(deltaTime);
    }
}

void EntityManagerImpl::UpdateEntityPosition(uint64_t entityId, const glm::vec3& newPosition) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto* entity = GetEntity(entityId))
        entity->SetPosition(newPosition);
}

// ============================================================================
// Serialization
// ============================================================================

nlohmann::json EntityManagerImpl::SerializeEntity(uint64_t entityId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entities_.find(entityId);
    return it != entities_.end() ? it->second->Serialize() : nlohmann::json{};
}

nlohmann::json EntityManagerImpl::SerializeEntitiesInRadius(const glm::vec3& position,
                                                            float radius) const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json arr = nlohmann::json::array();
    float radiusSq = radius * radius;

    for (const auto& [id, entity] : entities_) {
        glm::vec3 diff = entity->GetPosition() - position;
        if (glm::dot(diff, diff) <= radiusSq)
            arr.push_back(entity->Serialize());
    }
    return arr;
}

// ============================================================================
// Ownership Management
// ============================================================================

void EntityManagerImpl::SetEntityOwner(uint64_t entityId, uint64_t ownerId) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (entities_.find(entityId) == entities_.end()) {
        Logger::Warn("Cannot set owner for non-existent entity: {}", entityId);
        return;
    }
    if (ownerId != 0 && entities_.find(ownerId) == entities_.end()) {
        Logger::Warn("Owner entity {} does not exist", ownerId);
        return;
    }

    // Remove from previous owner
    for (auto& [existingOwner, vec] : ownership_) {
        auto it = std::find(vec.begin(), vec.end(), entityId);
        if (it != vec.end()) {
            vec.erase(it);
            if (vec.empty())
                ownership_.erase(existingOwner);
            break;
        }
    }

    if (ownerId != 0) {
        ownership_[ownerId].push_back(entityId);
        Logger::Debug("Entity {} is now owned by {}", entityId, ownerId);
    }
}

std::vector<uint64_t> EntityManagerImpl::GetOwnedEntities(uint64_t ownerId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ownership_.find(ownerId);
    return it != ownership_.end() ? it->second : std::vector<uint64_t>{};
}

// ============================================================================
// Statistics
// ============================================================================

size_t EntityManagerImpl::GetTotalEntities() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entities_.size();
}

size_t EntityManagerImpl::GetPendingDestructionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pendingDestruction_.size();
}

// ============================================================================
// Debugging
// ============================================================================

void EntityManagerImpl::DumpEntityStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Logger::Info("=== Entity Manager Statistics ===");
    Logger::Info("  Total Entities: {}", entities_.size());
    Logger::Info("  Pending Destruction: {}", pendingDestruction_.size());
    Logger::Info("  Ownership Relations: {}", ownership_.size());

    std::unordered_map<EntityType, size_t> counts;
    for (const auto& [id, e] : entities_)
        counts[e->GetType()]++;

    Logger::Info("  Entity Type Breakdown:");
    for (const auto& [type, cnt] : counts)
        Logger::Info("    - {}: {}", EntityTypeToString(type), cnt);
    Logger::Info("=================================");
}

const char* EntityManagerImpl::EntityTypeToString(EntityType type) const {
    switch (type) {
        case EntityType::PLAYER:     return "PLAYER";
        case EntityType::NPC:        return "NPC";
        case EntityType::ITEM:       return "ITEM";
        case EntityType::PROJECTILE: return "PROJECTILE";
        case EntityType::VEHICLE:    return "VEHICLE";
        case EntityType::ENVIRONMENT:return "ENVIRONMENT";
        case EntityType::COLLECTIBLE:return "COLLECTIBLE";
        case EntityType::CONTAINER:  return "CONTAINER";
        case EntityType::DOOR:       return "DOOR";
        case EntityType::TRAP:       return "TRAP";
        case EntityType::SPAWNER:    return "SPAWNER";
        case EntityType::DECORATION: return "DECORATION";
        case EntityType::PARTICLE:   return "PARTICLE";
        case EntityType::LIGHT:      return "LIGHT";
        case EntityType::SOUND:      return "SOUND";
        case EntityType::TRIGGER:    return "TRIGGER";
        case EntityType::CHECKPOINT: return "CHECKPOINT";
        case EntityType::WAYPOINT:   return "WAYPOINT";
        case EntityType::ANY:        return "ANY";
        default:                     return "UNKNOWN";
    }
}

// ============================================================================
// Advanced Queries
// ============================================================================

std::vector<uint64_t> EntityManagerImpl::FindEntitiesByCriteria(
    const std::function<bool(const GameEntity&)>& predicate) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint64_t> result;
    for (const auto& [id, e] : entities_)
        if (predicate(*e))
            result.push_back(id);
    return result;
}

std::vector<uint64_t> EntityManagerImpl::FindEntitiesInBox(
    const glm::vec3& minBounds, const glm::vec3& maxBounds, EntityType filter) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint64_t> result;
    for (const auto& [id, e] : entities_) {
        if (filter != EntityType::ANY && e->GetType() != filter)
            continue;
        glm::vec3 pos = e->GetPosition();
        if (pos.x >= minBounds.x && pos.x <= maxBounds.x &&
            pos.y >= minBounds.y && pos.y <= maxBounds.y &&
            pos.z >= minBounds.z && pos.z <= maxBounds.z)
            result.push_back(id);
    }
    return result;
}

// ============================================================================
// Entity Pooling
// ============================================================================

void EntityManagerImpl::PreallocateEntityPool(EntityType type, size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < count; ++i) {
        uint64_t id = nextEntityId_++;
        auto e = std::make_unique<GameEntity>(type, glm::vec3(-10000, -10000, -10000));
        e->SetId(id);
        inactiveEntities_[id] = std::move(e);
    }
    Logger::Info("Preallocated {} entities of type {}", count, EntityTypeToString(type));
}

uint64_t EntityManagerImpl::ActivatePooledEntity(EntityType type, const glm::vec3& position) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = inactiveEntities_.begin(); it != inactiveEntities_.end(); ++it) {
        if (it->second->GetType() == type) {
            uint64_t id = it->first;
            it->second->SetPosition(position);
            entities_[id] = std::move(it->second);
            inactiveEntities_.erase(it);
            Logger::Debug("Activated pooled entity {} of type {}", id, EntityTypeToString(type));
            return id;
        }
    }
    return CreateEntity(type, position);
}

void EntityManagerImpl::DeactivateEntity(uint64_t entityId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entities_.find(entityId);
    if (it == entities_.end()) {
        Logger::Warn("Cannot deactivate non-existent entity: {}", entityId);
        return;
    }

    // Remove from ownership
    ownership_.erase(entityId);
    for (auto& [owner, vec] : ownership_) {
        auto pos = std::find(vec.begin(), vec.end(), entityId);
        if (pos != vec.end()) {
            vec.erase(pos);
            break;
        }
    }

    // Detach script
    DetachScript(entityId);

    inactiveEntities_[entityId] = std::move(it->second);
    entities_.erase(it);
    Logger::Debug("Deactivated entity {} to pool", entityId);
}

// ============================================================================
// Python Scripting
// ============================================================================

bool EntityManagerImpl::InitializePython() {
    if (pythonInitialized_)
        return true;

    Py_Initialize();
    if (!Py_IsInitialized()) {
        Logger::Error("Failed to initialize Python interpreter");
        return false;
    }

    // Optionally add the script directory to sys.path
    PyRun_SimpleString("import sys\nimport os\nsys.path.append(os.getcwd())");

    pythonInitialized_ = true;
    Logger::Info("Python interpreter initialized");
    return true;
}

void EntityManagerImpl::ShutdownPython() {
    if (!pythonInitialized_)
        return;

    // Clean up all script instances
    for (auto& [id, obj] : scriptInstances_) {
        Py_XDECREF(obj);
    }
    scriptInstances_.clear();

    Py_Finalize();
    pythonInitialized_ = false;
    Logger::Info("Python interpreter shut down");
}

PyObject* EntityManagerImpl::ImportModuleFromPath(const std::string& path) {
    // Extract directory and module name
    std::filesystem::path fsPath(path);
    std::string moduleName = fsPath.stem().string();
    std::string dir = fsPath.parent_path().string();

    PyObject* sysPath = PySys_GetObject("path");
    PyObject* dirObj = PyUnicode_FromString(dir.c_str());
    PyList_Append(sysPath, dirObj);
    Py_DECREF(dirObj);

    PyObject* module = PyImport_ImportModule(moduleName.c_str());
    if (!module) {
        PyErr_Print();
        Logger::Error("Failed to import module '{}' from '{}'", moduleName, dir);
    }
    return module;
}

bool EntityManagerImpl::AttachScript(uint64_t entityId, const std::string& scriptPath) {
    if (!pythonInitialized_) {
        Logger::Error("Python not initialized; call InitializePython() first");
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Ensure entity exists
    if (entities_.find(entityId) == entities_.end()) {
        Logger::Error("Cannot attach script to non-existent entity {}", entityId);
        return false;
    }

    // Detach any existing script
    DetachScript(entityId);

    // Import the module
    PyObject* module = ImportModuleFromPath(scriptPath);
    if (!module)
        return false;

    // Get the class (assumed to be same as module name)
    std::string className = std::filesystem::path(scriptPath).stem().string();
    PyObject* pyClass = PyObject_GetAttrString(module, className.c_str());
    Py_DECREF(module);

    if (!pyClass || !PyCallable_Check(pyClass)) {
        Py_XDECREF(pyClass);
        Logger::Error("Class '{}' not found or not callable in {}", className, scriptPath);
        return false;
    }

    // Create an instance: pass entity_id to __init__
    PyObject* args = Py_BuildValue("(K)", entityId);
    PyObject* instance = PyObject_CallObject(pyClass, args);
    Py_DECREF(pyClass);
    Py_DECREF(args);

    if (!instance) {
        PyErr_Print();
        Logger::Error("Failed to instantiate script class '{}'", className);
        return false;
    }

    // Store the instance
    scriptInstances_[entityId] = instance;

    // Call on_create if present
    CallScriptMethod(entityId, "on_create");

    Logger::Debug("Attached script '{}' to entity {}", scriptPath, entityId);
    return true;
}

bool EntityManagerImpl::CallScriptMethod(uint64_t entityId, const std::string& methodName,
                                         PyObject* args, PyObject* kwargs) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = scriptInstances_.find(entityId);
    if (it == scriptInstances_.end())
        return false;

    PyObject* instance = it->second;
    PyObject* method = PyObject_GetAttrString(instance, methodName.c_str());
    if (!method) {
        PyErr_Clear(); // Method not defined, ignore
        return false;
    }

    PyObject* result = PyObject_Call(method, args ? args : PyTuple_New(0), kwargs);
    Py_DECREF(method);
    Py_XDECREF(args); // args is borrowed if passed in, but we incref? We'll manage carefully.

    if (!result) {
        PyErr_Print();
        Logger::Error("Error calling script method '{}' on entity {}", methodName, entityId);
        return false;
    }

    Py_DECREF(result);
    return true;
}

void EntityManagerImpl::DetachScript(uint64_t entityId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = scriptInstances_.find(entityId);
    if (it != scriptInstances_.end()) {
        // Call on_destroy if present
        CallScriptMethod(entityId, "on_destroy");

        Py_DECREF(it->second);
        scriptInstances_.erase(it);
        Logger::Debug("Detached script from entity {}", entityId);
    }
}

bool EntityManagerImpl::HasScript(uint64_t entityId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return scriptInstances_.find(entityId) != scriptInstances_.end();
}

// ============================================================================
// Cleanup Helpers
// ============================================================================

void EntityManagerImpl::CleanupDestroyedEntities() {
    auto now = std::chrono::steady_clock::now();
    const std::chrono::milliseconds DELAY(100);
    size_t cleaned = 0;

    for (auto it = pendingDestruction_.begin(); it != pendingDestruction_.end();) {
        if (now - it->destructionTime >= DELAY) {
            uint64_t id = it->entityId;

            // Remove from ownership lists
            for (auto& [owner, vec] : ownership_) {
                auto pos = std::find(vec.begin(), vec.end(), id);
                if (pos != vec.end()) {
                    vec.erase(pos);
                    if (vec.empty())
                        ownership_.erase(owner);
                    break;
                }
            }

            // Detach script (should already be detached, but just in case)
            DetachScript(id);

            entities_.erase(id);
            cleaned++;
            it = pendingDestruction_.erase(it);
        } else {
            ++it;
        }
    }

    CleanupStaleOwnershipReferences();
    if (cleaned > 0)
        Logger::Debug("Cleaned up {} destroyed entities", cleaned);
}

void EntityManagerImpl::CleanupStaleOwnershipReferences() {
    for (auto it = ownership_.begin(); it != ownership_.end();) {
        it->second.erase(
            std::remove_if(it->second.begin(), it->second.end(),
                [this](uint64_t id) { return entities_.find(id) == entities_.end(); }),
            it->second.end()
        );
        if (it->second.empty())
            it = ownership_.erase(it);
        else
            ++it;
    }
}