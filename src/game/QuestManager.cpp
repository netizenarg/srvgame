#include "game/QuestManager.hpp"

//TODO: we need refactor architecture, else it do cyclic include in .hpp file
#include "game/LogicCore.hpp"

namespace fs = std::filesystem;

// =============== Singleton ===============

static std::mutex s_instance_mutex;
static QuestManager* s_instance = nullptr;

QuestManager& QuestManager::GetInstance() {
    std::lock_guard<std::mutex> lock(s_instance_mutex);
    if (!s_instance) {
        s_instance = new QuestManager();
    }
    return *s_instance;
}

// =============== Lifecycle ===============

void QuestManager::Initialize() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto& config = ConfigManager::GetInstance();
    std::string quests_path = config.GetString("game.quests.path", "./data/quests/");
    if (!LoadQuestsFromDirectory(quests_path)) {
        Logger::Warn("QuestManager: No quests loaded from {}", quests_path);
    }
    Logger::Info("QuestManager initialized");
}

void QuestManager::Shutdown() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    quest_definitions_.clear();
    quest_chains_.clear();
    entity_quests_.clear();
    Logger::Info("QuestManager shut down");
}

// =============== Quest Data Loading ===============

bool QuestManager::LoadQuestsFromFile(const std::string& filepath) {
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            Logger::Error("QuestManager: Cannot open quest file: {}", filepath);
            return false;
        }

        nlohmann::json j;
        file >> j;

        int loaded = 0;
        if (j.is_array()) {
            for (const auto& item : j) {
                QuestDefinition def;
                if (ParseQuestDefinition(item, def)) {
                    std::unique_lock<std::shared_mutex> lock(mutex_);
                    quest_definitions_[def.id] = std::move(def);
                    loaded++;
                }
            }
        } else if (j.is_object() && j.contains("quests")) {
            for (const auto& item : j["quests"]) {
                QuestDefinition def;
                if (ParseQuestDefinition(item, def)) {
                    std::unique_lock<std::shared_mutex> lock(mutex_);
                    quest_definitions_[def.id] = std::move(def);
                    loaded++;
                }
            }
            if (j.contains("quest_chains") && j["quest_chains"].is_object()) {
                for (const auto& [chain_id, quests_array] : j["quest_chains"].items()) {
                    // Convert each element in the array to uint64_t
                    std::vector<uint64_t> quest_ids;
                    for (const auto& qid_val : quests_array) {
                        if (qid_val.is_number())
                            quest_ids.push_back(qid_val.get<uint64_t>());
                        else
                            quest_ids.push_back(std::stoull(qid_val.get<std::string>()));
                    }
                    quest_chains_[chain_id] = quest_ids;
                }
            }
        }

        Logger::Info("QuestManager: Loaded {} quest(s) from {}", loaded, filepath);
        return loaded > 0;
    } catch (const std::exception& e) {
        Logger::Error("QuestManager: Exception while loading {}: {}", filepath, e.what());
        return false;
    }
}

bool QuestManager::LoadQuestsFromDirectory(const std::string& directory) {
    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        Logger::Warn("QuestManager: Directory does not exist: {}", directory);
        return false;
    }

    int total = 0;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.path().extension() == ".json") {
            if (LoadQuestsFromFile(entry.path().string())) {
                total++;
            }
        }
    }
    Logger::Info("QuestManager: Loaded {} quest file(s)", total);
    return total > 0;
}

bool QuestManager::SaveQuestsToFile(const std::string& filepath) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    try {
        nlohmann::json j;
        nlohmann::json quests_array = nlohmann::json::array();
        for (const auto& [id, def] : quest_definitions_) {
            quests_array.push_back(def.Serialize());
        }
        j["quests"] = quests_array;

        nlohmann::json chains_obj;
        for (const auto& [chain_id, quests] : quest_chains_) {
            chains_obj[chain_id] = quests;  // quests is vector<uint64_t>, becomes array of numbers
        }
        j["quest_chains"] = chains_obj;

        std::ofstream file(filepath);
        if (!file.is_open()) {
            Logger::Error("QuestManager: Cannot open file for writing: {}", filepath);
            return false;
        }
        file << j.dump(2);
        return true;
    } catch (const std::exception& e) {
        Logger::Error("QuestManager: Failed to save quests: {}", e.what());
        return false;
    }
}

// =============== JSON Parsing Helpers ===============

bool QuestManager::ParseQuestDefinition(const nlohmann::json& j, QuestDefinition& def) {
    try {
        // Parse id – can be number or string
        if (j.at("id").is_number())
            def.id = j.at("id").get<uint64_t>();
        else
            def.id = std::stoull(j.at("id").get<std::string>());

        def.name = j.value("name", def.name);
        def.description = j.value("description", "");
        def.completion_text = j.value("completion_text", "");
        def.failure_text = j.value("failure_text", "");

        def.type = static_cast<QuestType>(j.value("type", 1));
        def.difficulty = static_cast<QuestDifficulty>(j.value("difficulty", 2));

        def.giver_npc_id = j.value("giver_npc_id", "");
        def.turn_in_npc_id = j.value("turn_in_npc_id", "");

        if (j.contains("giver_location") && j["giver_location"].is_array()) {
            auto& arr = j["giver_location"];
            def.giver_location = glm::vec3(arr[0], arr[1], arr[2]);
        }
        if (j.contains("turn_in_location") && j["turn_in_location"].is_array()) {
            auto& arr = j["turn_in_location"];
            def.turn_in_location = glm::vec3(arr[0], arr[1], arr[2]);
        }

        if (j.contains("prerequisite_quests")) {
            auto& prereq_array = j["prerequisite_quests"];
            std::vector<uint64_t> prereq_ids;
            for (const auto& val : prereq_array) {
                if (val.is_number())
                    prereq_ids.push_back(val.get<uint64_t>());
                else
                    prereq_ids.push_back(std::stoull(val.get<std::string>()));
            }
            def.prerequisite_quests = std::move(prereq_ids);
        }

        // Objectives
        if (j.contains("objectives") && j["objectives"].is_array()) {
            for (const auto& obj_j : j["objectives"]) {
                QuestObjective obj;
                obj.id = obj_j.value("id", "");
                obj.type = static_cast<ObjectiveType>(obj_j.value("type", 0));
                obj.target = obj_j.value("target", "");
                obj.required_count = obj_j.value("count", 1);
                obj.description = obj_j.value("description", "");
                obj.location_hint = obj_j.value("location_hint", "");
                if (obj_j.contains("location") && obj_j["location"].is_array()) {
                    auto& arr = obj_j["location"];
                    obj.location = glm::vec3(arr[0], arr[1], arr[2]);
                }
                obj.radius = obj_j.value("radius", 0.0f);
                obj.time_limit = obj_j.value("time_limit", 0.0f);
                obj.is_optional = obj_j.value("optional", false);
                obj.is_hidden = obj_j.value("hidden", false);
                def.objectives.push_back(std::move(obj));
            }
        }

        // Rewards
        if (j.contains("rewards")) {
            const auto& r = j["rewards"];
            def.reward.experience = r.value("experience", 0);
            def.reward.gold = r.value("gold", 0);
            def.reward.reputation = r.value("reputation", 0);
            def.reward.reputation_faction = r.value("reputation_faction", "");

            if (r.contains("items") && r["items"].is_array()) {
                for (const auto& item_j : r["items"]) {
                    std::string item_id = item_j["id"].get<std::string>();
                    int count = item_j.value("count", 1);
                    def.reward.items.emplace_back(item_id, count);
                }
            }
            if (r.contains("skills")) {
                def.reward.skills = r["skills"].get<std::vector<std::string>>();
            }
            if (r.contains("titles")) {
                def.reward.titles = r["titles"].get<std::vector<std::string>>();
            }
            if (r.contains("cosmetics")) {
                def.reward.cosmetics = r["cosmetics"].get<std::vector<std::string>>();
            }
            def.reward.skill_points = r.value("skill_points", 0);
            def.reward.talent_points = r.value("talent_points", 0);
            def.reward.honor_points = r.value("honor_points", 0);

            def.reward.is_choice_reward = r.value("is_choice_reward", false);
            def.reward.choose_count = r.value("choose_count", 1);
            if (r.contains("choice_items") && r["choice_items"].is_array()) {
                for (const auto& item_j : r["choice_items"]) {
                    std::string item_id = item_j["id"].get<std::string>();
                    int count = item_j.value("count", 1);
                    def.reward.choice_items.emplace_back(item_id, count);
                }
            }
        }

        if (j.contains("next_quests")) {
            auto& next_array = j["next_quests"];
            std::vector<uint64_t> next_ids;
            for (const auto& val : next_array) {
                if (val.is_number())
                    next_ids.push_back(val.get<uint64_t>());
                else
                    next_ids.push_back(std::stoull(val.get<std::string>()));
            }
            def.next_quests = std::move(next_ids);
        }

        def.is_repeatable = j.value("repeatable", false);
        def.repeat_cooldown_hours = j.value("repeat_cooldown_hours", 24);
        def.is_shareable = j.value("shareable", false);
        def.is_discoverable = j.value("discoverable", false);
        def.auto_complete = j.value("auto_complete", false);
        def.min_level = j.value("min_level", 1);
        def.max_level = j.value("max_level", 100);
        def.suggested_party_size = j.value("suggested_party_size", 1);
        def.zone = j.value("zone", "");
        if (j.contains("tags")) {
            def.tags = j["tags"].get<std::vector<std::string>>();
        }

        return true;
    } catch (const std::exception& e) {
        Logger::Error("QuestManager: ParseQuestDefinition error: {}", e.what());
        return false;
    }
}

