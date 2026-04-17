#include "game/Player.hpp"

// =============== Attribute Serialization ===============
nlohmann::json PlayerAttributes::Serialize() const {
    return {
        {"strength", strength},
        {"dexterity", dexterity},
        {"intelligence", intelligence},
        {"vitality", vitality},
        {"luck", luck},
        {"attack_power", attack_power},
        {"defense", defense},
        {"critical_chance", critical_chance},
        {"critical_damage", critical_damage},
        {"move_speed", move_speed},
        {"attack_speed", attack_speed},
        {"health_regen", health_regen},
        {"mana_regen", mana_regen},
        {"dodge_chance", dodge_chance},
        {"block_chance", block_chance},
        {"parry_chance", parry_chance}
    };
}

void PlayerAttributes::Deserialize(const nlohmann::json& data) {
    strength = data.value("strength", 10);
    dexterity = data.value("dexterity", 10);
    intelligence = data.value("intelligence", 10);
    vitality = data.value("vitality", 10);
    luck = data.value("luck", 5);
    attack_power = data.value("attack_power", 10);
    defense = data.value("defense", 5);
    critical_chance = data.value("critical_chance", 0.05f);
    critical_damage = data.value("critical_damage", 1.5f);
    move_speed = data.value("move_speed", 1.0f);
    attack_speed = data.value("attack_speed", 1.0f);
    health_regen = data.value("health_regen", 0.1f);
    mana_regen = data.value("mana_regen", 0.2f);
    dodge_chance = data.value("dodge_chance", 0.05f);
    block_chance = data.value("block_chance", 0.03f);
    parry_chance = data.value("parry_chance", 0.02f);
}

// =============== Stats Serialization ===============
nlohmann::json PlayerStats::Serialize() const {
    return {
        {"health", health},
        {"max_health", max_health},
        {"mana", mana},
        {"max_mana", max_mana},
        {"stamina", stamina},
        {"max_stamina", max_stamina},
        {"level", level},
        {"experience", experience},
        {"experience_to_next_level", experience_to_next_level},
        {"score", score},
        {"currency_gold", currency_gold},
        {"currency_gems", currency_gems},
        {"honor_points", honor_points},
        {"pvp_rating", pvp_rating},
        {"skill_points", skill_points},
        {"talent_points", talent_points},
        {"reputation_level", reputation_level},
        {"kills", kills},
        {"deaths", deaths},
        {"assists", assists},
        {"quests_completed", quests_completed},
        {"dungeons_completed", dungeons_completed},
        {"raids_completed", raids_completed},
        {"total_playtime", total_playtime},
        {"session_playtime", session_playtime}
    };
}

void PlayerStats::Deserialize(const nlohmann::json& data) {
    health = data.value("health", 100);
    max_health = data.value("max_health", 100);
    mana = data.value("mana", 100);
    max_mana = data.value("max_mana", 100);
    stamina = data.value("stamina", 100);
    max_stamina = data.value("max_stamina", 100);
    level = data.value("level", 1);
    experience = data.value("experience", 0);
    experience_to_next_level = data.value("experience_to_next_level", 100);
    score = data.value("score", 0);
    currency_gold = data.value("currency_gold", 100);
    currency_gems = data.value("currency_gems", 10);
    honor_points = data.value("honor_points", 0);
    pvp_rating = data.value("pvp_rating", 1500);
    skill_points = data.value("skill_points", 0);
    talent_points = data.value("talent_points", 0);
    reputation_level = data.value("reputation_level", 0);
    kills = data.value("kills", 0);
    deaths = data.value("deaths", 0);
    assists = data.value("assists", 0);
    quests_completed = data.value("quests_completed", 0);
    dungeons_completed = data.value("dungeons_completed", 0);
    raids_completed = data.value("raids_completed", 0);
    total_playtime = data.value("total_playtime", 0.0f);
    session_playtime = data.value("session_playtime", 0.0f);
}

// =============== Equipment Serialization ===============
nlohmann::json PlayerEquipment::Serialize() const {
    return {
        {"head", head},
        {"chest", chest},
        {"legs", legs},
        {"feet", feet},
        {"hands", hands},
        {"main_hand", main_hand},
        {"off_hand", off_hand},
        {"ring1", ring1},
        {"ring2", ring2},
        {"neck", neck},
        {"trinket1", trinket1},
        {"trinket2", trinket2},
        {"cloak", cloak},
        {"belt", belt},
        {"shoulders", shoulders},
        {"wrist", wrist},
        {"mount", mount},
        {"pet", pet}
    };
}

void PlayerEquipment::Deserialize(const nlohmann::json& data) {
    head = data.value("head", "");
    chest = data.value("chest", "");
    legs = data.value("legs", "");
    feet = data.value("feet", "");
    hands = data.value("hands", "");
    main_hand = data.value("main_hand", "");
    off_hand = data.value("off_hand", "");
    ring1 = data.value("ring1", "");
    ring2 = data.value("ring2", "");
    neck = data.value("neck", "");
    trinket1 = data.value("trinket1", "");
    trinket2 = data.value("trinket2", "");
    cloak = data.value("cloak", "");
    belt = data.value("belt", "");
    shoulders = data.value("shoulders", "");
    wrist = data.value("wrist", "");
    mount = data.value("mount", "");
    pet = data.value("pet", "");
}

// =============== Settings Serialization ===============
nlohmann::json PlayerSettings::Serialize() const {
    nlohmann::json json;
    json["ui_scale"] = ui_scale;
    json["sound_volume"] = sound_volume;
    json["music_volume"] = music_volume;
    json["master_volume"] = master_volume;
    json["voice_volume"] = voice_volume;

    json["chat_enabled"] = chat_enabled;
    json["combat_text"] = combat_text;
    json["auto_loot"] = auto_loot;
    json["show_damage_numbers"] = show_damage_numbers;
    json["show_floating_text"] = show_floating_text;
    json["show_party_frames"] = show_party_frames;
    json["show_guild_names"] = show_guild_names;
    json["show_player_names"] = show_player_names;
    json["show_npc_names"] = show_npc_names;
    json["show_quest_tracker"] = show_quest_tracker;
    json["show_minimap"] = show_minimap;

    json["language"] = language;
    json["timezone"] = timezone;
    json["input_mode"] = input_mode;
    json["graphics_quality"] = graphics_quality;

    // Key bindings (simplified)
    nlohmann::json keys;
    for (int i = 0; i < 256; ++i) {
        if (key_bindings[i] != 0) {
            keys[std::to_string(i)] = key_bindings[i];
        }
    }
    json["key_bindings"] = keys;

    return json;
}

