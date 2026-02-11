#include "game/EntityManager.hpp"

// =============== Singleton Implementation ===============
EntityManager& EntityManager::GetInstance() {
    static EntityManager instance;
    return instance;
}

// =============== Private Constructor ===============
EntityManager::EntityManager() 
    : nextEntityId_(1) {
    Logger::Info("EntityManager initialized");
}

// =============== Entity Lifecycle ===============
uint64_t EntityManager::CreateEntity(EntityType type, const glm::vec3& position) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    uint64_t entityId = nextEntityId_++;
    std::unique_ptr<GameEntity> entity;
    
    // Create entity based on type
    switch (type) {
        case EntityType::PLAYER: {
            auto playerEntity = std::make_unique<PlayerEntity>(position);
            playerEntities_[entityId] = playerEntity.get();
            entity = std::move(playerEntity);
            break;
        }
        case EntityType::NPC: {
            auto npcEntity = std::make_unique<NPCEntity>(position);
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
    
    auto entityIt = entities_.find(entityId);
    if (entityIt == entities_.end()) {
        Logger::Warn("Attempted to destroy non-existent entity: {}", entityId);
        return;
    }
    
    // Mark the entity as pending destruction
    EntityType type = entityIt->second->GetType();
    
    // Add to pending destruction queue with timestamp
    pendingDestruction_.push_back({entityId, type, std::chrono::steady_clock::now()});
    
    // Immediately remove from active maps to prevent further interactions
    if (type == EntityType::PLAYER) {
        playerEntities_.erase(entityId);
    } else if (type == EntityType::NPC) {
        npcEntities_.erase(entityId);
    }
    
    // Also remove from ownership mappings
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
    float radiusSquared = radius * radius;
    
    for (const auto& [entityId, entity] : entities_) {
        // Skip if doesn't match filter
        if (filter != EntityType::ANY && entity->GetType() != filter) {
            continue;
        }
        
        // Calculate squared distance
        glm::vec3 entityPos = entity->GetPosition();
        glm::vec3 diff = entityPos - position;
        float distanceSquared = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;
        
        if (distanceSquared <= radiusSquared) {
            result.push_back(entityId);
        }
    }
    
    return result;
}

std::vector<uint64_t> EntityManager::GetEntitiesInChunk(int chunkX, int chunkZ) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<uint64_t> result;
    
    // Assuming chunk size is 32 units (common in voxel games)
    const float CHUNK_SIZE = 32.0f;
    const float HALF_CHUNK = CHUNK_SIZE / 2.0f;
    
    // Calculate chunk bounds
    float minX = chunkX * CHUNK_SIZE - HALF_CHUNK;
    float maxX = (chunkX + 1) * CHUNK_SIZE - HALF_CHUNK;
    float minZ = chunkZ * CHUNK_SIZE - HALF_CHUNK;
    float maxZ = (chunkZ + 1) * CHUNK_SIZE - HALF_CHUNK;
    
    for (const auto& [entityId, entity] : entities_) {
        glm::vec3 pos = entity->GetPosition();
        
        // Check if entity is within chunk bounds (ignore Y coordinate for now)
        if (pos.x >= minX && pos.x <= maxX && 
            pos.z >= minZ && pos.z <= maxZ) {
            result.push_back(entityId);
        }
    }
    
    return result;
}

// =============== Entity Updates ===============
void EntityManager::Update(float deltaTime) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    CleanupDestroyedEntities();
    
    // Update all entities
    for (auto& [entityId, entity] : entities_) {
        // Apply velocity to position
        glm::vec3 pos = entity->GetPosition();
        glm::vec3 vel = entity->GetVelocity();
        
        if (vel.x != 0.0f || vel.y != 0.0f || vel.z != 0.0f) {
            pos += vel * deltaTime;
            entity->SetPosition(pos);
        }
    }
}

void EntityManager::UpdateEntityPosition(uint64_t entityId, const glm::vec3& newPosition) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto entity = GetEntity(entityId);
    if (entity) {
        entity->SetPosition(newPosition);
    }
}

// =============== Serialization ===============
nlohmann::json EntityManager::SerializeEntity(uint64_t entityId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = entities_.find(entityId);
    if (it == entities_.end()) {
        return nlohmann::json();
    }
    
    return it->second->Serialize();
}

nlohmann::json EntityManager::SerializeEntitiesInRadius(const glm::vec3& position, 
                                                       float radius) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    nlohmann::json result = nlohmann::json::array();
    float radiusSquared = radius * radius;
    
    for (const auto& [entityId, entity] : entities_) {
        glm::vec3 entityPos = entity->GetPosition();
        glm::vec3 diff = entityPos - position;
        float distanceSquared = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;
        
        if (distanceSquared <= radiusSquared) {
            result.push_back(entity->Serialize());
        }
    }
    
    return result;
}

