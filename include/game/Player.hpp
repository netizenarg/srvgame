#pragma once

#include <algorithm>
#include <cmath>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

#include "logging/Logger.hpp"
#include "config/ConfigManager.hpp"
#include "database/DbManager.hpp"

#include "game/GameEntity.hpp"
#include "game/InventorySystem.hpp"
#include "game/SkillSystem.hpp"
#include "game/QuestManager.hpp"

class InventorySystem;
class SkillSystem;

struct PlayerAttributes {
    int strength = 10;        // Physical power
    int dexterity = 10;       // Agility and precision
    int intelligence = 10;    // Magical power
    int vitality = 10;        // Health and endurance
    int luck = 5;             // Critical and rare chances

    // Derived stats
    int attack_power = 10;
    int defense = 5;
    int max_mana = 100;
    float critical_chance = 0.05f;
    float critical_damage = 1.5f;
    float move_speed = 1.0f;
    float attack_speed = 1.0f;
    float health_regen = 0.1f;
    float mana_regen = 0.2f;
    float dodge_chance = 0.05f;
    float block_chance = 0.03f;
    float parry_chance = 0.02f;

    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

struct PlayerStats {
    int health = 100;
    int max_health = 100;
    int mana = 100;
    int max_mana = 100;
    int stamina = 100;
    int max_stamina = 100;

    int level = 1;
    int64_t experience = 0;
    int64_t experience_to_next_level = 100;

    int score = 0;
    int64_t currency_gold = 100;
    int64_t currency_gems = 10;
    int64_t honor_points = 0;
    int64_t pvp_rating = 1500;

    int skill_points = 0;
    int talent_points = 0;
    int reputation_level = 0;

    float attack_damage = 10.0f;
    float attack_speed = 1.0f;
    float attack_range = 2.0f;
    float critical_chance = 0.05f;
    float critical_damage = 1.5f;

    int kills = 0;
    int deaths = 0;
    int assists = 0;
    int quests_completed = 0;
    int dungeons_completed = 0;
    int raids_completed = 0;

    float total_playtime = 0.0f;
    float session_playtime = 0.0f;

    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

struct PlayerEquipment {
    std::string head;
    std::string chest;
    std::string legs;
    std::string feet;
    std::string hands;
    std::string main_hand;
    std::string off_hand;
    std::string ring1;
    std::string ring2;
    std::string neck;
    std::string trinket1;
    std::string trinket2;
    std::string cloak;
    std::string belt;
    std::string shoulders;
    std::string wrist;
    std::string mount;
    std::string pet;

    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

struct PlayerSettings {
    float ui_scale = 1.0f;
    float sound_volume = 0.8f;
    float music_volume = 0.6f;
    float master_volume = 1.0f;
    float voice_volume = 0.7f;

    bool chat_enabled = true;
    bool combat_text = true;
    bool auto_loot = false;
    bool show_damage_numbers = true;
    bool show_floating_text = true;
    bool show_party_frames = true;
    bool show_guild_names = true;
    bool show_player_names = true;
    bool show_npc_names = true;
    bool show_quest_tracker = true;
    bool show_minimap = true;

    std::string language = "en";
    std::string timezone = "UTC";
    std::string input_mode = "keyboard_mouse";
    std::string graphics_quality = "medium";

    int key_bindings[256] = {0};

    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

class Player : public GameEntity {
public:
    Player(int64_t id, const std::string& username);
    Player(const glm::vec3& position);
    Player(const glm::vec3& position, PlayerClass player_class, PlayerRace race);
    virtual ~Player();

    int64_t GetId() const { return id_; }
    const std::string& GetUsername() const { return username_; }

    void UpdatePosition(float x, float y, float z);
    void UpdateHeartbeat();
    bool IsHeartbeatExpired(int timeoutSeconds) const;
    void ApplyDamage(int damage, int64_t attackerId);
    void ApplyHealing(int amount, int64_t healerId);

    // Player-specific properties
    void SetPlayerClass(PlayerClass player_class) { player_class_ = player_class; }
    PlayerClass GetPlayerClass() const { return player_class_; }

    void SetPlayerRace(PlayerRace race) { race_ = race; }
    PlayerRace GetPlayerRace() const { return race_; }

    void SetStatus(PlayerStatus status) { status_ = status; }
    PlayerStatus GetStatus() const { return status_; }

    // Player stats management
    void SetHealth(int health);
    void SetMaxHealth(int max_health);
    int GetHealth() const { return stats_.health; }
    int GetMaxHealth() const { return stats_.max_health; }

    void SetMana(int mana);
    void SetMaxMana(int max_mana);
    int GetMana() const { return stats_.mana; }
    int GetMaxMana() const { return stats_.max_mana; }
    float GetAttackDamage() const { return stats_.attack_damage; }
    float GetAttackRange() const { return stats_.attack_range; }

