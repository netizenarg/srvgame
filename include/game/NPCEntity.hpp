#pragma once

#include <algorithm>
#include <cmath>
#include <chrono>
#include <memory>
#include <random>
#include <queue>
#include <sstream>
#include <vector>
#include <unordered_map>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "game/GameEntity.hpp"
#include "game/LootTableManager.hpp"
#include "game/MobSystem.hpp"
#include "logging/Logger.hpp"
#include "config/ConfigManager.hpp"

enum class NPCType {
    // Humanoids
    VILLAGER,
    GUARD,
    MERCHANT,
    BLACKSMITH,
    ALCHEMIST,
    INNKEEPER,
    QUEST_GIVER,
    TRAINER,
    BANKER,

    // Monsters
    GOBLIN,
    ORC,
    TROLL,
    OGRE,
    SKELETON,
    ZOMBIE,
    GHOST,
    VAMPIRE,
    WEREWOLF,
    DRAGON,
    SLIME,
    SPIDER,
    BAT,
    RAT,
    WOLF,
    BEAR,
    BOAR,

    // Familiars
    CAT_FAMILIAR,
    WOLF_FAMILIAR,

    // Bosses
    DRAGON_LORD,
    LICH_KING,
    DEMON_LORD,
    ANCIENT_TREANT,
    SEA_SERPENT,
    PHOENIX,
    GOLEM,
    HYDRA,

    // Special
    PET,
    SUMMON,
    MINION,
    ELITE,
    RARE,
    LEGENDARY,
    WORLD_BOSS
};

enum class NPCAIState {
    IDLE,
    PATROL,
    FOLLOW,
    INTERACT,
    CHASE,
    COMBAT,
    ATTACK,
    FLEE,
    DEAD,
    SPAWNING,
    DESPAWNING,
    TALKING,
    TRADING,
    CRAFTING,
    SLEEPING,
    EATING,
    WORKING,
    GUARDING,
    WANDER
};

enum class NPCRarity {
    COMMON,
    UNCOMMON,
    RARE,
    EPIC,
    LEGENDARY,
    MYTHIC,
    UNIQUE
};

enum class NPCFaction {
    NEUTRAL,
    FRIENDLY,
    HOSTILE,
    ALLIED,
    ENEMY,
    BEAST,
    UNDEAD,
    DEMONIC,
    ELEMENTAL,
    MECHANICAL
};

struct NPCStats {
    int level = 1;
    float health = 100.0f;
    float max_health = 100.0f;
    float mana = 50.0f;
    float max_mana = 50.0f;
    float stamina = 100.0f;
    float max_stamina = 100.0f;

    float attack_damage = 10.0f;
    float attack_speed = 1.0f;
    float attack_range = 2.0f;
    float critical_chance = 0.05f;
    float critical_damage = 1.5f;

    float defense = 5.0f;
    float magic_resist = 0.0f;
    float physical_resist = 0.0f;
    float dodge_chance = 0.02f;
    float block_chance = 0.03f;
    float parry_chance = 0.01f;

    float move_speed = 3.0f;
    float chase_speed = 4.0f;
    float flee_speed = 5.0f;

    float sight_range = 20.0f;
    float chase_range = 30.0f;
    float attack_range_min = 1.0f;
    float attack_range_max = 5.0f;

    float respawn_time = 30.0f;
    int min_gold = 1;
    int max_gold = 10;
    int experience_reward = 10;

    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

struct NPCAIProfile {
    bool is_aggressive = false;
    bool is_passive = true;
    bool is_friendly = false;
    bool is_hostile = false;
    bool is_neutral = true;

    float idle_time_min = 3.0f;
    float idle_time_max = 10.0f;
    float patrol_speed = 2.0f;
    float chase_speed_multiplier = 1.5f;
    float flee_health_threshold = 0.3f;
    float sight_range = 25.0f;
    float chase_range = 40.0f;

    bool can_summon_allies = false;
    int max_allies = 0;
    float summon_cooldown = 30.0f;

    bool can_flee = false;
    bool can_call_for_help = true;
    bool respawns = true;
    bool drops_loot = true;

    std::vector<glm::vec3> patrol_points;
    float patrol_radius = 10.0f;
    bool patrol_loop = true;
    bool patrol_random = false;

    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

struct NPCLootTable {
    std::string table_id;
    float drop_chance = 1.0f;
    int min_items = 1;
    int max_items = 3;
    std::unordered_map<std::string, float> item_drop_rates;

    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

struct NPCDialogue {
    std::string greeting;
    std::vector<std::string> topics;
    std::unordered_map<std::string, std::string> responses;
    std::unordered_map<std::string, nlohmann::json> quest_dialogues;
    std::unordered_map<std::string, nlohmann::json> trade_dialogues;

    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

class NPCEntity : public GameEntity {
public:
    NPCEntity(NPCType type, const glm::vec3& position, int level = 1);
    virtual ~NPCEntity();