void PlayerSettings::Deserialize(const nlohmann::json& data) {
    ui_scale = data.value("ui_scale", 1.0f);
    sound_volume = data.value("sound_volume", 0.8f);
    music_volume = data.value("music_volume", 0.6f);
    master_volume = data.value("master_volume", 1.0f);
    voice_volume = data.value("voice_volume", 0.7f);

    chat_enabled = data.value("chat_enabled", true);
    combat_text = data.value("combat_text", true);
    auto_loot = data.value("auto_loot", false);
    show_damage_numbers = data.value("show_damage_numbers", true);
    show_floating_text = data.value("show_floating_text", true);
    show_party_frames = data.value("show_party_frames", true);
    show_guild_names = data.value("show_guild_names", true);
    show_player_names = data.value("show_player_names", true);
    show_npc_names = data.value("show_npc_names", true);
    show_quest_tracker = data.value("show_quest_tracker", true);
    show_minimap = data.value("show_minimap", true);

    language = data.value("language", "en");
    timezone = data.value("timezone", "UTC");
    input_mode = data.value("input_mode", "keyboard_mouse");
    graphics_quality = data.value("graphics_quality", "medium");

    // Key bindings
    if (data.contains("key_bindings")) {
        const auto& keys = data["key_bindings"];
        for (const auto& [key_str, value] : keys.items()) {
            int key = std::stoi(key_str);
            if (key >= 0 && key < 256) {
                key_bindings[key] = value.get<int>();
            }
        }
    }
}

// =============== Player Implementation ===============

Player::Player(uint64_t id, const std::string& username, const std::string& password)
: GameEntity(EntityType::PLAYER, glm::vec3(0.0f, 0.0f, 0.0f)),
id_(id),
username_(username),
password_hash_(Passwords::HashPassword(password)),
onGround_(true),
last_movement_(std::chrono::system_clock::now()),
player_class_(PlayerClass::WARRIOR),
race_(PlayerRace::HUMAN),
status_(PlayerStatus::IDLE),
inventory_system_(InventorySystem::GetInstance()),
skill_system_(SkillSystem::GetInstance()),
quest_manager_(QuestManager::GetInstance())
{
    ApplyRaceBonuses();
    ApplyClassBonuses();
    UpdateDerivedStats();
    CalculateExperienceToNextLevel();
    Logger::Debug("Player {} created with ID {}", username, id);
}

Player::Player(const glm::vec3& position)
: GameEntity(EntityType::PLAYER, position),
id_(0),
username_(""),
password_hash_(""),
onGround_(true),
last_movement_(std::chrono::system_clock::now()),
player_class_(PlayerClass::WARRIOR),
race_(PlayerRace::HUMAN),
status_(PlayerStatus::IDLE),
inventory_system_(InventorySystem::GetInstance()),
skill_system_(SkillSystem::GetInstance()),
quest_manager_(QuestManager::GetInstance())
{
    ApplyRaceBonuses();
    ApplyClassBonuses();
    UpdateDerivedStats();
    CalculateExperienceToNextLevel();
    Logger::Debug("Player created at [{:.1f}, {:.1f}, {:.1f}]",
                  position.x, position.y, position.z);
}

Player::Player(const glm::vec3& position, PlayerClass player_class, PlayerRace race)
: GameEntity(EntityType::PLAYER, position),
id_(0),
username_(""),
password_hash_(""),
onGround_(true),
last_movement_(std::chrono::system_clock::now()),
player_class_(player_class),
race_(race),
status_(PlayerStatus::IDLE),
inventory_system_(InventorySystem::GetInstance()),
skill_system_(SkillSystem::GetInstance()),
quest_manager_(QuestManager::GetInstance())
{
    ApplyRaceBonuses();
    ApplyClassBonuses();
    UpdateDerivedStats();
    CalculateExperienceToNextLevel();
    Logger::Debug("Player created: {} {} at [{:.1f}, {:.1f}, {:.1f}]",
                  GetRaceString(race), GetClassString(player_class),
                  position.x, position.y, position.z);
}

Player::~Player() {
    SaveToDatabase();
    Logger::Debug("Player {} destroyed", GetId());
}

void Player::UpdatePosition(float x, float y, float z) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    position_.x = x;
    position_.y = y;
    position_.z = z;
    last_movement_ = std::chrono::system_clock::now();
}

float Player::GetDistanceTo(const glm::vec3& other) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    float dx = position_.x - other.x;
    float dy = position_.y - other.y;
    float dz = position_.z - other.z;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

nlohmann::json Player::JsonGetInventory() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    nlohmann::json inventory_array = nlohmann::json::array();
    for (const auto& [item_id, count] : inventory_) {
        nlohmann::json item;
        item["id"] = item_id;
        item["count"] = count;
        // Optional: include additional metadata if available (e.g., "acquired" timestamp)
        inventory_array.push_back(item);
    }
    return inventory_array;
}

void Player::AddCurrencyGold(int amount) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    stats_.currency_gold += amount;
    if (stats_.currency_gold < 0) stats_.currency_gold = 0;
}

void Player::AddCurrencyGems(int amount) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    stats_.currency_gems += amount;
    if (stats_.currency_gems < 0) stats_.currency_gems = 0;
}

void Player::SetOnline(bool online) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    online_ = online;

    auto now = std::chrono::system_clock::now();
    if (online) {
        last_login_ = now;
    } else {
        last_logout_ = now;

        // Update total playtime
        auto sessionTime = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_login_);
        stats_.total_playtime += sessionTime.count();
    }
}

void Player::UpdateHeartbeat() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    last_heartbeat_ = std::chrono::system_clock::now();
}

bool Player::IsHeartbeatExpired(int timeoutSeconds) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_heartbeat_);
    return elapsed.count() > timeoutSeconds;
}

void Player::ApplyDamage(int damage, uint64_t attackerId) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    // Calculate actual damage (consider defense)
    int actualDamage = std::max(1, damage - attributes_.defense / 2);

    stats_.health -= actualDamage;
    if (stats_.health < 0) stats_.health = 0;

    // Record damage source
    if (damage_sources_.size() > 10) {
        damage_sources_.pop_front();
    }
    damage_sources_.push_back({attackerId, actualDamage, std::chrono::system_clock::now()});
}

void Player::ApplyHealing(int amount, uint64_t healerId) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    stats_.health += amount;
    if (stats_.health > stats_.max_health) stats_.health = stats_.max_health;

    // Record healing source
    if (healing_sources_.size() > 10) {
        healing_sources_.pop_front();
    }
    healing_sources_.push_back({healerId, amount, std::chrono::system_clock::now()});
}

nlohmann::json Player::ToJson() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    nlohmann::json json;
    json["id"] = id_;
    json["username"] = username_;
    json["level"] = stats_.level;
    json["experience"] = stats_.experience;
    json["health"] = stats_.health;
    json["max_health"] = stats_.max_health;
    json["mana"] = stats_.mana;
    json["max_mana"] = stats_.max_mana;
    json["score"] = stats_.score;
    json["currency_gold"] = stats_.currency_gold;
    json["currency_gems"] = stats_.currency_gems;
    json["position"] = {
        {"x", position_.x},
        {"y", position_.y},
        {"z", position_.z}
    };
    json["attributes"] = attributes_.Serialize();
    json["inventory"] = inventory_;
    json["equipment"] = equipment_.Serialize();
    json["achievements"] = achievements_;
    json["online"] = online_;
    json["total_playtime"] = stats_.total_playtime;
    json["created_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        created_at_.time_since_epoch()).count();
    json["last_login"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        last_login_.time_since_epoch()).count();
    json["connection_quality"] = connection_quality_;
    return json;
}

