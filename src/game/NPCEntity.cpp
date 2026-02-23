#include "game/NPCEntity.hpp"

// =============== NPCStats Serialization ===============
nlohmann::json NPCStats::Serialize() const {
    return {
        {"level", level},
        {"health", health},
        {"max_health", max_health},
        {"mana", mana},
        {"max_mana", max_mana},
        {"stamina", stamina},
        {"max_stamina", max_stamina},

        {"attack_damage", attack_damage},
        {"attack_speed", attack_speed},
        {"attack_range", attack_range},
        {"critical_chance", critical_chance},
        {"critical_damage", critical_damage},

        {"defense", defense},
        {"magic_resist", magic_resist},
        {"physical_resist", physical_resist},
        {"dodge_chance", dodge_chance},
        {"block_chance", block_chance},
        {"parry_chance", parry_chance},

        {"move_speed", move_speed},
        {"chase_speed", chase_speed},
        {"flee_speed", flee_speed},

        {"sight_range", sight_range},
        {"chase_range", chase_range},
        {"attack_range_min", attack_range_min},
        {"attack_range_max", attack_range_max},

        {"respawn_time", respawn_time},
        {"min_gold", min_gold},
        {"max_gold", max_gold},
        {"experience_reward", experience_reward}
    };
}

void NPCStats::Deserialize(const nlohmann::json& data) {
    level = data.value("level", 1);
    health = data.value("health", 100.0f);
    max_health = data.value("max_health", 100.0f);
    mana = data.value("mana", 50.0f);
    max_mana = data.value("max_mana", 50.0f);
    stamina = data.value("stamina", 100.0f);
    max_stamina = data.value("max_stamina", 100.0f);

    attack_damage = data.value("attack_damage", 10.0f);
    attack_speed = data.value("attack_speed", 1.0f);
    attack_range = data.value("attack_range", 2.0f);
    critical_chance = data.value("critical_chance", 0.05f);
    critical_damage = data.value("critical_damage", 1.5f);

    defense = data.value("defense", 5.0f);
    magic_resist = data.value("magic_resist", 0.0f);
    physical_resist = data.value("physical_resist", 0.0f);
    dodge_chance = data.value("dodge_chance", 0.02f);
    block_chance = data.value("block_chance", 0.03f);
    parry_chance = data.value("parry_chance", 0.01f);

    move_speed = data.value("move_speed", 3.0f);
    chase_speed = data.value("chase_speed", 4.0f);
    flee_speed = data.value("flee_speed", 5.0f);

    sight_range = data.value("sight_range", 20.0f);
    chase_range = data.value("chase_range", 30.0f);
    attack_range_min = data.value("attack_range_min", 1.0f);
    attack_range_max = data.value("attack_range_max", 5.0f);

    respawn_time = data.value("respawn_time", 30.0f);
    min_gold = data.value("min_gold", 1);
    max_gold = data.value("max_gold", 10);
    experience_reward = data.value("experience_reward", 10);
}

// =============== NPCAIProfile Serialization ===============
nlohmann::json NPCAIProfile::Serialize() const {
    nlohmann::json json;

    json["is_aggressive"] = is_aggressive;
    json["is_passive"] = is_passive;
    json["is_friendly"] = is_friendly;
    json["is_hostile"] = is_hostile;
    json["is_neutral"] = is_neutral;

    json["idle_time_min"] = idle_time_min;
    json["idle_time_max"] = idle_time_max;
    json["patrol_speed"] = patrol_speed;
    json["chase_speed_multiplier"] = chase_speed_multiplier;
    json["flee_health_threshold"] = flee_health_threshold;

    json["can_summon_allies"] = can_summon_allies;
    json["max_allies"] = max_allies;
    json["summon_cooldown"] = summon_cooldown;

    json["can_flee"] = can_flee;
    json["can_call_for_help"] = can_call_for_help;
    json["respawns"] = respawns;
    json["drops_loot"] = drops_loot;

    // Serialize patrol points
    nlohmann::json points_json = nlohmann::json::array();
    for (const auto& point : patrol_points) {
        points_json.push_back({point.x, point.y, point.z});
    }
    json["patrol_points"] = points_json;

    json["patrol_radius"] = patrol_radius;
    json["patrol_loop"] = patrol_loop;
    json["patrol_random"] = patrol_random;

    return json;
}

void NPCAIProfile::Deserialize(const nlohmann::json& data) {
    is_aggressive = data.value("is_aggressive", false);
    is_passive = data.value("is_passive", true);
    is_friendly = data.value("is_friendly", false);
    is_hostile = data.value("is_hostile", false);
    is_neutral = data.value("is_neutral", true);

    idle_time_min = data.value("idle_time_min", 3.0f);
    idle_time_max = data.value("idle_time_max", 10.0f);
    patrol_speed = data.value("patrol_speed", 2.0f);
    chase_speed_multiplier = data.value("chase_speed_multiplier", 1.5f);
    flee_health_threshold = data.value("flee_health_threshold", 0.3f);

    can_summon_allies = data.value("can_summon_allies", false);
    max_allies = data.value("max_allies", 0);
    summon_cooldown = data.value("summon_cooldown", 30.0f);

    can_flee = data.value("can_flee", false);
    can_call_for_help = data.value("can_call_for_help", true);
    respawns = data.value("respawns", true);
    drops_loot = data.value("drops_loot", true);

    // Deserialize patrol points
    patrol_points.clear();
    if (data.contains("patrol_points") && data["patrol_points"].is_array()) {
        for (const auto& point_json : data["patrol_points"]) {
            if (point_json.is_array() && point_json.size() >= 3) {
                glm::vec3 point;
                point.x = point_json[0];
                point.y = point_json[1];
                point.z = point_json[2];
                patrol_points.push_back(point);
            }
        }
    }

    patrol_radius = data.value("patrol_radius", 10.0f);
    patrol_loop = data.value("patrol_loop", true);
    patrol_random = data.value("patrol_random", false);
}

