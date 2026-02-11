#include "game/SkillSystem.hpp"

// Static member initialization
SkillSystem& SkillSystem::GetInstance() {
    static SkillSystem instance;
    return instance;
}

// SkillRequirement serialization
nlohmann::json SkillRequirement::Serialize() const {
    nlohmann::json json;
    json["level"] = level;
    json["required_skill"] = required_skill;
    json["required_skill_level"] = required_skill_level;
    json["required_class"] = static_cast<int>(required_class);
    json["required_race"] = static_cast<int>(required_race);
    json["required_attribute_value"] = required_attribute_value;
    json["required_attribute"] = required_attribute;
    json["required_item"] = required_item;
    json["required_quest"] = required_quest;
    return json;
}

void SkillRequirement::Deserialize(const nlohmann::json& data) {
    level = data.value("level", 1);
    required_skill = data.value("required_skill", "");
    required_skill_level = data.value("required_skill_level", 0);
    required_class = static_cast<PlayerClass>(data.value("required_class", 10));
    required_race = static_cast<PlayerRace>(data.value("required_race", 0));
    required_attribute_value = data.value("required_attribute_value", 0);
    required_attribute = data.value("required_attribute", "");
    required_item = data.value("required_item", "");
    required_quest = data.value("required_quest", "");
}

// SkillEffect serialization
nlohmann::json SkillEffect::Serialize() const {
    nlohmann::json json;
    json["effect_type"] = effect_type;
    json["value"] = value;
    json["duration"] = duration;
    json["tick_interval"] = tick_interval;
    json["max_stacks"] = max_stacks;
    json["stat_modified"] = stat_modified;
    json["modifier"] = modifier;
    json["additional_effects"] = additional_effects;
    return json;
}

void SkillEffect::Deserialize(const nlohmann::json& data) {
    effect_type = data.value("effect_type", "");
    value = data.value("value", 0.0f);
    duration = data.value("duration", 0.0f);
    tick_interval = data.value("tick_interval", 0.0f);
    max_stacks = data.value("max_stacks", 1);
    stat_modified = data.value("stat_modified", "");
    modifier = data.value("modifier", 0.0f);
    
    if (data.contains("additional_effects")) {
        additional_effects = data["additional_effects"].get<std::vector<std::string>>();
    }
}

// SkillData serialization
nlohmann::json SkillData::Serialize() const {
    nlohmann::json json;
    json["id"] = id;
    json["name"] = name;
    json["description"] = description;
    json["type"] = static_cast<int>(type);
    json["target"] = static_cast<int>(target);
    json["resource"] = static_cast<int>(resource);
    
    json["resource_cost"] = resource_cost;
    json["cooldown"] = cooldown;
    json["cast_time"] = cast_time;
    json["range"] = range;
    json["area_radius"] = area_radius;
    json["duration"] = duration;
    
    json["max_level"] = max_level;
    json["required_level"] = required_level;
    
    // Serialize requirements array
    nlohmann::json req_array = nlohmann::json::array();
    for (const auto& req : requirements) {
        req_array.push_back(req.Serialize());
    }
    json["requirements"] = req_array;
    
    // Serialize effects array
    nlohmann::json effect_array = nlohmann::json::array();
    for (const auto& effect : effects) {
        effect_array.push_back(effect.Serialize());
    }
    json["effects"] = effect_array;
    
    json["icon_path"] = icon_path;
    json["animation_name"] = animation_name;
    json["sound_effect"] = sound_effect;
    json["visual_effect"] = visual_effect;
    
    json["is_toggleable"] = is_toggleable;
    json["is_channeled"] = is_channeled;
    json["is_interruptible"] = is_interruptible;
    json["can_crit"] = can_crit;
    json["scales_with_level"] = scales_with_level;
    
    json["level_scaling_factor"] = level_scaling_factor;
    json["scaling_attribute"] = scaling_attribute;
    
    return json;
}

void SkillData::Deserialize(const nlohmann::json& data) {
    id = data.value("id", "");
    name = data.value("name", "");
    description = data.value("description", "");
    type = static_cast<SkillType>(data.value("type", 0));
    target = static_cast<SkillTarget>(data.value("target", 1));
    resource = static_cast<SkillResource>(data.value("resource", 0));
    
    resource_cost = data.value("resource_cost", 0.0f);
    cooldown = data.value("cooldown", 0.0f);
    cast_time = data.value("cast_time", 0.0f);
    range = data.value("range", 0.0f);
    area_radius = data.value("area_radius", 0.0f);
    duration = data.value("duration", 0.0f);
    
    max_level = data.value("max_level", 1);
    required_level = data.value("required_level", 1);
    
    // Deserialize requirements
    if (data.contains("requirements")) {
        for (const auto& req_json : data["requirements"]) {
            SkillRequirement req;
            req.Deserialize(req_json);
            requirements.push_back(req);
        }
    }
    
    // Deserialize effects
    if (data.contains("effects")) {
        for (const auto& effect_json : data["effects"]) {
            SkillEffect effect;
            effect.Deserialize(effect_json);
            effects.push_back(effect);
        }
    }
    
    icon_path = data.value("icon_path", "");
    animation_name = data.value("animation_name", "");
    sound_effect = data.value("sound_effect", "");
    visual_effect = data.value("visual_effect", "");
    
    is_toggleable = data.value("is_toggleable", false);
    is_channeled = data.value("is_channeled", false);
    is_interruptible = data.value("is_interruptible", true);
    can_crit = data.value("can_crit", false);
    scales_with_level = data.value("scales_with_level", true);
    
    level_scaling_factor = data.value("level_scaling_factor", 1.0f);
    scaling_attribute = data.value("scaling_attribute", "");
}