    // NPC type and classification
    NPCType GetNPCType() const { return npc_type_; }
    void SetNPCType(NPCType type);

    NPCRarity GetRarity() const { return rarity_; }
    void SetRarity(NPCRarity rarity) { rarity_ = rarity; }

    NPCFaction GetFaction() const { return faction_; }
    void SetFaction(NPCFaction faction) { faction_ = faction; }

    NPCAIState GetAIState() const { return ai_state_; }
    void SetAIState(NPCAIState state);

    // Stats and combat
    const NPCStats& GetNPCStats() const { return npc_stats_; }
    void SetNPCStats(const NPCStats& stats);

    void TakeDamage(float damage, uint64_t attacker_id = 0);
    void Heal(float amount, uint64_t healer_id = 0);

    float GetAttackDamage() const { return npc_stats_.attack_damage; }
    float GetAttackRange() const { return npc_stats_.attack_range; }
    float GetSightRange() const { return npc_stats_.sight_range; }
    float GetChaseRange() const { return npc_stats_.chase_range; }

    // AI and behavior
    const NPCAIProfile& GetAIProfile() const { return ai_profile_; }
    void SetAIProfile(const NPCAIProfile& profile);
    void SetDefaultAIProfile();

    void UpdateAI(float delta_time);
    void SetTarget(uint64_t target_id);
    uint64_t GetTarget() const { return target_id_; }
    bool HasTarget() const { return target_id_ != 0; }

    void AddPatrolPoint(const glm::vec3& point);
    void ClearPatrolPoints();
    const std::vector<glm::vec3>& GetPatrolPoints() const { return ai_profile_.patrol_points; }

    // Loot and rewards
    void SetLootTable(const std::string& table_id);
    std::string GetLootTable() const { return loot_table_.table_id; }

    void AddDropItem(const std::string& item_id, float drop_rate);
    void RemoveDropItem(const std::string& item_id);

    std::vector<std::pair<std::string, int>> GenerateLoot() const;
    int GenerateGold() const;
    int GetExperienceReward() const { return npc_stats_.experience_reward; }

    // Dialogue and interaction
    const NPCDialogue& GetDialogue() const { return dialogue_; }
    void SetDialogue(const NPCDialogue& dialogue);

    std::string GetGreeting() const { return dialogue_.greeting; }
    void SetGreeting(const std::string& greeting) { dialogue_.greeting = greeting; }

    void AddDialogueTopic(const std::string& topic, const std::string& response);
    void AddQuestDialogue(const std::string& quest_id, const nlohmann::json& dialogue);
    void AddTradeDialogue(const std::string& item_id, const nlohmann::json& dialogue);

    bool HasDialogueTopic(const std::string& topic) const;
    std::string GetDialogueResponse(const std::string& topic) const;

    // Quest and trade
    void AddQuest(const std::string& quest_id);
    void RemoveQuest(const std::string& quest_id);
    bool HasQuest(const std::string& quest_id) const;
    const std::vector<std::string>& GetQuests() const { return quests_; }

    void AddTradeItem(const std::string& item_id, int price);
    void RemoveTradeItem(const std::string& item_id);
    bool SellsItem(const std::string& item_id) const;
    int GetItemPrice(const std::string& item_id) const;
    const std::unordered_map<std::string, int>& GetTradeItems() const { return trade_items_; }

    // Spawn and respawn
    void SetSpawnPosition(const glm::vec3& position) { spawn_position_ = position; }
    glm::vec3 GetSpawnPosition() const { return spawn_position_; }

    void SetRespawnTime(float time) { npc_stats_.respawn_time = time; }
    float GetRespawnTime() const { return npc_stats_.respawn_time; }

    bool ShouldRespawn() const { return ai_profile_.respawns && IsDead(); }
    void Respawn();

    // Serialization
    virtual nlohmann::json Serialize() const override;
    virtual void Deserialize(const nlohmann::json& data) override;

    // Update
    virtual void Update(float delta_time) override;
    virtual void FixedUpdate(float delta_time) override;

    // Events
    virtual void OnCreate() override;
    virtual void OnDestroy() override;
    virtual void OnCollision(std::shared_ptr<GameEntity> other) override;