// =============== NPCLootTable Serialization ===============
nlohmann::json NPCLootTable::Serialize() const {
    nlohmann::json json;

    json["table_id"] = table_id;
    json["drop_chance"] = drop_chance;
    json["min_items"] = min_items;
    json["max_items"] = max_items;

    // Serialize item drop rates
    nlohmann::json items_json;
    for (const auto& [item_id, rate] : item_drop_rates) {
        items_json[item_id] = rate;
    }
    json["item_drop_rates"] = items_json;

    return json;
}

void NPCLootTable::Deserialize(const nlohmann::json& data) {
    table_id = data.value("table_id", "");
    drop_chance = data.value("drop_chance", 1.0f);
    min_items = data.value("min_items", 1);
    max_items = data.value("max_items", 3);

    // Deserialize item drop rates
    item_drop_rates.clear();
    if (data.contains("item_drop_rates")) {
        for (const auto& [item_id, rate] : data["item_drop_rates"].items()) {
            item_drop_rates[item_id] = rate.get<float>();
        }
    }
}

// =============== NPCDialogue Serialization ===============
nlohmann::json NPCDialogue::Serialize() const {
    nlohmann::json json;

    json["greeting"] = greeting;

    // Serialize topics
    json["topics"] = topics;

    // Serialize responses
    nlohmann::json responses_json;
    for (const auto& [topic, response] : responses) {
        responses_json[topic] = response;
    }
    json["responses"] = responses_json;

    // Serialize quest dialogues
    json["quest_dialogues"] = quest_dialogues;

    // Serialize trade dialogues
    json["trade_dialogues"] = trade_dialogues;

    return json;
}

void NPCDialogue::Deserialize(const nlohmann::json& data) {
    greeting = data.value("greeting", "");

    // Deserialize topics
    topics.clear();
    if (data.contains("topics") && data["topics"].is_array()) {
        topics = data["topics"].get<std::vector<std::string>>();
    }

    // Deserialize responses
    responses.clear();
    if (data.contains("responses")) {
        for (const auto& [topic, response] : data["responses"].items()) {
            responses[topic] = response.get<std::string>();
        }
    }

    // Deserialize quest dialogues
    quest_dialogues.clear();
    if (data.contains("quest_dialogues")) {
        quest_dialogues = data["quest_dialogues"].get<std::unordered_map<std::string, nlohmann::json>>();
    }

    // Deserialize trade dialogues
    trade_dialogues.clear();
    if (data.contains("trade_dialogues")) {
        trade_dialogues = data["trade_dialogues"].get<std::unordered_map<std::string, nlohmann::json>>();
    }
}

// =============== NPCEntity Implementation ===============
NPCEntity::NPCEntity(NPCType type, const glm::vec3& position, int level)
    : GameEntity(EntityType::NPC, position),
      npc_type_(type),
      rarity_(NPCRarity::COMMON),
      faction_(NPCFaction::NEUTRAL),
      ai_state_(NPCAIState::IDLE),
      spawn_position_(position) {

    // Set name based on type
    name_ = GetNPCTypeString() + "_" + std::to_string(GetId());
    category_ = "npc";

    // Set default stats based on type and level
    npc_stats_.level = level;
    CalculateStats();

    // Set default AI profile based on type
    SetDefaultAIProfile();

    // Set default loot table
    loot_table_.table_id = "default_" + GetNPCTypeString();

    // Set spawn position
    spawn_position_ = position;

    Logger::Debug("NPCEntity created: {} (ID: {}) at [{:.1f}, {:.1f}, {:.1f}]",
                  name_, GetId(), position.x, position.y, position.z);
}

NPCEntity::~NPCEntity() {
    Logger::Debug("NPCEntity destroyed: {} (ID: {})", name_, GetId());
}

// =============== NPC Type Management ===============
void NPCEntity::SetNPCType(NPCType type) {
    npc_type_ = type;
    CalculateStats();
    SetDefaultAIProfile();
    name_ = GetNPCTypeString() + "_" + std::to_string(GetId());
}

std::string NPCEntity::GetNPCTypeString() const {
    switch (npc_type_) {
        case NPCType::VILLAGER: return "Villager";
        case NPCType::GUARD: return "Guard";
        case NPCType::MERCHANT: return "Merchant";
        case NPCType::BLACKSMITH: return "Blacksmith";
        case NPCType::ALCHEMIST: return "Alchemist";
        case NPCType::INNKEEPER: return "Innkeeper";
        case NPCType::QUEST_GIVER: return "QuestGiver";
        case NPCType::TRAINER: return "Trainer";
        case NPCType::BANKER: return "Banker";
        case NPCType::GOBLIN: return "Goblin";
        case NPCType::ORC: return "Orc";
        case NPCType::TROLL: return "Troll";
        case NPCType::OGRE: return "Ogre";
        case NPCType::SKELETON: return "Skeleton";
        case NPCType::ZOMBIE: return "Zombie";
        case NPCType::GHOST: return "Ghost";
        case NPCType::VAMPIRE: return "Vampire";
        case NPCType::WEREWOLF: return "Werewolf";
        case NPCType::DRAGON: return "Dragon";
        case NPCType::SLIME: return "Slime";
        case NPCType::SPIDER: return "Spider";
        case NPCType::BAT: return "Bat";
        case NPCType::RAT: return "Rat";
        case NPCType::WOLF: return "Wolf";
        case NPCType::BEAR: return "Bear";
        case NPCType::BOAR: return "Boar";
        case NPCType::DRAGON_LORD: return "DragonLord";
        case NPCType::LICH_KING: return "LichKing";
        case NPCType::DEMON_LORD: return "DemonLord";
        case NPCType::ANCIENT_TREANT: return "AncientTreant";
        case NPCType::SEA_SERPENT: return "SeaSerpent";
        case NPCType::PHOENIX: return "Phoenix";
        case NPCType::GOLEM: return "Golem";
        case NPCType::HYDRA: return "Hydra";
        case NPCType::PET: return "Pet";
        case NPCType::SUMMON: return "Summon";
        case NPCType::MINION: return "Minion";
        case NPCType::ELITE: return "Elite";
        case NPCType::RARE: return "Rare";
        case NPCType::LEGENDARY: return "Legendary";
        case NPCType::WORLD_BOSS: return "WorldBoss";
        default: return "Unknown";
    }
}