// =============== Quest Definition Queries ===============

const QuestDefinition* QuestManager::GetQuestDefinition(uint64_t quest_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = quest_definitions_.find(quest_id);
    return it != quest_definitions_.end() ? &it->second : nullptr;
}

std::vector<uint64_t> QuestManager::GetAllQuestIds() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<uint64_t> ids;
    ids.reserve(quest_definitions_.size());
    for (const auto& pair : quest_definitions_) {
        ids.push_back(pair.first);
    }
    return ids;
}

std::vector<uint64_t> QuestManager::GetQuestsByType(QuestType type) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<uint64_t> result;
    for (const auto& [id, def] : quest_definitions_) {
        if (def.type == type) {
            result.push_back(id);
        }
    }
    return result;
}

std::vector<uint64_t> QuestManager::GetQuestsByZone(const std::string& zone) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<uint64_t> result;
    for (const auto& [id, def] : quest_definitions_) {
        if (def.zone == zone) {
            result.push_back(id);
        }
    }
    return result;
}

std::vector<uint64_t> QuestManager::GetQuestsByLevel(int level) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<uint64_t> result;
    for (const auto& [id, def] : quest_definitions_) {
        if (level >= def.min_level && level <= def.max_level) {
            result.push_back(id);
        }
    }
    return result;
}

// =============== Entity Quest Management ===============

bool QuestManager::CanStartQuest(uint64_t entity_id, uint64_t quest_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto def_it = quest_definitions_.find(quest_id);
    if (def_it == quest_definitions_.end()) return false;

    const auto& def = def_it->second;

    // Check if already active or completed (and not repeatable)
    auto entity_it = entity_quests_.find(entity_id);
    if (entity_it != entity_quests_.end()) {
        if (entity_it->second.active_quests.count(quest_id)) return false;

        auto comp_it = entity_it->second.completed_quests.find(quest_id);
        if (comp_it != entity_it->second.completed_quests.end()) {
            if (!def.is_repeatable) return false;
            // For daily/weekly, check cooldown
            if (def.type == QuestType::DAILY_QUEST || def.type == QuestType::WEEKLY_QUEST) {
                auto now = std::chrono::system_clock::now();
                auto& timestamps = (def.type == QuestType::DAILY_QUEST)
                                    ? entity_it->second.daily_completions
                                    : entity_it->second.weekly_completions;
                auto ts_it = timestamps.find(quest_id);
                if (ts_it != timestamps.end()) {
                    auto hours = std::chrono::duration_cast<std::chrono::hours>(now - ts_it->second).count();
                    if (def.type == QuestType::DAILY_QUEST && hours < 24) return false;
                    if (def.type == QuestType::WEEKLY_QUEST && hours < 7*24) return false;
                }
            }
        }
    }

    // Prerequisite quests
    for (const auto& prereq : def.prerequisite_quests) {
        if (!IsQuestCompleted(entity_id, prereq)) return false;
    }

    // External checks (level, class, etc.) are done by caller.
    return true;
}