// =============== Health and Mana Management ===============
void Player::SetHealth(int health) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    stats_.health = std::clamp(health, 0, stats_.max_health);
    if (stats_.health <= 0 && status_ != PlayerStatus::DEAD) {
        status_ = PlayerStatus::DEAD;
        Logger::Info("Player {} has died", GetId());
    } else if (stats_.health > 0 && status_ == PlayerStatus::DEAD) {
        status_ = PlayerStatus::IDLE;
    }
}

void Player::SetMaxHealth(int max_health) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    stats_.max_health = std::min(max_health, MAX_HEALTH);
    stats_.health = std::min(stats_.health, stats_.max_health);
}

void Player::SetMana(int mana) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    stats_.mana = std::clamp(mana, 0, stats_.max_mana);
}

void Player::SetMaxMana(int max_mana) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    stats_.max_mana = std::min(max_mana, MAX_MANA);
    stats_.mana = std::min(stats_.mana, stats_.max_mana);
}

void Player::SetLevel(int level) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    int old_level = stats_.level;
    stats_.level = std::clamp(level, 1, MAX_LEVEL);
    if (stats_.level > old_level) {
        OnLevelUp();
    }
    CalculateExperienceToNextLevel();
    UpdateDerivedStats();
}

// =============== Experience Management ===============
void Player::AddExperience(int64_t amount) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (stats_.level >= MAX_LEVEL || amount <= 0) {
        return;
    }
    stats_.experience += amount;
    while (stats_.experience >= stats_.experience_to_next_level && stats_.level < MAX_LEVEL) {
        stats_.experience -= stats_.experience_to_next_level;
        stats_.level++;
        OnLevelUp();
        CalculateExperienceToNextLevel();
    }
    if (stats_.level >= MAX_LEVEL) {
        stats_.experience = stats_.experience_to_next_level;
    }
    Logger::Debug("Player {} gained {} experience (now: {}/{})", 
                  GetId(), amount, stats_.experience, stats_.experience_to_next_level);
}

void Player::LoseExperience(int64_t amount) {
    if (amount <= 0) {
        return;
    }
    std::unique_lock<std::shared_mutex> lock(mutex_);
    stats_.experience = std::max<int64_t>(0, stats_.experience - amount);
    Logger::Debug("Player {} lost {} experience", GetId(), amount);
}

int64_t Player::CalculateExperienceRequired(int level) const {
    // Exponential experience curve
    if (level <= 1) return 0;
    if (level >= MAX_LEVEL) return 1000000000; // Max value

    // Base formula: 100 * (1.5^(level-1))
    return static_cast<int64_t>(100 * std::pow(1.5, level - 1));
}

void Player::CalculateExperienceToNextLevel() {
    if (stats_.level >= MAX_LEVEL) {
        stats_.experience_to_next_level = 0;
    } else {
        stats_.experience_to_next_level = CalculateExperienceRequired(stats_.level + 1);
    }
}

// =============== Level Up Handler ===============
void Player::OnLevelUp() {
    // Increase stats
    stats_.max_health += 20;
    stats_.max_mana += 15;
    stats_.health = stats_.max_health; // Full heal
    stats_.mana = stats_.max_mana;     // Full mana

    // Increase attributes based on class
    switch (player_class_) {
        case PlayerClass::WARRIOR:
            attributes_.strength += 3;
            attributes_.vitality += 2;
            attributes_.dexterity += 1;
            break;
        case PlayerClass::MAGE:
            attributes_.intelligence += 3;
            attributes_.mana_regen += 0.1f;
            attributes_.vitality += 1;
            break;
        case PlayerClass::RANGER:
            attributes_.dexterity += 3;
            attributes_.strength += 1;
            attributes_.vitality += 1;
            break;
        case PlayerClass::ROGUE:
            attributes_.dexterity += 3;
            attributes_.luck += 1;
            attributes_.strength += 1;
            break;
        case PlayerClass::CLERIC:
            attributes_.intelligence += 2;
            attributes_.vitality += 2;
            attributes_.strength += 1;
            break;
        default:
            attributes_.strength += 1;
            attributes_.dexterity += 1;
            attributes_.intelligence += 1;
            attributes_.vitality += 1;
            break;
    }

    // Award skill/talent points
    stats_.skill_points += 3;
    stats_.talent_points += 1;

    UpdateDerivedStats();

    Logger::Info("Player {} leveled up to level {}!", GetId(), stats_.level);
}

// =============== Attribute Management ===============
void Player::SetAttribute(const std::string& attribute_name, float value) {
    if (attribute_name == "strength") attributes_.strength = static_cast<int>(value);
    else if (attribute_name == "dexterity") attributes_.dexterity = static_cast<int>(value);
    else if (attribute_name == "intelligence") attributes_.intelligence = static_cast<int>(value);
    else if (attribute_name == "vitality") attributes_.vitality = static_cast<int>(value);
    else if (attribute_name == "luck") attributes_.luck = static_cast<int>(value);
    else if (attribute_name == "attack_power") attributes_.attack_power = static_cast<int>(value);
    else if (attribute_name == "defense") attributes_.defense = static_cast<int>(value);
    else if (attribute_name == "critical_chance") attributes_.critical_chance = value;
    else if (attribute_name == "critical_damage") attributes_.critical_damage = value;
    else if (attribute_name == "move_speed") attributes_.move_speed = value;
    else if (attribute_name == "attack_speed") attributes_.attack_speed = value;
    else if (attribute_name == "health_regen") attributes_.health_regen = value;
    else if (attribute_name == "mana_regen") attributes_.mana_regen = value;
    else if (attribute_name == "dodge_chance") attributes_.dodge_chance = value;
    else if (attribute_name == "block_chance") attributes_.block_chance = value;
    else if (attribute_name == "parry_chance") attributes_.parry_chance = value;

    UpdateDerivedStats();
}

float Player::GetAttribute(const std::string& attribute_name) const {
    if (attribute_name == "strength") return static_cast<float>(attributes_.strength);
    else if (attribute_name == "dexterity") return static_cast<float>(attributes_.dexterity);
    else if (attribute_name == "intelligence") return static_cast<float>(attributes_.intelligence);
    else if (attribute_name == "vitality") return static_cast<float>(attributes_.vitality);
    else if (attribute_name == "luck") return static_cast<float>(attributes_.luck);
    else if (attribute_name == "attack_power") return static_cast<float>(attributes_.attack_power);
    else if (attribute_name == "defense") return static_cast<float>(attributes_.defense);
    else if (attribute_name == "critical_chance") return attributes_.critical_chance;
    else if (attribute_name == "critical_damage") return attributes_.critical_damage;
    else if (attribute_name == "move_speed") return attributes_.move_speed;
    else if (attribute_name == "attack_speed") return attributes_.attack_speed;
    else if (attribute_name == "health_regen") return attributes_.health_regen;
    else if (attribute_name == "mana_regen") return attributes_.mana_regen;
    else if (attribute_name == "dodge_chance") return attributes_.dodge_chance;
    else if (attribute_name == "block_chance") return attributes_.block_chance;
    else if (attribute_name == "parry_chance") return attributes_.parry_chance;

    return 0.0f;
}