std::string NPCEntity::GetRarityString() const {
    switch (rarity_) {
        case NPCRarity::COMMON: return "Common";
        case NPCRarity::UNCOMMON: return "Uncommon";
        case NPCRarity::RARE: return "Rare";
        case NPCRarity::EPIC: return "Epic";
        case NPCRarity::LEGENDARY: return "Legendary";
        case NPCRarity::MYTHIC: return "Mythic";
        case NPCRarity::UNIQUE: return "Unique";
        default: return "Unknown";
    }
}

std::string NPCEntity::GetFactionString() const {
    switch (faction_) {
        case NPCFaction::NEUTRAL: return "Neutral";
        case NPCFaction::FRIENDLY: return "Friendly";
        case NPCFaction::HOSTILE: return "Hostile";
        case NPCFaction::ALLIED: return "Allied";
        case NPCFaction::ENEMY: return "Enemy";
        case NPCFaction::BEAST: return "Beast";
        case NPCFaction::UNDEAD: return "Undead";
        case NPCFaction::DEMONIC: return "Demonic";
        case NPCFaction::ELEMENTAL: return "Elemental";
        case NPCFaction::MECHANICAL: return "Mechanical";
        default: return "Unknown";
    }
}

std::string NPCEntity::GetAIStateString() const {
    return AIStateToString(ai_state_);
}

std::string NPCEntity::AIStateToString(NPCAIState state) const {
    switch (state) {
        case NPCAIState::IDLE:        return "Idle";
        case NPCAIState::PATROL:      return "Patrol";
        case NPCAIState::FOLLOW:      return "Follow";
        case NPCAIState::CHASE:       return "Chase";
        case NPCAIState::ATTACK:      return "Attack";
        case NPCAIState::FLEE:        return "Flee";
        case NPCAIState::DEAD:        return "Dead";
        case NPCAIState::SPAWNING:    return "Spawning";
        case NPCAIState::DESPAWNING:  return "Despawning";
        case NPCAIState::TALKING:     return "Talking";
        case NPCAIState::TRADING:     return "Trading";
        case NPCAIState::CRAFTING:    return "Crafting";
        case NPCAIState::SLEEPING:    return "Sleeping";
        case NPCAIState::EATING:      return "Eating";
        case NPCAIState::WORKING:     return "Working";
        case NPCAIState::GUARDING:    return "Guarding";
        case NPCAIState::WANDER:      return "Wander";
        default:                      return "Unknown";
    }
}

// =============== Stats Management ===============
void NPCEntity::SetNPCStats(const NPCStats& stats) {
    npc_stats_ = stats;
    SetHealth(npc_stats_.health);
    SetMaxHealth(npc_stats_.max_health);
}

void NPCEntity::CalculateStats() {
    // Base stats based on level
    float level_multiplier = 1.0f + (npc_stats_.level - 1) * 0.1f;

    // Adjust stats based on NPC type
    switch (npc_type_) {
        case NPCType::GOBLIN:
            npc_stats_.max_health = 50.0f * level_multiplier;
            npc_stats_.attack_damage = 8.0f * level_multiplier;
            npc_stats_.defense = 2.0f * level_multiplier;
            npc_stats_.move_speed = 3.5f;
            break;

        case NPCType::ORC:
            npc_stats_.max_health = 80.0f * level_multiplier;
            npc_stats_.attack_damage = 12.0f * level_multiplier;
            npc_stats_.defense = 5.0f * level_multiplier;
            npc_stats_.move_speed = 3.0f;
            break;

        case NPCType::DRAGON:
            npc_stats_.max_health = 500.0f * level_multiplier;
            npc_stats_.attack_damage = 30.0f * level_multiplier;
            npc_stats_.defense = 15.0f * level_multiplier;
            npc_stats_.move_speed = 4.0f;
            break;

        case NPCType::VILLAGER:
            npc_stats_.max_health = 30.0f;
            npc_stats_.attack_damage = 1.0f;
            npc_stats_.defense = 0.0f;
            npc_stats_.move_speed = 2.5f;
            break;

        default:
            npc_stats_.max_health = 100.0f * level_multiplier;
            npc_stats_.attack_damage = 10.0f * level_multiplier;
            npc_stats_.defense = 5.0f * level_multiplier;
            npc_stats_.move_speed = 3.0f;
            break;
    }

    // Apply rarity modifiers
    ApplyRarityModifiers();

    // Apply faction modifiers
    ApplyFactionModifiers();

    // Set current health to max
    npc_stats_.health = npc_stats_.max_health;
    SetHealth(npc_stats_.health);
    SetMaxHealth(npc_stats_.max_health);
}

void NPCEntity::ApplyRarityModifiers() {
    switch (rarity_) {
        case NPCRarity::UNCOMMON:
            npc_stats_.max_health *= 1.2f;
            npc_stats_.attack_damage *= 1.2f;
            npc_stats_.experience_reward *= 2;
            break;

        case NPCRarity::RARE:
            npc_stats_.max_health *= 1.5f;
            npc_stats_.attack_damage *= 1.5f;
            npc_stats_.defense *= 1.2f;
            npc_stats_.experience_reward *= 3;
            break;

        case NPCRarity::EPIC:
            npc_stats_.max_health *= 2.0f;
            npc_stats_.attack_damage *= 2.0f;
            npc_stats_.defense *= 1.5f;
            npc_stats_.experience_reward *= 5;
            break;

        case NPCRarity::LEGENDARY:
            npc_stats_.max_health *= 3.0f;
            npc_stats_.attack_damage *= 3.0f;
            npc_stats_.defense *= 2.0f;
            npc_stats_.experience_reward *= 10;
            break;

        case NPCRarity::MYTHIC:
            npc_stats_.max_health *= 5.0f;
            npc_stats_.attack_damage *= 5.0f;
            npc_stats_.defense *= 3.0f;
            npc_stats_.experience_reward *= 20;
            break;

        case NPCRarity::UNIQUE:
            npc_stats_.max_health *= 10.0f;
            npc_stats_.attack_damage *= 10.0f;
            npc_stats_.defense *= 5.0f;
            npc_stats_.experience_reward *= 50;
            break;

        default:
            // Common - no modifiers
            break;
    }
}

