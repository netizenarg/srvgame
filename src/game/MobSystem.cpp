#include "game/MobSystem.hpp"
#include "game/GameLogic.hpp"

MobSystem& MobSystem::GetInstance() {
    static MobSystem instance;
    return instance;
}

void MobSystem::Initialize() {
    rng_.seed(std::random_device()());
    InitializeDefaultLootTables();
    InitializeDefaultVariants();
    Logger::Info("MobSystem initialized");
}

void MobSystem::Shutdown() {
    spawnZones_.clear();
    zoneMobs_.clear();
    pendingRespawns_.clear();
    mobToZone_.clear();
    recentDeaths_.clear();
    Logger::Info("MobSystem shut down");
}

std::string MobSystem::GetLootTableIdForMob(NPCType type, const std::string& zoneName) const {
    // Check zone-specific loot table first
    if (!zoneName.empty()) {
        auto zoneIt = zoneLootTables_.find(zoneName);
        if (zoneIt != zoneLootTables_.end()) {
            return zoneIt->second;
        }
    }

    // Check mob-type specific loot table
    auto mobIt = mobLootTables_.find(type);
    if (mobIt != mobLootTables_.end()) {
        return mobIt->second;
    }

    // Default loot table based on mob type
    switch (type) {
        case NPCType::GOBLIN: return "goblin_loot";
        case NPCType::ORC: return "orc_loot";
        case NPCType::DRAGON: return "dragon_loot";
        case NPCType::SLIME: return "slime_loot";
        default: return "default_loot";
    }
}

std::vector<std::pair<std::shared_ptr<LootItem>, int>> MobSystem::GenerateLoot(NPCType type, int level, uint64_t killerId) const {
    std::vector<std::pair<std::shared_ptr<LootItem>, int>> loot;

    // Get loot table ID for this mob
    std::string lootTableId = GetLootTableIdForMob(type);

    // Get player level for loot generation
    int playerLevel = 1;
    if (killerId != 0) {
        // TODO: Get player level from player system
        // For now, use mob level
        playerLevel = level;
    }

    // Use LootTableManager to generate loot
    auto& lootTableManager = LootTableManager::GetInstance();
    loot = lootTableManager.GenerateLoot(lootTableId, playerLevel, 1.0f);

    return loot;
}

void MobSystem::DropLoot(const MobDeathInfo& deathInfo) {
    // Generate loot
    auto lootItems = GenerateLoot(deathInfo.mobType, deathInfo.level, deathInfo.killerId);

    if (lootItems.empty()) {
        return;
    }

    // Create loot entities in world
    for (const auto& [item, quantity] : lootItems) {
        // Add slight random offset to spread loot
        glm::vec3 lootPos = deathInfo.deathPosition;
        std::uniform_real_distribution<float> offsetDist(-1.0f, 1.0f);
        lootPos.x += offsetDist(rng_);
        lootPos.z += offsetDist(rng_);

        // Create loot entity
        CreateLootEntity(lootPos, item, quantity);
    }

    // Fire Python event for loot drop
    nlohmann::json lootEvent = {
        {"type", "mob_loot_drop"},
        {"mobId", deathInfo.mobId},
        {"mobType", static_cast<int>(deathInfo.mobType)},
        {"level", deathInfo.level},
        {"killerId", deathInfo.killerId},
        {"position", {deathInfo.deathPosition.x, deathInfo.deathPosition.y, deathInfo.deathPosition.z}},
        {"lootCount", lootItems.size()},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };

    auto& gameLogic = GameLogic::GetInstance();
    gameLogic.FirePythonEvent("mob_loot_drop", lootEvent);

    Logger::Debug("Dropped {} loot items from mob {}", lootItems.size(), deathInfo.mobId);
}

void MobSystem::CreateLootEntity(const glm::vec3& position, std::shared_ptr<LootItem> item, int quantity) {
    // This should create a physical loot entity in the world
    // For now, we'll just log it
    Logger::Debug("Loot entity created at [{:.1f}, {:.1f}, {:.1f}]: {} x{}",
                  position.x, position.y, position.z,
                  item->GetName(), quantity);

    // TODO: Implement actual loot entity creation
    // This would use EntityManager to create an ITEM entity
    // with the loot data attached
}