bool QuestManager::StartQuest(uint64_t entity_id, uint64_t quest_id) {
    if (!CanStartQuest(entity_id, quest_id)) {
        Logger::Warn("QuestManager: Entity {} cannot start quest {}", entity_id, quest_id);
        return false;
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto& data = entity_quests_[entity_id];
    if (data.active_quests.size() >= MAX_ACTIVE_QUESTS) {
        Logger::Warn("QuestManager: Entity {} has too many active quests", entity_id);
        return false;
    }

    const auto& def = quest_definitions_[quest_id];

    QuestProgress progress;
    progress.quest_id = quest_id;
    progress.state = QuestState::IN_PROGRESS;
    progress.start_time = std::chrono::system_clock::now();

    for (const auto& obj : def.objectives) {
        ObjectiveProgress obj_prog;
        obj_prog.current_count = 0;
        obj_prog.time_remaining = obj.time_limit;
        obj_prog.completed = false;
        progress.objective_progress[obj.id] = obj_prog;
    }

    data.active_quests[quest_id] = std::move(progress);
    data.discovered_quests[quest_id] = true;

    lock.unlock();
    FireQuestEvent("quest_started", entity_id, quest_id);
    Logger::Debug("QuestManager: Entity {} started quest {}", entity_id, quest_id);
    return true;
}

bool QuestManager::AbandonQuest(uint64_t entity_id, uint64_t quest_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto entity_it = entity_quests_.find(entity_id);
    if (entity_it == entity_quests_.end()) return false;

    auto& active = entity_it->second.active_quests;
    auto it = active.find(quest_id);
    if (it == active.end()) return false;

    it->second.state = QuestState::ABANDONED;
    active.erase(it);

    if (entity_it->second.tracked_quest_id == quest_id) {
        entity_it->second.tracked_quest_id = 0;
    }

    lock.unlock();
    FireQuestEvent("quest_abandoned", entity_id, quest_id);
    Logger::Debug("QuestManager: Entity {} abandoned quest {}", entity_id, quest_id);
    return true;
}

bool QuestManager::CanCompleteQuest(uint64_t entity_id, uint64_t quest_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto entity_it = entity_quests_.find(entity_id);
    if (entity_it == entity_quests_.end()) return false;

    auto it = entity_it->second.active_quests.find(quest_id);
    if (it == entity_it->second.active_quests.end() || it->second.state != QuestState::IN_PROGRESS)
        return false;

    const auto& def = quest_definitions_.at(quest_id);
    for (const auto& obj : def.objectives) {
        auto prog_it = it->second.objective_progress.find(obj.id);
        if (prog_it == it->second.objective_progress.end()) return false;
        if (prog_it->second.current_count < obj.required_count && !obj.is_optional) {
            return false;
        }
    }
    return true;
}

bool QuestManager::CompleteQuest(uint64_t entity_id, uint64_t quest_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto entity_it = entity_quests_.find(entity_id);
    if (entity_it == entity_quests_.end()) return false;

    auto it = entity_it->second.active_quests.find(quest_id);
    if (it == entity_it->second.active_quests.end()) return false;
    if (it->second.state != QuestState::IN_PROGRESS) return false;

    const auto& def = quest_definitions_[quest_id];
    bool all_complete = true;
    for (const auto& obj : def.objectives) {
        auto prog_it = it->second.objective_progress.find(obj.id);
        if (prog_it == it->second.objective_progress.end()) {
            all_complete = false;
            break;
        }
        if (prog_it->second.current_count < obj.required_count && !obj.is_optional) {
            all_complete = false;
            break;
        }
    }

    if (!all_complete) return false;

    it->second.state = QuestState::COMPLETED;
    it->second.completion_time = std::chrono::system_clock::now();

    lock.unlock();
    FireQuestEvent("quest_completed", entity_id, quest_id);
    Logger::Debug("QuestManager: Entity {} completed quest {}", entity_id, quest_id);
    return true;
}

bool QuestManager::FailQuest(uint64_t entity_id, uint64_t quest_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto entity_it = entity_quests_.find(entity_id);
    if (entity_it == entity_quests_.end()) return false;

    auto it = entity_it->second.active_quests.find(quest_id);
    if (it == entity_it->second.active_quests.end()) return false;

    it->second.state = QuestState::FAILED;
    it->second.completion_time = std::chrono::system_clock::now();

    // Move to completed quests (as failed)
    entity_it->second.completed_quests[quest_id] = it->second;
    entity_it->second.active_quests.erase(it);

    if (entity_it->second.tracked_quest_id == quest_id) {
        entity_it->second.tracked_quest_id = 0;
    }

    lock.unlock();
    FireQuestEvent("quest_failed", entity_id, quest_id);
    Logger::Debug("QuestManager: Entity {} failed quest {}", entity_id, quest_id);
    return true;
}

bool QuestManager::ClaimQuestReward(uint64_t entity_id, uint64_t quest_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto entity_it = entity_quests_.find(entity_id);
    if (entity_it == entity_quests_.end()) return false;

    auto it = entity_it->second.active_quests.find(quest_id);
    if (it == entity_it->second.active_quests.end() || it->second.state != QuestState::COMPLETED) {
        return false;
    }

    const auto& def = quest_definitions_[quest_id];
    auto& progress = it->second;

    // TODO: Actually give rewards to the entity (via external systems).
    // For now, just mark rewards claimed.
    progress.state = QuestState::REWARDS_CLAIMED;
    progress.rewards_claimed = true;
    progress.completion_count++;

    // Move to completed if not repeatable
    if (!def.is_repeatable) {
        entity_it->second.completed_quests[quest_id] = progress;
        entity_it->second.active_quests.erase(it);
    } else {
        // Reset for next time
        progress.state = QuestState::NOT_STARTED;
        progress.rewards_claimed = false;
        for (auto& [_, obj_prog] : progress.objective_progress) {
            obj_prog.current_count = 0;
            obj_prog.completed = false;
        }
        // Record completion time for cooldown
        if (def.type == QuestType::DAILY_QUEST) {
            entity_it->second.daily_completions[quest_id] = std::chrono::system_clock::now();
        } else if (def.type == QuestType::WEEKLY_QUEST) {
            entity_it->second.weekly_completions[quest_id] = std::chrono::system_clock::now();
        }
    }

    entity_it->second.total_completed++;
    entity_it->second.total_points += static_cast<int>(def.difficulty) + 1;

    if (entity_it->second.tracked_quest_id == quest_id) {
        entity_it->second.tracked_quest_id = 0;
    }

    lock.unlock();
    FireQuestEvent("quest_reward_claimed", entity_id, quest_id);

    // Auto-start next quests
    for (const auto& next_id : def.next_quests) {
        if (CanStartQuest(entity_id, next_id)) {
            StartQuest(entity_id, next_id);
        }
    }

    Logger::Debug("QuestManager: Entity {} claimed reward for quest {}", entity_id, quest_id);
    return true;
}

// =============== Quest Progress Updates ===============

void QuestManager::UpdateObjective(uint64_t entity_id, uint64_t quest_id,
                                   const std::string& objective_id, int delta) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto entity_it = entity_quests_.find(entity_id);
    if (entity_it == entity_quests_.end()) return;

    auto it = entity_it->second.active_quests.find(quest_id);
    if (it == entity_it->second.active_quests.end()) return;
    if (it->second.state != QuestState::IN_PROGRESS) return;

    auto& prog_map = it->second.objective_progress;
    auto obj_it = prog_map.find(objective_id);
    if (obj_it == prog_map.end()) return;

    const auto& def = quest_definitions_[quest_id];
    const QuestObjective* obj_def = nullptr;
    for (const auto& obj : def.objectives) {
        if (obj.id == objective_id) {
            obj_def = &obj;
            break;
        }
    }
    if (!obj_def) return;

    int old_count = obj_it->second.current_count;
    int new_count = std::min(old_count + delta, obj_def->required_count);
    if (new_count == old_count) return;

    obj_it->second.current_count = new_count;
    if (!obj_it->second.completed && new_count >= obj_def->required_count) {
        obj_it->second.completed = true;
    }

    nlohmann::json extra;
    extra["objective_id"] = objective_id;
    extra["current"] = new_count;
    extra["required"] = obj_def->required_count;
    lock.unlock();

    FireQuestEvent("objective_updated", entity_id, quest_id, extra);

    // Re-check completion after update
    CompleteQuest(entity_id, quest_id);
}