void Player::UpdateDerivedStats() {
    // Calculate derived stats from base attributes
    attributes_.attack_power = attributes_.strength * 2 + attributes_.dexterity;
    attributes_.defense = attributes_.vitality + attributes_.dexterity / 2;
    attributes_.critical_chance = std::min(0.5f, attributes_.luck * 0.01f + attributes_.dexterity * 0.002f);
    attributes_.critical_damage = 1.5f + attributes_.luck * 0.02f;
    attributes_.move_speed = 1.0f + attributes_.dexterity * 0.01f;
    attributes_.attack_speed = 1.0f + attributes_.dexterity * 0.005f;
    attributes_.health_regen = 0.1f + attributes_.vitality * 0.01f;
    attributes_.mana_regen = 0.2f + attributes_.intelligence * 0.02f;
    attributes_.dodge_chance = std::min(0.3f, attributes_.dexterity * 0.005f);
    attributes_.block_chance = std::min(0.2f, attributes_.strength * 0.003f);
    attributes_.parry_chance = std::min(0.15f, attributes_.dexterity * 0.004f);
}

// =============== Equipment Management ===============
bool Player::EquipItem(const std::string& item_id, const std::string& slot) {
    // Check if player has the item
    if (!HasItem(item_id, 1)) {
        Logger::Warn("Player {} doesn't have item {} to equip", GetId(), item_id);
        return false;
    }

    // Check slot validity
    if (slot == "head") equipment_.head = item_id;
    else if (slot == "chest") equipment_.chest = item_id;
    else if (slot == "legs") equipment_.legs = item_id;
    else if (slot == "feet") equipment_.feet = item_id;
    else if (slot == "hands") equipment_.hands = item_id;
    else if (slot == "main_hand") equipment_.main_hand = item_id;
    else if (slot == "off_hand") equipment_.off_hand = item_id;
    else if (slot == "ring1") equipment_.ring1 = item_id;
    else if (slot == "ring2") equipment_.ring2 = item_id;
    else if (slot == "neck") equipment_.neck = item_id;
    else if (slot == "trinket1") equipment_.trinket1 = item_id;
    else if (slot == "trinket2") equipment_.trinket2 = item_id;
    else if (slot == "cloak") equipment_.cloak = item_id;
    else if (slot == "belt") equipment_.belt = item_id;
    else if (slot == "shoulders") equipment_.shoulders = item_id;
    else if (slot == "wrist") equipment_.wrist = item_id;
    else if (slot == "mount") equipment_.mount = item_id;
    else if (slot == "pet") equipment_.pet = item_id;
    else {
        Logger::Warn("Invalid equipment slot: {}", slot);
        return false;
    }

    // Remove item from inventory
    RemoveItem(item_id, 1);

    Logger::Debug("Player {} equipped {} to {}", GetId(), item_id, slot);
    return true;
}

bool Player::UnequipItem(const std::string& slot) {
    std::string item_id = GetEquippedItem(slot);
    if (item_id.empty()) {
        return false;
    }

    // Add item back to inventory
    AddItem(item_id, 1);

    // Clear the slot
    if (slot == "head") equipment_.head = "";
    else if (slot == "chest") equipment_.chest = "";
    else if (slot == "legs") equipment_.legs = "";
    else if (slot == "feet") equipment_.feet = "";
    else if (slot == "hands") equipment_.hands = "";
    else if (slot == "main_hand") equipment_.main_hand = "";
    else if (slot == "off_hand") equipment_.off_hand = "";
    else if (slot == "ring1") equipment_.ring1 = "";
    else if (slot == "ring2") equipment_.ring2 = "";
    else if (slot == "neck") equipment_.neck = "";
    else if (slot == "trinket1") equipment_.trinket1 = "";
    else if (slot == "trinket2") equipment_.trinket2 = "";
    else if (slot == "cloak") equipment_.cloak = "";
    else if (slot == "belt") equipment_.belt = "";
    else if (slot == "shoulders") equipment_.shoulders = "";
    else if (slot == "wrist") equipment_.wrist = "";
    else if (slot == "mount") equipment_.mount = "";
    else if (slot == "pet") equipment_.pet = "";

    Logger::Debug("Player {} unequipped {} from {}", GetId(), item_id, slot);
    return true;
}

std::string Player::GetEquippedItem(const std::string& slot) const {
    if (slot == "head") return equipment_.head;
    else if (slot == "chest") return equipment_.chest;
    else if (slot == "legs") return equipment_.legs;
    else if (slot == "feet") return equipment_.feet;
    else if (slot == "hands") return equipment_.hands;
    else if (slot == "main_hand") return equipment_.main_hand;
    else if (slot == "off_hand") return equipment_.off_hand;
    else if (slot == "ring1") return equipment_.ring1;
    else if (slot == "ring2") return equipment_.ring2;
    else if (slot == "neck") return equipment_.neck;
    else if (slot == "trinket1") return equipment_.trinket1;
    else if (slot == "trinket2") return equipment_.trinket2;
    else if (slot == "cloak") return equipment_.cloak;
    else if (slot == "belt") return equipment_.belt;
    else if (slot == "shoulders") return equipment_.shoulders;
    else if (slot == "wrist") return equipment_.wrist;
    else if (slot == "mount") return equipment_.mount;
    else if (slot == "pet") return equipment_.pet;

    return "";
}

// =============== Inventory Management ===============
void Player::AddItem(const std::string& item_id, int count) {
    if (count <= 0) return;
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = inventory_.find(item_id);
    if (it != inventory_.end()) {
        it->second += count;
    } else {
        inventory_[item_id] = count;
    }
    Logger::Debug("Player {} received {} x{}", GetId(), item_id, count);
}

void Player::RemoveItem(const std::string& itemId, int count) {
    if (count <= 0) return;
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if(!inventory_.empty())
    {
        for(const auto& [k, v] : inventory_) {
            if (k == itemId) {
                if (v <= count) {
                    inventory_.erase(k);
                } else {
                    inventory_[k] = v - count;
                }
                return;
            }
        }
    }
}

int Player::GetItemCount(const std::string& item_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = inventory_.find(item_id);
    return it != inventory_.end() ? it->second : 0;
}

bool Player::HasItem(const std::string& item_id, int count) const {
    return GetItemCount(item_id) >= count;
}

// =============== Currency Management ===============
void Player::AddGold(int64_t amount) {
    if (amount < 0) return;
    
    stats_.currency_gold += amount;
    Logger::Debug("Player {} gained {} gold", GetId(), amount);
}

void Player::RemoveGold(int64_t amount) {
    if (amount < 0) return;
    
    stats_.currency_gold = std::max<int64_t>(0, stats_.currency_gold - amount);
    Logger::Debug("Player {} lost {} gold", GetId(), amount);
}