    void SetLevel(int level);
    int GetLevel() const { return stats_.level; }

    void AddExperience(int64_t amount);
    void LoseExperience(int64_t amount);
    int64_t GetExperience() const { return stats_.experience; }
    int64_t GetExperienceToNextLevel() const { return stats_.experience_to_next_level; }

    // Attributes management
    const PlayerAttributes& GetAttributes() const { return attributes_; }
    void SetAttribute(const std::string& attribute_name, float value);
    float GetAttribute(const std::string& attribute_name) const;
    void UpdateDerivedStats();

    // Equipment management
    bool EquipItem(const std::string& item_id, const std::string& slot);
    bool UnequipItem(const std::string& slot);
    std::string GetEquippedItem(const std::string& slot) const;
    const PlayerEquipment& GetEquipment() const { return equipment_; }

    // Inventory management
    void AddItem(const std::string& item_id, int count = 1);
    void RemoveItem(const std::string& item_id, int count = 1);
    int GetItemCount(const std::string& item_id) const;
    bool HasItem(const std::string& item_id, int count = 1) const;

    // Currency management
    void AddGold(int64_t amount);
    void RemoveGold(int64_t amount);
    int64_t GetGold() const { return stats_.currency_gold; }

    void AddGems(int64_t amount);
    void RemoveGems(int64_t amount);
    int64_t GetGems() const { return stats_.currency_gems; }

    // Skills and abilities
    void LearnSkill(const std::string& skill_id, int level = 1);
    void ForgetSkill(const std::string& skill_id);
    bool HasSkill(const std::string& skill_id) const;
    int GetSkillLevel(const std::string& skill_id) const;

    // Quests
    void StartQuest(const std::string& quest_id);
    void CompleteQuest(const std::string& quest_id);
    void FailQuest(const std::string& quest_id);
    bool HasQuest(const std::string& quest_id) const;
    bool IsQuestCompleted(const std::string& quest_id) const;

    // Combat
    void TakeDamage(int amount, uint64_t attacker_id = 0);
    void Heal(int amount, uint64_t healer_id = 0);
    int CalculateDamage(const std::string& attackType = "melee") const;
    void ApplyBuff(const std::string& buff_id, const nlohmann::json& buff_data, float duration);
    void RemoveBuff(const std::string& buff_id);
    void UpdateBuffs(float delta_time);

    // Player state management
    void Update(float delta_time);
    void SaveToDatabase();
    bool LoadFromDatabase();
    void Regenerate(float delta_time);

    // Player actions
    void UseSkill(const std::string& skill_id, uint64_t target_id = 0);
    void CastSpell(const std::string& spell_id, uint64_t target_id = 0);
    void Interact(uint64_t target_id);
    void Emote(const std::string& emote_id);
    void Chat(const std::string& message);

    // Player movement
    void MoveTo(const glm::vec3& destination, float speed_multiplier = 1.0f);
    void StopMovement();
    void Jump();
    void ToggleSprint(bool sprinting);

    // Player social
    void JoinParty(uint64_t party_id);
    void LeaveParty();
    void JoinGuild(uint64_t guild_id);
    void LeaveGuild();
    void AddFriend(uint64_t player_id);
    void RemoveFriend(uint64_t player_id);
    void BlockPlayer(uint64_t player_id);
    void UnblockPlayer(uint64_t player_id);

    // Player systems access
    InventorySystem& GetInventorySystem() const { return inventory_system_; }
    SkillSystem& GetSkillSystem() const { return skill_system_; }
    QuestManager& GetQuestManager() const { return quest_manager_; }

    // Utility methods
    bool IsAlive() const { return stats_.health > 0; }
    bool IsDead() const { return stats_.health <= 0; }
    bool IsInCombat() const { return status_ == PlayerStatus::COMBAT; }
    bool IsCasting() const { return status_ == PlayerStatus::CASTING; }
    bool IsMoving() const { return status_ == PlayerStatus::MOVING; }
    std::string GetClassString(PlayerClass player_class) const;
    std::string GetRaceString(PlayerRace race) const;

    float GetAttackPower() const { return attributes_.attack_power; }
    float GetDefense() const { return attributes_.defense; }
    float GetCriticalChance() const { return attributes_.critical_chance; }
    float GetMoveSpeed() const { return attributes_.move_speed; }

    // Player achievements
    void AddAchievement(const std::string& achievement_id);
    bool HasAchievement(const std::string& achievement_id) const;
    int GetAchievementCount() const { return achievements_.size(); }

    // Player titles and cosmetics
    void SetTitle(const std::string& title);
    std::string GetTitle() const { return title_; }

    void SetCosmetic(const std::string& slot, const std::string& cosmetic_id);
    std::string GetCosmetic(const std::string& slot) const;
    float GetDistanceTo(const glm::vec3& other) const;
    void AddCurrencyGold(int amount);
    void AddCurrencyGems(int amount);
    void SetOnline(bool online);