void NPCEntity::ApplyFactionModifiers() {
    // Some factions have inherent bonuses
    switch (faction_) {
        case NPCFaction::UNDEAD:
            npc_stats_.magic_resist += 10.0f;
            npc_stats_.physical_resist += 5.0f;
            break;

        case NPCFaction::DEMONIC:
            npc_stats_.attack_damage *= 1.2f;
            npc_stats_.magic_resist += 5.0f;
            break;

        case NPCFaction::ELEMENTAL:
            npc_stats_.magic_resist += 15.0f;
            break;

        case NPCFaction::MECHANICAL:
            npc_stats_.defense *= 1.3f;
            npc_stats_.physical_resist += 10.0f;
            break;

        default:
            // No modifiers for other factions
            break;
    }
}

bool NPCEntity::IsBoss() const {
    return npc_type_ == NPCType::DRAGON_LORD ||
           npc_type_ == NPCType::LICH_KING ||
           npc_type_ == NPCType::DEMON_LORD ||
           npc_type_ == NPCType::ANCIENT_TREANT ||
           npc_type_ == NPCType::SEA_SERPENT ||
           npc_type_ == NPCType::PHOENIX ||
           npc_type_ == NPCType::GOLEM ||
           npc_type_ == NPCType::HYDRA ||
           npc_type_ == NPCType::WORLD_BOSS;
}

bool NPCEntity::IsElite() const {
    return npc_type_ == NPCType::ELITE ||
           rarity_ >= NPCRarity::RARE;
}

bool NPCEntity::IsRare() const {
    return npc_type_ == NPCType::RARE ||
           rarity_ >= NPCRarity::RARE;
}

// =============== AI Profile Management ===============
void NPCEntity::SetAIProfile(const NPCAIProfile& profile) {
    ai_profile_ = profile;

    // Initialize patrol queue
    patrol_queue_ = std::queue<glm::vec3>();
    for (const auto& point : ai_profile_.patrol_points) {
        patrol_queue_.push(point);
    }
}

void NPCEntity::SetDefaultAIProfile() {
    ai_profile_ = NPCAIProfile();

    // Set defaults based on NPC type
    switch (npc_type_) {
        case NPCType::GUARD:
            ai_profile_.is_neutral = false;
            ai_profile_.is_friendly = true;
            ai_profile_.patrol_radius = 15.0f;
            ai_profile_.can_call_for_help = true;
            break;

        case NPCType::GOBLIN:
        case NPCType::ORC:
        case NPCType::TROLL:
            ai_profile_.is_neutral = false;
            ai_profile_.is_hostile = true;
            ai_profile_.is_aggressive = true;
            ai_profile_.sight_range = 25.0f;
            ai_profile_.chase_range = 40.0f;
            ai_profile_.can_flee = true;
            ai_profile_.flee_health_threshold = 0.2f;
            break;

        case NPCType::DRAGON:
        case NPCType::DRAGON_LORD:
            ai_profile_.is_hostile = true;
            ai_profile_.is_aggressive = true;
            ai_profile_.sight_range = 50.0f;
            ai_profile_.chase_range = 100.0f;
            ai_profile_.can_summon_allies = true;
            ai_profile_.max_allies = 3;
            ai_profile_.can_flee = false; // Bosses don't flee
            break;

        case NPCType::VILLAGER:
        case NPCType::MERCHANT:
            ai_profile_.is_passive = true;
            ai_profile_.is_friendly = true;
            ai_profile_.patrol_radius = 5.0f;
            ai_profile_.patrol_random = true;
            break;

        default:
            // Use default settings
            break;
    }
}

// =============== AI State Management ===============
void NPCEntity::SetAIState(NPCAIState state) {
    if (ai_state_ == state) return;

    NPCAIState old_state = ai_state_;
    ai_state_ = state;

    // Reset state timer
    state_timer_ = 0.0f;

    // State entry logic
    switch (ai_state_) {
        case NPCAIState::IDLE:
            ChangeToIdle();
            break;
        case NPCAIState::PATROL:
            ChangeToPatrol();
            break;
        case NPCAIState::CHASE:
            if (target_id_ != 0) {
                ChangeToChase(target_id_);
            }
            break;
        case NPCAIState::ATTACK:
            if (target_id_ != 0) {
                ChangeToAttack(target_id_);
            }
            break;
        case NPCAIState::FLEE:
            ChangeToFlee();
            break;
        case NPCAIState::DEAD:
            ChangeToDead();
            break;
        default:
            break;
    }

    Logger::Debug("NPC {} changed AI state from {} to {}",
                  GetId(), AIStateToString(old_state), GetAIStateString());
}

void NPCEntity::ChangeToIdle() {
    Stop();
    idle_timer_ = 0.0f;

    // Set random idle time
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(
        ai_profile_.idle_time_min,
        ai_profile_.idle_time_max
    );
    state_timer_ = dis(gen);
}

void NPCEntity::ChangeToPatrol() {
    // If no patrol points, generate random patrol area
    if (ai_profile_.patrol_points.empty() && ai_profile_.patrol_radius > 0) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dis(-ai_profile_.patrol_radius, ai_profile_.patrol_radius);

        glm::vec3 patrol_point = spawn_position_;
        patrol_point.x += dis(gen);
        patrol_point.z += dis(gen);

        MoveTo(patrol_point, ai_profile_.patrol_speed);
    } else if (!ai_profile_.patrol_points.empty()) {
        // Move to first patrol point
        glm::vec3 target = GetNextPatrolPoint();
        MoveTo(target, ai_profile_.patrol_speed);
    }
}

void NPCEntity::ChangeToChase(uint64_t target_id) {
    target_id_ = target_id;
    // Chase logic handled in UpdateChase
}

void NPCEntity::ChangeToAttack(uint64_t target_id) {
    target_id_ = target_id;
    attack_cooldown_ = 0.0f;
}

void NPCEntity::ChangeToFlee() {
    flee_timer_ = 0.0f;

    // Calculate flee direction away from target
    if (target_id_ != 0) {
        // This would require getting target position from EntityManager
        // For now, just move in a random direction
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dis(-1.0f, 1.0f);

        glm::vec3 flee_dir = glm::vec3(dis(gen), 0.0f, dis(gen));
        if (glm::length(flee_dir) > 0.01f) {
            flee_dir = glm::normalize(flee_dir);
        }

        glm::vec3 flee_target = GetPosition() + flee_dir * 20.0f;
        MoveTo(flee_target, npc_stats_.flee_speed);
    }
}