void Player::AddGems(int64_t amount) {
    if (amount < 0) return;
    
    stats_.currency_gems += amount;
    Logger::Debug("Player {} gained {} gems", GetId(), amount);
}

void Player::RemoveGems(int64_t amount) {
    if (amount < 0) return;
    
    stats_.currency_gems = std::max<int64_t>(0, stats_.currency_gems - amount);
    Logger::Debug("Player {} lost {} gems", GetId(), amount);
}

// =============== Skill Management ===============
void Player::LearnSkill(const std::string& skill_id, int level) {
    skills_[skill_id] = level;
    Logger::Debug("Player {} learned skill {} (level {})", GetId(), skill_id, level);
}

void Player::ForgetSkill(const std::string& skill_id) {
    skills_.erase(skill_id);
    Logger::Debug("Player {} forgot skill {}", GetId(), skill_id);
}

bool Player::HasSkill(const std::string& skill_id) const {
    return skills_.find(skill_id) != skills_.end();
}

int Player::GetSkillLevel(const std::string& skill_id) const {
    auto it = skills_.find(skill_id);
    return it != skills_.end() ? it->second : 0;
}

// =============== Quest Management ===============
void Player::StartQuest(const std::string& quest_id) {
    if (active_quests_.size() >= MAX_ACTIVE_QUESTS) {
        Logger::Warn("Player {} has too many active quests", GetId());
        return;
    }

    nlohmann::json quest_data = {
        {"start_time", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()},
        {"progress", nlohmann::json::object()}
    };

    active_quests_[quest_id] = quest_data;
    Logger::Debug("Player {} started quest {}", GetId(), quest_id);
}

void Player::CompleteQuest(const std::string& quest_id) {
    auto it = active_quests_.find(quest_id);
    if (it == active_quests_.end()) {
        Logger::Warn("Player {} doesn't have active quest {}", GetId(), quest_id);
        return;
    }

    // Add to completed quests
    completed_quests_[quest_id] = it->second;

    // Remove from active quests
    active_quests_.erase(it);

    // Award experience and rewards
    stats_.quests_completed++;

    Logger::Debug("Player {} completed quest {}", GetId(), quest_id);
}

void Player::FailQuest(const std::string& quest_id) {
    active_quests_.erase(quest_id);
    Logger::Debug("Player {} failed quest {}", GetId(), quest_id);
}

bool Player::HasQuest(const std::string& quest_id) const {
    return active_quests_.find(quest_id) != active_quests_.end();
}

bool Player::IsQuestCompleted(const std::string& quest_id) const {
    return completed_quests_.find(quest_id) != completed_quests_.end();
}

// =============== Combat Management ===============
void Player::TakeDamage(int amount, uint64_t attacker_id) {
    if (IsDead() || amount <= 0) return;

    // Calculate actual damage (consider defense and other reductions)
    int actual_damage = std::max(1, amount - attributes_.defense / 2);

    // Apply dodge/block/parry chances
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);

    float roll = dis(gen);
    if (roll < attributes_.dodge_chance) {
        Logger::Debug("Player {} dodged {} damage", GetId(), actual_damage);
        return;
    }

    if (roll < attributes_.dodge_chance + attributes_.block_chance) {
        actual_damage /= 2;
        Logger::Debug("Player {} blocked {} damage", GetId(), actual_damage);
    }

    if (roll < attributes_.dodge_chance + attributes_.block_chance + attributes_.parry_chance) {
        actual_damage = 0;
        Logger::Debug("Player {} parried the attack", GetId());
    }

    // Apply damage
    SetHealth(stats_.health - actual_damage);

    // Record damage source
    if (damage_sources_.size() > 10) {
        damage_sources_.pop_front();
    }
    damage_sources_.push_back({attacker_id, actual_damage, std::chrono::system_clock::now()});

    // Update status
    if (actual_damage > 0) {
        status_ = PlayerStatus::COMBAT;
    }

    Logger::Debug("Player {} took {} damage (health: {}/{})", 
                  GetId(), actual_damage, stats_.health, stats_.max_health);
}

void Player::Heal(int amount, uint64_t healer_id) {
    if (IsDead() || amount <= 0) return;

    int old_health = stats_.health;
    SetHealth(stats_.health + amount);
    int actual_healing = stats_.health - old_health;

    if (actual_healing > 0) {
        // Record healing source
        if (healing_sources_.size() > 10) {
            healing_sources_.pop_front();
        }
        healing_sources_.push_back({healer_id, actual_healing, std::chrono::system_clock::now()});

        Logger::Debug("Player {} healed for {} (health: {}/{})", 
                      GetId(), actual_healing, stats_.health, stats_.max_health);
    }
}

int Player::CalculateDamage(const std::string& attackType) const {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    float baseDamage = static_cast<float>(stats_.attack_damage);
    if (attackType == "melee") {
        baseDamage += attributes_.strength * 0.5f;
    } else if (attackType == "ranged") {
        baseDamage += attributes_.dexterity * 0.5f;
    } else if (attackType == "magic") {
        baseDamage += attributes_.intelligence * 0.5f;
    }
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    if (dist(rng) < attributes_.critical_chance) {
        baseDamage *= attributes_.critical_damage;
    }
    return static_cast<int>(std::round(baseDamage));
}

void Player::ApplyBuff(const std::string& buff_id, const nlohmann::json& buff_data, float duration) {
    if (active_buffs_.size() >= MAX_BUFFS) {
        Logger::Warn("Player {} has too many active buffs", GetId());
        return;
    }
    std::unique_lock<std::shared_mutex> lock(mutex_);
    ActiveBuff buff;
    buff.buff_id = buff_id;
    buff.buff_data = buff_data;
    buff.duration = duration;
    buff.time_remaining = duration;
    buff.applied_time = std::chrono::system_clock::now();
    active_buffs_[buff_id] = buff;
    for (const auto& [key, value] : buff_data.items()) {
        if (value.is_number()) {
            float current_value = GetAttribute(key);
            SetAttribute(key, current_value + value.get<float>());
        }
    }
    Logger::Debug("Player {} applied buff {} (duration: {}s)", GetId(), buff_id, duration);
}

void Player::RemoveBuff(const std::string& buff_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = active_buffs_.find(buff_id);
    if (it == active_buffs_.end()) {
        return;
    }
    for (const auto& [key, value] : it->second.buff_data.items()) {
        if (value.is_number()) {
            float current_value = GetAttribute(key);
            SetAttribute(key, current_value - value.get<float>());
        }
    }
    active_buffs_.erase(it);
    Logger::Debug("Player {} removed buff {}", GetId(), buff_id);
}

void Player::UpdateBuffs(float delta_time) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    //auto now = std::chrono::system_clock::now();
    for (auto it = active_buffs_.begin(); it != active_buffs_.end();) {
        it->second.time_remaining -= delta_time;
        if (it->second.time_remaining <= 0.0f) {
            for (const auto& [key, value] : it->second.buff_data.items()) {
                if (value.is_number()) {
                    float current_value = GetAttribute(key);
                    SetAttribute(key, current_value - value.get<float>());
                }
            }
            Logger::Debug("Player {}'s buff {} expired", GetId(), it->first);
            it = active_buffs_.erase(it);
        } else {
            ++it;
        }
    }
}