// =============== Ownership Management ===============
void EntityManager::SetEntityOwner(uint64_t entityId, uint64_t ownerId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if entity exists
    if (entities_.find(entityId) == entities_.end()) {
        Logger::Warn("Cannot set owner for non-existent entity: {}", entityId);
        return;
    }
    
    // Check if owner exists (optional, could be NPC or player)
    if (ownerId != 0 && entities_.find(ownerId) == entities_.end()) {
        Logger::Warn("Owner entity {} does not exist", ownerId);
        return;
    }
    
    // Remove entity from previous owner if any
    for (auto& [existingOwnerId, ownedEntities] : ownership_) {
        auto it = std::find(ownedEntities.begin(), ownedEntities.end(), entityId);
        if (it != ownedEntities.end()) {
            ownedEntities.erase(it);
            // If owner no longer owns anything, remove the entry
            if (ownedEntities.empty()) {
                ownership_.erase(existingOwnerId);
            }
            break;
        }
    }
    
    // Add to new owner
    if (ownerId != 0) {
        ownership_[ownerId].push_back(entityId);
        Logger::Debug("Entity {} is now owned by {}", entityId, ownerId);
    }
}

std::vector<uint64_t> EntityManager::GetOwnedEntities(uint64_t ownerId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = ownership_.find(ownerId);
    if (it != ownership_.end()) {
        return it->second;
    }
    
    return {};
}

// =============== Cleanup Destroyed Entities ===============
void EntityManager::CleanupDestroyedEntities() {
    auto now = std::chrono::steady_clock::now();
    
    // Configuration: Delay before actually removing entities
    const std::chrono::milliseconds DESTRUCTION_DELAY(100); // 100ms delay
    size_t cleanedCount = 0;
    
    // Process pending destruction queue
    for (auto it = pendingDestruction_.begin(); it != pendingDestruction_.end();) {
        // Check if enough time has passed since destruction was requested
        auto timeSinceDestruction = now - it->destructionTime;
        
        if (timeSinceDestruction >= DESTRUCTION_DELAY) {
            uint64_t entityId = it->entityId;
            EntityType type = it->type;
            
            // Actually remove the entity from the main storage
            auto entityIt = entities_.find(entityId);
            if (entityIt != entities_.end()) {
                // Remove from any ownership lists where this entity might be owned
                for (auto& [ownerId, ownedEntities] : ownership_) {
                    auto ownedIt = std::find(ownedEntities.begin(), ownedEntities.end(), entityId);
                    if (ownedIt != ownedEntities.end()) {
                        ownedEntities.erase(ownedIt);
                        
                        // If owner no longer owns anything, remove the entry
                        if (ownedEntities.empty()) {
                            ownership_.erase(ownerId);
                        }
                        
                        // Break after finding the owner (entity can only have one owner)
                        break;
                    }
                }
                
                // Perform any type-specific cleanup
                switch (type) {
                    case EntityType::PLAYER:
                        // Player-specific cleanup
                        Logger::Debug("Performing player-specific cleanup for entity {}", entityId);
                        break;
                    case EntityType::NPC:
                        // NPC-specific cleanup
                        Logger::Debug("Performing NPC-specific cleanup for entity {}", entityId);
                        break;
                    case EntityType::ITEM:
                        // Item-specific cleanup
                        Logger::Debug("Performing item-specific cleanup for entity {}", entityId);
                        break;
                    case EntityType::PROJECTILE:
                        // Projectile-specific cleanup
                        Logger::Debug("Performing projectile-specific cleanup for entity {}", entityId);
                        break;
                    default:
                        break;
                }
                
                // Remove the entity
                entities_.erase(entityIt);
                cleanedCount++;
                
                Logger::Debug("Completely removed entity {} from EntityManager", entityId);
            }
            
            // Remove from pending destruction list
            it = pendingDestruction_.erase(it);
        } else {
            ++it;
        }
    }
    
    // Clean up stale references in ownership lists
    CleanupStaleOwnershipReferences();
    
    if (cleanedCount > 0) {
        Logger::Debug("Cleaned up {} destroyed entities", cleanedCount);
    }
}