void QuestManager::OnMonsterKilled(uint64_t entity_id, const std::string& monster_id) {
    // Collect quests to update
    std::vector<std::pair<uint64_t, std::string>> updates; // (quest_id, objective_id)
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto entity_it = entity_quests_.find(entity_id);
        if (entity_it == entity_quests_.end()) return;

        for (const auto& [qid, progress] : entity_it->second.active_quests) {
            if (progress.state != QuestState::IN_PROGRESS) continue;
            const auto& def = quest_definitions_.at(qid);
            for (const auto& obj : def.objectives) {
                if (obj.type == ObjectiveType::KILL_MONSTER && obj.target == monster_id) {
                    updates.emplace_back(qid, obj.id);
                }
            }
        }
    }
    // Apply updates outside lock
    for (const auto& [qid, obj_id] : updates) {
        UpdateObjective(entity_id, qid, obj_id, 1);
    }
}

void QuestManager::OnItemCollected(uint64_t entity_id, const std::string& item_id, int count) {
    std::vector<std::pair<uint64_t, std::string>> updates;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto entity_it = entity_quests_.find(entity_id);
        if (entity_it == entity_quests_.end()) return;
        for (const auto& [qid, progress] : entity_it->second.active_quests) {
            if (progress.state != QuestState::IN_PROGRESS) continue;
            const auto& def = quest_definitions_.at(qid);
            for (const auto& obj : def.objectives) {
                if (obj.type == ObjectiveType::COLLECT_ITEM && obj.target == item_id) {
                    updates.emplace_back(qid, obj.id);
                }
            }
        }
    }
    for (const auto& [qid, obj_id] : updates) {
        UpdateObjective(entity_id, qid, obj_id, count);
    }
}

void QuestManager::OnNPCTalkedTo(uint64_t entity_id, uint64_t npc_id) {
    auto to_uint64 = [](const std::string& s) -> uint64_t {
        try { return std::stoull(s); } catch (...) { return 0; }
    };

    std::vector<std::pair<uint64_t, std::string>> updates;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto entity_it = entity_quests_.find(entity_id);
        if (entity_it == entity_quests_.end()) return;

        for (const auto& [qid, progress] : entity_it->second.active_quests) {
            if (progress.state != QuestState::IN_PROGRESS) continue;
            const auto& def = quest_definitions_.at(qid);
            for (const auto& obj : def.objectives) {
                if (obj.type == ObjectiveType::TALK_TO_NPC) {
                    // Convert the objective's target (string) to uint64_t and compare
                    if (to_uint64(obj.target) == npc_id) {
                        updates.emplace_back(qid, obj.id);
                    }
                }
            }
        }
    }
    for (const auto& [qid, obj_id] : updates) {
        UpdateObjective(entity_id, qid, obj_id, 1);
    }

    // Quest discovery from NPC
    std::vector<uint64_t> possible_quests;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        for (const auto& [qid, def] : quest_definitions_) {
            if (def.is_discoverable && !HasQuest(entity_id, qid) && CanStartQuest(entity_id, qid)) {
                // Convert giver_npc_id (string) to uint64_t and compare
                if (to_uint64(def.giver_npc_id) == npc_id) {
                    possible_quests.push_back(qid);
                }
            }
        }
    }
    for (const auto& qid : possible_quests) {
        StartQuest(entity_id, qid);
    }
}

void QuestManager::OnLocationVisited(uint64_t entity_id, const glm::vec3& location, float radius) {
    std::vector<std::pair<uint64_t, std::string>> updates;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto entity_it = entity_quests_.find(entity_id);
        if (entity_it == entity_quests_.end()) return;
        for (const auto& [qid, progress] : entity_it->second.active_quests) {
            if (progress.state != QuestState::IN_PROGRESS) continue;
            const auto& def = quest_definitions_.at(qid);
            for (const auto& obj : def.objectives) {
                if (obj.type == ObjectiveType::VISIT_LOCATION) {
                    float dist = glm::distance(location, obj.location);
                    if (dist <= radius + obj.radius) {
                        updates.emplace_back(qid, obj.id);
                    }
                }
            }
        }
    }
    for (const auto& [qid, obj_id] : updates) {
        UpdateObjective(entity_id, qid, obj_id, 1);
    }
}

void QuestManager::OnDungeonCompleted(uint64_t entity_id, const std::string& dungeon_id) {
    std::vector<std::pair<uint64_t, std::string>> updates;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto entity_it = entity_quests_.find(entity_id);
        if (entity_it == entity_quests_.end()) return;
        for (const auto& [qid, progress] : entity_it->second.active_quests) {
            if (progress.state != QuestState::IN_PROGRESS) continue;
            const auto& def = quest_definitions_.at(qid);
            for (const auto& obj : def.objectives) {
                if (obj.type == ObjectiveType::COMPLETE_DUNGEON && obj.target == dungeon_id) {
                    updates.emplace_back(qid, obj.id);
                }
            }
        }
    }
    for (const auto& [qid, obj_id] : updates) {
        UpdateObjective(entity_id, qid, obj_id, 1);
    }
}

void QuestManager::OnLevelGained(uint64_t entity_id, int new_level) {
    std::vector<std::pair<uint64_t, std::string>> updates;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto entity_it = entity_quests_.find(entity_id);
        if (entity_it == entity_quests_.end()) return;
        for (const auto& [qid, progress] : entity_it->second.active_quests) {
            if (progress.state != QuestState::IN_PROGRESS) continue;
            const auto& def = quest_definitions_.at(qid);
            for (const auto& obj : def.objectives) {
                if (obj.type == ObjectiveType::REACH_LEVEL && new_level >= obj.required_count) {
                    // This objective is completed instantly
                    int delta = obj.required_count - progress.objective_progress.at(obj.id).current_count;
                    if (delta > 0) {
                        updates.emplace_back(qid, obj.id);
                    }
                }
            }
        }
    }
    for (const auto& [qid, obj_id] : updates) {
        UpdateObjective(entity_id, qid, obj_id, 1000); // large delta (will be capped)
    }
}

void QuestManager::OnSkillLearned(uint64_t entity_id, const std::string& skill_id) {
    std::vector<std::pair<uint64_t, std::string>> updates;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto entity_it = entity_quests_.find(entity_id);
        if (entity_it == entity_quests_.end()) return;
        for (const auto& [qid, progress] : entity_it->second.active_quests) {
            if (progress.state != QuestState::IN_PROGRESS) continue;
            const auto& def = quest_definitions_.at(qid);
            for (const auto& obj : def.objectives) {
                if (obj.type == ObjectiveType::LEARN_SKILL && obj.target == skill_id) {
                    updates.emplace_back(qid, obj.id);
                }
            }
        }
    }
    for (const auto& [qid, obj_id] : updates) {
        UpdateObjective(entity_id, qid, obj_id, 1);
    }
}

void QuestManager::OnTimerElapsed(uint64_t entity_id, float delta_time) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto entity_it = entity_quests_.find(entity_id);
    if (entity_it == entity_quests_.end()) return;

    std::vector<uint64_t> quests_to_fail;
    for (auto& [qid, progress] : entity_it->second.active_quests) {
        if (progress.state != QuestState::IN_PROGRESS) continue;
        bool expired = false;
        for (auto& [obj_id, obj_prog] : progress.objective_progress) {
            if (obj_prog.time_remaining > 0) {
                obj_prog.time_remaining -= delta_time;
                if (obj_prog.time_remaining <= 0 && !obj_prog.completed) {
                    expired = true;
                }
            }
        }
        if (expired) {
            quests_to_fail.push_back(qid);
        }
    }
    lock.unlock();

    for (const auto& qid : quests_to_fail) {
        FailQuest(entity_id, qid);
    }
}