// =============== Serialization ===============
nlohmann::json Player::Serialize() const {
    nlohmann::json json = GameEntity::Serialize();

    // Player-specific data
    json["player_class"] = static_cast<int>(player_class_);
    json["race"] = static_cast<int>(race_);
    json["status"] = static_cast<int>(status_);
    json["title"] = title_;
    json["session_id"] = session_id_;
    json["connection_quality"] = connection_quality_;

    // Player systems data
    json["attributes"] = attributes_.Serialize();
    json["stats"] = stats_.Serialize();
    json["equipment"] = equipment_.Serialize();
    json["settings"] = settings_.Serialize();

    // Other collections
    SaveInventoryToJson(json);
    SaveSkillsToJson(json);
    SaveQuestsToJson(json);
    SaveBuffsToJson(json);
    SaveCooldownsToJson(json);

    // Social
    json["party_id"] = party_id_;
    json["guild_id"] = guild_id_;
    json["friends"] = friends_;
    json["blocked_players"] = blocked_players_;

    // Cosmetics
    nlohmann::json cosmetics_json;
    for (const auto& [slot, cosmetic_id] : cosmetics_) {
        cosmetics_json[slot] = cosmetic_id;
    }
    json["cosmetics"] = cosmetics_json;

    return json;
}

void Player::Deserialize(const nlohmann::json& data) {
    GameEntity::Deserialize(data);

    // Player-specific data
    player_class_ = static_cast<PlayerClass>(data.value("player_class", 0));
    race_ = static_cast<PlayerRace>(data.value("race", 0));
    status_ = static_cast<PlayerStatus>(data.value("status", 0));
    title_ = data.value("title", "");
    session_id_ = data.value("session_id", 0);
    connection_quality_ = data.value("connection_quality", 100.0f);

    // Player systems data
    if (data.contains("attributes")) {
        attributes_.Deserialize(data["attributes"]);
    }

    if (data.contains("stats")) {
        stats_.Deserialize(data["stats"]);
    }

    if (data.contains("equipment")) {
        equipment_.Deserialize(data["equipment"]);
    }

    if (data.contains("settings")) {
        settings_.Deserialize(data["settings"]);
    }

    // Other collections
    LoadInventoryFromJson(data);
    LoadSkillsFromJson(data);
    LoadQuestsFromJson(data);
    LoadBuffsFromJson(data);
    LoadCooldownsFromJson(data);

    // Social
    party_id_ = data.value("party_id", 0);
    guild_id_ = data.value("guild_id", 0);

    if (data.contains("friends")) {
        friends_ = data["friends"].get<std::vector<uint64_t>>();
    }

    if (data.contains("blocked_players")) {
        blocked_players_ = data["blocked_players"].get<std::vector<uint64_t>>();
    }

    // Cosmetics
    if (data.contains("cosmetics")) {
        for (const auto& [slot, cosmetic_id] : data["cosmetics"].items()) {
            cosmetics_[slot] = cosmetic_id.get<std::string>();
        }
    }

    // Re-apply bonuses after deserialization
    ApplyClassBonuses();
    ApplyRaceBonuses();
    UpdateDerivedStats();
}

// =============== Helper Serialization Methods ===============
void Player::SaveInventoryToJson(nlohmann::json& json) const {
    nlohmann::json inv_json;
    for (const auto& [item_id, count] : inventory_) {
        inv_json[item_id] = count;
    }
    json["inventory"] = inv_json;
}

void Player::LoadInventoryFromJson(const nlohmann::json& data) {
    inventory_.clear();
    if (data.contains("inventory")) {
        for (const auto& [item_id, count] : data["inventory"].items()) {
            inventory_[item_id] = count.get<int>();
        }
    }
}

void Player::SaveSkillsToJson(nlohmann::json& json) const {
    nlohmann::json skills_json;
    for (const auto& [skill_id, level] : skills_) {
        skills_json[skill_id] = level;
    }
    json["skills"] = skills_json;
}

void Player::LoadSkillsFromJson(const nlohmann::json& data) {
    skills_.clear();
    if (data.contains("skills")) {
        for (const auto& [skill_id, level] : data["skills"].items()) {
            skills_[skill_id] = level.get<int>();
        }
    }
}

void Player::SaveQuestsToJson(nlohmann::json& json) const {
    json["active_quests"] = active_quests_;
    json["completed_quests"] = completed_quests_;
}

void Player::LoadQuestsFromJson(const nlohmann::json& data) {
    if (data.contains("active_quests")) {
        active_quests_ = data["active_quests"].get<std::unordered_map<std::string, nlohmann::json>>();
    }

    if (data.contains("completed_quests")) {
        completed_quests_ = data["completed_quests"].get<std::unordered_map<std::string, nlohmann::json>>();
    }
}

void Player::SaveBuffsToJson(nlohmann::json& json) const {
    nlohmann::json buffs_json = nlohmann::json::array();
    for (const auto& [buff_id, buff] : active_buffs_) {
        nlohmann::json buff_json;
        buff_json["buff_id"] = buff_id;
        buff_json["buff_data"] = buff.buff_data;
        buff_json["duration"] = buff.duration;
        buff_json["time_remaining"] = buff.time_remaining;
        buff_json["applied_time"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            buff.applied_time.time_since_epoch()).count();

        buffs_json.push_back(buff_json);
    }

    json["active_buffs"] = buffs_json;
}

void Player::LoadBuffsFromJson(const nlohmann::json& data) {
    active_buffs_.clear();

    if (data.contains("active_buffs")) {
        for (const auto& buff_json : data["active_buffs"]) {
            ActiveBuff buff;
            buff.buff_id = buff_json["buff_id"];
            buff.buff_data = buff_json["buff_data"];
            buff.duration = buff_json["duration"];
            buff.time_remaining = buff_json["time_remaining"];

            int64_t applied_time_ms = buff_json["applied_time"];
            buff.applied_time = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(applied_time_ms));

            // Re-apply buff effects
            for (const auto& [key, value] : buff.buff_data.items()) {
                if (value.is_number()) {
                    float current_value = GetAttribute(key);
                    SetAttribute(key, current_value + value.get<float>());
                }
            }

            active_buffs_[buff.buff_id] = buff;
        }
    }
}

void Player::SaveCooldownsToJson(nlohmann::json& json) const {
    nlohmann::json cooldowns_json = nlohmann::json::array();

    for (const auto& [ability_id, cooldown] : cooldowns_) {
        nlohmann::json cd_json;
        cd_json["ability_id"] = ability_id;
        cd_json["duration"] = cooldown.duration;
        cd_json["time_remaining"] = cooldown.time_remaining;
        cd_json["start_time"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            cooldown.start_time.time_since_epoch()).count();

        cooldowns_json.push_back(cd_json);
    }

    json["cooldowns"] = cooldowns_json;
}

