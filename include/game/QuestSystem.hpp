#pragma once

#include <algorithm>
#include <cmath>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <string>
#include <set>

#include <nlohmann/json.hpp>

#include "game/PlayerEntity.hpp"
#include "logging/Logger.hpp"
#include "config/ConfigManager.hpp"

#include "database/DbManager.hpp"

class PlayerEntity;

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
    READY_TO_TURN_IN = 5
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

struct QuestRequirement {
    int min_level = 1;
    int max_level = 100;
    PlayerClass required_class = PlayerClass::ANY;
    PlayerRace required_race = PlayerRace::HUMAN;
    std::string required_faction;
    int required_reputation_level = 0;
    std::vector<std::string> required_quests;
    std::vector<std::string> required_items;
    std::vector<std::string> required_skills;
    
    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

struct QuestObjective {
    std::string id;
    ObjectiveType type = ObjectiveType::KILL_MONSTER;
    std::string target_id;  // Monster ID, Item ID, NPC ID, etc.
    int required_count = 1;
    int current_count = 0;
    std::string description;
    std::string location_hint;
    glm::vec3 location_position = glm::vec3(0.0f);
    float location_radius = 0.0f;
    float time_limit = 0.0f;  // 0 = no time limit
    float time_remaining = 0.0f;
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
    std::vector<std::pair<std::string, int>> items;  // item_id, quantity
    std::vector<std::string> skills;
    std::vector<std::string> titles;
    std::vector<std::string> cosmetics;
    int skill_points = 0;
    int talent_points = 0;
    int honor_points = 0;
    
    // Choice rewards (player selects X from list)
    bool is_choice_reward = false;
    int choose_count = 1;
    std::vector<std::pair<std::string, int>> choice_items;
    
    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

struct QuestData {
    std::string id;
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
    
    QuestRequirement requirements;
    std::vector<QuestObjective> objectives;
    QuestReward reward;
    
    std::string prerequisite_quest;
    std::vector<std::string> next_quests;
    
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

struct PlayerQuest {
    std::string quest_id;
    QuestState state = QuestState::NOT_STARTED;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point completion_time;
    std::vector<QuestObjective> objectives;
    int completion_count = 0;  // For repeatable quests
    bool rewards_claimed = false;
    
    // Tracking for daily/weekly quests
    std::chrono::system_clock::time_point last_reset_time;
    
    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

struct QuestChain {
    std::string chain_id;
    std::string name;
    std::string description;
    std::vector<std::string> quests_in_order;
    int current_quest_index = 0;
    bool is_completed = false;
    QuestReward chain_completion_reward;
    
    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

class QuestSystem {
public:
    static QuestSystem& GetInstance();
    
    // Quest data management
    bool LoadQuestData(const std::string& file_path);
    bool SaveQuestData(const std::string& file_path);
    const QuestData* GetQuestData(const std::string& quest_id) const;
    std::vector<std::string> GetQuestsByType(QuestType type) const;
    std::vector<std::string> GetQuestsByZone(const std::string& zone) const;
    std::vector<std::string> GetQuestsByLevel(int level) const;
    std::vector<std::string> GetAvailableQuests(uint64_t player_id) const;
    
    // Player quest management
    bool StartQuest(uint64_t player_id, const std::string& quest_id);
    bool AbandonQuest(uint64_t player_id, const std::string& quest_id);
    bool CompleteQuest(uint64_t player_id, const std::string& quest_id);
    bool FailQuest(uint64_t player_id, const std::string& quest_id);
    bool ShareQuest(uint64_t from_player_id, uint64_t to_player_id, const std::string& quest_id);
    
    // Quest progress tracking
    bool UpdateQuestObjective(uint64_t player_id, const std::string& quest_id, 
                              const std::string& objective_id, int increment = 1);
    bool CheckQuestCompletion(uint64_t player_id, const std::string& quest_id);
    bool ClaimQuestReward(uint64_t player_id, const std::string& quest_id);
    
    // Query methods
    bool HasQuest(uint64_t player_id, const std::string& quest_id) const;
    bool IsQuestCompleted(uint64_t player_id, const std::string& quest_id) const;
    bool IsQuestInProgress(uint64_t player_id, const std::string& quest_id) const;
    QuestState GetQuestState(uint64_t player_id, const std::string& quest_id) const;
    std::vector<PlayerQuest> GetPlayerQuests(uint64_t player_id) const;
    std::vector<PlayerQuest> GetActiveQuests(uint64_t player_id) const;
    std::vector<PlayerQuest> GetCompletedQuests(uint64_t player_id) const;
    
    // Quest chain management
    bool StartQuestChain(uint64_t player_id, const std::string& chain_id);
    bool AdvanceQuestChain(uint64_t player_id, const std::string& chain_id);
    bool IsQuestChainCompleted(uint64_t player_id, const std::string& chain_id) const;
    std::vector<QuestChain> GetPlayerQuestChains(uint64_t player_id) const;
    
    // Daily/Weekly quests
    void ResetDailyQuests(uint64_t player_id);
    void ResetWeeklyQuests(uint64_t player_id);
    bool CanAcceptDailyQuest(uint64_t player_id, const std::string& quest_id) const;
    bool CanAcceptWeeklyQuest(uint64_t player_id, const std::string& quest_id) const;
    
    // Event handling (to be called from game systems)
    void OnMonsterKilled(uint64_t player_id, const std::string& monster_id);
    void OnItemCollected(uint64_t player_id, const std::string& item_id);
    void OnNPCTalkedTo(uint64_t player_id, const std::string& npc_id);
    void OnLocationVisited(uint64_t player_id, const glm::vec3& location, float radius = 10.0f);
    void OnDungeonCompleted(uint64_t player_id, const std::string& dungeon_id);
    void OnLevelGained(uint64_t player_id, int new_level);
    void OnSkillLearned(uint64_t player_id, const std::string& skill_id);
    
