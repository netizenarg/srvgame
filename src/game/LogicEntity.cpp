#include "game/LogicEntity.hpp"

// =============== Static Members ===============
std::mutex LogicEntity::instanceMutex_;
LogicEntity* LogicEntity::instance_ = nullptr;

// =============== Singleton Access ===============
LogicEntity& LogicEntity::GetInstance() {
    std::lock_guard<std::mutex> lock(instanceMutex_);
    if (!instance_) {
        instance_ = new LogicEntity();
    }
    return *instance_;
}

LogicEntity::LogicEntity()
    : mobSystem_(MobSystem::GetInstance()),
      entityManager_(EntityManager::GetInstance()) {
    Logger::Trace("LogicEntity created");
}

LogicEntity::~LogicEntity() {
    Shutdown();
}

void LogicEntity::Initialize() {
    InitializeCollisionSystem();
    InitializeNPCSystem();
    InitializeMobSystem();
    //inventorySystem_ = std::make_unique<InventorySystem>();
    //lootTableManager_ = std::make_unique<LootTableManager>();
    //lootTableManager_->LoadLootTables();
    //LootTableManager::GetInstance().LoadLootTables();
    Logger::Info("LogicEntity initialized");
}

void LogicEntity::Shutdown() {
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
        static const nlohmann::json mobConfig = config.GetJson("mobs");
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
    if (!collisionSystem_) {
        Logger::Error("Failed to register collision system for NPC {}", npcId);
    } else {
        BoundingSphere bounds{position, 1.0f};
        collisionSystem_->RegisterEntity(npcId, bounds, CollisionType::ENTITY);
    }
    //Logger::Trace("Spawned NPC {} at [{:.1f}, {:.1f}, {:.1f}]",
    //              npcId, position.x, position.y, position.z);
    return npcId;
}

void LogicEntity::DespawnNPC(uint64_t npcId) {
    std::lock_guard<std::mutex> lock(npcMutex_);
    if (!npcManager_->GetNPC(npcId)) return;
    if (collisionSystem_) {
        collisionSystem_->UnregisterEntity(npcId);
    }
    npcManager_->DespawnNPC(npcId);
    //Logger::Trace("Despawned NPC {}", npcId);
}

void LogicEntity::UpdateNPCs(float deltaTime) {
    std::lock_guard<std::mutex> lock(npcMutex_);
    if (!npcManager_) return;
    npcManager_->Update(deltaTime);
    for (auto& [npcId, npc] : npcManager_->GetAllNPCs()) {
        if (npc && collisionSystem_) {
            collisionSystem_->UpdateEntity(npcId, npc->GetPosition());
        }
    }
    mobSystem_.UpdateSpawnZones(deltaTime);
    mobSystem_.ProcessRespawns(deltaTime);
}

NPCEntity* LogicEntity::GetNPCEntity(uint64_t npcId) {
    std::lock_guard<std::mutex> lock(npcMutex_);
    return npcManager_->GetNPC(npcId);
}

GameEntity* LogicEntity::GetEntity(uint64_t entityId) {
    return entityManager_.GetEntity(entityId);
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
        //collisionSystem_->UpdateBroadPhase();
        collisionSystem_->Update(deltaTime);
    }
}

void LogicEntity::CreateLootEntity(const glm::vec3& position, std::shared_ptr<LootItem> item, int quantity) {
    uint64_t entityId = entityManager_.CreateEntity(EntityType::ITEM, position);
    if (collisionSystem_) {
        BoundingSphere bounds{position, 0.5f};
        collisionSystem_->RegisterEntity(entityId, bounds, CollisionType::TRIGGER);
    }
    Logger::Trace("Created loot entity {}: {} x{}", entityId, item->GetName(), quantity);
}