// PlayerSkill serialization
nlohmann::json PlayerSkill::Serialize() const {
    nlohmann::json json;
    json["skill_id"] = skill_id;
    json["level"] = level;
    json["experience"] = experience;
    json["experience_to_next_level"] = experience_to_next_level;
    json["unlocked"] = unlocked;
    json["equipped"] = equipped;
    json["slot"] = slot;
    json["current_cooldown"] = current_cooldown;
    json["is_active"] = is_active;
    json["is_channeling"] = is_channeling;
    json["channel_time_remaining"] = channel_time_remaining;
    return json;
}

void PlayerSkill::Deserialize(const nlohmann::json& data) {
    skill_id = data.value("skill_id", "");
    level = data.value("level", 1);
    experience = data.value("experience", 0.0f);
    experience_to_next_level = data.value("experience_to_next_level", 100.0f);
    unlocked = data.value("unlocked", false);
    equipped = data.value("equipped", false);
    slot = data.value("slot", -1);
    current_cooldown = data.value("current_cooldown", 0.0f);
    is_active = data.value("is_active", false);
    is_channeling = data.value("is_channeling", false);
    channel_time_remaining = data.value("channel_time_remaining", 0.0f);
}

// PlayerSkillData serialization
nlohmann::json SkillSystem::PlayerSkillData::Serialize() const {
    nlohmann::json json;
    
    // Serialize skills map
    nlohmann::json skills_json;
    for (const auto& [skill_id, skill] : skills) {
        skills_json[skill_id] = skill.Serialize();
    }
    json["skills"] = skills_json;
    
    // Serialize unlocked trees
    nlohmann::json trees_json;
    for (const auto& [tree_id, unlocked] : unlocked_trees) {
        trees_json[tree_id] = unlocked;
    }
    json["unlocked_trees"] = trees_json;
    
    // Serialize equipped slots
    nlohmann::json slots_json;
    for (const auto& [slot, skill_id] : equipped_slots) {
        slots_json[std::to_string(slot)] = skill_id;
    }
    json["equipped_slots"] = slots_json;
    
    json["global_cooldown"] = global_cooldown;
    json["is_global_cooldown_active"] = is_global_cooldown_active;
    
    // Serialize active skills
    json["active_skills"] = active_skills;
    json["channeling_skills"] = channeling_skills;
    
    return json;
}

void SkillSystem::PlayerSkillData::Deserialize(const nlohmann::json& data) {
    // Deserialize skills
    if (data.contains("skills")) {
        for (const auto& [skill_id, skill_json] : data["skills"].items()) {
            PlayerSkill skill;
            skill.Deserialize(skill_json);
            skills[skill_id] = skill;
        }
    }
    
    // Deserialize unlocked trees
    if (data.contains("unlocked_trees")) {
        for (const auto& [tree_id, unlocked] : data["unlocked_trees"].items()) {
            unlocked_trees[tree_id] = unlocked;
        }
    }
    
    // Deserialize equipped slots
    if (data.contains("equipped_slots")) {
        for (const auto& [slot_str, skill_id] : data["equipped_slots"].items()) {
            int slot = std::stoi(slot_str);
            equipped_slots[slot] = skill_id;
        }
    }
    
    global_cooldown = data.value("global_cooldown", 0.0f);
    is_global_cooldown_active = data.value("is_global_cooldown_active", false);
    
    // Deserialize active skills
    if (data.contains("active_skills")) {
        active_skills = data["active_skills"].get<std::set<std::string>>();
    }
    
    // Deserialize channeling skills
    if (data.contains("channeling_skills")) {
        channeling_skills = data["channeling_skills"].get<std::set<std::string>>();
    }
}

// Constructor
SkillSystem::SkillSystem() 
#ifdef USE_CITUS
    : db_client_(CitusClient::GetInstance())
#else
    : db_backend_(std::make_unique<PostgreSQLBackend>())
#endif
{
    Logger::GetInstance().Log(LogLevel::INFO, "SkillSystem initialized");
}

// Skill data management
bool SkillSystem::LoadSkillData(const std::string& file_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            Logger::GetInstance().Log(LogLevel::ERROR, "Failed to open skill data file: " + file_path);
            return false;
        }
        
        nlohmann::json json_data;
        file >> json_data;
        
        if (!json_data.is_array()) {
            Logger::GetInstance().Log(LogLevel::ERROR, "Invalid skill data format");
            return false;
        }
        
        for (const auto& skill_json : json_data) {
            SkillData skill;
            skill.Deserialize(skill_json);
            skill_database_[skill.id] = skill;
        }
        
        Logger::GetInstance().Log(LogLevel::INFO, "Loaded " + std::to_string(skill_database_.size()) + " skills from " + file_path);
        return true;
    } catch (const std::exception& e) {
        Logger::GetInstance().Log(LogLevel::ERROR, "Error loading skill data: " + std::string(e.what()));
        return false;
    }
}

