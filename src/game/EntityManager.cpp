#include "game/EntityManager.hpp"

// =============== Singleton Implementation ===============
EntityManager& EntityManager::GetInstance() {
    static EntityManager instance;
    return instance;
}

// =============== Constructor ===============
EntityManager::EntityManager() : nextEntityId_(1) {
    Logger::Info("EntityManager initialized");
}
// =============== Entity Lifecycle ===============
uint64_t EntityManager::CreateEntity(EntityType type, const glm::vec3& position) {
    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t entityId = nextEntityId_++;
    std::unique_ptr<GameEntity> entity;

    switch (type) {
        case EntityType::PLAYER: {
            auto playerEntity = std::make_unique<PlayerEntity>(position);
            playerEntities_[entityId] = playerEntity.get();
            entity = std::move(playerEntity);
            break;
        }
        case EntityType::NPC: {
            auto npcEntity = std::make_unique<NPCEntity>(NPCType::VILLAGER, position);
            npcEntities_[entityId] = npcEntity.get();
            entity = std::move(npcEntity);
            break;
        }
        case EntityType::ITEM:
        case EntityType::PROJECTILE:
            entity = std::make_unique<GameEntity>(type, position);
            break;
        default:
            Logger::Error("Unknown entity type: {}", static_cast<int>(type));
            return 0;
    }

    if (!entity) {
        Logger::Error("Failed to create entity of type {}", static_cast<int>(type));
        return 0;
    }

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

    if (type == EntityType::PLAYER) {
        playerEntities_.erase(entityId);
    } else if (type == EntityType::NPC) {
        npcEntities_.erase(entityId);
    }

    ownership_.erase(entityId);

    Logger::Debug("Marked entity {} for destruction", entityId);
}

// =============== Entity Access ===============
GameEntity* EntityManager::GetEntity(uint64_t entityId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entities_.find(entityId);
    return it != entities_.end() ? it->second.get() : nullptr;
}

PlayerEntity* EntityManager::GetPlayerEntity(uint64_t playerId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = playerEntities_.find(playerId);
    return it != playerEntities_.end() ? it->second : nullptr;
}

NPCEntity* EntityManager::GetNPCEntity(uint64_t npcId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = npcEntities_.find(npcId);
    return it != npcEntities_.end() ? it->second : nullptr;
}

// =============== Spatial Queries ===============
std::vector<uint64_t> EntityManager::GetEntitiesInRadius(const glm::vec3& position,
                                                         float radius,
                                                         EntityType filter) {
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

std::vector<uint64_t> EntityManager::GetEntitiesInChunk(int chunkX, int chunkZ) {
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

// =============== Entity Updates ===============
void EntityManager::Update(float deltaTime) {
    std::lock_guard<std::mutex> lock(mutex_);
    CleanupDestroyedEntities();

    for (auto& [id, entity] : entities_) {
        glm::vec3 vel = entity->GetVelocity();
        if (vel.x != 0.0f || vel.y != 0.0f || vel.z != 0.0f) {
            entity->SetPosition(entity->GetPosition() + vel * deltaTime);
        }
    }
}

void EntityManager::UpdateEntityPosition(uint64_t entityId, const glm::vec3& newPosition) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto* entity = GetEntity(entityId))
        entity->SetPosition(newPosition);
}

// =============== Serialization ===============
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

// =============== Ownership Management ===============
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

std::vector<uint64_t> EntityManager::GetOwnedEntities(uint64_t ownerId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ownership_.find(ownerId);
    return it != ownership_.end() ? it->second : std::vector<uint64_t>{};
}

// =============== Cleanup ===============
void EntityManager::CleanupDestroyedEntities() {
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

            // Type‑specific cleanup (can be extended)
            switch (it->type) {
                case EntityType::PLAYER:
                case EntityType::NPC:
                case EntityType::ITEM:
                case EntityType::PROJECTILE:
                    Logger::Debug("Performing cleanup for entity {} (type {})",
                                  id, EntityTypeToString(it->type));
                    break;
                default: break;
            }

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

// =============== Statistics ===============
size_t EntityManager::GetTotalEntities() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entities_.size();
}

size_t EntityManager::GetPlayerCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return playerEntities_.size();
}

size_t EntityManager::GetNPCCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return npcEntities_.size();
}

size_t EntityManager::GetPendingDestructionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pendingDestruction_.size();
}

// =============== Debug ===============
void EntityManager::DumpEntityStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Logger::Info("=== Entity Manager Statistics ===");
    Logger::Info("  Total Entities: {}", entities_.size());
    Logger::Info("  Players: {}", playerEntities_.size());
    Logger::Info("  NPCs: {}", npcEntities_.size());
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
        case EntityType::ANY:        return "ANY";
        default:                     return "UNKNOWN";
    }
}

// =============== Advanced Queries ===============
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

// =============== Entity Pooling ===============
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

            if (type == EntityType::PLAYER)
                playerEntities_[id] = dynamic_cast<PlayerEntity*>(entities_[id].get());
            else if (type == EntityType::NPC)
                npcEntities_[id] = dynamic_cast<NPCEntity*>(entities_[id].get());

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

    EntityType type = it->second->GetType();
    if (type == EntityType::PLAYER)
        playerEntities_.erase(entityId);
    else if (type == EntityType::NPC)
        npcEntities_.erase(entityId);

    // Remove from ownership
    ownership_.erase(entityId);
    for (auto& [owner, vec] : ownership_) {
        auto pos = std::find(vec.begin(), vec.end(), entityId);
        if (pos != vec.end()) {
            vec.erase(pos);
            break;
        }
    }

    inactiveEntities_[entityId] = std::move(it->second);
    entities_.erase(it);
    Logger::Debug("Deactivated entity {} to pool", entityId);
}