// =============== Query Methods ===============

bool QuestManager::HasQuest(uint64_t entity_id, uint64_t quest_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = entity_quests_.find(entity_id);
    if (it == entity_quests_.end()) return false;
    return it->second.active_quests.count(quest_id) || it->second.completed_quests.count(quest_id);
}

bool QuestManager::IsQuestCompleted(uint64_t entity_id, uint64_t quest_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = entity_quests_.find(entity_id);
    if (it == entity_quests_.end()) return false;
    auto comp_it = it->second.completed_quests.find(quest_id);
    return comp_it != it->second.completed_quests.end() &&
           (comp_it->second.state == QuestState::COMPLETED || comp_it->second.state == QuestState::REWARDS_CLAIMED);
}

bool QuestManager::IsQuestInProgress(uint64_t entity_id, uint64_t quest_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = entity_quests_.find(entity_id);
    if (it == entity_quests_.end()) return false;
    auto act_it = it->second.active_quests.find(quest_id);
    return act_it != it->second.active_quests.end() && act_it->second.state == QuestState::IN_PROGRESS;
}

QuestState QuestManager::GetQuestState(uint64_t entity_id, uint64_t quest_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = entity_quests_.find(entity_id);
    if (it == entity_quests_.end()) return QuestState::NOT_STARTED;
    auto act_it = it->second.active_quests.find(quest_id);
    if (act_it != it->second.active_quests.end()) return act_it->second.state;
    auto comp_it = it->second.completed_quests.find(quest_id);
    if (comp_it != it->second.completed_quests.end()) return comp_it->second.state;
    return QuestState::NOT_STARTED;
}

std::vector<QuestProgress> QuestManager::GetAllEntityQuests(uint64_t entity_id) const {
    std::vector<QuestProgress> result;
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = entity_quests_.find(entity_id);
    if (it == entity_quests_.end()) return result;
    for (const auto& [_, prog] : it->second.active_quests) result.push_back(prog);
    for (const auto& [_, prog] : it->second.completed_quests) result.push_back(prog);
    return result;
}

std::vector<QuestProgress> QuestManager::GetActiveQuests(uint64_t entity_id) const {
    std::vector<QuestProgress> result;
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = entity_quests_.find(entity_id);
    if (it == entity_quests_.end()) return result;
    for (const auto& [_, prog] : it->second.active_quests) result.push_back(prog);
    return result;
}

std::vector<QuestProgress> QuestManager::GetCompletedQuests(uint64_t entity_id) const {
    std::vector<QuestProgress> result;
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = entity_quests_.find(entity_id);
    if (it == entity_quests_.end()) return result;
    for (const auto& [_, prog] : it->second.completed_quests) result.push_back(prog);
    return result;
}

// =============== Quest Chains ===============

bool QuestManager::StartQuestChain(uint64_t entity_id, const std::string& chain_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto chain_it = quest_chains_.find(chain_id);
    if (chain_it == quest_chains_.end()) return false;

    auto& data = entity_quests_[entity_id];
    if (data.quest_chains.count(chain_id)) return false; // already started

    QuestChain chain;
    chain.chain_id = chain_id;
    chain.name = chain_id; // could be stored in separate map
    chain.quests_in_order = chain_it->second;
    chain.current_quest_index = 0;
    data.quest_chains[chain_id] = chain;

    if (!chain.quests_in_order.empty()) {
        StartQuest(entity_id, chain.quests_in_order[0]);
    }
    return true;
}

bool QuestManager::AdvanceQuestChain(uint64_t entity_id, const std::string& chain_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto entity_it = entity_quests_.find(entity_id);
    if (entity_it == entity_quests_.end()) return false;
    auto chain_it = entity_it->second.quest_chains.find(chain_id);
    if (chain_it == entity_it->second.quest_chains.end()) return false;

    auto& chain = chain_it->second;
    if (chain.is_completed) return false;

    chain.current_quest_index++;
    if ((uint64_t)chain.current_quest_index >= chain.quests_in_order.size()) {
        chain.is_completed = true;
        // Optionally give chain completion reward
        return true;
    }

    uint64_t next_quest = chain.quests_in_order[chain.current_quest_index];
    lock.unlock();
    return StartQuest(entity_id, next_quest);
}

bool QuestManager::IsQuestChainCompleted(uint64_t entity_id, const std::string& chain_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto entity_it = entity_quests_.find(entity_id);
    if (entity_it == entity_quests_.end()) return false;
    auto chain_it = entity_it->second.quest_chains.find(chain_id);
    return chain_it != entity_it->second.quest_chains.end() && chain_it->second.is_completed;
}

std::vector<QuestChain> QuestManager::GetEntityQuestChains(uint64_t entity_id) const {
    std::vector<QuestChain> result;
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = entity_quests_.find(entity_id);
    if (it == entity_quests_.end()) return result;
    for (const auto& [_, chain] : it->second.quest_chains) {
        result.push_back(chain);
    }
    return result;
}

// =============== Daily / Weekly Quests ===============

void QuestManager::ResetDailyQuests(uint64_t entity_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto& data = entity_quests_[entity_id];
    data.last_daily_reset = std::chrono::system_clock::now();
    data.daily_completions.clear();
    Logger::Debug("QuestManager: Reset daily quests for entity {}", entity_id);
}

void QuestManager::ResetWeeklyQuests(uint64_t entity_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto& data = entity_quests_[entity_id];
    data.last_weekly_reset = std::chrono::system_clock::now();
    data.weekly_completions.clear();
    Logger::Debug("QuestManager: Reset weekly quests for entity {}", entity_id);
}

bool QuestManager::CanAcceptDailyQuest(uint64_t entity_id, uint64_t quest_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto def_it = quest_definitions_.find(quest_id);
    if (def_it == quest_definitions_.end() || def_it->second.type != QuestType::DAILY_QUEST) {
        return false;
    }
    return CanAcceptQuest(entity_id, quest_id);
}

bool QuestManager::CanAcceptWeeklyQuest(uint64_t entity_id, uint64_t quest_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto def_it = quest_definitions_.find(quest_id);
    if (def_it == quest_definitions_.end() || def_it->second.type != QuestType::WEEKLY_QUEST) {
        return false;
    }
    return CanAcceptQuest(entity_id, quest_id);
}

// =============== Quest Discovery ===============

void QuestManager::DiscoverQuest(uint64_t entity_id, uint64_t quest_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    entity_quests_[entity_id].discovered_quests[quest_id] = true;
    FireQuestEvent("quest_discovered", entity_id, quest_id);
    Logger::Debug("QuestManager: Entity {} discovered quest {}", entity_id, quest_id);
}

std::vector<uint64_t> QuestManager::GetDiscoveredQuests(uint64_t entity_id) const {
    std::vector<uint64_t> result;
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = entity_quests_.find(entity_id);
    if (it == entity_quests_.end()) return result;
    for (const auto& [qid, disc] : it->second.discovered_quests) {
        if (disc) result.push_back(qid);
    }
    return result;
}