bool SkillSystem::SaveSkillData(const std::string& file_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        nlohmann::json json_array = nlohmann::json::array();
        
        for (const auto& [id, skill] : skill_database_) {
            json_array.push_back(skill.Serialize());
        }
        
        std::ofstream file(file_path);
        if (!file.is_open()) {
            Logger::GetInstance().Log(LogLevel::ERROR, "Failed to open skill data file for writing: " + file_path);
            return false;
        }
        
        file << json_array.dump(2);
        return true;
    } catch (const std::exception& e) {
        Logger::GetInstance().Log(LogLevel::ERROR, "Error saving skill data: " + std::string(e.what()));
        return false;
    }
}

const SkillData* SkillSystem::GetSkillData(const std::string& skill_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = skill_database_.find(skill_id);
    if (it != skill_database_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<std::string> SkillSystem::GetSkillsByType(SkillType type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> result;
    for (const auto& [id, skill] : skill_database_) {
        if (skill.type == type) {
            result.push_back(id);
        }
    }
    return result;
}

std::vector<std::string> SkillSystem::GetSkillsByClass(PlayerClass player_class) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> result;
    for (const auto& [id, skill] : skill_database_) {
        for (const auto& req : skill.requirements) {
            if (req.required_class == player_class || req.required_class == PlayerClass::ANY) {
                result.push_back(id);
                break;
            }
        }
    }
    return result;
}

// Player skill management
bool SkillSystem::LearnSkill(uint64_t player_id, const std::string& skill_id, int level) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if skill exists
    auto skill_data_it = skill_database_.find(skill_id);
    if (skill_data_it == skill_database_.end()) {
        Logger::GetInstance().Log(LogLevel::WARNING, "Skill not found: " + skill_id);
        return false;
    }
    
    // Check if player already has the skill
    auto& player_data = player_skills_[player_id];
    auto skill_it = player_data.skills.find(skill_id);
    
    if (skill_it != player_data.skills.end()) {
        // Player already has the skill, maybe upgrade it
        return UpgradeSkill(player_id, skill_id);
    }
    
    // Check requirements
    if (!MeetsSkillRequirements(player_id, skill_id)) {
        Logger::GetInstance().Log(LogLevel::WARNING, "Player " + std::to_string(player_id) + 
                                  " doesn't meet requirements for skill: " + skill_id);
        return false;
    }
    
    // Check if player has skill points
    if (!HasSkillPoint(player_id)) {
        Logger::GetInstance().Log(LogLevel::WARNING, "Player " + std::to_string(player_id) + 
                                  " has no skill points");
        return false;
    }
    
    // Learn the skill
    PlayerSkill new_skill;
    new_skill.skill_id = skill_id;
    new_skill.level = std::min(level, skill_data_it->second.max_level);
    new_skill.unlocked = true;
    
    player_data.skills[skill_id] = new_skill;
    ConsumeSkillPoint(player_id);
    
    Logger::GetInstance().Log(LogLevel::INFO, "Player " + std::to_string(player_id) + 
                              " learned skill: " + skill_id + " at level " + std::to_string(level));
    
    if (on_skill_upgraded_) {
        on_skill_upgraded_(player_id, skill_id);
    }
    
    return true;
}

bool SkillSystem::ForgetSkill(uint64_t player_id, const std::string& skill_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it == player_skills_.end()) {
        return false;
    }
    
    auto& player_data = player_data_it->second;
    auto skill_it = player_data.skills.find(skill_id);
    if (skill_it == player_data.skills.end()) {
        return false;
    }
    
    // Unequip if equipped
    if (skill_it->second.equipped) {
        UnequipSkill(player_id, skill_id);
    }
    
    // Remove from active/channeling skills
    player_data.active_skills.erase(skill_id);
    player_data.channeling_skills.erase(skill_id);
    
    // Remove the skill
    player_data.skills.erase(skill_it);
    
    // Refund skill point
    RefundSkillPoint(player_id);
    
    Logger::GetInstance().Log(LogLevel::INFO, "Player " + std::to_string(player_id) + 
                              " forgot skill: " + skill_id);
    
    return true;
}

bool SkillSystem::UpgradeSkill(uint64_t player_id, const std::string& skill_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it == player_skills_.end()) {
        return false;
    }
    
    auto& player_data = player_data_it->second;
    auto skill_it = player_data.skills.find(skill_id);
    if (skill_it == player_data.skills.end()) {
        return false;
    }
    
    auto skill_data_it = skill_database_.find(skill_id);
    if (skill_data_it == skill_database_.end()) {
        return false;
    }
    
    PlayerSkill& player_skill = skill_it->second;
    const SkillData& skill_data = skill_data_it->second;
    
    // Check if already at max level
    if (player_skill.level >= skill_data.max_level) {
        return false;
    }
    
    // Check if player has skill points
    if (!HasSkillPoint(player_id)) {
        return false;
    }
    
    // Upgrade the skill
    player_skill.level++;
    ConsumeSkillPoint(player_id);
    
    Logger::GetInstance().Log(LogLevel::INFO, "Player " + std::to_string(player_id) + 
                              " upgraded skill: " + skill_id + " to level " + std::to_string(player_skill.level));
    
    if (on_skill_upgraded_) {
        on_skill_upgraded_(player_id, skill_id);
    }
    
    return true;
}