void NPCEntity::ChangeToDead() {
    Stop();
    SetActive(false);
    SetVisible(false);
    SetCollidable(false);

    // Schedule despawn
    state_timer_ = DESPAWN_DELAY;
    ai_state_ = NPCAIState::DESPAWNING;
}

// =============== AI Update Methods ===============
void NPCEntity::Update(float delta_time) {
    GameEntity::Update(delta_time);

    // Update AI if active and not dead
    if (IsActive() && ai_state_ != NPCAIState::DEAD && ai_state_ != NPCAIState::DESPAWNING) {
        UpdateAI(delta_time);
    }

    // Update timers
    state_timer_ += delta_time;

    // Update attack cooldown
    if (attack_cooldown_ > 0.0f) {
        attack_cooldown_ -= delta_time;
    }

    // Update stun timer
    if (stun_timer_ > 0.0f) {
        stun_timer_ -= delta_time;
        if (stun_timer_ <= 0.0f && ai_state_ == NPCAIState::IDLE) {
            // Return to previous state after stun
            SetAIState(NPCAIState::IDLE);
        }
    }

    // Update summon cooldown
    if (summon_cooldown_ > 0.0f) {
        summon_cooldown_ -= delta_time;
    }
}

void NPCEntity::UpdateAI(float delta_time) {
    // Don't update AI if stunned
    if (stun_timer_ > 0.0f) {
        return;
    }

    // Update target selection
    UpdateTargetSelection();

    // Update current AI state
    switch (ai_state_) {
        case NPCAIState::IDLE:
            UpdateIdle(delta_time);
            break;
        case NPCAIState::PATROL:
            UpdatePatrol(delta_time);
            break;
        case NPCAIState::CHASE:
            UpdateChase(delta_time);
            break;
        case NPCAIState::ATTACK:
            UpdateAttack(delta_time);
            break;
        case NPCAIState::FLEE:
            UpdateFlee(delta_time);
            break;
        case NPCAIState::SPAWNING:
            UpdateSpawning(delta_time);
            break;
        case NPCAIState::DESPAWNING:
            UpdateDespawning(delta_time);
            break;
        default:
            // Other states don't need AI updates
            break;
    }
}

void NPCEntity::UpdateIdle(float delta_time) {
    idle_timer_ += delta_time;

    // Check for targets if aggressive
    if (ai_profile_.is_aggressive && HasTarget()) {
        SetAIState(NPCAIState::CHASE);
        return;
    }

    // Check if idle time is up
    if (idle_timer_ >= state_timer_) {
        // Transition to patrol or wander
        if (!ai_profile_.patrol_points.empty() || ai_profile_.patrol_radius > 0) {
            SetAIState(NPCAIState::PATROL);
        } else {
            // Reset idle timer
            ChangeToIdle();
        }
    }
}

void NPCEntity::UpdatePatrol(float delta_time) {
    // Check for targets if aggressive
    if (ai_profile_.is_aggressive && HasTarget()) {
        SetAIState(NPCAIState::CHASE);
        return;
    }

    if (waiting_at_patrol_point_) {
        // Decrease wait timer
        patrol_wait_timer_ -= delta_time;
        if (patrol_wait_timer_ <= 0.0f) {
            // Finished waiting, move to next point
            waiting_at_patrol_point_ = false;
            glm::vec3 next_point = GetNextPatrolPoint();
            MoveTo(next_point, ai_profile_.patrol_speed);
        }
        return;
    }

    // Check if reached patrol point
    if (!IsMoving()) {
        // Arrived at point – start waiting
        waiting_at_patrol_point_ = true;
        // Random wait duration (e.g., 2–5 seconds)
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> wait_dist(2.0f, 5.0f);
        patrol_wait_timer_ = wait_dist(gen);
    }
}

void NPCEntity::UpdateChase(float delta_time) {
    if (!HasTarget()) {
        SetAIState(NPCAIState::IDLE);
        return;
    }

    // Check if target is in attack range
    // This would require getting target position from EntityManager
    // For now, simulate with random chance
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);

    if (dis(gen) < 0.1f) { // 10% chance per frame to enter attack range
        SetAIState(NPCAIState::ATTACK);
        return;
    }

    // Check if target is out of chase range
    if (dis(gen) < 0.05f) { // 5% chance per frame to lose target
        target_id_ = 0;
        SetAIState(NPCAIState::IDLE);
        return;
    }

    // Move toward target (simulated)
    // In real implementation, you would get target position and move toward it
    glm::vec3 chase_dir = glm::vec3(1.0f, 0.0f, 1.0f); // Simplified
    chase_dir = glm::normalize(chase_dir);

    glm::vec3 velocity = chase_dir * npc_stats_.chase_speed * delta_time;
    Translate(velocity);
}

void NPCEntity::UpdateAttack(float delta_time) {
    (void)delta_time;
    if (!HasTarget()) {
        SetAIState(NPCAIState::IDLE);
        return;
    }

    // Check if should flee
    if (ai_profile_.can_flee &&
        GetHealth() / GetMaxHealth() <= ai_profile_.flee_health_threshold) {
        SetAIState(NPCAIState::FLEE);
        return;
    }

    // Check attack cooldown
    if (attack_cooldown_ <= 0.0f) {
        PerformAttack();
        attack_cooldown_ = ATTACK_COOLDOWN_BASE / npc_stats_.attack_speed;
    }

    // Check if should summon allies
    if (ai_profile_.can_summon_allies && summon_cooldown_ <= 0.0f) {
        SummonAllies();
        summon_cooldown_ = ai_profile_.summon_cooldown;
    }
}

void NPCEntity::UpdateFlee(float delta_time) {
    flee_timer_ += delta_time;

    // Check if flee time is up
    if (flee_timer_ >= FLEE_DURATION) {
        SetAIState(NPCAIState::IDLE);
        return;
    }

    // Check if still need to flee
    if (GetHealth() / GetMaxHealth() > ai_profile_.flee_health_threshold * 1.5f) {
        SetAIState(NPCAIState::IDLE);
        return;
    }
}