void MobSystem::OnMobDeath(uint64_t mobId, uint64_t killerId) {
    NPCEntity* mob = GetMob(mobId);
    if (!mob) {
        Logger::Warn("MobSystem::OnMobDeath: mob {} not found", mobId);
        return;
    }

    // Determine zone for loot table
    std::string zoneName;
    auto zoneIt = mobToZone_.find(mobId);
    if (zoneIt != mobToZone_.end()) {
        zoneName = zoneIt->second;
    }

    MobDeathInfo deathInfo;
    deathInfo.mobId = mobId;
    deathInfo.killerId = killerId;
    deathInfo.mobType = mob->GetNPCType();
    deathInfo.deathPosition = mob->GetPosition();
    deathInfo.deathTime = std::chrono::steady_clock::now();
    // TODO: Get level from mob (needs to be stored in NPCEntity)
    deathInfo.level = 1;
    deathInfo.lootTableId = GetLootTableIdForMob(mob->GetNPCType(), zoneName);

    // Award experience
    float experience = GetExperienceReward(deathInfo.mobType, deathInfo.level);
    if (killerId != 0) {
        AwardExperience(killerId, experience);
    }

    // Drop loot
    DropLoot(deathInfo);

    // Store death info for respawn
    recentDeaths_[mobId] = deathInfo;

    // Check if mob belongs to a spawn zone
    if (!zoneName.empty()) {
        auto zoneIt2 = spawnZones_.find(zoneName);
        if (zoneIt2 != spawnZones_.end()) {
            const MobSpawnZone& zone = zoneIt2->second;

            // Schedule respawn
            PendingRespawn respawn;
            respawn.zoneName = zoneName;
            respawn.respawnTime = deathInfo.deathTime + std::chrono::seconds(static_cast<int>(zone.respawnTime));
            respawn.mobType = zone.mobType;
            respawn.level = deathInfo.level;
            pendingRespawns_.push_back(respawn);
        }
    }

    // Despawn the mob
    DespawnMob(mobId);

    // Fire Python events
    auto& gameLogic = GameLogic::GetInstance();
    gameLogic.FirePythonEvent("mob_death", {
        {"mobId", mobId},
        {"killerId", killerId},
        {"mobType", static_cast<int>(mob->GetNPCType())},
        {"level", deathInfo.level},
        {"experience", experience},
        {"deathPosition", {
            deathInfo.deathPosition.x,
            deathInfo.deathPosition.y,
            deathInfo.deathPosition.z
        }}
    });

    Logger::Info("Mob {} killed by player {}", mobId, killerId);
}

void MobSystem::InitializeDefaultLootTables() {
    // Set default loot tables for mob types
    SetMobLootTable(NPCType::GOBLIN, "goblin_loot");
    SetMobLootTable(NPCType::ORC, "orc_loot");
    SetMobLootTable(NPCType::DRAGON, "dragon_loot");
    SetMobLootTable(NPCType::SLIME, "slime_loot");
}

void MobSystem::SetMobLootTable(NPCType type, const std::string& lootTableId) {
    mobLootTables_[type] = lootTableId;
}

void MobSystem::SetZoneLootTable(const std::string& zoneName, const std::string& lootTableId) {
    zoneLootTables_[zoneName] = lootTableId;
}

void MobSystem::LoadMobConfig(const nlohmann::json& config) {
    if (config.contains("spawnZones")) {
        for (const auto& zoneData : config["spawnZones"]) {
            MobSpawnZone zone;
            zone.name = zoneData.value("name", "");
            zone.center.x = zoneData["center"][0];
            zone.center.y = zoneData["center"][1];
            zone.center.z = zoneData["center"][2];
            zone.radius = zoneData.value("radius", 50.0f);
            zone.mobType = static_cast<NPCType>(zoneData.value("mobType", 0));
            zone.minLevel = zoneData.value("minLevel", 1);
            zone.maxLevel = zoneData.value("maxLevel", 10);
            zone.maxMobs = zoneData.value("maxMobs", 10);
            zone.respawnTime = zoneData.value("respawnTime", 30.0f);
            zone.lootTableId = zoneData.value("lootTableId", "");

            RegisterSpawnZone(zone);

            // Set zone loot table if specified
            if (!zone.lootTableId.empty()) {
                SetZoneLootTable(zone.name, zone.lootTableId);
            }
        }
    }

    if (config.contains("mobLootTables")) {
        for (const auto& [typeStr, tableId] : config["mobLootTables"].items()) {
            NPCType type = static_cast<NPCType>(std::stoi(typeStr));
            SetMobLootTable(type, tableId.get<std::string>());
        }
    }
}

// ==================== Missing Function Implementations ====================