bool QuestManager::IsQuestDiscovered(uint64_t entity_id, uint64_t quest_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = entity_quests_.find(entity_id);
    if (it == entity_quests_.end()) return false;
    auto disc_it = it->second.discovered_quests.find(quest_id);
    return disc_it != it->second.discovered_quests.end() && disc_it->second;
}

// =============== Quest Tracking ===============

void QuestManager::TrackQuest(uint64_t entity_id, uint64_t quest_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (HasQuest(entity_id, quest_id)) {
        entity_quests_[entity_id].tracked_quest_id = quest_id;
    }
}

void QuestManager::UntrackQuest(uint64_t entity_id, uint64_t quest_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto& data = entity_quests_[entity_id];
    if (data.tracked_quest_id == quest_id) {
        data.tracked_quest_id = 0;
    }
}

bool QuestManager::IsQuestTracked(uint64_t entity_id, uint64_t quest_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = entity_quests_.find(entity_id);
    return it != entity_quests_.end() && it->second.tracked_quest_id == quest_id;
}

uint64_t QuestManager::GetTrackedQuest(uint64_t entity_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = entity_quests_.find(entity_id);
    return it != entity_quests_.end() ? it->second.tracked_quest_id : 0;
}

// =============== NPC Interaction ===============

std::vector<uint64_t> QuestManager::GetQuestsFromNPC(uint64_t entity_id, uint64_t npc_id) const {
    auto to_uint64 = [](const std::string& s) -> uint64_t {
        try { return std::stoull(s); } catch (...) { return 0; }
    };

    std::vector<uint64_t> result;
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto& [qid, def] : quest_definitions_) {
        if (to_uint64(def.giver_npc_id) == npc_id && !HasQuest(entity_id, qid) && CanStartQuest(entity_id, qid)) {
            result.push_back(qid);
        }
    }
    return result;
}

std::vector<uint64_t> QuestManager::GetQuestsToTurnIn(uint64_t entity_id, uint64_t npc_id) const {
    auto to_uint64 = [](const std::string& s) -> uint64_t {
        try { return std::stoull(s); } catch (...) { return 0; }
    };

    std::vector<uint64_t> result;
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = entity_quests_.find(entity_id);
    if (it == entity_quests_.end()) return result;

    for (const auto& [qid, progress] : it->second.active_quests) {
        if (progress.state == QuestState::COMPLETED) {
            auto def_it = quest_definitions_.find(qid);
            if (def_it != quest_definitions_.end() && to_uint64(def_it->second.turn_in_npc_id) == npc_id) {
                result.push_back(qid);
            }
        }
    }
    return result;
}

// =============== Statistics ===============

int QuestManager::GetTotalQuestsCompleted(uint64_t entity_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = entity_quests_.find(entity_id);
    return it != entity_quests_.end() ? it->second.total_completed : 0;
}

int QuestManager::GetTotalQuestPoints(uint64_t entity_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = entity_quests_.find(entity_id);
    return it != entity_quests_.end() ? it->second.total_points : 0;
}

std::unordered_map<std::string, int> QuestManager::GetQuestCompletionByZone(uint64_t entity_id) const {
    std::unordered_map<std::string, int> result;
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = entity_quests_.find(entity_id);
    if (it == entity_quests_.end()) return result;
    for (const auto& [qid, _] : it->second.completed_quests) {
        auto def_it = quest_definitions_.find(qid);
        if (def_it != quest_definitions_.end()) {
            result[def_it->second.zone]++;
        }
    }
    return result;
}

// =============== Serialization ===============

nlohmann::json QuestManager::EntityQuestData::Serialize() const {
    nlohmann::json j;
    for (const auto& [qid, prog] : active_quests) {
        j["active"][std::to_string(qid)] = prog.Serialize();   // key as string
    }
    for (const auto& [qid, prog] : completed_quests) {
        j["completed"][std::to_string(qid)] = prog.Serialize();
    }
    for (const auto& [cid, chain] : quest_chains) {
        j["chains"][cid] = chain.Serialize();
    }
    for (const auto& [qid, disc] : discovered_quests) {
        j["discovered"][std::to_string(qid)] = disc;
    }
    j["tracked"] = std::to_string(tracked_quest_id);

    for (const auto& [qid, tp] : daily_completions) {
        j["daily"][std::to_string(qid)] = std::chrono::system_clock::to_time_t(tp);
    }
    for (const auto& [qid, tp] : weekly_completions) {
        j["weekly"][std::to_string(qid)] = std::chrono::system_clock::to_time_t(tp);
    }

    j["total_completed"] = total_completed;
    j["total_points"] = total_points;
    j["last_daily_reset"] = std::chrono::system_clock::to_time_t(last_daily_reset);
    j["last_weekly_reset"] = std::chrono::system_clock::to_time_t(last_weekly_reset);
    return j;
}

void QuestManager::EntityQuestData::Deserialize(const nlohmann::json& j) {
    if (j.contains("active") && j["active"].is_object()) {
        for (const auto& [qid_str, prog_j] : j["active"].items()) {
            uint64_t qid = std::stoull(qid_str);
            QuestProgress prog;
            prog.Deserialize(prog_j);
            active_quests[qid] = prog;
        }
    }
    if (j.contains("completed") && j["completed"].is_object()) {
        for (const auto& [qid_str, prog_j] : j["completed"].items()) {
            uint64_t qid = std::stoull(qid_str);
            QuestProgress prog;
            prog.Deserialize(prog_j);
            completed_quests[qid] = prog;
        }
    }
    if (j.contains("chains") && j["chains"].is_object()) {
        for (const auto& [cid, chain_j] : j["chains"].items()) {
            QuestChain chain;
            chain.Deserialize(chain_j);
            quest_chains[cid] = chain;
        }
    }
    if (j.contains("discovered") && j["discovered"].is_object()) {
        for (const auto& [qid_str, disc] : j["discovered"].items()) {
            uint64_t qid = std::stoull(qid_str);
            discovered_quests[qid] = disc.get<bool>();
        }
    }
    tracked_quest_id = j.value("tracked", std::string("0")) == "0" ? 0 : std::stoull(j["tracked"].get<std::string>());

    if (j.contains("daily") && j["daily"].is_object()) {
        for (const auto& [qid_str, ts] : j["daily"].items()) {
            uint64_t qid = std::stoull(qid_str);
            daily_completions[qid] = std::chrono::system_clock::from_time_t(ts.get<std::time_t>());
        }
    }
    if (j.contains("weekly") && j["weekly"].is_object()) {
        for (const auto& [qid_str, ts] : j["weekly"].items()) {
            uint64_t qid = std::stoull(qid_str);
            weekly_completions[qid] = std::chrono::system_clock::from_time_t(ts.get<std::time_t>());
        }
    }

    total_completed = j.value("total_completed", 0);
    total_points = j.value("total_points", 0);
    last_daily_reset = std::chrono::system_clock::from_time_t(j.value("last_daily_reset", 0));
    last_weekly_reset = std::chrono::system_clock::from_time_t(j.value("last_weekly_reset", 0));
}