void NPCEntity::UpdateSpawning(float delta_time) {
    (void)delta_time;
    // For now, just transition to idle after a short time
    if (state_timer_ >= 2.0f) {
        SetAIState(NPCAIState::IDLE);
    }
}

void NPCEntity::UpdateDespawning(float delta_time) {
    (void)delta_time;
    if (state_timer_ >= DESPAWN_DELAY) {
        // Mark for removal (should be handled by EntityManager)
        SetActive(false);

        // If respawns, schedule respawn
        if (ai_profile_.respawns) {
            // This would be handled by a respawn system
            Logger::Debug("NPC {} scheduled for respawn", GetId());
        }
    }
}

// =============== Targeting ===============
void NPCEntity::SetTarget(uint64_t target_id) {
    if (target_id == GetId()) return; // Can't target self

    target_id_ = target_id;

    // If we have a target and are aggressive, start chasing
    if (target_id_ != 0 && ai_profile_.is_aggressive) {
        SetAIState(NPCAIState::CHASE);
    }
}

void NPCEntity::UpdateTargetSelection() {
    // Only update target if we don't have one or current target is invalid
    if (HasTarget() || !ai_profile_.is_aggressive) {
        return;
    }

    // In real implementation, you would:
    // 1. Query EntityManager for entities in sight range
    // 2. Filter by faction (hostile to player, etc.)
    // 3. Select closest or most threatening target

    // For now, this is a placeholder
}

void NPCEntity::UpdateHateList(uint64_t attacker_id, float damage) {
    if (attacker_id == 0 || attacker_id == GetId()) return;

    // Add damage to the attacker's total
    damage_taken_[attacker_id] += damage;

    // Update hate list order
    hate_list_.clear();
    for (const auto& [id, dmg] : damage_taken_) {
        hate_list_.push_back(id);
    }

    // Sort by damage dealt (descending)
    std::sort(hate_list_.begin(), hate_list_.end(),
        [this](uint64_t a, uint64_t b) {
            return damage_taken_[a] > damage_taken_[b];
        });

    // Limit hate list size
    if (hate_list_.size() > 10) {
        hate_list_.resize(10);
    }

    // Update target to top hated
    if (!hate_list_.empty()) {
        SetTarget(hate_list_[0]);
    }
}

void NPCEntity::ClearHateList() {
    hate_list_.clear();
    damage_taken_.clear();
    target_id_ = 0;
}

uint64_t NPCEntity::GetTopHated() const {
    return hate_list_.empty() ? 0 : hate_list_[0];
}

// =============== Combat Actions ===============
void NPCEntity::PerformAttack() {
    if (!HasTarget() || attack_cooldown_ > 0.0f) {
        return;
    }

    // Calculate damage with critical chance
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);

    float damage = npc_stats_.attack_damage;

    // Critical hit check
    if (dis(gen) < npc_stats_.critical_chance) {
        damage *= npc_stats_.critical_damage;
        Logger::Debug("NPC {} critical hit on target {}: {:.1f} damage",
                      GetId(), target_id_, damage);
    } else {
        Logger::Debug("NPC {} attacks target {}: {:.1f} damage",
                      GetId(), target_id_, damage);
    }

    // Apply damage to target (would be handled by combat system)
    FireEvent("on_attack");

    // Reset attack cooldown
    attack_cooldown_ = ATTACK_COOLDOWN_BASE / npc_stats_.attack_speed;
}

void NPCEntity::SummonAllies() {
    if (!ai_profile_.can_summon_allies || summon_cooldown_ > 0.0f) {
        return;
    }

    int allies_to_summon = std::min(ai_profile_.max_allies, 5); // Limit
    Logger::Debug("NPC {} summoning {} allies", GetId(), allies_to_summon);

    // This would create new NPC entities around this NPC
    // For now, just log it

    summon_cooldown_ = ai_profile_.summon_cooldown;
}

void NPCEntity::TakeDamage(float damage, uint64_t attacker_id) {
    if (IsDead() || damage <= 0.0f) return;

    // Update hate list
    UpdateHateList(attacker_id, damage);

    // Call base class to apply damage
    GameEntity::TakeDamage(damage, attacker_id);

    // Update AI state based on damage
    if (IsAlive()) {
        // If we have a target and are not already chasing/attacking, start chasing
        if (HasTarget() && ai_state_ != NPCAIState::CHASE && ai_state_ != NPCAIState::ATTACK) {
            SetAIState(NPCAIState::CHASE);
        }

        // Check for stun (simplified)
        if (damage > GetMaxHealth() * 0.3f) { // Large hit stuns
            stun_timer_ = STUN_DURATION;
            Stop();
        }
    } else {
        // Died
        SetAIState(NPCAIState::DEAD);

        // Generate loot and experience
        if (ai_profile_.drops_loot) {
            auto loot = GenerateLoot();
            int gold = GenerateGold();

            Logger::Debug("NPC {} died. Loot: {} items, {} gold",
                          GetId(), loot.size(), gold);
        }
    }
}

void NPCEntity::Heal(float amount, uint64_t healer_id) {
    GameEntity::Heal(amount, healer_id);

    // Update AI if healed significantly
    if (amount > GetMaxHealth() * 0.2f && ai_state_ == NPCAIState::FLEE) {
        // Stop fleeing if healed enough
        SetAIState(NPCAIState::IDLE);
    }
}

// =============== Patrol Management ===============
void NPCEntity::AddPatrolPoint(const glm::vec3& point) {
    ai_profile_.patrol_points.push_back(point);
    patrol_queue_.push(point);
}

void NPCEntity::ClearPatrolPoints() {
    ai_profile_.patrol_points.clear();
    while (!patrol_queue_.empty()) {
        patrol_queue_.pop();
    }
}

