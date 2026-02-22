#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <fstream>
#include <filesystem>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "logging/Logger.hpp"
#include "config/ConfigManager.hpp"

//TODO: we need refactor architecture, else it do cyclic include there
//#include "game/LogicCore.hpp"

// =============== Enums ===============

enum class QuestType {
    MAIN_STORY = 0,
    SIDE_QUEST = 1,
    DAILY_QUEST = 2,
    WEEKLY_QUEST = 3,
    EVENT_QUEST = 4,
    REPEATABLE_QUEST = 5,
    PROFESSION_QUEST = 6,
    GUILD_QUEST = 7,
    PVP_QUEST = 8,
    DUNGEON_QUEST = 9,
    RAID_QUEST = 10,
    COLLECTION_QUEST = 11,
    EXPLORATION_QUEST = 12,
    KILL_QUEST = 13,
    ESCORT_QUEST = 14,
    DELIVERY_QUEST = 15
};

enum class QuestDifficulty {
    TRIVIAL = 0,
    EASY = 1,
    NORMAL = 2,
    HARD = 3,
    ELITE = 4,
    EPIC = 5,
    LEGENDARY = 6
};

enum class QuestState {
    NOT_STARTED = 0,
    IN_PROGRESS = 1,
    COMPLETED = 2,
    FAILED = 3,
    ABANDONED = 4,
    REWARDS_CLAIMED = 5
};

enum class ObjectiveType {
    KILL_MONSTER = 0,
    COLLECT_ITEM = 1,
    TALK_TO_NPC = 2,
    VISIT_LOCATION = 3,
    USE_ITEM = 4,
    CRAFT_ITEM = 5,
    GATHER_RESOURCE = 6,
    COMPLETE_DUNGEON = 7,
    COMPLETE_QUEST = 8,
    GAIN_REPUTATION = 9,
    LEARN_SKILL = 10,
    REACH_LEVEL = 11,
    PVP_KILLS = 12,
    ESCORT_NPC = 13,
    PROTECT_NPC = 14,
    DEFEND_LOCATION = 15,
    TIMED_OBJECTIVE = 16
};

// =============== Quest Structures ===============

struct QuestObjective {
    std::string id;
    ObjectiveType type = ObjectiveType::KILL_MONSTER;
    std::string target;
    int required_count = 1;
    std::string description;
    std::string location_hint;
    glm::vec3 location = glm::vec3(0.0f);
    float radius = 0.0f;
    float time_limit = 0.0f;
    bool is_optional = false;
    bool is_hidden = false;

    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

struct QuestReward {
    int64_t experience = 0;
    int64_t gold = 0;
    int64_t reputation = 0;
    std::string reputation_faction;
    std::vector<std::pair<std::string, int>> items;
    std::vector<std::string> skills;
    std::vector<std::string> titles;
    std::vector<std::string> cosmetics;
    int skill_points = 0;
    int talent_points = 0;
    int honor_points = 0;

    // Choice rewards
    bool is_choice_reward = false;
    int choose_count = 1;
    std::vector<std::pair<std::string, int>> choice_items;

    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

struct QuestDefinition {
    uint64_t id;
    std::string name;
    std::string description;
    std::string completion_text;
    std::string failure_text;

    QuestType type = QuestType::SIDE_QUEST;
    QuestDifficulty difficulty = QuestDifficulty::NORMAL;

    std::string giver_npc_id;
    std::string turn_in_npc_id;
    glm::vec3 giver_location = glm::vec3(0.0f);
    glm::vec3 turn_in_location = glm::vec3(0.0f);

    // Prerequisites (now uint64_t quest IDs)
    std::vector<uint64_t> prerequisite_quests;

    std::vector<QuestObjective> objectives;
    QuestReward reward;

    std::vector<uint64_t> next_quests;

    bool is_repeatable = false;
    int repeat_cooldown_hours = 24;
    bool is_shareable = false;
    bool is_discoverable = false;
    bool auto_complete = false;

    int min_level = 1;
    int max_level = 100;
    int suggested_party_size = 1;