bool SkillSystem::SetSkillLevel(uint64_t player_id, const std::string& skill_id, int level) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it == player_skills_.end()) {
        return false;
    }
    
    auto& player_data = player_data_it->second;
    auto skill_it = player_data.skills.find(skill_id);
    if (skill_it == player_data.skills.end()) {
        return false;
    }
    
    auto skill_data_it = skill_database_.find(skill_id);
    if (skill_data_it == skill_database_.end()) {
        return false;
    }
    
    int new_level = std::clamp(level, 1, skill_data_it->second.max_level);
    skill_it->second.level = new_level;
    
    return true;
}

bool SkillSystem::EquipSkill(uint64_t player_id, const std::string& skill_id, int slot) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!ValidateSkillSlot(slot)) {
        return false;
    }
    
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it == player_skills_.end()) {
        return false;
    }
    
    auto& player_data = player_data_it->second;
    auto skill_it = player_data.skills.find(skill_id);
    if (skill_it == player_data.skills.end()) {
        return false;
    }
    
    // Check if slot is already occupied
    auto slot_it = player_data.equipped_slots.find(slot);
    if (slot_it != player_data.equipped_slots.end()) {
        // Unequip the skill currently in this slot
        std::string current_skill_id = slot_it->second;
        auto current_skill_it = player_data.skills.find(current_skill_id);
        if (current_skill_it != player_data.skills.end()) {
            current_skill_it->second.equipped = false;
            current_skill_it->second.slot = -1;
        }
    }
    
    // Equip the new skill
    skill_it->second.equipped = true;
    skill_it->second.slot = slot;
    player_data.equipped_slots[slot] = skill_id;
    
    return true;
}

bool SkillSystem::UnequipSkill(uint64_t player_id, const std::string& skill_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it == player_skills_.end()) {
        return false;
    }
    
    auto& player_data = player_data_it->second;
    auto skill_it = player_data.skills.find(skill_id);
    if (skill_it == player_data.skills.end() || !skill_it->second.equipped) {
        return false;
    }
    
    int slot = skill_it->second.slot;
    
    // Remove from equipped slots
    player_data.equipped_slots.erase(slot);
    
    // Update skill
    skill_it->second.equipped = false;
    skill_it->second.slot = -1;
    
    return true;
}

// Skill usage and cooldowns
bool SkillSystem::CanUseSkill(uint64_t player_id, const std::string& skill_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it == player_skills_.end()) {
        return false;
    }
    
    const auto& player_data = player_data_it->second;
    
    // Check if skill exists
    auto skill_data_it = skill_database_.find(skill_id);
    if (skill_data_it == skill_database_.end()) {
        return false;
    }
    
    // Check if player has the skill
    auto skill_it = player_data.skills.find(skill_id);
    if (skill_it == player_data.skills.end() || !skill_it->second.unlocked) {
        return false;
    }
    
    // Check cooldowns
    if (skill_it->second.current_cooldown > 0.0f) {
        return false;
    }
    
    // Check global cooldown
    if (player_data.is_global_cooldown_active && skill_data_it->second.type == SkillType::ACTIVE) {
        return false;
    }
    
    // Check if skill is toggleable and already active
    if (skill_data_it->second.is_toggleable && skill_it->second.is_active) {
        return true; // Can toggle off
    }
    
    // Check if player is already channeling
    if (!player_data.channeling_skills.empty() && skill_id != *player_data.channeling_skills.begin()) {
        return false; // Can only channel one skill at a time
    }
    
    return true;
}

bool SkillSystem::UseSkill(uint64_t player_id, const std::string& skill_id, uint64_t target_id, const glm::vec3& target_position) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!CanUseSkill(player_id, skill_id)) {
        return false;
    }
    
    auto& player_data = player_skills_[player_id];
    auto skill_data_it = skill_database_.find(skill_id);
    if (skill_data_it == skill_database_.end()) {
        return false;
    }
    
    auto& player_skill = player_data.skills[skill_id];
    const SkillData& skill_data = skill_data_it->second;
    
    // Handle toggleable skills
    if (skill_data.is_toggleable) {
        player_skill.is_active = !player_skill.is_active;
        
        if (player_skill.is_active) {
            player_data.active_skills.insert(skill_id);
        } else {
            player_data.active_skills.erase(skill_id);
        }
        
        return true;
    }
    
    // Handle channeled skills
    if (skill_data.is_channeled) {
        player_skill.is_channeling = true;
        player_skill.channel_time_remaining = skill_data.duration;
        player_data.channeling_skills.insert(skill_id);
    }
    
    // Start cooldown
    float cooldown = CalculateCooldown(player_id, skill_id);
    player_skill.current_cooldown = cooldown;
    player_skill.last_used_time = std::chrono::steady_clock::now();
    
    // Start global cooldown for active skills
    if (skill_data.type == SkillType::ACTIVE) {
        StartGlobalCooldown(player_id, GLOBAL_COOLDOWN_DURATION);
    }
    
    // Apply skill effects
    ApplySkillEffects(player_id, target_id, skill_id);
    
    // Handle area/cone effects
    if (skill_data.target == SkillTarget::AREA_OF_EFFECT) {
        ApplyAreaEffect(player_id, target_position, skill_data.area_radius, skill_id);
    } else if (skill_data.target == SkillTarget::CONE) {
        // Calculate direction from player position (would need player position)
        glm::vec3 direction(1.0f, 0.0f, 0.0f); // Default direction
        ApplyConeEffect(player_id, direction, 45.0f, skill_data.range, skill_id);
    }
    
    Logger::GetInstance().Log(LogLevel::INFO, "Player " + std::to_string(player_id) + 
                              " used skill: " + skill_id);
    
    if (on_skill_used_) {
        on_skill_used_(player_id, target_id, skill_id);
    }
    
    if (on_skill_cooldown_start_) {
        on_skill_cooldown_start_(player_id, skill_id, cooldown);
    }
    
    return true;
}