// =============== Private Helper Methods ===============
void EntityManager::CleanupStaleOwnershipReferences() {
    // Clean up any ownership references to non-existent entities
    for (auto it = ownership_.begin(); it != ownership_.end();) {
        auto& [ownerId, ownedEntities] = *it;
        
        // Remove any owned entities that no longer exist
        ownedEntities.erase(
            std::remove_if(ownedEntities.begin(), ownedEntities.end(),
                [this](uint64_t entityId) {
                    return entities_.find(entityId) == entities_.end();
                }),
            ownedEntities.end()
        );
        
        // If owner has no more valid owned entities, remove the entry
        if (ownedEntities.empty()) {
            it = ownership_.erase(it);
        } else {
            ++it;
        }
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

// =============== Debug Methods ===============
void EntityManager::DumpEntityStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    Logger::Info("=== Entity Manager Statistics ===");
    Logger::Info("  Total Entities: {}", entities_.size());
    Logger::Info("  Players: {}", playerEntities_.size());
    Logger::Info("  NPCs: {}", npcEntities_.size());
    Logger::Info("  Pending Destruction: {}", pendingDestruction_.size());
    Logger::Info("  Ownership Relations: {}", ownership_.size());
    
    // Count by type
    std::unordered_map<EntityType, size_t> typeCounts;
    for (const auto& [id, entity] : entities_) {
        typeCounts[entity->GetType()]++;
    }
    
    Logger::Info("  Entity Type Breakdown:");
    for (const auto& [type, count] : typeCounts) {
        Logger::Info("    - {}: {}", EntityTypeToString(type), count);
    }
    
    Logger::Info("=================================");
}

// =============== Utility Methods ===============
const char* EntityManager::EntityTypeToString(EntityType type) const {
    switch (type) {
        case EntityType::PLAYER: return "PLAYER";
        case EntityType::NPC: return "NPC";
        case EntityType::ITEM: return "ITEM";
        case EntityType::PROJECTILE: return "PROJECTILE";
        case EntityType::ANY: return "ANY";
        default: return "UNKNOWN";
    }
}

// =============== Advanced Queries ===============
std::vector<uint64_t> EntityManager::FindEntitiesByCriteria(
    const std::function<bool(const GameEntity&)>& predicate) const {
    
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint64_t> result;
    
    for (const auto& [entityId, entity] : entities_) {
        if (predicate(*entity)) {
            result.push_back(entityId);
        }
    }
    
    return result;
}

std::vector<uint64_t> EntityManager::FindEntitiesInBox(
    const glm::vec3& minBounds, const glm::vec3& maxBounds, EntityType filter) const {
    
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint64_t> result;
    
    for (const auto& [entityId, entity] : entities_) {
        // Skip if doesn't match filter
        if (filter != EntityType::ANY && entity->GetType() != filter) {
            continue;
        }
        
        glm::vec3 pos = entity->GetPosition();
        if (pos.x >= minBounds.x && pos.x <= maxBounds.x &&
            pos.y >= minBounds.y && pos.y <= maxBounds.y &&
            pos.z >= minBounds.z && pos.z <= maxBounds.z) {
            result.push_back(entityId);
        }
    }
    
    return result;
}

// =============== Entity Pool Management ===============
void EntityManager::PreallocateEntityPool(EntityType type, size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (size_t i = 0; i < count; ++i) {
        // Create entities at a far away location
        uint64_t entityId = nextEntityId_++;
        auto entity = std::make_unique<GameEntity>(type, glm::vec3(-10000, -10000, -10000));
        entity->SetId(entityId);
        
        // Mark as inactive/pooled
        inactiveEntities_[entityId] = std::move(entity);
    }
    
    Logger::Info("Preallocated {} entities of type {}", count, EntityTypeToString(type));
}

uint64_t EntityManager::ActivatePooledEntity(EntityType type, const glm::vec3& position) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Find an inactive entity of the right type
    for (auto it = inactiveEntities_.begin(); it != inactiveEntities_.end(); ++it) {
        if (it->second->GetType() == type) {
            uint64_t entityId = it->first;
            
            // Move to active entities
            it->second->SetPosition(position);
            entities_[entityId] = std::move(it->second);
            inactiveEntities_.erase(it);
            
            // Add to type-specific maps
            if (type == EntityType::PLAYER) {
                playerEntities_[entityId] = dynamic_cast<PlayerEntity*>(entities_[entityId].get());
            } else if (type == EntityType::NPC) {
                npcEntities_[entityId] = dynamic_cast<NPCEntity*>(entities_[entityId].get());
            }
            
            Logger::Debug("Activated pooled entity {} of type {}", entityId, EntityTypeToString(type));
            return entityId;
        }
    }
    
    // No pooled entity available, create new one
    return CreateEntity(type, position);
}

void EntityManager::DeactivateEntity(uint64_t entityId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto entityIt = entities_.find(entityId);
    if (entityIt == entities_.end()) {
        Logger::Warn("Cannot deactivate non-existent entity: {}", entityId);
        return;
    }
    
    // Move to inactive pool
    EntityType type = entityIt->second->GetType();
    
    // Remove from active maps
    if (type == EntityType::PLAYER) {
        playerEntities_.erase(entityId);
    } else if (type == EntityType::NPC) {
        npcEntities_.erase(entityId);
    }
    
    // Remove from ownership
    ownership_.erase(entityId);
    for (auto& [ownerId, ownedEntities] : ownership_) {
        auto it = std::find(ownedEntities.begin(), ownedEntities.end(), entityId);
        if (it != ownedEntities.end()) {
            ownedEntities.erase(it);
            break;
        }
    }
    
    // Move to inactive pool
    inactiveEntities_[entityId] = std::move(entityIt->second);
    entities_.erase(entityIt);
    
    Logger::Debug("Deactivated entity {} to pool", entityId);
}