void MobSystem::InitializeDefaultVariants() {
    // Register default mob variants based on NPC type and level
    // For now, we register a few common variants
    Logger::Debug("MobSystem::InitializeDefaultVariants() called");

    // Example: Goblin variants for levels 1-5
    for (int level = 1; level <= 5; ++level) {
        MobVariant v;
        v.baseType = NPCType::GOBLIN;
        v.level = level;
        v.healthMultiplier = 1.0f + (level * 0.1f);
        v.damageMultiplier = 1.0f + (level * 0.1f);
        v.experienceReward = 10.0f * level;
        v.lootTableId = "goblin_loot";
        RegisterMobVariant(v);
    }

    // Orc variants
    for (int level = 5; level <= 10; ++level) {
        MobVariant v;
        v.baseType = NPCType::ORC;
        v.level = level;
        v.healthMultiplier = 1.2f + (level * 0.1f);
        v.damageMultiplier = 1.1f + (level * 0.1f);
        v.experienceReward = 15.0f * level;
        v.lootTableId = "orc_loot";
        RegisterMobVariant(v);
    }

    // Dragon (boss) variants
    MobVariant dragon;
    dragon.baseType = NPCType::DRAGON;
    dragon.level = 20;
    dragon.healthMultiplier = 5.0f;
    dragon.damageMultiplier = 3.0f;
    dragon.experienceReward = 500.0f;
    dragon.lootTableId = "dragon_loot";
    RegisterMobVariant(dragon);
}

void MobSystem::UpdateSpawnZones(float deltaTime) {
    (void)deltaTime;
    // This function should check each spawn zone and spawn mobs if needed
    // For now, we'll implement a simple timer-based spawning
    for (auto& [zoneName, zone] : spawnZones_) {
        // Check if we need to spawn more mobs
        auto& mobIds = zoneMobs_[zoneName];
        if (static_cast<int>(mobIds.size()) < zone.maxMobs) {
            // Check last spawn time
            auto now = std::chrono::steady_clock::now();
            auto& lastSpawn = zoneLastSpawn_[zoneName];
            if (std::chrono::duration_cast<std::chrono::seconds>(now - lastSpawn).count() >= zone.respawnTime) {
                // Spawn a new mob
                uint64_t mobId = SpawnMobInZone(zoneName);
                if (mobId != 0) {
                    mobIds.push_back(mobId);
                    mobToZone_[mobId] = zoneName;
                    lastSpawn = now;
                }
            }
        }
    }
}

void MobSystem::ProcessRespawns(float deltaTime) {
    (void)deltaTime;
    // Check pending respawns and spawn mobs when time is reached
    auto now = std::chrono::steady_clock::now();
    for (auto it = pendingRespawns_.begin(); it != pendingRespawns_.end(); ) {
        if (now >= it->respawnTime) {
            // Spawn the mob in its zone
            uint64_t mobId = SpawnMob(it->mobType, GetRandomSpawnPosition(spawnZones_[it->zoneName]), it->level);
            if (mobId != 0) {
                zoneMobs_[it->zoneName].push_back(mobId);
                mobToZone_[mobId] = it->zoneName;
            }
            it = pendingRespawns_.erase(it);
        } else {
            ++it;
        }
    }
}

uint64_t MobSystem::SpawnMob(NPCType type, const glm::vec3& position, int level) {
    // TODO: Create and register a new NPCEntity using EntityManager
    Logger::Warn("MobSystem::SpawnMob not fully implemented – returning mock ID");
    // Simulate a new mob ID (in real implementation, use EntityManager to create an entity)
    static uint64_t nextId = 1000;
    uint64_t newId = ++nextId;
    Logger::Debug("Spawned mob {} of type {} at ({:.1f},{:.1f},{:.1f}) level {}",
                  newId, static_cast<int>(type), position.x, position.y, position.z, level);
    return newId;
}

uint64_t MobSystem::SpawnMobInZone(const std::string& zoneName) {
    auto it = spawnZones_.find(zoneName);
    if (it == spawnZones_.end()) {
        Logger::Error("MobSystem::SpawnMobInZone: zone '{}' not found", zoneName);
        return 0;
    }
    const MobSpawnZone& zone = it->second;
    glm::vec3 spawnPos = GetRandomSpawnPosition(zone);
    int level = CalculateMobLevel(spawnPos);
    return SpawnMob(zone.mobType, spawnPos, level);
}

void MobSystem::DespawnMob(uint64_t mobId) {
    // Remove from zone tracking and entity manager
    auto zoneIt = mobToZone_.find(mobId);
    if (zoneIt != mobToZone_.end()) {
        auto& mobIds = zoneMobs_[zoneIt->second];
        mobIds.erase(std::remove(mobIds.begin(), mobIds.end(), mobId), mobIds.end());
        mobToZone_.erase(zoneIt);
    }
    // TODO: Also remove from EntityManager
    Logger::Debug("Despawned mob {}", mobId);
}