bool SkillSystem::InterruptSkill(uint64_t player_id, const std::string& skill_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it == player_skills_.end()) {
        return false;
    }
    
    auto& player_data = player_data_it->second;
    
    // Check if skill is being channeled
    if (!player_data.channeling_skills.contains(skill_id)) {
        return false;
    }
    
    auto skill_it = player_data.skills.find(skill_id);
    if (skill_it == player_data.skills.end()) {
        return false;
    }
    
    // Stop channeling
    skill_it->second.is_channeling = false;
    skill_it->second.channel_time_remaining = 0.0f;
    player_data.channeling_skills.erase(skill_id);
    
    // Apply interrupt cooldown (if any)
    skill_it->second.current_cooldown = std::max(1.0f, skill_it->second.current_cooldown * 0.5f);
    
    return true;
}

bool SkillSystem::ToggleSkill(uint64_t player_id, const std::string& skill_id) {
    return UseSkill(player_id, skill_id);
}

// Query methods
int SkillSystem::GetSkillLevel(uint64_t player_id, const std::string& skill_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it == player_skills_.end()) {
        return 0;
    }
    
    const auto& player_data = player_data_it->second;
    auto skill_it = player_data.skills.find(skill_id);
    if (skill_it == player_data.skills.end()) {
        return 0;
    }
    
    return skill_it->second.level;
}

bool SkillSystem::HasSkill(uint64_t player_id, const std::string& skill_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it == player_skills_.end()) {
        return false;
    }
    
    const auto& player_data = player_data_it->second;
    return player_data.skills.find(skill_id) != player_data.skills.end();
}

bool SkillSystem::IsSkillEquipped(uint64_t player_id, const std::string& skill_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it == player_skills_.end()) {
        return false;
    }
    
    const auto& player_data = player_data_it->second;
    auto skill_it = player_data.skills.find(skill_id);
    if (skill_it == player_data.skills.end()) {
        return false;
    }
    
    return skill_it->second.equipped;
}

float SkillSystem::GetSkillCooldownRemaining(uint64_t player_id, const std::string& skill_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it == player_skills_.end()) {
        return 0.0f;
    }
    
    const auto& player_data = player_data_it->second;
    auto skill_it = player_data.skills.find(skill_id);
    if (skill_it == player_data.skills.end()) {
        return 0.0f;
    }
    
    return skill_it->second.current_cooldown;
}

std::vector<PlayerSkill> SkillSystem::GetPlayerSkills(uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<PlayerSkill> result;
    
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it != player_skills_.end()) {
        for (const auto& [id, skill] : player_data_it->second.skills) {
            result.push_back(skill);
        }
    }
    
    return result;
}

std::vector<PlayerSkill> SkillSystem::GetEquippedSkills(uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<PlayerSkill> result;
    
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it != player_skills_.end()) {
        for (const auto& [slot, skill_id] : player_data_it->second.equipped_slots) {
            auto skill_it = player_data_it->second.skills.find(skill_id);
            if (skill_it != player_data_it->second.skills.end()) {
                result.push_back(skill_it->second);
            }
        }
    }
    
    return result;
}

// Skill effects and calculations
float SkillSystem::CalculateSkillValue(uint64_t player_id, const std::string& skill_id, const std::string& effect_type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto skill_data_it = skill_database_.find(skill_id);
    if (skill_data_it == skill_database_.end()) {
        return 0.0f;
    }
    
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it == player_skills_.end()) {
        return 0.0f;
    }
    
    const SkillData& skill_data = skill_data_it->second;
    const auto& player_data = player_data_it->second;
    
    auto skill_it = player_data.skills.find(skill_id);
    if (skill_it == player_data.skills.end()) {
        return 0.0f;
    }
    
    int skill_level = skill_it->second.level;
    
    // Find the effect
    for (const auto& effect : skill_data.effects) {
        if (effect.effect_type == effect_type) {
            float base_value = effect.value;
            
            // Apply level scaling
            if (skill_data.scales_with_level) {
                base_value *= (1.0f + (skill_level - 1) * skill_data.level_scaling_factor);
            }
            
            return base_value;
        }
    }
    
    return 0.0f;
}

float SkillSystem::CalculateResourceCost(uint64_t player_id, const std::string& skill_id) const {
    auto skill_data_it = skill_database_.find(skill_id);
    if (skill_data_it == skill_database_.end()) {
        return 0.0f;
    }
    
    return skill_data_it->second.resource_cost;
}

