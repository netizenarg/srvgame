#include "game/EntityManager.hpp"

EntityManager::EntityManager() : nextEntityId_(1) {
    Logger::Info("EntityManager created");
}

EntityManager::~EntityManager() {
    ShutdownPython();
}

EntityManager& EntityManager::GetInstance() {
    static EntityManager instance;
    return instance;
}

uint64_t EntityManager::CreateEntity(EntityType type, const glm::vec3& position) {
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

void EntityManager::DestroyEntity(uint64_t entityId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entities_.find(entityId);
    if (it == entities_.end()) {
        Logger::Warn("Attempted to destroy non-existent entity: {}", entityId);
        return;
    }
    EntityType type = it->second->GetType();
    pendingDestruction_.push_back({entityId, type, std::chrono::steady_clock::now()});
    ownership_.erase(entityId);
    for (auto& [owner, vec] : ownership_) {
        auto pos = std::find(vec.begin(), vec.end(), entityId);
        if (pos != vec.end()) {
            vec.erase(pos);
            break;
        }
    }
    DetachScript(entityId);
    Logger::Debug("Marked entity {} for destruction", entityId);
}

GameEntity* EntityManager::GetEntity(uint64_t entityId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entities_.find(entityId);
    return it != entities_.end() ? it->second.get() : nullptr;
}

const GameEntity* EntityManager::GetEntity(uint64_t entityId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entities_.find(entityId);
    return it != entities_.end() ? it->second.get() : nullptr;
}

std::vector<uint64_t> EntityManager::GetEntitiesInRadius(const glm::vec3& position,
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

std::vector<uint64_t> EntityManager::GetEntitiesInChunk(int chunkX, int chunkZ) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint64_t> result;
    const float chunk_size = float(WorldChunk::DEFAULT_SIZE);
    const float HALF_CHUNK = chunk_size / 2.0f;
    float minX = chunkX * chunk_size - HALF_CHUNK;
    float maxX = (chunkX + 1) * chunk_size - HALF_CHUNK;
    float minZ = chunkZ * chunk_size - HALF_CHUNK;
    float maxZ = (chunkZ + 1) * chunk_size - HALF_CHUNK;
    for (const auto& [id, entity] : entities_) {
        glm::vec3 pos = entity->GetPosition();
        if (pos.x >= minX && pos.x <= maxX && pos.z >= minZ && pos.z <= maxZ)
            result.push_back(id);
    }
    return result;
}

void EntityManager::Update(float deltaTime) {
    std::lock_guard<std::mutex> lock(mutex_);
    CleanupDestroyedEntities();
    for (auto& [id, entity] : entities_) {
        glm::vec3 vel = entity->GetVelocity();
        if (vel.x != 0.0f || vel.y != 0.0f || vel.z != 0.0f) {
            entity->SetPosition(entity->GetPosition() + vel * deltaTime);
        }
        if (HasScript(id)) {
            PyObject* args = Py_BuildValue("(f)", deltaTime);
            CallScriptMethod(id, "on_update", args, nullptr);
            Py_XDECREF(args);
        }
        entity->Update(deltaTime);
    }
}

void EntityManager::UpdateEntityPosition(uint64_t entityId, const glm::vec3& newPosition) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto* entity = GetEntity(entityId))
        entity->SetPosition(newPosition);
}

nlohmann::json EntityManager::SerializeEntity(uint64_t entityId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entities_.find(entityId);
    return it != entities_.end() ? it->second->Serialize() : nlohmann::json{};
}

nlohmann::json EntityManager::SerializeEntitiesInRadius(const glm::vec3& position,
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

void EntityManager::SetEntityOwner(uint64_t entityId, uint64_t ownerId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (entities_.find(entityId) == entities_.end()) {
        Logger::Warn("Cannot set owner for non-existent entity: {}", entityId);
        return;
    }
    if (ownerId != 0 && entities_.find(ownerId) == entities_.end()) {
        Logger::Warn("Owner entity {} does not exist", ownerId);
        return;
    }
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

std::vector<uint64_t> EntityManager::GetOwnedEntities(uint64_t ownerId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ownership_.find(ownerId);
    return it != ownership_.end() ? it->second : std::vector<uint64_t>{};
}

size_t EntityManager::GetTotalEntities() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entities_.size();
}

size_t EntityManager::GetPendingDestructionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pendingDestruction_.size();
}