bool QuestManager::LoadEntityQuests(uint64_t entity_id) {
    // In a real implementation, load from database or file.
    // For now, we just ensure an entry exists.
    std::unique_lock<std::shared_mutex> lock(mutex_);
    entity_quests_[entity_id]; // creates if not exists
    Logger::Debug("QuestManager: Loaded quest data for entity {}", entity_id);
    return true;
}

bool QuestManager::SaveEntityQuests(uint64_t entity_id) const {
    // In a real implementation, save to database or file.
    // For now, just log.
    Logger::Debug("QuestManager: Saved quest data for entity {}", entity_id);
    return true;
}

nlohmann::json QuestManager::SerializeEntityQuests(uint64_t entity_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = entity_quests_.find(entity_id);
    return it != entity_quests_.end() ? it->second.Serialize() : nlohmann::json();
}

bool QuestManager::DeserializeEntityQuests(uint64_t entity_id, const nlohmann::json& data) {
    if (data.is_null()) return false;
    try {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        EntityQuestData data_struct;
        data_struct.Deserialize(data);
        entity_quests_[entity_id] = data_struct;
        return true;
    } catch (const std::exception& e) {
        Logger::Error("QuestManager: DeserializeEntityQuests failed: {}", e.what());
        return false;
    }
}

// =============== Debug ===============

void QuestManager::DebugGiveQuest(uint64_t entity_id, uint64_t quest_id) {
    StartQuest(entity_id, quest_id);
}

void QuestManager::DebugCompleteQuest(uint64_t entity_id, uint64_t quest_id) {
    // Force complete all objectives and claim reward
    if (!HasQuest(entity_id, quest_id)) {
        StartQuest(entity_id, quest_id);
    }
    // Manually set all objectives to required count
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto& data = entity_quests_[entity_id];
        auto it = data.active_quests.find(quest_id);
        if (it == data.active_quests.end()) return;
        const auto& def = quest_definitions_[quest_id];
        for (const auto& obj : def.objectives) {
            it->second.objective_progress[obj.id].current_count = obj.required_count;
            it->second.objective_progress[obj.id].completed = true;
        }
        it->second.state = QuestState::COMPLETED;
    }
    ClaimQuestReward(entity_id, quest_id);
    Logger::Debug("QuestManager: Debug complete quest {} for entity {}", quest_id, entity_id);
}

void QuestManager::DebugResetEntityQuests(uint64_t entity_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    entity_quests_.erase(entity_id);
    Logger::Debug("QuestManager: Debug reset all quests for entity {}", entity_id);
}

// =============== Internal Helpers ===============

bool QuestManager::MeetsPrerequisites(uint64_t entity_id, const QuestDefinition& def) const {
    for (const auto& prereq : def.prerequisite_quests) {
        if (!IsQuestCompleted(entity_id, prereq)) return false;
    }
    return true;
}

bool QuestManager::CanAcceptQuest(uint64_t entity_id, uint64_t quest_id) const {
    return CanStartQuest(entity_id, quest_id); // same logic now
}

void QuestManager::FireQuestEvent(const std::string& event_name, uint64_t entity_id,
                                  uint64_t quest_id, const nlohmann::json& extra) {
    nlohmann::json data;
    data["entity_id"] = entity_id;
    data["quest_id"] = quest_id;   // now a number
    data["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (!extra.empty()) {
        data["extra"] = extra;
    }
    LogicCore::GetInstance().FirePythonEvent(event_name, data);
}

// =============== Serialization Methods for Structures ===============

nlohmann::json QuestObjective::Serialize() const {
    nlohmann::json j;
    j["id"] = id;
    j["type"] = static_cast<int>(type);
    j["target"] = target;
    j["count"] = required_count;
    j["description"] = description;
    j["location_hint"] = location_hint;
    j["location"] = {location.x, location.y, location.z};
    j["radius"] = radius;
    j["time_limit"] = time_limit;
    j["optional"] = is_optional;
    j["hidden"] = is_hidden;
    return j;
}

void QuestObjective::Deserialize(const nlohmann::json& data) {
    id = data.value("id", "");
    type = static_cast<ObjectiveType>(data.value("type", 0));
    target = data.value("target", "");
    required_count = data.value("count", 1);
    description = data.value("description", "");
    location_hint = data.value("location_hint", "");
    if (data.contains("location") && data["location"].is_array() && data["location"].size() >= 3) {
        location.x = data["location"][0];
        location.y = data["location"][1];
        location.z = data["location"][2];
    }
    radius = data.value("radius", 0.0f);
    time_limit = data.value("time_limit", 0.0f);
    is_optional = data.value("optional", false);
    is_hidden = data.value("hidden", false);
}

nlohmann::json QuestReward::Serialize() const {
    nlohmann::json j;
    j["experience"] = experience;
    j["gold"] = gold;
    j["reputation"] = reputation;
    j["reputation_faction"] = reputation_faction;

    nlohmann::json items_array = nlohmann::json::array();
    for (const auto& [id, cnt] : items) {
        items_array.push_back({{"id", id}, {"count", cnt}});
    }
    j["items"] = items_array;

    j["skills"] = skills;
    j["titles"] = titles;
    j["cosmetics"] = cosmetics;
    j["skill_points"] = skill_points;
    j["talent_points"] = talent_points;
    j["honor_points"] = honor_points;
    j["is_choice_reward"] = is_choice_reward;
    j["choose_count"] = choose_count;

    nlohmann::json choice_array = nlohmann::json::array();
    for (const auto& [id, cnt] : choice_items) {
        choice_array.push_back({{"id", id}, {"count", cnt}});
    }
    j["choice_items"] = choice_array;
    return j;
}

void QuestReward::Deserialize(const nlohmann::json& data) {
    experience = data.value("experience", 0);
    gold = data.value("gold", 0);
    reputation = data.value("reputation", 0);
    reputation_faction = data.value("reputation_faction", "");

    if (data.contains("items") && data["items"].is_array()) {
        for (const auto& item_j : data["items"]) {
            items.emplace_back(item_j["id"], item_j.value("count", 1));
        }
    }
    if (data.contains("skills")) skills = data["skills"].get<std::vector<std::string>>();
    if (data.contains("titles")) titles = data["titles"].get<std::vector<std::string>>();
    if (data.contains("cosmetics")) cosmetics = data["cosmetics"].get<std::vector<std::string>>();
    skill_points = data.value("skill_points", 0);
    talent_points = data.value("talent_points", 0);
    honor_points = data.value("honor_points", 0);
    is_choice_reward = data.value("is_choice_reward", false);
    choose_count = data.value("choose_count", 1);
    if (data.contains("choice_items") && data["choice_items"].is_array()) {
        for (const auto& item_j : data["choice_items"]) {
            choice_items.emplace_back(item_j["id"], item_j.value("count", 1));
        }
    }
}

