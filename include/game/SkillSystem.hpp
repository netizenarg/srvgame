#pragma once

#include <algorithm>
#include <cmath>
#include <chrono>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <string>
#include <set>

#include <nlohmann/json.hpp>

#include "logging/Logger.hpp"
#include "config/ConfigManager.hpp"
#include "database/DbManager.hpp"

#include "game/PlayerTypes.hpp"

enum class SkillType {
    ACTIVE = 0,
    PASSIVE = 1,
    AURA = 2,
    BUFF = 3,
    DEBUFF = 4,
    SUMMON = 5,
    TRANSFORMATION = 6,
    UTILITY = 7,
    PROFESSION = 8
};

enum class SkillTarget {
    SELF = 0,
    SINGLE_TARGET = 1,
    AREA_OF_EFFECT = 2,
    CONE = 3,
    PROJECTILE = 4,
    CHAIN = 5,
    GROUND_TARGET = 6,
    PET = 7
};

enum class SkillResource {
    MANA = 0,
    ENERGY = 1,
    RAGE = 2,
    FOCUS = 3,
    RUNES = 4,
    COMBO_POINTS = 5,
    SOUL_SHARDS = 6,
    HOLY_POWER = 7,
    CHI = 8,
    AMMUNITION = 9,
    NONE = 10
};

struct SkillRequirement {
    int level = 1;
    std::string required_skill;
    int required_skill_level = 0;
    PlayerClass required_class = PlayerClass::ANY;
    PlayerRace required_race = PlayerRace::HUMAN;
    int required_attribute_value = 0;
    std::string required_attribute;
    std::string required_item;
    std::string required_quest;
    
    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

struct SkillEffect {
    std::string effect_type;  // "damage", "heal", "buff", "debuff", "summon", "teleport", etc.
    float value = 0.0f;
    float duration = 0.0f;
    float tick_interval = 0.0f;
    int max_stacks = 1;
    std::string stat_modified;
    float modifier = 0.0f;
    std::vector<std::string> additional_effects;
    
    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

struct SkillData {
    std::string id;
    std::string name;
    std::string description;
    SkillType type = SkillType::ACTIVE;
    SkillTarget target = SkillTarget::SINGLE_TARGET;
    SkillResource resource = SkillResource::MANA;
    
    float resource_cost = 0.0f;
    float cooldown = 0.0f;
    float cast_time = 0.0f;
    float range = 0.0f;
    float area_radius = 0.0f;
    float duration = 0.0f;
    
    int max_level = 1;
    int required_level = 1;
    std::vector<SkillRequirement> requirements;
    std::vector<SkillEffect> effects;
    
    std::string icon_path;
    std::string animation_name;
    std::string sound_effect;
    std::string visual_effect;
    
    bool is_toggleable = false;
    bool is_channeled = false;
    bool is_interruptible = true;
    bool can_crit = false;
    bool scales_with_level = true;
    
    float level_scaling_factor = 1.0f;
    std::string scaling_attribute;
    
    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

struct PlayerSkill {
    std::string skill_id;
    int level = 1;
    float experience = 0.0f;
    float experience_to_next_level = 100.0f;
    bool unlocked = false;
    bool equipped = false;
    int slot = -1;
    
    // Cooldown tracking
    float current_cooldown = 0.0f;
    std::chrono::steady_clock::time_point last_used_time;
    
    // For toggleable/channeled skills
    bool is_active = false;
    bool is_channeling = false;
    float channel_time_remaining = 0.0f;
    
    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

class SkillSystem {
public:
    static SkillSystem& GetInstance();
    
    // Skill data management
    bool LoadSkillData(const std::string& file_path);
    bool SaveSkillData(const std::string& file_path);
    const SkillData* GetSkillData(const std::string& skill_id) const;
    std::vector<std::string> GetSkillsByType(SkillType type) const;
    std::vector<std::string> GetSkillsByClass(PlayerClass player_class) const;
    
    // Player skill management
    bool LearnSkill(uint64_t player_id, const std::string& skill_id, int level = 1);
    bool ForgetSkill(uint64_t player_id, const std::string& skill_id);
    bool UpgradeSkill(uint64_t player_id, const std::string& skill_id);
    bool SetSkillLevel(uint64_t player_id, const std::string& skill_id, int level);
    bool EquipSkill(uint64_t player_id, const std::string& skill_id, int slot);
    bool UnequipSkill(uint64_t player_id, const std::string& skill_id);
    
    // Skill usage and cooldowns
    bool CanUseSkill(uint64_t player_id, const std::string& skill_id) const;
    bool UseSkill(uint64_t player_id, const std::string& skill_id, uint64_t target_id = 0, const glm::vec3& target_position = glm::vec3(0.0f));
    bool InterruptSkill(uint64_t player_id, const std::string& skill_id);
    bool ToggleSkill(uint64_t player_id, const std::string& skill_id);
    