    // Serialization
    bool LoadPlayerQuests(uint64_t player_id);
    bool SavePlayerQuests(uint64_t player_id);
    nlohmann::json SerializePlayerQuests(uint64_t player_id) const;
    bool DeserializePlayerQuests(uint64_t player_id, const nlohmann::json& data);
    
    // Quest discovery
    void DiscoverQuest(uint64_t player_id, const std::string& quest_id);
    std::vector<std::string> GetDiscoveredQuests(uint64_t player_id) const;
    bool IsQuestDiscovered(uint64_t player_id, const std::string& quest_id) const;
    
    // Quest tracking
    void TrackQuest(uint64_t player_id, const std::string& quest_id);
    void UntrackQuest(uint64_t player_id, const std::string& quest_id);
    bool IsQuestTracked(uint64_t player_id, const std::string& quest_id) const;
    std::string GetTrackedQuest(uint64_t player_id) const;
    
    // Quest giver/turn-in
    std::vector<std::string> GetQuestsFromNPC(uint64_t player_id, const std::string& npc_id) const;
    std::vector<std::string> GetQuestsToTurnIn(uint64_t player_id, const std::string& npc_id) const;
    
    // Statistics
    int GetTotalQuestsCompleted(uint64_t player_id) const;
    int GetTotalQuestPoints(uint64_t player_id) const;
    std::unordered_map<std::string, int> GetQuestCompletionByZone(uint64_t player_id) const;
    
    // Debug and testing
    void DebugGiveQuest(uint64_t player_id, const std::string& quest_id);
    void DebugCompleteQuest(uint64_t player_id, const std::string& quest_id);
    void DebugResetQuests(uint64_t player_id);
    
private:
    QuestSystem();
    ~QuestSystem() = default;
    
    struct PlayerQuestData {
        std::unordered_map<std::string, PlayerQuest> active_quests;
        std::unordered_map<std::string, PlayerQuest> completed_quests;
        std::unordered_map<std::string, QuestChain> quest_chains;
        std::unordered_map<std::string, bool> discovered_quests;
        std::string tracked_quest_id;
        
        // Daily/weekly quest tracking
        std::unordered_map<std::string, std::chrono::system_clock::time_point> daily_quest_completions;
        std::unordered_map<std::string, std::chrono::system_clock::time_point> weekly_quest_completions;
        
        // Statistics
        int total_quests_completed = 0;
        int total_quest_points = 0;
        std::chrono::system_clock::time_point last_daily_reset;
        std::chrono::system_clock::time_point last_weekly_reset;
        
        nlohmann::json Serialize() const;
        void Deserialize(const nlohmann::json& data);
    };
    
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, PlayerQuestData> player_quests_;
    std::unordered_map<std::string, QuestData> quest_database_;
    std::unordered_map<std::string, std::vector<std::string>> quest_chains_;  // chain_id -> quest_ids
    
#ifdef USE_CITUS
    CitusClient& db_client_;
#else
    std::unique_ptr<PostgreSQLBackend> db_backend_;
#endif
    
    // Helper methods
    bool MeetsQuestRequirements(uint64_t player_id, const std::string& quest_id) const;
    bool CanAcceptQuest(uint64_t player_id, const std::string& quest_id) const;
    void GiveQuestReward(uint64_t player_id, const std::string& quest_id);
    void ProcessQuestCompletion(uint64_t player_id, const std::string& quest_id);
    void CheckQuestChainProgress(uint64_t player_id, const std::string& quest_id);
    
    // Objective checking helpers
    void CheckKillObjectives(uint64_t player_id, const std::string& monster_id);
    void CheckCollectObjectives(uint64_t player_id, const std::string& item_id);
    void CheckTalkObjectives(uint64_t player_id, const std::string& npc_id);
    void CheckLocationObjectives(uint64_t player_id, const glm::vec3& location);
    void CheckDungeonObjectives(uint64_t player_id, const std::string& dungeon_id);
    void CheckLevelObjectives(uint64_t player_id, int level);
    void CheckSkillObjectives(uint64_t player_id, const std::string& skill_id);
    
    // Time-based quest management
    void CheckTimedObjectives(uint64_t player_id, float delta_time);
    void CleanupExpiredQuests(uint64_t player_id);
    bool IsDailyQuestAvailable(uint64_t player_id, const std::string& quest_id) const;
    bool IsWeeklyQuestAvailable(uint64_t player_id, const std::string& quest_id) const;
    
    // Database operations
    bool LoadQuestDataFromDatabase();
    bool SaveQuestDataToDatabase();
    bool LoadPlayerQuestsFromDatabase(uint64_t player_id);
    bool SavePlayerQuestsToDatabase(uint64_t player_id);
    
    // Constants
    static constexpr int MAX_ACTIVE_QUESTS = 25;
    static constexpr int MAX_QUEST_LOG_SIZE = 50;
    static constexpr float QUEST_UPDATE_INTERVAL = 1.0f;
    
    // Callbacks for quest events
    std::function<void(uint64_t, const std::string&)> on_quest_started_;
    std::function<void(uint64_t, const std::string&)> on_quest_completed_;
    std::function<void(uint64_t, const std::string&)> on_quest_failed_;
    std::function<void(uint64_t, const std::string&, const std::string&, int)> on_objective_updated_;
    std::function<void(uint64_t, const std::string&)> on_quest_reward_claimed_;
};
