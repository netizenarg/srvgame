#include "game/LogicEntity.hpp"

LogicEntity::LogicEntity()
    : mobSystem_(MobSystem::GetInstance()),
      entityManager_(EntityManager::GetInstance()) {
    Logger::Debug("LogicEntity created");
}

LogicEntity::~LogicEntity() {
    Shutdown();
}

void LogicEntity::Initialize() {
    InitializeNPCSystem();
    InitializeMobSystem();
    InitializeCollisionSystem();
    auto& config = ConfigManager::GetInstance();
    // Initialize loot systems
    //inventorySystem_ = std::make_unique<InventorySystem>();
    //lootTableManager_ = std::make_unique<LootTableManager>();
    //lootTableManager_->LoadLootTables("config/loot_tables.json");
    LootTableManager::GetInstance().LoadLootTables("config/loot_tables.json");
    Logger::Info("LogicEntity initialized");
}

void LogicEntity::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(npcMutex_);
        npcEntities_.clear();
        activeNPCCount_ = 0;
    }
    npcManager_.reset();
    collisionSystem_.reset();
    //inventorySystem_.reset();
    lootTableManager_.reset();
    Logger::Info("LogicEntity shutdown");
}

void LogicEntity::InitializeNPCSystem() {
    Logger::Info("Initializing NPC system...");
    npcManager_ = std::make_unique<NPCManager>();
    auto& config = ConfigManager::GetInstance();
    int initialNPCCount = config.GetInt("npcs.initial_count", 20);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> posDist(-200.0f, 200.0f);
    std::uniform_int_distribution<int> npcTypeDist(0, 3);
    for (int i = 0; i < initialNPCCount; ++i) {
        float x = posDist(gen);
        float z = posDist(gen);
        float y = 20.0f; // Default height
        NPCType type = static_cast<NPCType>(npcTypeDist(gen));
        SpawnNPC(type, glm::vec3(x, y, z));
    }
    Logger::Info("Spawned {} initial NPCs", initialNPCCount);
}

void LogicEntity::InitializeMobSystem() {
    Logger::Info("Initializing mob system...");
    mobSystem_.Initialize();
    auto& config = ConfigManager::GetInstance();
    if (config.HasKey("mobs")) {
        nlohmann::json mobConfig = config.GetJson("mobs");
        mobSystem_.LoadMobConfig(mobConfig);
    }
}

void LogicEntity::InitializeCollisionSystem() {
    Logger::Info("Initializing collision system...");
    collisionSystem_ = std::make_unique<CollisionSystem>();
}

uint64_t LogicEntity::SpawnNPC(NPCType type, const glm::vec3& position, uint64_t ownerId) {
    std::lock_guard<std::mutex> lock(npcMutex_);
    uint64_t npcId = npcManager_->SpawnNPC(type, position, ownerId);
    if (npcId == 0) {
        Logger::Error("Failed to spawn NPC type {}", static_cast<int>(type));
        return 0;
    }
    NPCEntity* npc = npcManager_->GetNPC(npcId);
    if (!npc) {
        Logger::Error("Failed to get spawned NPC");
        return 0;
    }
    npcEntities_[npcId] = std::unique_ptr<NPCEntity>(npc);
    activeNPCCount_++;
    // Register in collision system
    BoundingSphere bounds{position, 1.0f};
    collisionSystem_->RegisterEntity(npcId, bounds, CollisionType::ENTITY);
    Logger::Debug("Spawned NPC {} at [{:.1f}, {:.1f}, {:.1f}]",
                  npcId, position.x, position.y, position.z);
    return npcId;
}

void LogicEntity::DespawnNPC(uint64_t npcId) {
    std::lock_guard<std::mutex> lock(npcMutex_);
    auto it = npcEntities_.find(npcId);
    if (it == npcEntities_.end()) {
        return;
    }
    collisionSystem_->UnregisterEntity(npcId);
    npcManager_->DespawnNPC(npcId);
    npcEntities_.erase(it);
    activeNPCCount_--;
    Logger::Debug("Despawned NPC {}", npcId);
}

void LogicEntity::UpdateNPCs(float deltaTime) {
    std::lock_guard<std::mutex> lock(npcMutex_);
    for (auto& [npcId, npc] : npcEntities_) {
        if (!npc) continue;
        npc->Update(deltaTime);
        collisionSystem_->UpdateEntity(npcId, npc->GetPosition());
    }
    mobSystem_.UpdateSpawnZones(deltaTime);
    mobSystem_.ProcessRespawns(deltaTime);
}

NPCEntity* LogicEntity::GetNPCEntity(uint64_t npcId) {
    std::lock_guard<std::mutex> lock(npcMutex_);
    auto it = npcEntities_.find(npcId);
    return it != npcEntities_.end() ? it->second.get() : nullptr;
}

GameEntity* LogicEntity::GetEntity(uint64_t entityId) {
    return entityManager_.GetEntity(entityId);
}

PlayerEntity* LogicEntity::GetPlayerEntity(uint64_t playerId) {
    return entityManager_.GetPlayerEntity(playerId);
}

CollisionResult LogicEntity::CheckCollision(const glm::vec3& position, float radius, uint64_t excludeEntityId) {
    if (!collisionSystem_) {
        return CollisionResult{false};
    }
    return collisionSystem_->CheckCollision(position, radius, excludeEntityId);
}

bool LogicEntity::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit) {
    if (!collisionSystem_) {
        return false;
    }
    return collisionSystem_->Raycast(origin, direction, maxDistance, hit);
}

void LogicEntity::UpdateCollisions(float deltaTime) {
    if (collisionSystem_) {
        collisionSystem_->UpdateBroadPhase();
    }
}

void LogicEntity::CreateLootEntity(const glm::vec3& position, std::shared_ptr<LootItem> item, int quantity) {
    uint64_t entityId = entityManager_.CreateEntity(EntityType::ITEM, position);
    BoundingSphere bounds{position, 0.5f};
    collisionSystem_->RegisterEntity(entityId, bounds, CollisionType::TRIGGER);
    Logger::Debug("Created loot entity {}: {} x{}", entityId, item->GetName(), quantity);
}