    // Utility
    bool IsHostile() const { return faction_ == NPCFaction::HOSTILE || ai_profile_.is_hostile; }
    bool IsFriendly() const { return faction_ == NPCFaction::FRIENDLY || ai_profile_.is_friendly; }
    bool IsNeutral() const { return faction_ == NPCFaction::NEUTRAL || ai_profile_.is_neutral; }

    bool IsAggressive() const { return ai_profile_.is_aggressive; }
    bool IsBoss() const;
    bool IsElite() const;
    bool IsRare() const;

    std::string GetNPCTypeString() const;
    std::string GetRarityString() const;
    std::string GetFactionString() const;
    std::string GetAIStateString() const;
    std::string AIStateToString(NPCAIState state) const;

    NPCStats GetStats() const { return npc_stats_; };
    uint64_t GetOwnerId() const { return target_id_; };
    void SetBehaviorState(NPCAIState st) { ai_state_ = st; };
    void MoveTo(const glm::vec3& destination, float speed_multiplier = 1.0f);

    // AI decision making
    void UpdateIdle(float delta_time);
    void UpdatePatrol(float delta_time);
    void UpdateChase(float delta_time);
    void UpdateAttack(float delta_time);
    void UpdateFlee(float delta_time);
    void UpdateSpawning(float delta_time);
    void UpdateDespawning(float delta_time);

    void UpdateTargetSelection();
    void UpdateHateList(uint64_t attacker_id, float damage);
    void ClearHateList();
    uint64_t GetTopHated() const;

    void PerformAttack();
    void PerformAbility(const std::string& ability_id);
    void SummonAllies();

    glm::vec3 GetNextPatrolPoint();
    bool IsAtPatrolPoint(const glm::vec3& point) const;

    // Internal state changes
    void ChangeToIdle();
    void ChangeToPatrol();
    void ChangeToChase(uint64_t target_id);
    void ChangeToAttack(uint64_t target_id);
    void ChangeToFlee();
    void ChangeToDead();

    // Stat calculation
    void CalculateStats();
    void ApplyRarityModifiers();
    void ApplyFactionModifiers();

    // Serialization helpers
    void SaveStatsToJson(nlohmann::json& json) const;
    void LoadStatsFromJson(const nlohmann::json& json);

    void SaveAIProfileToJson(nlohmann::json& json) const;
    void LoadAIProfileFromJson(const nlohmann::json& json);

    void SaveLootTableToJson(nlohmann::json& json) const;
    void LoadLootTableFromJson(const nlohmann::json& json);

    void SaveDialogueToJson(nlohmann::json& json) const;
    void LoadDialogueFromJson(const nlohmann::json& json);

    void SaveQuestsToJson(nlohmann::json& json) const;
    void LoadQuestsFromJson(const nlohmann::json& json);

    void SaveTradeItemsToJson(nlohmann::json& json) const;
    void LoadTradeItemsFromJson(const nlohmann::json& json);

private:
    NPCType npc_type_;
    NPCRarity rarity_;
    NPCFaction faction_;
    NPCAIState ai_state_;

    NPCStats npc_stats_;
    NPCAIProfile ai_profile_;
    NPCLootTable loot_table_;
    NPCDialogue dialogue_;

    // Targeting
    uint64_t target_id_ = 0;
    std::vector<uint64_t> hate_list_; // Ordered by hate/damage dealt
    std::unordered_map<uint64_t, float> damage_taken_; // Damage taken from each attacker

    // AI state tracking
    float state_timer_ = 0.0f;
    float idle_timer_ = 0.0f;
    float patrol_index_ = 0.0f;
    bool patrol_direction_ = true; // true = forward, false = backward

    // Combat tracking
    float attack_cooldown_ = 0.0f;
    float stun_timer_ = 0.0f;
    float flee_timer_ = 0.0f;
    float summon_cooldown_ = 0.0f;

    // Patrol and movement
    glm::vec3 spawn_position_;
    std::queue<glm::vec3> patrol_queue_;

    // Quests and trade
    std::vector<std::string> quests_;
    std::unordered_map<std::string, int> trade_items_;

    // Special abilities
    std::vector<std::string> abilities_;
    std::unordered_map<std::string, float> ability_cooldowns_;

    // Constants
    static constexpr float ATTACK_COOLDOWN_BASE = 2.0f;
    static constexpr float STUN_DURATION = 1.0f;
    static constexpr float FLEE_DURATION = 5.0f;
    static constexpr float DESPAWN_DELAY = 10.0f;

    friend class NPCAISystem;
    friend class MobSystem;
    friend class EntityManager;
};