float SkillSystem::CalculateCooldown(uint64_t player_id, const std::string& skill_id) const {
    auto skill_data_it = skill_database_.find(skill_id);
    if (skill_data_it == skill_database_.end()) {
        return 0.0f;
    }
    
    return skill_data_it->second.cooldown;
}

// Update methods
void SkillSystem::UpdatePlayerCooldowns(uint64_t player_id, float delta_time) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it == player_skills_.end()) {
        return;
    }
    
    auto& player_data = player_data_it->second;
    
    // Update skill cooldowns
    for (auto& [skill_id, player_skill] : player_data.skills) {
        if (player_skill.current_cooldown > 0.0f) {
            player_skill.current_cooldown -= delta_time;
            if (player_skill.current_cooldown < 0.0f) {
                player_skill.current_cooldown = 0.0f;
            }
        }
    }
    
    // Update global cooldown
    UpdateGlobalCooldown(player_id, delta_time);
}

void SkillSystem::UpdateActiveSkills(uint64_t player_id, float delta_time) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it == player_skills_.end()) {
        return;
    }
    
    auto& player_data = player_data_it->second;
    
    // Update active (toggleable) skills
    for (const auto& skill_id : player_data.active_skills) {
        auto skill_it = player_data.skills.find(skill_id);
        if (skill_it != player_data.skills.end()) {
            // Apply continuous effects of active skills
            // This would be game-specific logic
        }
    }
}

void SkillSystem::UpdateChanneledSkills(uint64_t player_id, float delta_time) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it == player_skills_.end()) {
        return;
    }
    
    auto& player_data = player_data_it->second;
    
    // Update channeled skills
    std::vector<std::string> finished_channeling;
    
    for (const auto& skill_id : player_data.channeling_skills) {
        auto skill_it = player_data.skills.find(skill_id);
        if (skill_it != player_data.skills.end()) {
            auto& player_skill = skill_it->second;
            
            player_skill.channel_time_remaining -= delta_time;
            
            if (player_skill.channel_time_remaining <= 0.0f) {
                finished_channeling.push_back(skill_id);
                player_skill.is_channeling = false;
                player_skill.channel_time_remaining = 0.0f;
                
                // Channeling completed - apply final effects
                // This would be game-specific logic
            }
        }
    }
    
    // Remove finished channeling skills
    for (const auto& skill_id : finished_channeling) {
        player_data.channeling_skills.erase(skill_id);
    }
}

// Skill requirements checking
bool SkillSystem::MeetsSkillRequirements(uint64_t player_id, const std::string& skill_id) const {
    auto skill_data_it = skill_database_.find(skill_id);
    if (skill_data_it == skill_database_.end()) {
        return false;
    }
    
    const SkillData& skill_data = skill_data_it->second;
    
    // Check level requirement
    // This would require access to player's level from PlayerEntity
    // For now, we'll assume the requirement is met
    
    // Check skill requirements
    for (const auto& req : skill_data.requirements) {
        if (!req.required_skill.empty()) {
            int required_level = GetSkillLevel(player_id, req.required_skill);
            if (required_level < req.required_skill_level) {
                return false;
            }
        }
    }
    
    return true;
}

std::vector<std::string> SkillSystem::GetMissingRequirements(uint64_t player_id, const std::string& skill_id) const {
    std::vector<std::string> missing;
    
    auto skill_data_it = skill_database_.find(skill_id);
    if (skill_data_it == skill_database_.end()) {
        missing.push_back("Skill not found");
        return missing;
    }
    
    const SkillData& skill_data = skill_data_it->second;
    
    // Check requirements
    for (const auto& req : skill_data.requirements) {
        if (!req.required_skill.empty()) {
            int required_level = GetSkillLevel(player_id, req.required_skill);
            if (required_level < req.required_skill_level) {
                missing.push_back("Requires " + req.required_skill + " level " + 
                                 std::to_string(req.required_skill_level));
            }
        }
        
        // Add other requirement checks as needed
    }
    
    return missing;
}

// Skill trees and specializations
bool SkillSystem::UnlockSkillTree(uint64_t player_id, const std::string& tree_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& player_data = player_skills_[player_id];
    
    if (player_data.unlocked_trees.contains(tree_id)) {
        return false; // Already unlocked
    }
    
    // Check requirements for unlocking the tree
    // This would be game-specific logic
    
    player_data.unlocked_trees[tree_id] = true;
    return true;
}

bool SkillSystem::IsSkillTreeUnlocked(uint64_t player_id, const std::string& tree_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it == player_skills_.end()) {
        return false;
    }
    
    const auto& player_data = player_data_it->second;
    auto it = player_data.unlocked_trees.find(tree_id);
    return it != player_data.unlocked_trees.end() && it->second;
}

std::vector<std::string> SkillSystem::GetUnlockedSkillTrees(uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> result;
    
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it != player_skills_.end()) {
        for (const auto& [tree_id, unlocked] : player_data_it->second.unlocked_trees) {
            if (unlocked) {
                result.push_back(tree_id);
            }
        }
    }
    
    return result;
}