    // Query methods
    int GetSkillLevel(uint64_t player_id, const std::string& skill_id) const;
    bool HasSkill(uint64_t player_id, const std::string& skill_id) const;
    bool IsSkillEquipped(uint64_t player_id, const std::string& skill_id) const;
    float GetSkillCooldownRemaining(uint64_t player_id, const std::string& skill_id) const;
    std::vector<PlayerSkill> GetPlayerSkills(uint64_t player_id) const;
    std::vector<PlayerSkill> GetEquippedSkills(uint64_t player_id) const;
    
    // Skill effects and calculations
    float CalculateSkillValue(uint64_t player_id, const std::string& skill_id, const std::string& effect_type) const;
    float CalculateResourceCost(uint64_t player_id, const std::string& skill_id) const;
    float CalculateCooldown(uint64_t player_id, const std::string& skill_id) const;
    
    // Update methods
    void UpdatePlayerCooldowns(uint64_t player_id, float delta_time);
    void UpdateActiveSkills(uint64_t player_id, float delta_time);
    void UpdateChanneledSkills(uint64_t player_id, float delta_time);
    
    // Skill requirements checking
    bool MeetsSkillRequirements(uint64_t player_id, const std::string& skill_id) const;
    std::vector<std::string> GetMissingRequirements(uint64_t player_id, const std::string& skill_id) const;
    
    // Skill trees and specializations
    bool UnlockSkillTree(uint64_t player_id, const std::string& tree_id);
    bool IsSkillTreeUnlocked(uint64_t player_id, const std::string& tree_id) const;
    std::vector<std::string> GetUnlockedSkillTrees(uint64_t player_id) const;
    
    // Serialization
    bool LoadPlayerSkills(uint64_t player_id);
    bool SavePlayerSkills(uint64_t player_id);
    nlohmann::json SerializePlayerSkills(uint64_t player_id) const;
    bool DeserializePlayerSkills(uint64_t player_id, const nlohmann::json& data);
    
    // Skill effects application (called by UseSkill)
    void ApplySkillEffects(uint64_t caster_id, uint64_t target_id, const std::string& skill_id);
    void ApplyAreaEffect(uint64_t caster_id, const glm::vec3& center, float radius, const std::string& skill_id);
    void ApplyConeEffect(uint64_t caster_id, const glm::vec3& direction, float angle, float range, const std::string& skill_id);
    
private:
    SkillSystem();
    ~SkillSystem() = default;
    
    struct PlayerSkillData {
        std::unordered_map<std::string, PlayerSkill> skills;
        std::unordered_map<std::string, bool> unlocked_trees;
        std::unordered_map<int, std::string> equipped_slots;  // slot -> skill_id
        float global_cooldown = 0.0f;
        bool is_global_cooldown_active = false;
        
        // Active/channeled skills tracking
        std::set<std::string> active_skills;
        std::set<std::string> channeling_skills;
        
        nlohmann::json Serialize() const;
        void Deserialize(const nlohmann::json& data);
    };
    
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, PlayerSkillData> player_skills_;
    std::unordered_map<std::string, SkillData> skill_database_;
    
#ifdef USE_CITUS
    CitusClient& db_client_;
#else
    std::unique_ptr<PostgreSQLBackend> db_backend_;
#endif
    
    // Helper methods
    bool ValidateSkillSlot(int slot) const;
    bool HasSkillPoint(uint64_t player_id) const;
    void ConsumeSkillPoint(uint64_t player_id);
    void RefundSkillPoint(uint64_t player_id);
    void StartGlobalCooldown(uint64_t player_id, float duration);
    void UpdateGlobalCooldown(uint64_t player_id, float delta_time);
    bool IsGlobalCooldownActive(uint64_t player_id) const;
    
    // Effect application helpers
    void ApplyDamageEffect(uint64_t caster_id, uint64_t target_id, const SkillEffect& effect);
    void ApplyHealingEffect(uint64_t caster_id, uint64_t target_id, const SkillEffect& effect);
    void ApplyBuffEffect(uint64_t caster_id, uint64_t target_id, const SkillEffect& effect);
    void ApplySummonEffect(uint64_t caster_id, const SkillEffect& effect, const glm::vec3& position);
    void ApplyTeleportEffect(uint64_t caster_id, const glm::vec3& target_position);
    
    // Database operations
    bool LoadSkillDataFromDatabase();
    bool SaveSkillDataToDatabase();
    bool LoadPlayerSkillsFromDatabase(uint64_t player_id);
    bool SavePlayerSkillsToDatabase(uint64_t player_id);
    
    // Constants
    static constexpr int MAX_SKILL_LEVEL = 100;
    static constexpr int MAX_EQUIPPED_SKILLS = 12;
    static constexpr int SKILL_POINTS_PER_LEVEL = 1;
    static constexpr float GLOBAL_COOLDOWN_DURATION = 1.5f;
    
    // Callbacks for skill effects (can be overridden by game-specific logic)
    std::function<void(uint64_t, uint64_t, const std::string&)> on_skill_used_;
    std::function<void(uint64_t, const std::string&, float)> on_skill_cooldown_start_;
    std::function<void(uint64_t, const std::string&)> on_skill_upgraded_;
    std::function<void(uint64_t, uint64_t, const std::string&, float)> on_skill_damage_;
    std::function<void(uint64_t, uint64_t, const std::string&, float)> on_skill_healing_;
};