glm::vec3 NPCEntity::GetNextPatrolPoint() {
    if (patrol_queue_.empty()) {
        // Regenerate patrol queue from points
        for (const auto& point : ai_profile_.patrol_points) {
            patrol_queue_.push(point);
        }

        // If still empty, return current position
        if (patrol_queue_.empty()) {
            return GetPosition();
        }
    }

    glm::vec3 next_point = patrol_queue_.front();
    patrol_queue_.pop();

    // If looping, add back to end
    if (ai_profile_.patrol_loop) {
        patrol_queue_.push(next_point);
    }

    return next_point;
}

bool NPCEntity::IsAtPatrolPoint(const glm::vec3& point) const {
    glm::vec3 current_pos = GetPosition();
    glm::vec3 diff = current_pos - point;
    return glm::length(glm::vec2(diff.x, diff.z)) < 1.0f; // Within 1 unit horizontally
}

// =============== Loot Management ===============
void NPCEntity::SetLootTable(const std::string& table_id) {
    loot_table_.table_id = table_id;
}

void NPCEntity::AddDropItem(const std::string& item_id, float drop_rate) {
    loot_table_.item_drop_rates[item_id] = drop_rate;
}

void NPCEntity::RemoveDropItem(const std::string& item_id) {
    loot_table_.item_drop_rates.erase(item_id);
}

std::vector<std::pair<std::string, int>> NPCEntity::GenerateLoot() const {
    std::vector<std::pair<std::string, int>> loot;

    if (!ai_profile_.drops_loot) {
        return loot;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);

    // Check if loot should drop
    if (dis(gen) > loot_table_.drop_chance) {
        return loot;
    }

    // Determine number of items
    std::uniform_int_distribution<int> count_dis(
        loot_table_.min_items,
        loot_table_.max_items
    );
    int item_count = count_dis(gen);

    // Generate items
    for (int i = 0; i < item_count && !loot_table_.item_drop_rates.empty(); ++i) {
        for (const auto& [item_id, drop_rate] : loot_table_.item_drop_rates) {
            if (dis(gen) <= drop_rate) {
                // Determine quantity (usually 1, but could be more for stackable items)
                std::uniform_int_distribution<int> qty_dis(1, 3);
                int quantity = qty_dis(gen);

                loot.emplace_back(item_id, quantity);
                break; // One item per iteration
            }
        }
    }

    return loot;
}

int NPCEntity::GenerateGold() const {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(
        npc_stats_.min_gold,
        npc_stats_.max_gold
    );

    return dis(gen);
}

// =============== Dialogue Management ===============
void NPCEntity::SetDialogue(const NPCDialogue& dialogue) {
    dialogue_ = dialogue;
}

void NPCEntity::AddDialogueTopic(const std::string& topic, const std::string& response) {
    dialogue_.responses[topic] = response;

    // Add to topics list if not already there
    if (std::find(dialogue_.topics.begin(), dialogue_.topics.end(), topic) == dialogue_.topics.end()) {
        dialogue_.topics.push_back(topic);
    }
}

void NPCEntity::AddQuestDialogue(const std::string& quest_id, const nlohmann::json& dialogue) {
    dialogue_.quest_dialogues[quest_id] = dialogue;
}

void NPCEntity::AddTradeDialogue(const std::string& item_id, const nlohmann::json& dialogue) {
    dialogue_.trade_dialogues[item_id] = dialogue;
}

bool NPCEntity::HasDialogueTopic(const std::string& topic) const {
    return dialogue_.responses.find(topic) != dialogue_.responses.end();
}

std::string NPCEntity::GetDialogueResponse(const std::string& topic) const {
    auto it = dialogue_.responses.find(topic);
    return it != dialogue_.responses.end() ? it->second : "";
}

// =============== Quest Management ===============
void NPCEntity::AddQuest(const std::string& quest_id) {
    if (std::find(quests_.begin(), quests_.end(), quest_id) == quests_.end()) {
        quests_.push_back(quest_id);
    }
}

void NPCEntity::RemoveQuest(const std::string& quest_id) {
    quests_.erase(std::remove(quests_.begin(), quests_.end(), quest_id), quests_.end());
}

bool NPCEntity::HasQuest(const std::string& quest_id) const {
    return std::find(quests_.begin(), quests_.end(), quest_id) != quests_.end();
}

// =============== Trade Management ===============
void NPCEntity::AddTradeItem(const std::string& item_id, int price) {
    trade_items_[item_id] = price;
}

void NPCEntity::RemoveTradeItem(const std::string& item_id) {
    trade_items_.erase(item_id);
}

bool NPCEntity::SellsItem(const std::string& item_id) const {
    return trade_items_.find(item_id) != trade_items_.end();
}

int NPCEntity::GetItemPrice(const std::string& item_id) const {
    auto it = trade_items_.find(item_id);
    return it != trade_items_.end() ? it->second : 0;
}

// =============== Spawn/Respawn ===============
void NPCEntity::Respawn() {
    if (!IsDead()) return;

    // Reset position to spawn point
    SetPosition(spawn_position_);

    // Reset stats
    CalculateStats();
    SetHealth(GetMaxHealth());

    // Reset AI state
    ClearHateList();
    SetAIState(NPCAIState::SPAWNING);

    // Reset visual state
    SetActive(true);
    SetVisible(true);
    SetCollidable(true);

    Logger::Debug("NPC {} respawned at [{:.1f}, {:.1f}, {:.1f}]",
                  GetId(), spawn_position_.x, spawn_position_.y, spawn_position_.z);
}

// =============== Serialization ===============
nlohmann::json NPCEntity::Serialize() const {
    nlohmann::json json = GameEntity::Serialize();

    // NPC-specific data
    json["npc_type"] = static_cast<int>(npc_type_);
    json["rarity"] = static_cast<int>(rarity_);
    json["faction"] = static_cast<int>(faction_);
    json["ai_state"] = static_cast<int>(ai_state_);

    // Spawn position
    json["spawn_position"] = {spawn_position_.x, spawn_position_.y, spawn_position_.z};

    // Targeting
    json["target_id"] = target_id_;

    // Serialize NPC systems
    SaveStatsToJson(json);
    SaveAIProfileToJson(json);
    SaveLootTableToJson(json);
    SaveDialogueToJson(json);
    SaveQuestsToJson(json);
    SaveTradeItemsToJson(json);

    // Serialize hate list and damage taken
    json["hate_list"] = hate_list_;

    nlohmann::json damage_json;
    for (const auto& [attacker_id, damage] : damage_taken_) {
        damage_json[std::to_string(attacker_id)] = damage;
    }
    json["damage_taken"] = damage_json;

    // Serialize timers
    json["state_timer"] = state_timer_;
    json["idle_timer"] = idle_timer_;
    json["attack_cooldown"] = attack_cooldown_;
    json["stun_timer"] = stun_timer_;
    json["flee_timer"] = flee_timer_;
    json["summon_cooldown"] = summon_cooldown_;

    return json;
}