// Serialization
bool SkillSystem::LoadPlayerSkills(uint64_t player_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
#ifdef USE_CITUS
    return LoadPlayerSkillsFromDatabase(player_id);
#else
    // Load from local JSON file or database
    std::string file_path = "data/players/" + std::to_string(player_id) + "/skills.json";
    
    try {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            Logger::GetInstance().Log(LogLevel::WARNING, "No skill data found for player " + 
                                      std::to_string(player_id) + ", creating new");
            return true; // No existing data is not an error
        }
        
        nlohmann::json json_data;
        file >> json_data;
        
        PlayerSkillData player_data;
        player_data.Deserialize(json_data);
        
        player_skills_[player_id] = player_data;
        
        Logger::GetInstance().Log(LogLevel::INFO, "Loaded skills for player " + 
                                  std::to_string(player_id));
        return true;
    } catch (const std::exception& e) {
        Logger::GetInstance().Log(LogLevel::ERROR, "Error loading player skills: " + 
                                  std::string(e.what()));
        return false;
    }
#endif
}

bool SkillSystem::SavePlayerSkills(uint64_t player_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
#ifdef USE_CITUS
    return SavePlayerSkillsToDatabase(player_id);
#else
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it == player_skills_.end()) {
        return false;
    }
    
    std::string file_path = "data/players/" + std::to_string(player_id) + "/skills.json";
    
    try {
        // Create directory if it doesn't exist
        std::filesystem::create_directories(std::filesystem::path(file_path).parent_path());
        
        std::ofstream file(file_path);
        if (!file.is_open()) {
            Logger::GetInstance().Log(LogLevel::ERROR, "Failed to open skill file for writing: " + file_path);
            return false;
        }
        
        nlohmann::json json_data = player_data_it->second.Serialize();
        file << json_data.dump(2);
        
        Logger::GetInstance().Log(LogLevel::INFO, "Saved skills for player " + 
                                  std::to_string(player_id));
        return true;
    } catch (const std::exception& e) {
        Logger::GetInstance().Log(LogLevel::ERROR, "Error saving player skills: " + 
                                  std::string(e.what()));
        return false;
    }
#endif
}

nlohmann::json SkillSystem::SerializePlayerSkills(uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it == player_skills_.end()) {
        return nlohmann::json();
    }
    
    return player_data_it->second.Serialize();
}

bool SkillSystem::DeserializePlayerSkills(uint64_t player_id, const nlohmann::json& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (data.is_null()) {
        return false;
    }
    
    PlayerSkillData player_data;
    try {
        player_data.Deserialize(data);
        player_skills_[player_id] = player_data;
        return true;
    } catch (const std::exception& e) {
        Logger::GetInstance().Log(LogLevel::ERROR, "Error deserializing player skills: " + 
                                  std::string(e.what()));
        return false;
    }
}

// Skill effects application
void SkillSystem::ApplySkillEffects(uint64_t caster_id, uint64_t target_id, const std::string& skill_id) {
    auto skill_data_it = skill_database_.find(skill_id);
    if (skill_data_it == skill_database_.end()) {
        return;
    }
    
    const SkillData& skill_data = skill_data_it->second;
    
    for (const auto& effect : skill_data.effects) {
        if (effect.effect_type == "damage") {
            ApplyDamageEffect(caster_id, target_id, effect);
        } else if (effect.effect_type == "heal") {
            ApplyHealingEffect(caster_id, target_id, effect);
        } else if (effect.effect_type == "buff") {
            ApplyBuffEffect(caster_id, target_id, effect);
        } else if (effect.effect_type == "summon") {
            // ApplySummonEffect would need position information
        }
    }
}

void SkillSystem::ApplyAreaEffect(uint64_t caster_id, const glm::vec3& center, float radius, const std::string& skill_id) {
    // This would find all entities in the area and apply effects to them
    // Implementation depends on the game's entity management system
    Logger::GetInstance().Log(LogLevel::INFO, "Applying area effect from skill " + skill_id + 
                              " at position " + std::to_string(center.x) + ", " + 
                              std::to_string(center.y) + ", " + std::to_string(center.z));
}

void SkillSystem::ApplyConeEffect(uint64_t caster_id, const glm::vec3& direction, float angle, float range, const std::string& skill_id) {
    // This would find all entities in the cone and apply effects to them
    Logger::GetInstance().Log(LogLevel::INFO, "Applying cone effect from skill " + skill_id);
}

// Helper methods
bool SkillSystem::ValidateSkillSlot(int slot) const {
    return slot >= 0 && slot < MAX_EQUIPPED_SKILLS;
}

bool SkillSystem::HasSkillPoint(uint64_t player_id) const {
    // This would check the player's available skill points
    // For now, we'll assume players always have skill points
    return true;
}

void SkillSystem::ConsumeSkillPoint(uint64_t player_id) {
    // This would decrement the player's skill points
    // Implementation depends on how skill points are stored
}

void SkillSystem::RefundSkillPoint(uint64_t player_id) {
    // This would increment the player's skill points
    // Implementation depends on how skill points are stored
}

void SkillSystem::StartGlobalCooldown(uint64_t player_id, float duration) {
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it == player_skills_.end()) {
        return;
    }
    
    auto& player_data = player_data_it->second;
    player_data.global_cooldown = duration;
    player_data.is_global_cooldown_active = true;
}