    // Player session
    void SetSessionId(uint64_t session_id) { session_id_ = session_id; }
    uint64_t GetSessionId() const { return session_id_; }

    void SetConnectionQuality(float quality) { connection_quality_ = quality; }
    float GetConnectionQuality() const { return connection_quality_; }

    // Serialization
    virtual nlohmann::json Serialize() const override;
    virtual void Deserialize(const nlohmann::json& data) override;
    nlohmann::json JsonGetInventory() const;
    nlohmann::json ToJson() const;

private:
    int64_t id_;
    std::string username_;

    //struct Position {float x, y, z;} position_;
    std::chrono::system_clock::time_point last_movement_;

    bool online_ = false;
    std::chrono::system_clock::time_point created_at_;
    std::chrono::system_clock::time_point last_login_;
    std::chrono::system_clock::time_point last_logout_;
    std::chrono::system_clock::time_point last_heartbeat_;

    mutable std::shared_mutex mutex_;

    PlayerClass player_class_;
    PlayerRace race_;
    PlayerStatus status_;

    PlayerAttributes attributes_;
    PlayerStats stats_;
    PlayerEquipment equipment_;
    PlayerSettings settings_;

    std::unordered_map<std::string, int> inventory_;
    std::unordered_map<std::string, int> skills_;
    std::unordered_map<std::string, nlohmann::json> active_quests_;
    std::unordered_map<std::string, nlohmann::json> completed_quests_;
    std::vector<std::string> achievements_;

    // Active buffs/debuffs
    struct ActiveBuff {
        std::string buff_id;
        nlohmann::json buff_data;
        float duration;
        float time_remaining;
        std::chrono::system_clock::time_point applied_time;
    };
    std::unordered_map<std::string, ActiveBuff> active_buffs_;

    // Cooldowns
    struct Cooldown {
        std::string ability_id;
        float duration;
        float time_remaining;
        std::chrono::system_clock::time_point start_time;
    };
    std::unordered_map<std::string, Cooldown> cooldowns_;

    // Social
    uint64_t party_id_ = 0;
    uint64_t guild_id_ = 0;
    std::vector<uint64_t> friends_;
    std::vector<uint64_t> blocked_players_;

    // Cosmetic
    std::string title_;
    std::unordered_map<std::string, std::string> cosmetics_;

    // Session
    uint64_t session_id_ = 0;
    float connection_quality_ = 100.0f;

    // Movement
    glm::vec3 movement_target_;
    float movement_speed_multiplier_ = 1.0f;
    bool is_moving_ = false;
    bool is_sprinting_ = false;

    // Systems
    InventorySystem& inventory_system_;
    SkillSystem& skill_system_;
    QuestManager& quest_manager_;

    // Private methods
    void OnLevelUp();
    int64_t CalculateExperienceRequired(int level) const;
    void UpdateMovement(float delta_time);
    void UpdateCooldowns(float delta_time);
    void CleanupExpiredBuffs();
    void CleanupExpiredCooldowns();
    void ApplyClassBonuses();
    void ApplyRaceBonuses();
    void CalculateExperienceToNextLevel();
    void SaveBuffsToJson(nlohmann::json& json) const;
    void LoadBuffsFromJson(const nlohmann::json& json);
    void SaveCooldownsToJson(nlohmann::json& json) const;
    void LoadCooldownsFromJson(const nlohmann::json& json);
    void SaveQuestsToJson(nlohmann::json& json) const;
    void LoadQuestsFromJson(const nlohmann::json& json);
    void SaveInventoryToJson(nlohmann::json& json) const;
    void LoadInventoryFromJson(const nlohmann::json& json);
    void SaveSkillsToJson(nlohmann::json& json) const;
    void LoadSkillsFromJson(const nlohmann::json& json);

    // Damage/healing sources for combat logging
    struct DamageSource {
        uint64_t attacker_id;
        int damage;
        std::chrono::system_clock::time_point timestamp;
    };
    std::deque<DamageSource> damage_sources_;

    struct HealingSource {
        uint64_t healer_id;
        int healing;
        std::chrono::system_clock::time_point timestamp;
    };
    std::deque<HealingSource> healing_sources_;

    // Constants
    static constexpr int MAX_LEVEL = 100;
    static constexpr int MAX_HEALTH = 10000;
    static constexpr int MAX_MANA = 10000;
    static constexpr int MAX_INVENTORY_SLOTS = 40;
    static constexpr int MAX_ACTIVE_QUESTS = 25;
    static constexpr int MAX_BUFFS = 20;
    static constexpr int MAX_FRIENDS = 200;
    static constexpr int MAX_BLOCKED_PLAYERS = 100;

    friend class PlayerManager;
    friend class EntityManager;
};