void EntityManager::DumpEntityStats() const {
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

const char* EntityManager::EntityTypeToString(EntityType type) const {
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

std::vector<uint64_t> EntityManager::FindEntitiesByCriteria(
    const std::function<bool(const GameEntity&)>& predicate) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint64_t> result;
    for (const auto& [id, e] : entities_)
        if (predicate(*e))
            result.push_back(id);
    return result;
}

std::vector<uint64_t> EntityManager::FindEntitiesInBox(
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

void EntityManager::PreallocateEntityPool(EntityType type, size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < count; ++i) {
        uint64_t id = nextEntityId_++;
        auto e = std::make_unique<GameEntity>(type, glm::vec3(-10000, -10000, -10000));
        e->SetId(id);
        inactiveEntities_[id] = std::move(e);
    }
    Logger::Info("Preallocated {} entities of type {}", count, EntityTypeToString(type));
}

uint64_t EntityManager::ActivatePooledEntity(EntityType type, const glm::vec3& position) {
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

void EntityManager::DeactivateEntity(uint64_t entityId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entities_.find(entityId);
    if (it == entities_.end()) {
        Logger::Warn("Cannot deactivate non-existent entity: {}", entityId);
        return;
    }
    ownership_.erase(entityId);
    for (auto& [owner, vec] : ownership_) {
        auto pos = std::find(vec.begin(), vec.end(), entityId);
        if (pos != vec.end()) {
            vec.erase(pos);
            break;
        }
    }
    DetachScript(entityId);
    inactiveEntities_[entityId] = std::move(it->second);
    entities_.erase(it);
    Logger::Debug("Deactivated entity {} to pool", entityId);
}

void EntityManager::InitializePython() {
    if (pythonInitialized_)
        return;
    Py_Initialize();
    if (!Py_IsInitialized()) {
        Logger::Warn("EntityManager::InitializePython failed to initialize interpreter");
        return;
    }
    PyRun_SimpleString("import sys\nimport os\nsys.path.append(os.getcwd())");
    pythonInitialized_ = true;
    Logger::Info("Python interpreter initialized");
}

void EntityManager::ShutdownPython() {
    if (!pythonInitialized_)
        return;
    for (auto& [id, obj] : scriptInstances_) {
        Py_XDECREF(obj);
    }
    scriptInstances_.clear();
    Py_Finalize();
    pythonInitialized_ = false;
    Logger::Info("Python interpreter shut down");
}

PyObject* EntityManager::ImportModuleFromPath(const std::string& path) {
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

bool EntityManager::AttachScript(uint64_t entityId, const std::string& scriptPath) {
    if (!pythonInitialized_) {
        Logger::Error("Python not initialized; call InitializePython() first");
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (entities_.find(entityId) == entities_.end()) {
        Logger::Error("Cannot attach script to non-existent entity {}", entityId);
        return false;
    }
    DetachScript(entityId);
    PyObject* module = ImportModuleFromPath(scriptPath);
    if (!module)
        return false;
    std::string className = std::filesystem::path(scriptPath).stem().string();
    PyObject* pyClass = PyObject_GetAttrString(module, className.c_str());
    Py_DECREF(module);
    if (!pyClass || !PyCallable_Check(pyClass)) {
        Py_XDECREF(pyClass);
        Logger::Error("Class '{}' not found or not callable in {}", className, scriptPath);
        return false;
    }
    PyObject* args = Py_BuildValue("(K)", entityId);
    PyObject* instance = PyObject_CallObject(pyClass, args);
    Py_DECREF(pyClass);
    Py_DECREF(args);
    if (!instance) {
        PyErr_Print();
        Logger::Error("Failed to instantiate script class '{}'", className);
        return false;
    }
    scriptInstances_[entityId] = instance;
    CallScriptMethod(entityId, "on_create");
    Logger::Debug("Attached script '{}' to entity {}", scriptPath, entityId);
    return true;
}

bool EntityManager::CallScriptMethod(uint64_t entityId, const std::string& methodName,
                                     PyObject* args, PyObject* kwargs) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = scriptInstances_.find(entityId);
    if (it == scriptInstances_.end())
        return false;
    PyObject* instance = it->second;
    PyObject* method = PyObject_GetAttrString(instance, methodName.c_str());
    if (!method) {
        PyErr_Clear();
        return false;
    }
    PyObject* result = PyObject_Call(method, args ? args : PyTuple_New(0), kwargs);
    Py_DECREF(method);
    Py_XDECREF(args);
    if (!result) {
        PyErr_Print();
        Logger::Error("Error calling script method '{}' on entity {}", methodName, entityId);
        return false;
    }
    Py_DECREF(result);
    return true;
}

void EntityManager::DetachScript(uint64_t entityId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = scriptInstances_.find(entityId);
    if (it != scriptInstances_.end()) {
        CallScriptMethod(entityId, "on_destroy");
        Py_DECREF(it->second);
        scriptInstances_.erase(it);
        Logger::Debug("Detached script from entity {}", entityId);
    }
}

bool EntityManager::HasScript(uint64_t entityId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return scriptInstances_.find(entityId) != scriptInstances_.end();
}

void EntityManager::CleanupDestroyedEntities() {
    auto now = std::chrono::steady_clock::now();
    const std::chrono::milliseconds DELAY(100);
    size_t cleaned = 0;
    for (auto it = pendingDestruction_.begin(); it != pendingDestruction_.end();) {
        if (now - it->destructionTime >= DELAY) {
            uint64_t id = it->entityId;
            for (auto& [owner, vec] : ownership_) {
                auto pos = std::find(vec.begin(), vec.end(), id);
                if (pos != vec.end()) {
                    vec.erase(pos);
                    if (vec.empty())
                        ownership_.erase(owner);
                    break;
                }
            }
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

void EntityManager::CleanupStaleOwnershipReferences() {
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