void Player::LoadCooldownsFromJson(const nlohmann::json& data) {
    cooldowns_.clear();

    if (data.contains("cooldowns")) {
        for (const auto& cd_json : data["cooldowns"]) {
            Cooldown cd;
            cd.ability_id = cd_json["ability_id"];
            cd.duration = cd_json["duration"];
            cd.time_remaining = cd_json["time_remaining"];

            int64_t start_time_ms = cd_json["start_time"];
            cd.start_time = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(start_time_ms));

            cooldowns_[cd.ability_id] = cd;
        }
    }
}

// =============== Player State Management ===============
void Player::Update(float delta_time) {
    // Update movement
    if (is_moving_) {
        UpdateMovement(delta_time);
    }

    // Update regeneration
    Regenerate(delta_time);

    // Update buffs and cooldowns
    UpdateBuffs(delta_time);
    UpdateCooldowns(delta_time);

    // Update session playtime
    stats_.session_playtime += delta_time;
    stats_.total_playtime += delta_time;
}

bool Player::SaveToDatabase() {
    try {
        auto& dbManager = DbManager::GetInstance();
        auto* backend = dbManager.GetBackend();
        if (!backend) {
            Logger::Error("No database backend available for player {}", GetId());
            return false;
        }
        nlohmann::json data = Serialize();
        data["username"] = username_;
        std::string data_json = data.dump();
        std::string sql = dbManager.GetSQLProvider().GetQuery("save_player_data");
        if (sql.empty()) {
            Logger::Error("Missing SQL: save_player_data");
            return false;
        }
        bool success = backend->ExecuteWithParams(sql, {
            std::to_string(GetId()), data_json, password_hash_
        });
        if (success) {
            Logger::Debug("Player {} data saved to database", GetId());
        } else {
            Logger::Error("Failed to save player {} data to database", GetId());
        }
        return success;
    } catch (const std::exception& e) {
        Logger::Error("Failed to save player {} to database: {}", GetId(), e.what());
        return false;
    }
}

bool Player::LoadFromDatabase() {
    try {
        auto& dbManager = DbManager::GetInstance();
        auto* backend = dbManager.GetBackend();
        if (!backend) {
            Logger::Error("No database backend available for player {}", GetId());
            return false;
        }
        std::string sql = dbManager.GetSQLProvider().GetQuery("load_player_data");
        if (sql.empty()) {
            Logger::Error("Missing SQL: load_player_data");
            return false;
        }
        auto result = backend->QueryWithParams(sql, { std::to_string(GetId()) });
        if (!result.empty() && result[0].contains("data")) {
            std::string data = result[0]["data"];
            nlohmann::json data_json = nlohmann::json::parse(data);
            Deserialize(data_json);
            Logger::Debug("Player {} data loaded from database", GetId());
            return true;
        }
        Logger::Warn("No data found for player {}", GetId());
        return false;
    } catch (const std::exception& e) {
        Logger::Error("Failed to load player {} from database: {}", GetId(), e.what());
        return false;
    }
}

void Player::Regenerate(float delta_time) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (IsDead()) return;
    int health_gain = static_cast<int>(attributes_.health_regen * delta_time * 10);
    if (health_gain > 0) {
        SetHealth(stats_.health + health_gain);
    }
    int mana_gain = static_cast<int>(attributes_.mana_regen * delta_time * 10);
    if (mana_gain > 0) {
        SetMana(stats_.mana + mana_gain);
    }
}

// =============== Player Actions ===============
void Player::UseSkill(const std::string& skill_id, uint64_t target_id) {
    if (IsDead() || IsCasting()) return;

    // Check if skill is known
    if (!HasSkill(skill_id)) {
        Logger::Warn("Player {} doesn't know skill {}", GetId(), skill_id);
        return;
    }

    // Check cooldown
    auto cooldown_it = cooldowns_.find(skill_id);
    if (cooldown_it != cooldowns_.end() && cooldown_it->second.time_remaining > 0) {
        Logger::Debug("Skill {} is on cooldown", skill_id);
        return;
    }

    // Set casting status
    status_ = PlayerStatus::CASTING;

    // Apply cooldown (example: 2 seconds)
    Cooldown cd;
    cd.ability_id = skill_id;
    cd.duration = 2.0f;
    cd.time_remaining = 2.0f;
    cd.start_time = std::chrono::system_clock::now();
    cooldowns_[skill_id] = cd;

    Logger::Debug("Player {} used skill {} on target {}", GetId(), skill_id, target_id);

    // Note: Actual skill effect would be handled by SkillSystem
    // skill_system_->UseSkill(GetId(), skill_id, target_id);
}

void Player::CastSpell(const std::string& spell_id, uint64_t target_id) {
    // Similar to UseSkill but with mana cost check
    if (IsDead() || IsCasting()) return;

    // Check mana cost (would come from spell data)
    int mana_cost = 10; // Example
    if (stats_.mana < mana_cost) {
        Logger::Debug("Player {} doesn't have enough mana to cast {}", GetId(), spell_id);
        return;
    }

    // Consume mana
    SetMana(stats_.mana - mana_cost);

    // Cast spell (delegate to skill system)
    UseSkill(spell_id, target_id);
}

void Player::Interact(uint64_t target_id) {
    Logger::Debug("Player {} interacting with target {}", GetId(), target_id);
}

void Player::Emote(const std::string& emote_id) {
    Logger::Debug("Player {} emotes: {}", GetId(), emote_id);
}

void Player::Chat(const std::string& message) {
    Logger::Debug("Player {} says: {}", GetId(), message);
}

// =============== Player Movement ===============
void Player::MoveTo(const glm::vec3& destination, float speed_multiplier) {
    movement_target_ = destination;
    movement_speed_multiplier_ = speed_multiplier;
    is_moving_ = true;
    status_ = PlayerStatus::MOVING;

    Logger::Debug("Player {} moving to [{:.1f}, {:.1f}, {:.1f}]", 
                  GetId(), destination.x, destination.y, destination.z);
}

void Player::StopMovement() {
    is_moving_ = false;
    if (status_ == PlayerStatus::MOVING) {
        status_ = PlayerStatus::IDLE;
    }
    Logger::Debug("Player {} stopped moving", GetId());
}

void Player::Jump() {
    // Apply vertical velocity for jump
    glm::vec3 vel = GetVelocity();
    vel.y = 5.0f; // Jump strength
    SetVelocity(vel);

    Logger::Debug("Player {} jumped", GetId());
}

void Player::ToggleSprint(bool sprinting) {
    is_sprinting_ = sprinting;
    movement_speed_multiplier_ = sprinting ? 1.5f : 1.0f;

    Logger::Debug("Player {} {}", GetId(), sprinting ? "started sprinting" : "stopped sprinting");
}