void NPCEntity::Deserialize(const nlohmann::json& data) {
    GameEntity::Deserialize(data);

    // NPC-specific data
    npc_type_ = static_cast<NPCType>(data.value("npc_type", 0));
    rarity_ = static_cast<NPCRarity>(data.value("rarity", 0));
    faction_ = static_cast<NPCFaction>(data.value("faction", 0));
    ai_state_ = static_cast<NPCAIState>(data.value("ai_state", 0));

    // Spawn position
    if (data.contains("spawn_position") && data["spawn_position"].is_array() &&
        data["spawn_position"].size() >= 3) {
        spawn_position_.x = data["spawn_position"][0];
        spawn_position_.y = data["spawn_position"][1];
        spawn_position_.z = data["spawn_position"][2];
    } else {
        spawn_position_ = GetPosition();
    }

    // Targeting
    target_id_ = data.value("target_id", 0);

    // Deserialize NPC systems
    LoadStatsFromJson(data);
    LoadAIProfileFromJson(data);
    LoadLootTableFromJson(data);
    LoadDialogueFromJson(data);
    LoadQuestsFromJson(data);
    LoadTradeItemsFromJson(data);

    // Deserialize hate list and damage taken
    hate_list_.clear();
    if (data.contains("hate_list") && data["hate_list"].is_array()) {
        hate_list_ = data["hate_list"].get<std::vector<uint64_t>>();
    }

    damage_taken_.clear();
    if (data.contains("damage_taken")) {
        for (const auto& [attacker_str, damage] : data["damage_taken"].items()) {
            uint64_t attacker_id = std::stoull(attacker_str);
            damage_taken_[attacker_id] = damage.get<float>();
        }
    }

    // Deserialize timers
    state_timer_ = data.value("state_timer", 0.0f);
    idle_timer_ = data.value("idle_timer", 0.0f);
    attack_cooldown_ = data.value("attack_cooldown", 0.0f);
    stun_timer_ = data.value("stun_timer", 0.0f);
    flee_timer_ = data.value("flee_timer", 0.0f);
    summon_cooldown_ = data.value("summon_cooldown", 0.0f);

    // Update name based on type
    name_ = GetNPCTypeString() + "_" + std::to_string(GetId());
}

// =============== Serialization Helpers ===============
void NPCEntity::SaveStatsToJson(nlohmann::json& json) const {
    json["npc_stats"] = npc_stats_.Serialize();
}

void NPCEntity::LoadStatsFromJson(const nlohmann::json& json) {
    if (json.contains("npc_stats")) {
        npc_stats_.Deserialize(json["npc_stats"]);
        SetHealth(npc_stats_.health);
        SetMaxHealth(npc_stats_.max_health);
    }
}

void NPCEntity::SaveAIProfileToJson(nlohmann::json& json) const {
    json["ai_profile"] = ai_profile_.Serialize();
}

void NPCEntity::LoadAIProfileFromJson(const nlohmann::json& json) {
    if (json.contains("ai_profile")) {
        ai_profile_.Deserialize(json["ai_profile"]);
    }
}

void NPCEntity::SaveLootTableToJson(nlohmann::json& json) const {
    json["loot_table"] = loot_table_.Serialize();
}

void NPCEntity::LoadLootTableFromJson(const nlohmann::json& json) {
    if (json.contains("loot_table")) {
        loot_table_.Deserialize(json["loot_table"]);
    }
}

void NPCEntity::SaveDialogueToJson(nlohmann::json& json) const {
    json["dialogue"] = dialogue_.Serialize();
}

void NPCEntity::LoadDialogueFromJson(const nlohmann::json& json) {
    if (json.contains("dialogue")) {
        dialogue_.Deserialize(json["dialogue"]);
    }
}

void NPCEntity::SaveQuestsToJson(nlohmann::json& json) const {
    json["quests"] = quests_;
}

void NPCEntity::LoadQuestsFromJson(const nlohmann::json& json) {
    quests_.clear();
    if (json.contains("quests") && json["quests"].is_array()) {
        quests_ = json["quests"].get<std::vector<std::string>>();
    }
}

void NPCEntity::SaveTradeItemsToJson(nlohmann::json& json) const {
    nlohmann::json trade_json;
    for (const auto& [item_id, price] : trade_items_) {
        trade_json[item_id] = price;
    }
    json["trade_items"] = trade_json;
}

void NPCEntity::LoadTradeItemsFromJson(const nlohmann::json& json) {
    trade_items_.clear();
    if (json.contains("trade_items")) {
        for (const auto& [item_id, price] : json["trade_items"].items()) {
            trade_items_[item_id] = price.get<int>();
        }
    }
}

// =============== Event Handlers ===============
void NPCEntity::OnCreate() {
    GameEntity::OnCreate();
    Logger::Debug("NPC {} created", GetId());
}

void NPCEntity::OnDestroy() {
    GameEntity::OnDestroy();
    Logger::Debug("NPC {} destroyed", GetId());
}

void NPCEntity::OnCollision(std::shared_ptr<GameEntity> other) {
    GameEntity::OnCollision(other);

    // NPC-specific collision logic
    if (other && other->GetType() == EntityType::PLAYER) {
        // If aggressive and sees player, attack
        if (ai_profile_.is_aggressive && ai_state_ != NPCAIState::ATTACK) {
            SetTarget(other->GetId());
            SetAIState(NPCAIState::ATTACK);
        }
    }
}

// =============== Fixed Update ===============
void NPCEntity::FixedUpdate(float delta_time) {
    GameEntity::FixedUpdate(delta_time);

    // NPC-specific physics updates
    // (Could include pathfinding, collision avoidance, etc.)
}