void MobSystem::RegisterSpawnZone(const MobSpawnZone& zone) {
    spawnZones_[zone.name] = zone;
    zoneMobs_[zone.name] = {}; // initialize empty mob list
    zoneLastSpawn_[zone.name] = std::chrono::steady_clock::now(); // set last spawn to now
    Logger::Debug("Registered spawn zone '{}'", zone.name);
}

void MobSystem::UnregisterSpawnZone(const std::string& zoneName) {
    auto it = spawnZones_.find(zoneName);
    if (it != spawnZones_.end()) {
        // Despawn all mobs in this zone
        for (uint64_t mobId : zoneMobs_[zoneName]) {
            DespawnMob(mobId);
        }
        spawnZones_.erase(it);
        zoneMobs_.erase(zoneName);
        zoneLastSpawn_.erase(zoneName);
        Logger::Debug("Unregistered spawn zone '{}'", zoneName);
    }
}

void MobSystem::RegisterMobVariant(const MobVariant& variant) {
    std::string key = GetVariantKey(variant.baseType, variant.level);
    mobVariants_[key] = variant;
    Logger::Debug("Registered mob variant: {} level {}", static_cast<int>(variant.baseType), variant.level);
}

MobVariant MobSystem::GetMobVariant(NPCType type, int level) const {
    std::string key = GetVariantKey(type, level);
    auto it = mobVariants_.find(key);
    if (it != mobVariants_.end()) {
        return it->second;
    }
    // Return default variant (fallback)
    MobVariant v;
    v.baseType = type;
    v.level = level;
    v.healthMultiplier = 1.0f + (level * 0.1f);
    v.damageMultiplier = 1.0f + (level * 0.1f);
    v.experienceReward = 10.0f * level;
    v.lootTableId = GetLootTableIdForMob(type);
    return v;
}

float MobSystem::GetExperienceReward(NPCType type, int level) const {
    // Use variant if available, otherwise calculate simple formula
    auto variant = GetMobVariant(type, level);
    return variant.experienceReward;
}

void MobSystem::AwardExperience(uint64_t playerId, float experience) {
    // TODO: Add experience to player using PlayerManager
    Logger::Debug("Award {} experience to player {}", experience, playerId);
}

std::vector<uint64_t> MobSystem::GetMobsInRadius(const glm::vec3& position, float radius) const {
    (void)position;
    // TODO: Query entity manager for nearby mobs
    // For now, return empty vector
    Logger::Warn("MobSystem::GetMobsInRadius not implemented {}", radius);
    return {};
}

NPCEntity* MobSystem::GetMob(uint64_t mobId) const {
    // TODO: Retrieve from entity manager
    // For now, return nullptr
    Logger::Warn("MobSystem::GetMob not implemented {}", mobId);
    return nullptr;
}

bool MobSystem::IsHostileMob(NPCType type) const {
    // Define hostility based on type
    switch (type) {
        case NPCType::GOBLIN:
        case NPCType::ORC:
        case NPCType::TROLL:
        case NPCType::OGRE:
        case NPCType::SKELETON:
        case NPCType::ZOMBIE:
        case NPCType::GHOST:
        case NPCType::VAMPIRE:
        case NPCType::WEREWOLF:
        case NPCType::DRAGON:
        case NPCType::SLIME:
        case NPCType::SPIDER:
        case NPCType::BAT:
        case NPCType::RAT:
        case NPCType::WOLF:
        case NPCType::BEAR:
        case NPCType::BOAR:
        case NPCType::DRAGON_LORD:
        case NPCType::LICH_KING:
        case NPCType::DEMON_LORD:
        case NPCType::ANCIENT_TREANT:
        case NPCType::SEA_SERPENT:
        case NPCType::PHOENIX:
        case NPCType::GOLEM:
        case NPCType::HYDRA:
        case NPCType::ELITE:
        case NPCType::RARE:
        case NPCType::LEGENDARY:
        case NPCType::WORLD_BOSS:
            return true;
        default:
            return false;
    }
}

std::string MobSystem::GetVariantKey(NPCType type, int level) const {
    return std::to_string(static_cast<int>(type)) + "_" + std::to_string(level);
}

int MobSystem::CalculateMobLevel(const glm::vec3& position) const {
    (void)position;
    // TODO: Determine level based on zone difficulty, distance from spawn, etc.
    // For now, return a random level between 1 and 10
    std::uniform_int_distribution<int> dist(1, 10);
    return dist(rng_);
}

glm::vec3 MobSystem::GetRandomSpawnPosition(const MobSpawnZone& zone) const {
    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * 3.14159f);
    std::uniform_real_distribution<float> radiusDist(0.0f, zone.radius);
    float angle = angleDist(rng_);
    float r = radiusDist(rng_);
    return zone.center + glm::vec3(r * cos(angle), 0.0f, r * sin(angle));
}