void Player::UpdateMovement(float delta_time) {
    if (!is_moving_) return;

    glm::vec3 current_pos = GetPosition();
    glm::vec3 direction = movement_target_ - current_pos;
    float distance = glm::length(direction);

    if (distance < 0.1f) {
        // Reached destination
        StopMovement();
        return;
    }

    // Normalize direction and apply speed
    direction = glm::normalize(direction);
    float move_speed = attributes_.move_speed * movement_speed_multiplier_;

    // Calculate movement
    glm::vec3 movement = direction * move_speed * delta_time;

    // Don't overshoot
    if (glm::length(movement) > distance) {
        movement = direction * distance;
        StopMovement();
    }

    // Update position
    SetPosition(current_pos + movement);

    // Update rotation to face movement direction
    if (glm::length(glm::vec2(direction.x, direction.z)) > 0.01f) {
        float angle = std::atan2(direction.x, direction.z);
        SetRotation(glm::vec3(0.0f, angle, 0.0f));
    }
}

// =============== Cooldown Management ===============
void Player::UpdateCooldowns(float delta_time) {
    for (auto it = cooldowns_.begin(); it != cooldowns_.end();) {
        it->second.time_remaining -= delta_time;

        if (it->second.time_remaining <= 0.0f) {
            it = cooldowns_.erase(it);
        } else {
            ++it;
        }
    }
}

// =============== Class and Race Bonuses ===============
void Player::ApplyClassBonuses() {
    switch (player_class_) {
        case PlayerClass::WARRIOR:
            attributes_.strength += 5;
            attributes_.vitality += 3;
            attributes_.health_regen += 0.2f;
            break;
        case PlayerClass::MAGE:
            attributes_.intelligence += 5;
            attributes_.mana_regen += 0.3f;
            attributes_.max_mana += 20;
            break;
        case PlayerClass::RANGER:
            attributes_.dexterity += 5;
            attributes_.critical_chance += 0.05f;
            attributes_.move_speed += 0.1f;
            break;
        case PlayerClass::ROGUE:
            attributes_.dexterity += 4;
            attributes_.luck += 2;
            attributes_.dodge_chance += 0.05f;
            break;
        case PlayerClass::CLERIC:
            attributes_.intelligence += 3;
            attributes_.vitality += 3;
            attributes_.health_regen += 0.3f;
            break;
        default:
            break;
    }
}

void Player::ApplyRaceBonuses() {
    switch (race_) {
        case PlayerRace::HUMAN:
            // Balanced bonuses
            attributes_.strength += 1;
            attributes_.dexterity += 1;
            attributes_.intelligence += 1;
            attributes_.vitality += 1;
            break;
        case PlayerRace::ELF:
            attributes_.dexterity += 3;
            attributes_.intelligence += 2;
            attributes_.move_speed += 0.05f;
            break;
        case PlayerRace::DWARF:
            attributes_.strength += 3;
            attributes_.vitality += 2;
            attributes_.defense += 2;
            break;
        case PlayerRace::ORC:
            attributes_.strength += 4;
            attributes_.vitality += 1;
            attributes_.attack_power += 2;
            break;
        default:
            break;
    }
}

// =============== Social Actions ===============
void Player::JoinParty(uint64_t party_id) {
    party_id_ = party_id;
    Logger::Debug("Player {} joined party {}", GetId(), party_id);
}

void Player::LeaveParty() {
    Logger::Debug("Player {} left party {}", GetId(), party_id_);
    party_id_ = 0;
}

void Player::JoinGuild(uint64_t guild_id) {
    guild_id_ = guild_id;
    Logger::Debug("Player {} joined guild {}", GetId(), guild_id);
}

void Player::LeaveGuild() {
    Logger::Debug("Player {} left guild {}", GetId(), guild_id_);
    guild_id_ = 0;
}

void Player::AddFriend(uint64_t player_id) {
    if (friends_.size() >= MAX_FRIENDS) {
        Logger::Warn("Player {} has too many friends", GetId());
        return;
    }
    
    if (std::find(friends_.begin(), friends_.end(), player_id) == friends_.end()) {
        friends_.push_back(player_id);
        Logger::Debug("Player {} added friend {}", GetId(), player_id);
    }
}

void Player::RemoveFriend(uint64_t player_id) {
    auto it = std::find(friends_.begin(), friends_.end(), player_id);
    if (it != friends_.end()) {
        friends_.erase(it);
        Logger::Debug("Player {} removed friend {}", GetId(), player_id);
    }
}

void Player::BlockPlayer(uint64_t player_id) {
    if (blocked_players_.size() >= MAX_BLOCKED_PLAYERS) {
        Logger::Warn("Player {} has too many blocked players", GetId());
        return;
    }
    
    if (std::find(blocked_players_.begin(), blocked_players_.end(), player_id) == blocked_players_.end()) {
        blocked_players_.push_back(player_id);
        Logger::Debug("Player {} blocked player {}", GetId(), player_id);
    }
}

void Player::UnblockPlayer(uint64_t player_id) {
    auto it = std::find(blocked_players_.begin(), blocked_players_.end(), player_id);
    if (it != blocked_players_.end()) {
        blocked_players_.erase(it);
        Logger::Debug("Player {} unblocked player {}", GetId(), player_id);
    }
}

// =============== Achievements ===============
void Player::AddAchievement(const std::string& achievement_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (const auto& ach : achievements_) {
        if (ach == achievement_id) {
            return;
        }
    }
    if (std::find(achievements_.begin(), achievements_.end(), achievement_id) == achievements_.end()) {
        achievements_.push_back(achievement_id);
        Logger::Debug("Player {} earned achievement {}", GetId(), achievement_id);
    }
}

bool Player::HasAchievement(const std::string& achievement_id) const {
    return std::find(achievements_.begin(), achievements_.end(), achievement_id) != achievements_.end();
}

// =============== Utility Methods ===============
std::string Player::GetClassString(PlayerClass player_class) const {
    switch (player_class) {
        case PlayerClass::WARRIOR: return "Warrior";
        case PlayerClass::MAGE: return "Mage";
        case PlayerClass::RANGER: return "Ranger";
        case PlayerClass::ROGUE: return "Rogue";
        case PlayerClass::CLERIC: return "Cleric";
        case PlayerClass::PALADIN: return "Paladin";
        case PlayerClass::DRUID: return "Druid";
        case PlayerClass::MONK: return "Monk";
        case PlayerClass::BARD: return "Bard";
        case PlayerClass::NECROMANCER: return "Necromancer";
        default: return "Unknown";
    }
}

std::string Player::GetRaceString(PlayerRace race) const {
    switch (race) {
        case PlayerRace::HUMAN: return "Human";
        case PlayerRace::ELF: return "Elf";
        case PlayerRace::DWARF: return "Dwarf";
        case PlayerRace::ORC: return "Orc";
        case PlayerRace::GNOME: return "Gnome";
        case PlayerRace::HALFLING: return "Halfling";
        case PlayerRace::DRAGONBORN: return "Dragonborn";
        case PlayerRace::TIEFLING: return "Tiefling";
        case PlayerRace::HALF_ELF: return "Half-Elf";
        case PlayerRace::HALF_ORC: return "Half-Orc";
        default: return "Unknown";
    }
}