    std::string zone;
    std::vector<std::string> tags;

    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

// =============== Quest Progress ===============

struct ObjectiveProgress {
    int current_count = 0;
    float time_remaining = 0.0f;
    bool completed = false;

    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

struct QuestProgress {
    uint64_t quest_id;
    QuestState state = QuestState::NOT_STARTED;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point completion_time;
    std::unordered_map<std::string, ObjectiveProgress> objective_progress;
    int completion_count = 0;
    bool rewards_claimed = false;
    std::chrono::system_clock::time_point last_reset_time;

    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

struct QuestChain {
    std::string chain_id;
    std::string name;
    std::string description;
    std::vector<uint64_t> quests_in_order;
    int current_quest_index = 0;
    bool is_completed = false;
    QuestReward chain_completion_reward;

    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

// =============== QuestManager ===============

class QuestManager {
public:
    static QuestManager& GetInstance();

    // --- Lifecycle ---
    void Initialize();
    void Shutdown();

    // --- Quest Data Loading ---
    bool LoadQuestsFromFile(const std::string& filepath);
    bool LoadQuestsFromDirectory(const std::string& directory);
    bool SaveQuestsToFile(const std::string& filepath) const;

    // --- Quest Definition Queries ---
    const QuestDefinition* GetQuestDefinition(uint64_t quest_id) const;
    std::vector<uint64_t> GetAllQuestIds() const;
    std::vector<uint64_t> GetQuestsByType(QuestType type) const;
    std::vector<uint64_t> GetQuestsByZone(const std::string& zone) const;
    std::vector<uint64_t> GetQuestsByLevel(int level) const;

    // --- Entity Quest Management ---
    bool CanStartQuest(uint64_t entity_id, uint64_t quest_id) const;
    bool StartQuest(uint64_t entity_id, uint64_t quest_id);
    bool AbandonQuest(uint64_t entity_id, uint64_t quest_id);
    bool CanCompleteQuest(uint64_t entity_id, uint64_t quest_id) const;
    bool CompleteQuest(uint64_t entity_id, uint64_t quest_id);
    bool FailQuest(uint64_t entity_id, uint64_t quest_id);
    bool ClaimQuestReward(uint64_t entity_id, uint64_t quest_id);

    // --- Quest Progress Updates (called by game systems) ---
    void UpdateObjective(uint64_t entity_id, uint64_t quest_id,
                         const std::string& objective_id, int delta = 1);
    void OnMonsterKilled(uint64_t entity_id, const std::string& monster_id);
    void OnItemCollected(uint64_t entity_id, const std::string& item_id, int count = 1);
    void OnNPCTalkedTo(uint64_t entity_id, uint64_t npc_id);
    void OnLocationVisited(uint64_t entity_id, const glm::vec3& location, float radius = 10.0f);
    void OnDungeonCompleted(uint64_t entity_id, const std::string& dungeon_id);
    void OnLevelGained(uint64_t entity_id, int new_level);
    void OnSkillLearned(uint64_t entity_id, const std::string& skill_id);
    void OnTimerElapsed(uint64_t entity_id, float delta_time);

    // --- Query Methods ---
    bool HasQuest(uint64_t entity_id, uint64_t quest_id) const;
    bool IsQuestCompleted(uint64_t entity_id, uint64_t quest_id) const;
    bool IsQuestInProgress(uint64_t entity_id, uint64_t quest_id) const;
    QuestState GetQuestState(uint64_t entity_id, uint64_t quest_id) const;
    std::vector<QuestProgress> GetAllEntityQuests(uint64_t entity_id) const;
    std::vector<QuestProgress> GetActiveQuests(uint64_t entity_id) const;
    std::vector<QuestProgress> GetCompletedQuests(uint64_t entity_id) const;

    // --- Quest Chains ---
    bool StartQuestChain(uint64_t entity_id, const std::string& chain_id);
    bool AdvanceQuestChain(uint64_t entity_id, const std::string& chain_id);
    bool IsQuestChainCompleted(uint64_t entity_id, const std::string& chain_id) const;
    std::vector<QuestChain> GetEntityQuestChains(uint64_t entity_id) const;