nlohmann::json QuestDefinition::Serialize() const {
    nlohmann::json j;
    j["id"] = id;
    j["name"] = name;
    j["description"] = description;
    j["completion_text"] = completion_text;
    j["failure_text"] = failure_text;
    j["type"] = static_cast<int>(type);
    j["difficulty"] = static_cast<int>(difficulty);
    j["giver_npc_id"] = giver_npc_id;
    j["turn_in_npc_id"] = turn_in_npc_id;
    j["giver_location"] = {giver_location.x, giver_location.y, giver_location.z};
    j["turn_in_location"] = {turn_in_location.x, turn_in_location.y, turn_in_location.z};
    j["prerequisite_quests"] = prerequisite_quests;

    nlohmann::json objectives_array = nlohmann::json::array();
    for (const auto& obj : objectives) objectives_array.push_back(obj.Serialize());
    j["objectives"] = objectives_array;
    j["rewards"] = reward.Serialize();
    j["next_quests"] = next_quests;
    j["repeatable"] = is_repeatable;
    j["repeat_cooldown_hours"] = repeat_cooldown_hours;
    j["shareable"] = is_shareable;
    j["discoverable"] = is_discoverable;
    j["auto_complete"] = auto_complete;
    j["min_level"] = min_level;
    j["max_level"] = max_level;
    j["suggested_party_size"] = suggested_party_size;
    j["zone"] = zone;
    j["tags"] = tags;
    return j;
}

void QuestDefinition::Deserialize(const nlohmann::json& data) {
    // Parse id – can be number or string
    if (data.at("id").is_number())
        id = data.at("id").get<uint64_t>();
    else
        id = std::stoull(data.at("id").get<std::string>());

    name = data.value("name", name);
    description = data.value("description", "");
    completion_text = data.value("completion_text", "");
    failure_text = data.value("failure_text", "");

    type = static_cast<QuestType>(data.value("type", 1));
    difficulty = static_cast<QuestDifficulty>(data.value("difficulty", 2));

    giver_npc_id = data.value("giver_npc_id", "");
    turn_in_npc_id = data.value("turn_in_npc_id", "");

    if (data.contains("giver_location") && data["giver_location"].is_array()) {
        auto& arr = data["giver_location"];
        giver_location = glm::vec3(arr[0], arr[1], arr[2]);
    }
    if (data.contains("turn_in_location") && data["turn_in_location"].is_array()) {
        auto& arr = data["turn_in_location"];
        turn_in_location = glm::vec3(arr[0], arr[1], arr[2]);
    }

    if (data.contains("prerequisite_quests")) {
        auto& prereq_array = data["prerequisite_quests"];
        std::vector<uint64_t> prereq_ids;
        for (const auto& val : prereq_array) {
            if (val.is_number())
                prereq_ids.push_back(val.get<uint64_t>());
            else
                prereq_ids.push_back(std::stoull(val.get<std::string>()));
        }
        prerequisite_quests = std::move(prereq_ids);
    }

    // Objectives
    if (data.contains("objectives") && data["objectives"].is_array()) {
        objectives.clear();
        for (const auto& obj_j : data["objectives"]) {
            QuestObjective obj;
            obj.Deserialize(obj_j);
            objectives.push_back(std::move(obj));
        }
    }

    // Rewards
    if (data.contains("rewards")) {
        reward.Deserialize(data["rewards"]);
    }

    if (data.contains("next_quests")) {
        auto& next_array = data["next_quests"];
        std::vector<uint64_t> next_ids;
        for (const auto& val : next_array) {
            if (val.is_number())
                next_ids.push_back(val.get<uint64_t>());
            else
                next_ids.push_back(std::stoull(val.get<std::string>()));
        }
        next_quests = std::move(next_ids);
    }

    is_repeatable = data.value("repeatable", false);
    repeat_cooldown_hours = data.value("repeat_cooldown_hours", 24);
    is_shareable = data.value("shareable", false);
    is_discoverable = data.value("discoverable", false);
    auto_complete = data.value("auto_complete", false);
    min_level = data.value("min_level", 1);
    max_level = data.value("max_level", 100);
    suggested_party_size = data.value("suggested_party_size", 1);
    zone = data.value("zone", "");
    if (data.contains("tags")) {
        tags = data["tags"].get<std::vector<std::string>>();
    }
}

nlohmann::json ObjectiveProgress::Serialize() const {
    nlohmann::json j;
    j["current"] = current_count;
    j["time_remaining"] = time_remaining;
    j["completed"] = completed;
    return j;
}

void ObjectiveProgress::Deserialize(const nlohmann::json& data) {
    current_count = data.value("current", 0);
    time_remaining = data.value("time_remaining", 0.0f);
    completed = data.value("completed", false);
}

nlohmann::json QuestProgress::Serialize() const {
    nlohmann::json j;
    j["quest_id"] = quest_id;           // number
    j["state"] = static_cast<int>(state);
    j["start_time"] = std::chrono::system_clock::to_time_t(start_time);
    j["completion_time"] = std::chrono::system_clock::to_time_t(completion_time);
    j["completion_count"] = completion_count;
    j["rewards_claimed"] = rewards_claimed;
    j["last_reset_time"] = std::chrono::system_clock::to_time_t(last_reset_time);

    nlohmann::json obj_prog;
    for (const auto& [obj_id, prog] : objective_progress) {
        obj_prog[obj_id] = prog.Serialize();
    }
    j["objectives"] = obj_prog;
    return j;
}

void QuestProgress::Deserialize(const nlohmann::json& data) {
    if (data.contains("quest_id"))
        quest_id = data["quest_id"].is_number() ? data["quest_id"].get<uint64_t>() : std::stoull(data["quest_id"].get<std::string>());
    state = static_cast<QuestState>(data.value("state", 0));
    start_time = std::chrono::system_clock::from_time_t(data.value("start_time", 0));
    completion_time = std::chrono::system_clock::from_time_t(data.value("completion_time", 0));
    completion_count = data.value("completion_count", 0);
    rewards_claimed = data.value("rewards_claimed", false);
    last_reset_time = std::chrono::system_clock::from_time_t(data.value("last_reset_time", 0));

    if (data.contains("objectives") && data["objectives"].is_object()) {
        for (const auto& [obj_id, prog_j] : data["objectives"].items()) {
            ObjectiveProgress prog;
            prog.Deserialize(prog_j);
            objective_progress[obj_id] = prog;
        }
    }
}

nlohmann::json QuestChain::Serialize() const {
    nlohmann::json j;
    j["chain_id"] = chain_id;
    j["name"] = name;
    j["description"] = description;
    j["quests_in_order"] = quests_in_order;
    j["current_quest_index"] = current_quest_index;
    j["is_completed"] = is_completed;
    j["chain_completion_reward"] = chain_completion_reward.Serialize();
    return j;
}

void QuestChain::Deserialize(const nlohmann::json& data) {
    chain_id = data.value("chain_id", "");
    name = data.value("name", "");
    description = data.value("description", "");
    if (data.contains("quests_in_order")) {
        auto& arr = data["quests_in_order"];
        std::vector<uint64_t> ids;
        for (const auto& val : arr) {
            if (val.is_number())
                ids.push_back(val.get<uint64_t>());
            else
                ids.push_back(std::stoull(val.get<std::string>()));
        }
        quests_in_order = std::move(ids);
    }
    current_quest_index = data.value("current_quest_index", 0);
    is_completed = data.value("is_completed", false);
    if (data.contains("chain_completion_reward")) {
        chain_completion_reward.Deserialize(data["chain_completion_reward"]);
    }
}