void SkillSystem::UpdateGlobalCooldown(uint64_t player_id, float delta_time) {
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it == player_skills_.end()) {
        return;
    }
    
    auto& player_data = player_data_it->second;
    
    if (player_data.is_global_cooldown_active) {
        player_data.global_cooldown -= delta_time;
        
        if (player_data.global_cooldown <= 0.0f) {
            player_data.global_cooldown = 0.0f;
            player_data.is_global_cooldown_active = false;
        }
    }
}

bool SkillSystem::IsGlobalCooldownActive(uint64_t player_id) const {
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it == player_skills_.end()) {
        return false;
    }
    
    return player_data_it->second.is_global_cooldown_active;
}

// Effect application helpers
void SkillSystem::ApplyDamageEffect(uint64_t caster_id, uint64_t target_id, const SkillEffect& effect) {
    float damage = effect.value;
    
    // Apply skill level scaling
    // This would be calculated based on caster's skill level
    
    Logger::GetInstance().Log(LogLevel::INFO, "Applying damage effect: " + 
                              std::to_string(damage) + " damage from " + 
                              std::to_string(caster_id) + " to " + std::to_string(target_id));
    
    if (on_skill_damage_) {
        on_skill_damage_(caster_id, target_id, effect.effect_type, damage);
    }
}

void SkillSystem::ApplyHealingEffect(uint64_t caster_id, uint64_t target_id, const SkillEffect& effect) {
    float healing = effect.value;
    
    Logger::GetInstance().Log(LogLevel::INFO, "Applying healing effect: " + 
                              std::to_string(healing) + " healing from " + 
                              std::to_string(caster_id) + " to " + std::to_string(target_id));
    
    if (on_skill_healing_) {
        on_skill_healing_(caster_id, target_id, effect.effect_type, healing);
    }
}

void SkillSystem::ApplyBuffEffect(uint64_t caster_id, uint64_t target_id, const SkillEffect& effect) {
    Logger::GetInstance().Log(LogLevel::INFO, "Applying buff effect: " + effect.effect_type + 
                              " from " + std::to_string(caster_id) + " to " + 
                              std::to_string(target_id));
}

// Database operations
bool SkillSystem::LoadSkillDataFromDatabase() {
#ifdef USE_CITUS
    try {
        auto result = db_client_.ExecuteQuery("SELECT skill_data FROM skill_database");
        
        // Parse and load skill data from database
        // This is a simplified example
        
        Logger::GetInstance().Log(LogLevel::INFO, "Loaded skill data from database");
        return true;
    } catch (const std::exception& e) {
        Logger::GetInstance().Log(LogLevel::ERROR, "Error loading skill data from database: " + 
                                  std::string(e.what()));
        return false;
    }
#else
    return true; // Not implemented for non-Citus
#endif
}

bool SkillSystem::SaveSkillDataToDatabase() {
#ifdef USE_CITUS
    try {
        // Save skill data to database
        // This would serialize all skill data and store it
        
        Logger::GetInstance().Log(LogLevel::INFO, "Saved skill data to database");
        return true;
    } catch (const std::exception& e) {
        Logger::GetInstance().Log(LogLevel::ERROR, "Error saving skill data to database: " + 
                                  std::string(e.what()));
        return false;
    }
#else
    return true; // Not implemented for non-Citus
#endif
}

bool SkillSystem::LoadPlayerSkillsFromDatabase(uint64_t player_id) {
#ifdef USE_CITUS
    try {
        std::string query = "SELECT skill_data FROM player_skills WHERE player_id = " + 
                           std::to_string(player_id);
        auto result = db_client_.ExecuteQuery(query);
        
        if (result.empty()) {
            // No existing data, create new entry
            return true;
        }
        
        // Parse and deserialize skill data
        nlohmann::json json_data = nlohmann::json::parse(result[0]["skill_data"]);
        
        PlayerSkillData player_data;
        player_data.Deserialize(json_data);
        
        player_skills_[player_id] = player_data;
        
        Logger::GetInstance().Log(LogLevel::INFO, "Loaded skills for player " + 
                                  std::to_string(player_id) + " from database");
        return true;
    } catch (const std::exception& e) {
        Logger::GetInstance().Log(LogLevel::ERROR, "Error loading player skills from database: " + 
                                  std::string(e.what()));
        return false;
    }
#else
    return LoadPlayerSkills(player_id); // Fallback to local storage
#endif
}

bool SkillSystem::SavePlayerSkillsToDatabase(uint64_t player_id) {
#ifdef USE_CITUS
    auto player_data_it = player_skills_.find(player_id);
    if (player_data_it == player_skills_.end()) {
        return false;
    }
    
    try {
        nlohmann::json json_data = player_data_it->second.Serialize();
        std::string json_str = json_data.dump();
        
        std::string query = "INSERT INTO player_skills (player_id, skill_data) VALUES (" +
                           std::to_string(player_id) + ", '" + json_str + 
                           "') ON CONFLICT (player_id) DO UPDATE SET skill_data = EXCLUDED.skill_data";
        
        db_client_.ExecuteQuery(query);
        
        Logger::GetInstance().Log(LogLevel::INFO, "Saved skills for player " + 
                                  std::to_string(player_id) + " to database");
        return true;
    } catch (const std::exception& e) {
        Logger::GetInstance().Log(LogLevel::ERROR, "Error saving player skills to database: " + 
                                  std::string(e.what()));
        return false;
    }
#else
    return SavePlayerSkills(player_id); // Fallback to local storage
#endif
}