    // --- Daily / Weekly Quests ---
    void ResetDailyQuests(uint64_t entity_id);
    void ResetWeeklyQuests(uint64_t entity_id);
    bool CanAcceptDailyQuest(uint64_t entity_id, uint64_t quest_id) const;
    bool CanAcceptWeeklyQuest(uint64_t entity_id, uint64_t quest_id) const;

    // --- Quest Discovery ---
    void DiscoverQuest(uint64_t entity_id, uint64_t quest_id);
    std::vector<uint64_t> GetDiscoveredQuests(uint64_t entity_id) const;
    bool IsQuestDiscovered(uint64_t entity_id, uint64_t quest_id) const;

    // --- Quest Tracking (UI) ---
    void TrackQuest(uint64_t entity_id, uint64_t quest_id);
    void UntrackQuest(uint64_t entity_id, uint64_t quest_id);
    bool IsQuestTracked(uint64_t entity_id, uint64_t quest_id) const;
    uint64_t GetTrackedQuest(uint64_t entity_id) const;

    // --- NPC Interaction ---
    std::vector<uint64_t> GetQuestsFromNPC(uint64_t entity_id, uint64_t npc_id) const;
    std::vector<uint64_t> GetQuestsToTurnIn(uint64_t entity_id, uint64_t npc_id) const;

    // --- Statistics ---
    int GetTotalQuestsCompleted(uint64_t entity_id) const;
    int GetTotalQuestPoints(uint64_t entity_id) const;
    std::unordered_map<std::string, int> GetQuestCompletionByZone(uint64_t entity_id) const;

    // --- Serialization ---
    bool LoadEntityQuests(uint64_t entity_id);
    bool SaveEntityQuests(uint64_t entity_id) const;
    nlohmann::json SerializeEntityQuests(uint64_t entity_id) const;
    bool DeserializeEntityQuests(uint64_t entity_id, const nlohmann::json& data);

    // --- Debug ---
    void DebugGiveQuest(uint64_t entity_id, uint64_t quest_id);
    void DebugCompleteQuest(uint64_t entity_id, uint64_t quest_id);
    void DebugResetEntityQuests(uint64_t entity_id);

private:
    QuestManager() = default;
    ~QuestManager() = default;
    QuestManager(const QuestManager&) = delete;
    QuestManager& operator=(const QuestManager&) = delete;

    // Internal helpers
    bool ParseQuestDefinition(const nlohmann::json& j, QuestDefinition& def);
    bool MeetsPrerequisites(uint64_t entity_id, const QuestDefinition& def) const;
    void FireQuestEvent(const std::string& event_name, uint64_t entity_id,
                        uint64_t quest_id, const nlohmann::json& extra = {});
    bool CanAcceptQuest(uint64_t entity_id, uint64_t quest_id) const;

    // Storage
    std::unordered_map<uint64_t, QuestDefinition> quest_definitions_;
    std::unordered_map<std::string, std::vector<uint64_t>> quest_chains_;

    // Per‑entity data
    struct EntityQuestData {
        std::unordered_map<uint64_t, QuestProgress> active_quests;
        std::unordered_map<uint64_t, QuestProgress> completed_quests;
        std::unordered_map<std::string, QuestChain> quest_chains;
        std::unordered_map<uint64_t, bool> discovered_quests;
        uint64_t tracked_quest_id = 0;

        // Daily/weekly completion timestamps
        std::unordered_map<uint64_t, std::chrono::system_clock::time_point> daily_completions;
        std::unordered_map<uint64_t, std::chrono::system_clock::time_point> weekly_completions;

        int total_completed = 0;
        int total_points = 0;
        std::chrono::system_clock::time_point last_daily_reset;
        std::chrono::system_clock::time_point last_weekly_reset;

        nlohmann::json Serialize() const;
        void Deserialize(const nlohmann::json& data);
    };

    mutable std::shared_mutex mutex_;
    std::unordered_map<uint64_t, EntityQuestData> entity_quests_;

    // Constants
    static constexpr int MAX_ACTIVE_QUESTS = 25;
};
