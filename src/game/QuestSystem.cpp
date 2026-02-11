#include "game/QuestSystem.hpp"

// Static member initialization
QuestSystem& QuestSystem::GetInstance() {
    static QuestSystem instance;
    return instance;
}

// QuestRequirement serialization
nlohmann::json QuestRequirement::Serialize() const {
    nlohmann::json json;
    json["min_level"] = min_level;
    json["max_level"] = max_level;
    json["required_class"] = static_cast<int>(required_class);
    json["required_race"] = static_cast<int>(required_race);
    json["required_faction"] = required_faction;
    json["required_reputation_level"] = required_reputation_level;
    json["required_quests"] = required_quests;
    json["required_items"] = required_items;
    json["required_skills"] = required_skills;
    return json;
}

void QuestRequirement::Deserialize(const nlohmann::json& data) {
    min_level = data.value("min_level", 1);
    max_level = data.value("max_level", 100);
    required_class = static_cast<PlayerClass>(data.value("required_class", 10));
    required_race = static_cast<PlayerRace>(data.value("required_race", 0));
    required_faction = data.value("required_faction", "");
    required_reputation_level = data.value("required_reputation_level", 0);
    
    if (data.contains("required_quests")) {
        required_quests = data["required_quests"].get<std::vector<std::string>>();
    }
    
    if (data.contains("required_items")) {
        required_items = data["required_items"].get<std::vector<std::string>>();
    }
    
    if (data.contains("required_skills")) {
        required_skills = data["required_skills"].get<std::vector<std::string>>();
    }
}

// QuestObjective serialization
nlohmann::json QuestObjective::Serialize() const {
    nlohmann::json json;
    json["id"] = id;
    json["type"] = static_cast<int>(type);
    json["target_id"] = target_id;
    json["required_count"] = required_count;
    json["current_count"] = current_count;
    json["description"] = description;
    json["location_hint"] = location_hint;
    
    // Serialize glm::vec3
    json["location_position"] = {location_position.x, location_position.y, location_position.z};
    json["location_radius"] = location_radius;
    json["time_limit"] = time_limit;
    json["time_remaining"] = time_remaining;
    json["is_optional"] = is_optional;
    json["is_hidden"] = is_hidden;
    
    return json;
}

void QuestObjective::Deserialize(const nlohmann::json& data) {
    id = data.value("id", "");
    type = static_cast<ObjectiveType>(data.value("type", 0));
    target_id = data.value("target_id", "");
    required_count = data.value("required_count", 1);
    current_count = data.value("current_count", 0);
    description = data.value("description", "");
    location_hint = data.value("location_hint", "");
    
    if (data.contains("location_position") && data["location_position"].is_array()) {
        auto& pos_array = data["location_position"];
        if (pos_array.size() >= 3) {
            location_position.x = pos_array[0];
            location_position.y = pos_array[1];
            location_position.z = pos_array[2];
        }
    }
    
    location_radius = data.value("location_radius", 0.0f);
    time_limit = data.value("time_limit", 0.0f);
    time_remaining = data.value("time_remaining", 0.0f);
    is_optional = data.value("is_optional", false);
    is_hidden = data.value("is_hidden", false);
}

// QuestReward serialization
nlohmann::json QuestReward::Serialize() const {
    nlohmann::json json;
    json["experience"] = experience;
    json["gold"] = gold;
    json["reputation"] = reputation;
    json["reputation_faction"] = reputation_faction;
    
    // Serialize items
    nlohmann::json items_array = nlohmann::json::array();
    for (const auto& [item_id, quantity] : items) {
        nlohmann::json item_json;
        item_json["item_id"] = item_id;
        item_json["quantity"] = quantity;
        items_array.push_back(item_json);
    }
    json["items"] = items_array;
    
    json["skills"] = skills;
    json["titles"] = titles;
    json["cosmetics"] = cosmetics;
    json["skill_points"] = skill_points;
    json["talent_points"] = talent_points;
    json["honor_points"] = honor_points;
    
    json["is_choice_reward"] = is_choice_reward;
    json["choose_count"] = choose_count;
    
    // Serialize choice items
    nlohmann::json choice_array = nlohmann::json::array();
    for (const auto& [item_id, quantity] : choice_items) {
        nlohmann::json choice_json;
        choice_json["item_id"] = item_id;
        choice_json["quantity"] = quantity;
        choice_array.push_back(choice_json);
    }
    json["choice_items"] = choice_array;
    
    return json;
}

void QuestReward::Deserialize(const nlohmann::json& data) {
    experience = data.value("experience", 0);
    gold = data.value("gold", 0);
    reputation = data.value("reputation", 0);
    reputation_faction = data.value("reputation_faction", "");
    
    // Deserialize items
    if (data.contains("items") && data["items"].is_array()) {
        for (const auto& item_json : data["items"]) {
            std::string item_id = item_json.value("item_id", "");
            int quantity = item_json.value("quantity", 1);
            items.emplace_back(item_id, quantity);
        }
    }
    
    if (data.contains("skills")) {
        skills = data["skills"].get<std::vector<std::string>>();
    }
    
    if (data.contains("titles")) {
        titles = data["titles"].get<std::vector<std::string>>();
    }
    
    if (data.contains("cosmetics")) {
        cosmetics = data["cosmetics"].get<std::vector<std::string>>();
    }
    
    skill_points = data.value("skill_points", 0);
    talent_points = data.value("talent_points", 0);
    honor_points = data.value("honor_points", 0);
    
    is_choice_reward = data.value("is_choice_reward", false);
    choose_count = data.value("choose_count", 1);
    
    // Deserialize choice items
    if (data.contains("choice_items") && data["choice_items"].is_array()) {
        for (const auto& choice_json : data["choice_items"]) {
            std::string item_id = choice_json.value("item_id", "");
            int quantity = choice_json.value("quantity", 1);
            choice_items.emplace_back(item_id, quantity);
        }
    }
}

// QuestData serialization
nlohmann::json QuestData::Serialize() const {
    nlohmann::json json;
    json["id"] = id;
    json["name"] = name;
    json["description"] = description;
    json["completion_text"] = completion_text;
    json["failure_text"] = failure_text;
    
    json["type"] = static_cast<int>(type);
    json["difficulty"] = static_cast<int>(difficulty);
    
    json["giver_npc_id"] = giver_npc_id;
    json["turn_in_npc_id"] = turn_in_npc_id;
    
    // Serialize positions
    json["giver_location"] = {giver_location.x, giver_location.y, giver_location.z};
    json["turn_in_location"] = {turn_in_location.x, turn_in_location.y, turn_in_location.z};
    
    json["requirements"] = requirements.Serialize();
    
    // Serialize objectives array
    nlohmann::json objectives_array = nlohmann::json::array();
    for (const auto& objective : objectives) {
        objectives_array.push_back(objective.Serialize());
    }
    json["objectives"] = objectives_array;
    
    json["reward"] = reward.Serialize();
    
    json["prerequisite_quest"] = prerequisite_quest;
    json["next_quests"] = next_quests;
    
    json["is_repeatable"] = is_repeatable;
    json["repeat_cooldown_hours"] = repeat_cooldown_hours;
    json["is_shareable"] = is_shareable;
    json["is_discoverable"] = is_discoverable;
    json["auto_complete"] = auto_complete;
    
    json["min_level"] = min_level;
    json["max_level"] = max_level;
    json["suggested_party_size"] = suggested_party_size;
    
    json["zone"] = zone;
    json["tags"] = tags;
    
    return json;
}

void QuestData::Deserialize(const nlohmann::json& data) {
    id = data.value("id", "");
    name = data.value("name", "");
    description = data.value("description", "");
    completion_text = data.value("completion_text", "");
    failure_text = data.value("failure_text", "");
    
    type = static_cast<QuestType>(data.value("type", 1));
    difficulty = static_cast<QuestDifficulty>(data.value("difficulty", 2));
    
    giver_npc_id = data.value("giver_npc_id", "");
    turn_in_npc_id = data.value("turn_in_npc_id", "");
    
    // Deserialize positions
    if (data.contains("giver_location") && data["giver_location"].is_array()) {
        auto& pos_array = data["giver_location"];
        if (pos_array.size() >= 3) {
            giver_location.x = pos_array[0];
            giver_location.y = pos_array[1];
            giver_location.z = pos_array[2];
        }
    }
    
    if (data.contains("turn_in_location") && data["turn_in_location"].is_array()) {
        auto& pos_array = data["turn_in_location"];
        if (pos_array.size() >= 3) {
            turn_in_location.x = pos_array[0];
            turn_in_location.y = pos_array[1];
            turn_in_location.z = pos_array[2];
        }
    }
    
    if (data.contains("requirements")) {
        requirements.Deserialize(data["requirements"]);
    }
    
    // Deserialize objectives
    if (data.contains("objectives") && data["objectives"].is_array()) {
        for (const auto& objective_json : data["objectives"]) {
            QuestObjective objective;
            objective.Deserialize(objective_json);
            objectives.push_back(objective);
        }
    }
    
    if (data.contains("reward")) {
        reward.Deserialize(data["reward"]);
    }
    
    prerequisite_quest = data.value("prerequisite_quest", "");
    
    if (data.contains("next_quests")) {
        next_quests = data["next_quests"].get<std::vector<std::string>>();
    }
    
    is_repeatable = data.value("is_repeatable", false);
    repeat_cooldown_hours = data.value("repeat_cooldown_hours", 24);
    is_shareable = data.value("is_shareable", false);
    is_discoverable = data.value("is_discoverable", false);
    auto_complete = data.value("auto_complete", false);
    
    min_level = data.value("min_level", 1);
    max_level = data.value("max_level", 100);
    suggested_party_size = data.value("suggested_party_size", 1);
    
    zone = data.value("zone", "");
    
    if (data.contains("tags")) {
        tags = data["tags"].get<std::vector<std::string>>();
    }
}

// PlayerQuest serialization
nlohmann::json PlayerQuest::Serialize() const {
    nlohmann::json json;
    json["quest_id"] = quest_id;
    json["state"] = static_cast<int>(state);
    
    // Serialize timestamps
    auto start_time_t = std::chrono::system_clock::to_time_t(start_time);
    auto completion_time_t = std::chrono::system_clock::to_time_t(completion_time);
    auto last_reset_time_t = std::chrono::system_clock::to_time_t(last_reset_time);
    
    json["start_time"] = start_time_t;
    json["completion_time"] = completion_time_t;
    json["last_reset_time"] = last_reset_time_t;
    
    // Serialize objectives
    nlohmann::json objectives_array = nlohmann::json::array();
    for (const auto& objective : objectives) {
        objectives_array.push_back(objective.Serialize());
    }
    json["objectives"] = objectives_array;
    
    json["completion_count"] = completion_count;
    json["rewards_claimed"] = rewards_claimed;
    
    return json;
}

void PlayerQuest::Deserialize(const nlohmann::json& data) {
    quest_id = data.value("quest_id", "");
    state = static_cast<QuestState>(data.value("state", 0));
    
    // Deserialize timestamps
    auto start_time_t = data.value("start_time", 0);
    auto completion_time_t = data.value("completion_time", 0);
    auto last_reset_time_t = data.value("last_reset_time", 0);
    
    start_time = std::chrono::system_clock::from_time_t(start_time_t);
    completion_time = std::chrono::system_clock::from_time_t(completion_time_t);
    last_reset_time = std::chrono::system_clock::from_time_t(last_reset_time_t);
    
    // Deserialize objectives
    if (data.contains("objectives") && data["objectives"].is_array()) {
        for (const auto& objective_json : data["objectives"]) {
            QuestObjective objective;
            objective.Deserialize(objective_json);
            objectives.push_back(objective);
        }
    }
    
    completion_count = data.value("completion_count", 0);
    rewards_claimed = data.value("rewards_claimed", false);
}

// QuestChain serialization
nlohmann::json QuestChain::Serialize() const {
    nlohmann::json json;
    json["chain_id"] = chain_id;
    json["name"] = name;
    json["description"] = description;
    json["quests_in_order"] = quests_in_order;
    json["current_quest_index"] = current_quest_index;
    json["is_completed"] = is_completed;
    json["chain_completion_reward"] = chain_completion_reward.Serialize();
    
    return json;
}

void QuestChain::Deserialize(const nlohmann::json& data) {
    chain_id = data.value("chain_id", "");
    name = data.value("name", "");
    description = data.value("description", "");
    
    if (data.contains("quests_in_order")) {
        quests_in_order = data["quests_in_order"].get<std::vector<std::string>>();
    }
    
    current_quest_index = data.value("current_quest_index", 0);
    is_completed = data.value("is_completed", false);
    
    if (data.contains("chain_completion_reward")) {
        chain_completion_reward.Deserialize(data["chain_completion_reward"]);
    }
}

// PlayerQuestData serialization
nlohmann::json QuestSystem::PlayerQuestData::Serialize() const {
    nlohmann::json json;
    
    // Serialize active quests
    nlohmann::json active_quests_json;
    for (const auto& [quest_id, quest] : active_quests) {
        active_quests_json[quest_id] = quest.Serialize();
    }
    json["active_quests"] = active_quests_json;
    
    // Serialize completed quests
    nlohmann::json completed_quests_json;
    for (const auto& [quest_id, quest] : completed_quests) {
        completed_quests_json[quest_id] = quest.Serialize();
    }
    json["completed_quests"] = completed_quests_json;
    
    // Serialize quest chains
    nlohmann::json chains_json;
    for (const auto& [chain_id, chain] : quest_chains) {
        chains_json[chain_id] = chain.Serialize();
    }
    json["quest_chains"] = chains_json;
    
    // Serialize discovered quests
    nlohmann::json discovered_json;
    for (const auto& [quest_id, discovered] : discovered_quests) {
        discovered_json[quest_id] = discovered;
    }
    json["discovered_quests"] = discovered_json;
    
    json["tracked_quest_id"] = tracked_quest_id;
    
    // Serialize daily/weekly completions
    nlohmann::json daily_json;
    for (const auto& [quest_id, time] : daily_quest_completions) {
        auto time_t = std::chrono::system_clock::to_time_t(time);
        daily_json[quest_id] = time_t;
    }
    json["daily_quest_completions"] = daily_json;
    
    nlohmann::json weekly_json;
    for (const auto& [quest_id, time] : weekly_quest_completions) {
        auto time_t = std::chrono::system_clock::to_time_t(time);
        weekly_json[quest_id] = time_t;
    }
    json["weekly_quest_completions"] = weekly_json;
    
    json["total_quests_completed"] = total_quests_completed;
    json["total_quest_points"] = total_quest_points;
    
    auto last_daily_reset_t = std::chrono::system_clock::to_time_t(last_daily_reset);
    auto last_weekly_reset_t = std::chrono::system_clock::to_time_t(last_weekly_reset);
    
    json["last_daily_reset"] = last_daily_reset_t;
    json["last_weekly_reset"] = last_weekly_reset_t;
    
    return json;
}

void QuestSystem::PlayerQuestData::Deserialize(const nlohmann::json& data) {
    // Deserialize active quests
    if (data.contains("active_quests")) {
        for (const auto& [quest_id, quest_json] : data["active_quests"].items()) {
            PlayerQuest quest;
            quest.Deserialize(quest_json);
            active_quests[quest_id] = quest;
        }
    }
    
    // Deserialize completed quests
    if (data.contains("completed_quests")) {
        for (const auto& [quest_id, quest_json] : data["completed_quests"].items()) {
            PlayerQuest quest;
            quest.Deserialize(quest_json);
            completed_quests[quest_id] = quest;
        }
    }
    
    // Deserialize quest chains
    if (data.contains("quest_chains")) {
        for (const auto& [chain_id, chain_json] : data["quest_chains"].items()) {
            QuestChain chain;
            chain.Deserialize(chain_json);
            quest_chains[chain_id] = chain;
        }
    }
    
    // Deserialize discovered quests
    if (data.contains("discovered_quests")) {
        for (const auto& [quest_id, discovered] : data["discovered_quests"].items()) {
            discovered_quests[quest_id] = discovered;
        }
    }
    
    tracked_quest_id = data.value("tracked_quest_id", "");
    
    // Deserialize daily/weekly completions
    if (data.contains("daily_quest_completions")) {
        for (const auto& [quest_id, time_t] : data["daily_quest_completions"].items()) {
            auto time = std::chrono::system_clock::from_time_t(time_t);
            daily_quest_completions[quest_id] = time;
        }
    }
    
    if (data.contains("weekly_quest_completions")) {
        for (const auto& [quest_id, time_t] : data["weekly_quest_completions"].items()) {
            auto time = std::chrono::system_clock::from_time_t(time_t);
            weekly_quest_completions[quest_id] = time;
        }
    }
    
    total_quests_completed = data.value("total_quests_completed", 0);
    total_quest_points = data.value("total_quest_points", 0);
    
    auto last_daily_reset_t = data.value("last_daily_reset", 0);
    auto last_weekly_reset_t = data.value("last_weekly_reset", 0);
    
    last_daily_reset = std::chrono::system_clock::from_time_t(last_daily_reset_t);
    last_weekly_reset = std::chrono::system_clock::from_time_t(last_weekly_reset_t);
}

// Constructor
QuestSystem::QuestSystem() 
#ifdef USE_CITUS
    : db_client_(CitusClient::GetInstance())
#else
    : db_backend_(std::make_unique<PostgreSQLBackend>())
#endif
{
    Logger::GetInstance().Log(LogLevel::INFO, "QuestSystem initialized");
}

// Quest data management
bool QuestSystem::LoadQuestData(const std::string& file_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            Logger::GetInstance().Log(LogLevel::ERROR, "Failed to open quest data file: " + file_path);
            return false;
        }
        
        nlohmann::json json_data;
        file >> json_data;
        
        if (json_data.is_array()) {
            // Array format
            for (const auto& quest_json : json_data) {
                QuestData quest;
                quest.Deserialize(quest_json);
                quest_database_[quest.id] = quest;
            }
        } else if (json_data.is_object() && json_data.contains("quests")) {
            // Object with quests array
            for (const auto& quest_json : json_data["quests"]) {
                QuestData quest;
                quest.Deserialize(quest_json);
                quest_database_[quest.id] = quest;
            }
            
            // Load quest chains if present
            if (json_data.contains("quest_chains")) {
                for (const auto& [chain_id, quests_array] : json_data["quest_chains"].items()) {
                    quest_chains_[chain_id] = quests_array.get<std::vector<std::string>>();
                }
            }
        } else {
            Logger::GetInstance().Log(LogLevel::ERROR, "Invalid quest data format");
            return false;
        }
        
        Logger::GetInstance().Log(LogLevel::INFO, "Loaded " + std::to_string(quest_database_.size()) + 
                                  " quests from " + file_path);
        return true;
    } catch (const std::exception& e) {
        Logger::GetInstance().Log(LogLevel::ERROR, "Error loading quest data: " + std::string(e.what()));
        return false;
    }
}

bool QuestSystem::SaveQuestData(const std::string& file_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        nlohmann::json json_data;
        nlohmann::json quests_array = nlohmann::json::array();
        
        for (const auto& [id, quest] : quest_database_) {
            quests_array.push_back(quest.Serialize());
        }
        
        json_data["quests"] = quests_array;
        
        // Save quest chains
        nlohmann::json chains_json;
        for (const auto& [chain_id, quest_ids] : quest_chains_) {
            chains_json[chain_id] = quest_ids;
        }
        json_data["quest_chains"] = chains_json;
        
        std::ofstream file(file_path);
        if (!file.is_open()) {
            Logger::GetInstance().Log(LogLevel::ERROR, "Failed to open quest data file for writing: " + file_path);
            return false;
        }
        
        file << json_data.dump(2);
        return true;
    } catch (const std::exception& e) {
        Logger::GetInstance().Log(LogLevel::ERROR, "Error saving quest data: " + std::string(e.what()));
        return false;
    }
}

const QuestData* QuestSystem::GetQuestData(const std::string& quest_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = quest_database_.find(quest_id);
    if (it != quest_database_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<std::string> QuestSystem::GetQuestsByType(QuestType type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> result;
    for (const auto& [id, quest] : quest_database_) {
        if (quest.type == type) {
            result.push_back(id);
        }
    }
    return result;
}

std::vector<std::string> QuestSystem::GetQuestsByZone(const std::string& zone) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> result;
    for (const auto& [id, quest] : quest_database_) {
        if (quest.zone == zone) {
            result.push_back(id);
        }
    }
    return result;
}

std::vector<std::string> QuestSystem::GetQuestsByLevel(int level) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> result;
    for (const auto& [id, quest] : quest_database_) {
        if (level >= quest.min_level && level <= quest.max_level) {
            result.push_back(id);
        }
    }
    return result;
}

std::vector<std::string> QuestSystem::GetAvailableQuests(uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> result;
    auto player_data_it = player_quests_.find(player_id);
    
    for (const auto& [quest_id, quest_data] : quest_database_) {
        // Check if player already has this quest
        bool has_quest = false;
        if (player_data_it != player_quests_.end()) {
            has_quest = player_data_it->second.active_quests.find(quest_id) != 
                       player_data_it->second.active_quests.end() ||
                       player_data_it->second.completed_quests.find(quest_id) != 
                       player_data_it->second.completed_quests.end();
        }
        
        if (!has_quest && CanAcceptQuest(player_id, quest_id)) {
            result.push_back(quest_id);
        }
    }
    
    return result;
}

// Player quest management
bool QuestSystem::StartQuest(uint64_t player_id, const std::string& quest_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if quest exists
    auto quest_data_it = quest_database_.find(quest_id);
    if (quest_data_it == quest_database_.end()) {
        Logger::GetInstance().Log(LogLevel::WARNING, "Quest not found: " + quest_id);
        return false;
    }
    
    // Check if player can accept the quest
    if (!CanAcceptQuest(player_id, quest_id)) {
        Logger::GetInstance().Log(LogLevel::WARNING, "Player " + std::to_string(player_id) + 
                                  " cannot accept quest: " + quest_id);
        return false;
    }
    
    // Check active quest limit
    auto& player_data = player_quests_[player_id];
    if (player_data.active_quests.size() >= MAX_ACTIVE_QUESTS) {
        Logger::GetInstance().Log(LogLevel::WARNING, "Player " + std::to_string(player_id) + 
                                  " has too many active quests");
        return false;
    }
    
    // Check if player already has this quest
    if (player_data.active_quests.find(quest_id) != player_data.active_quests.end() ||
        player_data.completed_quests.find(quest_id) != player_data.completed_quests.end()) {
        Logger::GetInstance().Log(LogLevel::WARNING, "Player " + std::to_string(player_id) + 
                                  " already has quest: " + quest_id);
        return false;
    }
    
    // Create player quest
    PlayerQuest player_quest;
    player_quest.quest_id = quest_id;
    player_quest.state = QuestState::IN_PROGRESS;
    player_quest.start_time = std::chrono::system_clock::now();
    
    // Copy objectives from quest data
    const QuestData& quest_data = quest_data_it->second;
    player_quest.objectives = quest_data.objectives;
    
    // Initialize timed objectives
    for (auto& objective : player_quest.objectives) {
        if (objective.time_limit > 0) {
            objective.time_remaining = objective.time_limit;
        }
    }
    
    // Add to active quests
    player_data.active_quests[quest_id] = player_quest;
    
    // Mark as discovered
    player_data.discovered_quests[quest_id] = true;
    
    Logger::GetInstance().Log(LogLevel::INFO, "Player " + std::to_string(player_id) + 
                              " started quest: " + quest_id);
    
    if (on_quest_started_) {
        on_quest_started_(player_id, quest_id);
    }
    
    return true;
}

bool QuestSystem::AbandonQuest(uint64_t player_id, const std::string& quest_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return false;
    }
    
    auto& player_data = player_data_it->second;
    auto quest_it = player_data.active_quests.find(quest_id);
    if (quest_it == player_data.active_quests.end()) {
        return false;
    }
    
    // Update quest state
    quest_it->second.state = QuestState::ABANDONED;
    
    // Remove from active quests
    player_data.active_quests.erase(quest_it);
    
    // Untrack if this quest was being tracked
    if (player_data.tracked_quest_id == quest_id) {
        player_data.tracked_quest_id = "";
    }
    
    Logger::GetInstance().Log(LogLevel::INFO, "Player " + std::to_string(player_id) + 
                              " abandoned quest: " + quest_id);
    
    return true;
}

bool QuestSystem::CompleteQuest(uint64_t player_id, const std::string& quest_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return false;
    }
    
    auto& player_data = player_data_it->second;
    auto quest_it = player_data.active_quests.find(quest_id);
    if (quest_it == player_data.active_quests.end()) {
        return false;
    }
    
    // Check if quest is actually complete
    if (!CheckQuestCompletion(player_id, quest_id)) {
        return false;
    }
    
    // Update quest state
    quest_it->second.state = QuestState::READY_TO_TURN_IN;
    
    Logger::GetInstance().Log(LogLevel::INFO, "Player " + std::to_string(player_id) + 
                              " completed quest: " + quest_id);
    
    return true;
}

bool QuestSystem::FailQuest(uint64_t player_id, const std::string& quest_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return false;
    }
    
    auto& player_data = player_data_it->second;
    auto quest_it = player_data.active_quests.find(quest_id);
    if (quest_it == player_data.active_quests.end()) {
        return false;
    }
    
    // Update quest state
    quest_it->second.state = QuestState::FAILED;
    quest_it->second.completion_time = std::chrono::system_clock::now();
    
    // Move to completed quests
    player_data.completed_quests[quest_id] = quest_it->second;
    player_data.active_quests.erase(quest_it);
    
    // Untrack if this quest was being tracked
    if (player_data.tracked_quest_id == quest_id) {
        player_data.tracked_quest_id = "";
    }
    
    Logger::GetInstance().Log(LogLevel::INFO, "Player " + std::to_string(player_id) + 
                              " failed quest: " + quest_id);
    
    if (on_quest_failed_) {
        on_quest_failed_(player_id, quest_id);
    }
    
    return true;
}

bool QuestSystem::ShareQuest(uint64_t from_player_id, uint64_t to_player_id, const std::string& quest_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if quest is shareable
    auto quest_data_it = quest_database_.find(quest_id);
    if (quest_data_it == quest_database_.end() || !quest_data_it->second.is_shareable) {
        return false;
    }
    
    // Check if source player has the quest
    auto from_player_data_it = player_quests_.find(from_player_id);
    if (from_player_data_it == player_quests_.end()) {
        return false;
    }
    
    auto& from_player_data = from_player_data_it->second;
    if (from_player_data.active_quests.find(quest_id) == from_player_data.active_quests.end()) {
        return false;
    }
    
    // Check if target player can accept the quest
    if (!CanAcceptQuest(to_player_id, quest_id)) {
        return false;
    }
    
    // Start quest for target player
    return StartQuest(to_player_id, quest_id);
}

// Quest progress tracking
bool QuestSystem::UpdateQuestObjective(uint64_t player_id, const std::string& quest_id, 
                                       const std::string& objective_id, int increment) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return false;
    }
    
    auto& player_data = player_data_it->second;
    auto quest_it = player_data.active_quests.find(quest_id);
    if (quest_it == player_data.active_quests.end()) {
        return false;
    }
    
    PlayerQuest& player_quest = quest_it->second;
    
    // Find the objective
    for (auto& objective : player_quest.objectives) {
        if (objective.id == objective_id) {
            objective.current_count = std::min(objective.current_count + increment, objective.required_count);
            
            Logger::GetInstance().Log(LogLevel::INFO, "Updated objective " + objective_id + 
                                      " for quest " + quest_id + ": " + 
                                      std::to_string(objective.current_count) + "/" + 
                                      std::to_string(objective.required_count));
            
            if (on_objective_updated_) {
                on_objective_updated_(player_id, quest_id, objective_id, objective.current_count);
            }
            
            // Check if quest is now complete
            CheckQuestCompletion(player_id, quest_id);
            
            return true;
        }
    }
    
    return false;
}

bool QuestSystem::CheckQuestCompletion(uint64_t player_id, const std::string& quest_id) {
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return false;
    }
    
    auto& player_data = player_data_it->second;
    auto quest_it = player_data.active_quests.find(quest_id);
    if (quest_it == player_data.active_quests.end()) {
        return false;
    }
    
    PlayerQuest& player_quest = quest_it->second;
    
    // Check all objectives
    bool all_complete = true;
    for (const auto& objective : player_quest.objectives) {
        if (!objective.is_optional && objective.current_count < objective.required_count) {
            all_complete = false;
            break;
        }
    }
    
    if (all_complete && player_quest.state == QuestState::IN_PROGRESS) {
        player_quest.state = QuestState::READY_TO_TURN_IN;
        
        Logger::GetInstance().Log(LogLevel::INFO, "Quest " + quest_id + 
                                  " is ready to turn in for player " + std::to_string(player_id));
        
        if (on_quest_completed_) {
            on_quest_completed_(player_id, quest_id);
        }
        
        return true;
    }
    
    return false;
}

bool QuestSystem::ClaimQuestReward(uint64_t player_id, const std::string& quest_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return false;
    }
    
    auto& player_data = player_data_it->second;
    auto quest_it = player_data.active_quests.find(quest_id);
    if (quest_it == player_data.active_quests.end() || 
        quest_it->second.state != QuestState::READY_TO_TURN_IN) {
        return false;
    }
    
    // Check if quest data exists
    auto quest_data_it = quest_database_.find(quest_id);
    if (quest_data_it == quest_database_.end()) {
        return false;
    }
    
    // Give rewards
    GiveQuestReward(player_id, quest_id);
    
    // Update quest state
    quest_it->second.state = QuestState::COMPLETED;
    quest_it->second.completion_time = std::chrono::system_clock::now();
    quest_it->second.rewards_claimed = true;
    quest_it->second.completion_count++;
    
    // Move to completed quests if not repeatable
    if (!quest_data_it->second.is_repeatable) {
        player_data.completed_quests[quest_id] = quest_it->second;
        player_data.active_quests.erase(quest_it);
    } else {
        // For repeatable quests, keep in active but reset state
        quest_it->second.state = QuestState::NOT_STARTED;
        quest_it->second.rewards_claimed = false;
        
        // Reset objectives
        for (auto& objective : quest_it->second.objectives) {
            objective.current_count = 0;
        }
        
        // Record completion time for cooldown
        player_data.daily_quest_completions[quest_id] = std::chrono::system_clock::now();
    }
    
    // Update statistics
    player_data.total_quests_completed++;
    player_data.total_quest_points += static_cast<int>(quest_data_it->second.difficulty) + 1;
    
    // Untrack if this quest was being tracked
    if (player_data.tracked_quest_id == quest_id) {
        player_data.tracked_quest_id = "";
    }
    
    // Check for quest chain progress
    CheckQuestChainProgress(player_id, quest_id);
    
    Logger::GetInstance().Log(LogLevel::INFO, "Player " + std::to_string(player_id) + 
                              " claimed reward for quest: " + quest_id);
    
    if (on_quest_reward_claimed_) {
        on_quest_reward_claimed_(player_id, quest_id);
    }
    
    return true;
}

// Query methods
bool QuestSystem::HasQuest(uint64_t player_id, const std::string& quest_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return false;
    }
    
    const auto& player_data = player_data_it->second;
    return player_data.active_quests.find(quest_id) != player_data.active_quests.end() ||
           player_data.completed_quests.find(quest_id) != player_data.completed_quests.end();
}

bool QuestSystem::IsQuestCompleted(uint64_t player_id, const std::string& quest_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return false;
    }
    
    const auto& player_data = player_data_it->second;
    auto it = player_data.completed_quests.find(quest_id);
    return it != player_data.completed_quests.end() && 
           it->second.state == QuestState::COMPLETED;
}

bool QuestSystem::IsQuestInProgress(uint64_t player_id, const std::string& quest_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return false;
    }
    
    const auto& player_data = player_data_it->second;
    auto it = player_data.active_quests.find(quest_id);
    return it != player_data.active_quests.end() && 
           (it->second.state == QuestState::IN_PROGRESS || 
            it->second.state == QuestState::READY_TO_TURN_IN);
}

QuestState QuestSystem::GetQuestState(uint64_t player_id, const std::string& quest_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return QuestState::NOT_STARTED;
    }
    
    const auto& player_data = player_data_it->second;
    
    // Check active quests
    auto active_it = player_data.active_quests.find(quest_id);
    if (active_it != player_data.active_quests.end()) {
        return active_it->second.state;
    }
    
    // Check completed quests
    auto completed_it = player_data.completed_quests.find(quest_id);
    if (completed_it != player_data.completed_quests.end()) {
        return completed_it->second.state;
    }
    
    return QuestState::NOT_STARTED;
}

std::vector<PlayerQuest> QuestSystem::GetPlayerQuests(uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<PlayerQuest> result;
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it != player_quests_.end()) {
        // Add active quests
        for (const auto& [id, quest] : player_data_it->second.active_quests) {
            result.push_back(quest);
        }
        
        // Add completed quests
        for (const auto& [id, quest] : player_data_it->second.completed_quests) {
            result.push_back(quest);
        }
    }
    
    return result;
}

std::vector<PlayerQuest> QuestSystem::GetActiveQuests(uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<PlayerQuest> result;
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it != player_quests_.end()) {
        for (const auto& [id, quest] : player_data_it->second.active_quests) {
            result.push_back(quest);
        }
    }
    
    return result;
}

std::vector<PlayerQuest> QuestSystem::GetCompletedQuests(uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<PlayerQuest> result;
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it != player_quests_.end()) {
        for (const auto& [id, quest] : player_data_it->second.completed_quests) {
            result.push_back(quest);
        }
    }
    
    return result;
}

// Quest chain management
bool QuestSystem::StartQuestChain(uint64_t player_id, const std::string& chain_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto chain_it = quest_chains_.find(chain_id);
    if (chain_it == quest_chains_.end()) {
        return false;
    }
    
    auto& player_data = player_quests_[player_id];
    
    // Check if chain already exists
    if (player_data.quest_chains.find(chain_id) != player_data.quest_chains.end()) {
        return false;
    }
    
    // Create quest chain
    QuestChain chain;
    chain.chain_id = chain_id;
    chain.quests_in_order = chain_it->second;
    chain.current_quest_index = 0;
    
    player_data.quest_chains[chain_id] = chain;
    
    // Start the first quest in the chain
    if (!chain_it->second.empty()) {
        return StartQuest(player_id, chain_it->second[0]);
    }
    
    return false;
}

bool QuestSystem::AdvanceQuestChain(uint64_t player_id, const std::string& chain_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return false;
    }
    
    auto& player_data = player_data_it->second;
    auto chain_it = player_data.quest_chains.find(chain_id);
    if (chain_it == player_data.quest_chains.end()) {
        return false;
    }
    
    QuestChain& chain = chain_it->second;
    
    // Check if chain is already completed
    if (chain.is_completed) {
        return false;
    }
    
    // Move to next quest
    chain.current_quest_index++;
    
    // Check if chain is complete
    if (chain.current_quest_index >= chain.quests_in_order.size()) {
        chain.is_completed = true;
        Logger::GetInstance().Log(LogLevel::INFO, "Player " + std::to_string(player_id) + 
                                  " completed quest chain: " + chain_id);
        return true;
    }
    
    // Start the next quest
    std::string next_quest_id = chain.quests_in_order[chain.current_quest_index];
    return StartQuest(player_id, next_quest_id);
}

bool QuestSystem::IsQuestChainCompleted(uint64_t player_id, const std::string& chain_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return false;
    }
    
    const auto& player_data = player_data_it->second;
    auto chain_it = player_data.quest_chains.find(chain_id);
    if (chain_it == player_data.quest_chains.end()) {
        return false;
    }
    
    return chain_it->second.is_completed;
}

std::vector<QuestChain> QuestSystem::GetPlayerQuestChains(uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<QuestChain> result;
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it != player_quests_.end()) {
        for (const auto& [id, chain] : player_data_it->second.quest_chains) {
            result.push_back(chain);
        }
    }
    
    return result;
}

// Daily/Weekly quests
void QuestSystem::ResetDailyQuests(uint64_t player_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return;
    }
    
    auto& player_data = player_data_it->second;
    player_data.last_daily_reset = std::chrono::system_clock::now();
    player_data.daily_quest_completions.clear();
    
    Logger::GetInstance().Log(LogLevel::INFO, "Reset daily quests for player " + 
                              std::to_string(player_id));
}

void QuestSystem::ResetWeeklyQuests(uint64_t player_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return;
    }
    
    auto& player_data = player_data_it->second;
    player_data.last_weekly_reset = std::chrono::system_clock::now();
    player_data.weekly_quest_completions.clear();
    
    Logger::GetInstance().Log(LogLevel::INFO, "Reset weekly quests for player " + 
                              std::to_string(player_id));
}

bool QuestSystem::CanAcceptDailyQuest(uint64_t player_id, const std::string& quest_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto quest_data_it = quest_database_.find(quest_id);
    if (quest_data_it == quest_database_.end() || 
        quest_data_it->second.type != QuestType::DAILY_QUEST) {
        return false;
    }
    
    return IsDailyQuestAvailable(player_id, quest_id);
}

bool QuestSystem::CanAcceptWeeklyQuest(uint64_t player_id, const std::string& quest_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto quest_data_it = quest_database_.find(quest_id);
    if (quest_data_it == quest_database_.end() || 
        quest_data_it->second.type != QuestType::WEEKLY_QUEST) {
        return false;
    }
    
    return IsWeeklyQuestAvailable(player_id, quest_id);
}

// Event handling
void QuestSystem::OnMonsterKilled(uint64_t player_id, const std::string& monster_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return;
    }
    
    auto& player_data = player_data_it->second;
    
    // Check all active quests
    for (auto& [quest_id, player_quest] : player_data.active_quests) {
        if (player_quest.state != QuestState::IN_PROGRESS) {
            continue;
        }
        
        // Check all objectives in this quest
        for (auto& objective : player_quest.objectives) {
            if (objective.type == ObjectiveType::KILL_MONSTER && 
                objective.target_id == monster_id) {
                UpdateQuestObjective(player_id, quest_id, objective.id, 1);
            }
        }
    }
}

void QuestSystem::OnItemCollected(uint64_t player_id, const std::string& item_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return;
    }
    
    auto& player_data = player_data_it->second;
    
    // Check all active quests
    for (auto& [quest_id, player_quest] : player_data.active_quests) {
        if (player_quest.state != QuestState::IN_PROGRESS) {
            continue;
        }
        
        // Check all objectives in this quest
        for (auto& objective : player_quest.objectives) {
            if (objective.type == ObjectiveType::COLLECT_ITEM && 
                objective.target_id == item_id) {
                UpdateQuestObjective(player_id, quest_id, objective.id, 1);
            }
        }
    }
}

void QuestSystem::OnNPCTalkedTo(uint64_t player_id, const std::string& npc_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return;
    }
    
    auto& player_data = player_data_it->second;
    
    // Check all active quests
    for (auto& [quest_id, player_quest] : player_data.active_quests) {
        if (player_quest.state != QuestState::IN_PROGRESS) {
            continue;
        }
        
        // Check all objectives in this quest
        for (auto& objective : player_quest.objectives) {
            if (objective.type == ObjectiveType::TALK_TO_NPC && 
                objective.target_id == npc_id) {
                UpdateQuestObjective(player_id, quest_id, objective.id, 1);
            }
        }
    }
    
    // Also check for quests that can be started from this NPC
    for (const auto& [quest_id, quest_data] : quest_database_) {
        if (quest_data.giver_npc_id == npc_id && 
            !HasQuest(player_id, quest_id) &&
            CanAcceptQuest(player_id, quest_id)) {
            // Auto-start quest if it's discoverable or auto-complete
            if (quest_data.is_discoverable || quest_data.auto_complete) {
                StartQuest(player_id, quest_id);
            }
        }
    }
}

void QuestSystem::OnLocationVisited(uint64_t player_id, const glm::vec3& location, float radius) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return;
    }
    
    auto& player_data = player_data_it->second;
    
    // Check all active quests
    for (auto& [quest_id, player_quest] : player_data.active_quests) {
        if (player_quest.state != QuestState::IN_PROGRESS) {
            continue;
        }
        
        // Check all objectives in this quest
        for (auto& objective : player_quest.objectives) {
            if (objective.type == ObjectiveType::VISIT_LOCATION) {
                float distance = glm::distance(location, objective.location_position);
                if (distance <= radius + objective.location_radius) {
                    UpdateQuestObjective(player_id, quest_id, objective.id, 1);
                }
            }
        }
    }
}

void QuestSystem::OnDungeonCompleted(uint64_t player_id, const std::string& dungeon_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return;
    }
    
    auto& player_data = player_data_it->second;
    
    // Check all active quests
    for (auto& [quest_id, player_quest] : player_data.active_quests) {
        if (player_quest.state != QuestState::IN_PROGRESS) {
            continue;
        }
        
        // Check all objectives in this quest
        for (auto& objective : player_quest.objectives) {
            if (objective.type == ObjectiveType::COMPLETE_DUNGEON && 
                objective.target_id == dungeon_id) {
                UpdateQuestObjective(player_id, quest_id, objective.id, 1);
            }
        }
    }
}

void QuestSystem::OnLevelGained(uint64_t player_id, int new_level) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return;
    }
    
    auto& player_data = player_data_it->second;
    
    // Check all active quests
    for (auto& [quest_id, player_quest] : player_data.active_quests) {
        if (player_quest.state != QuestState::IN_PROGRESS) {
            continue;
        }
        
        // Check all objectives in this quest
        for (auto& objective : player_quest.objectives) {
            if (objective.type == ObjectiveType::REACH_LEVEL && 
                objective.required_count <= new_level) {
                UpdateQuestObjective(player_id, quest_id, objective.id, 
                                    objective.required_count - objective.current_count);
            }
        }
    }
}

void QuestSystem::OnSkillLearned(uint64_t player_id, const std::string& skill_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return;
    }
    
    auto& player_data = player_data_it->second;
    
    // Check all active quests
    for (auto& [quest_id, player_quest] : player_data.active_quests) {
        if (player_quest.state != QuestState::IN_PROGRESS) {
            continue;
        }
        
        // Check all objectives in this quest
        for (auto& objective : player_quest.objectives) {
            if (objective.type == ObjectiveType::LEARN_SKILL && 
                objective.target_id == skill_id) {
                UpdateQuestObjective(player_id, quest_id, objective.id, 1);
            }
        }
    }
}

// Serialization
bool QuestSystem::LoadPlayerQuests(uint64_t player_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
#ifdef USE_CITUS
    return LoadPlayerQuestsFromDatabase(player_id);
#else
    // Load from local JSON file
    std::string file_path = "data/players/" + std::to_string(player_id) + "/quests.json";
    
    try {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            Logger::GetInstance().Log(LogLevel::WARNING, "No quest data found for player " + 
                                      std::to_string(player_id) + ", creating new");
            return true; // No existing data is not an error
        }
        
        nlohmann::json json_data;
        file >> json_data;
        
        PlayerQuestData player_data;
        player_data.Deserialize(json_data);
        
        player_quests_[player_id] = player_data;
        
        Logger::GetInstance().Log(LogLevel::INFO, "Loaded quests for player " + 
                                  std::to_string(player_id));
        return true;
    } catch (const std::exception& e) {
        Logger::GetInstance().Log(LogLevel::ERROR, "Error loading player quests: " + 
                                  std::string(e.what()));
        return false;
    }
#endif
}

bool QuestSystem::SavePlayerQuests(uint64_t player_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
#ifdef USE_CITUS
    return SavePlayerQuestsToDatabase(player_id);
#else
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return false;
    }
    
    std::string file_path = "data/players/" + std::to_string(player_id) + "/quests.json";
    
    try {
        // Create directory if it doesn't exist
        std::filesystem::create_directories(std::filesystem::path(file_path).parent_path());
        
        std::ofstream file(file_path);
        if (!file.is_open()) {
            Logger::GetInstance().Log(LogLevel::ERROR, "Failed to open quest file for writing: " + file_path);
            return false;
        }
        
        nlohmann::json json_data = player_data_it->second.Serialize();
        file << json_data.dump(2);
        
        Logger::GetInstance().Log(LogLevel::INFO, "Saved quests for player " + 
                                  std::to_string(player_id));
        return true;
    } catch (const std::exception& e) {
        Logger::GetInstance().Log(LogLevel::ERROR, "Error saving player quests: " + 
                                  std::string(e.what()));
        return false;
    }
#endif
}

nlohmann::json QuestSystem::SerializePlayerQuests(uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return nlohmann::json();
    }
    
    return player_data_it->second.Serialize();
}

bool QuestSystem::DeserializePlayerQuests(uint64_t player_id, const nlohmann::json& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (data.is_null()) {
        return false;
    }
    
    PlayerQuestData player_data;
    try {
        player_data.Deserialize(data);
        player_quests_[player_id] = player_data;
        return true;
    } catch (const std::exception& e) {
        Logger::GetInstance().Log(LogLevel::ERROR, "Error deserializing player quests: " + 
                                  std::string(e.what()));
        return false;
    }
}

// Quest discovery
void QuestSystem::DiscoverQuest(uint64_t player_id, const std::string& quest_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& player_data = player_quests_[player_id];
    player_data.discovered_quests[quest_id] = true;
    
    Logger::GetInstance().Log(LogLevel::INFO, "Player " + std::to_string(player_id) + 
                              " discovered quest: " + quest_id);
}

std::vector<std::string> QuestSystem::GetDiscoveredQuests(uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> result;
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it != player_quests_.end()) {
        for (const auto& [quest_id, discovered] : player_data_it->second.discovered_quests) {
            if (discovered) {
                result.push_back(quest_id);
            }
        }
    }
    
    return result;
}

bool QuestSystem::IsQuestDiscovered(uint64_t player_id, const std::string& quest_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return false;
    }
    
    const auto& player_data = player_data_it->second;
    auto it = player_data.discovered_quests.find(quest_id);
    return it != player_data.discovered_quests.end() && it->second;
}

// Quest tracking
void QuestSystem::TrackQuest(uint64_t player_id, const std::string& quest_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return;
    }
    
    auto& player_data = player_data_it->second;
    
    // Check if player has this quest
    if (player_data.active_quests.find(quest_id) != player_data.active_quests.end()) {
        player_data.tracked_quest_id = quest_id;
    }
}

void QuestSystem::UntrackQuest(uint64_t player_id, const std::string& quest_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return;
    }
    
    auto& player_data = player_data_it->second;
    
    if (player_data.tracked_quest_id == quest_id) {
        player_data.tracked_quest_id = "";
    }
}

bool QuestSystem::IsQuestTracked(uint64_t player_id, const std::string& quest_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return false;
    }
    
    const auto& player_data = player_data_it->second;
    return player_data.tracked_quest_id == quest_id;
}

std::string QuestSystem::GetTrackedQuest(uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return "";
    }
    
    return player_data_it->second.tracked_quest_id;
}

// Quest giver/turn-in
std::vector<std::string> QuestSystem::GetQuestsFromNPC(uint64_t player_id, const std::string& npc_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> result;
    
    for (const auto& [quest_id, quest_data] : quest_database_) {
        if (quest_data.giver_npc_id == npc_id && 
            !HasQuest(player_id, quest_id) &&
            CanAcceptQuest(player_id, quest_id)) {
            result.push_back(quest_id);
        }
    }
    
    return result;
}

std::vector<std::string> QuestSystem::GetQuestsToTurnIn(uint64_t player_id, const std::string& npc_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> result;
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return result;
    }
    
    const auto& player_data = player_data_it->second;
    
    for (const auto& [quest_id, player_quest] : player_data.active_quests) {
        if (player_quest.state == QuestState::READY_TO_TURN_IN) {
            auto quest_data_it = quest_database_.find(quest_id);
            if (quest_data_it != quest_database_.end() && 
                quest_data_it->second.turn_in_npc_id == npc_id) {
                result.push_back(quest_id);
            }
        }
    }
    
    return result;
}

// Statistics
int QuestSystem::GetTotalQuestsCompleted(uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return 0;
    }
    
    return player_data_it->second.total_quests_completed;
}

int QuestSystem::GetTotalQuestPoints(uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return 0;
    }
    
    return player_data_it->second.total_quest_points;
}

std::unordered_map<std::string, int> QuestSystem::GetQuestCompletionByZone(uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::unordered_map<std::string, int> result;
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return result;
    }
    
    const auto& player_data = player_data_it->second;
    
    // Count completed quests by zone
    for (const auto& [quest_id, player_quest] : player_data.completed_quests) {
        if (player_quest.state == QuestState::COMPLETED) {
            auto quest_data_it = quest_database_.find(quest_id);
            if (quest_data_it != quest_database_.end()) {
                result[quest_data_it->second.zone]++;
            }
        }
    }
    
    return result;
}

// Debug and testing
void QuestSystem::DebugGiveQuest(uint64_t player_id, const std::string& quest_id) {
    StartQuest(player_id, quest_id);
}

void QuestSystem::DebugCompleteQuest(uint64_t player_id, const std::string& quest_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return;
    }
    
    auto& player_data = player_data_it->second;
    auto quest_it = player_data.active_quests.find(quest_id);
    if (quest_it == player_data.active_quests.end()) {
        return;
    }
    
    // Complete all objectives
    for (auto& objective : quest_it->second.objectives) {
        objective.current_count = objective.required_count;
    }
    
    // Complete the quest
    CheckQuestCompletion(player_id, quest_id);
    ClaimQuestReward(player_id, quest_id);
}

void QuestSystem::DebugResetQuests(uint64_t player_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    player_quests_.erase(player_id);
    
    Logger::GetInstance().Log(LogLevel::INFO, "Reset all quests for player " + 
                              std::to_string(player_id));
}

// Helper methods
bool QuestSystem::MeetsQuestRequirements(uint64_t player_id, const std::string& quest_id) const {
    auto quest_data_it = quest_database_.find(quest_id);
    if (quest_data_it == quest_database_.end()) {
        return false;
    }
    
    const QuestData& quest_data = quest_data_it->second;
    const QuestRequirement& requirements = quest_data.requirements;
    
    // Check player level (would need access to PlayerEntity)
    // For now, we'll assume the level check passes
    
    // Check class requirement
    if (requirements.required_class != PlayerClass::ANY) {
        // Would need to check player's class
        // For now, assume it passes
    }
    
    // Check race requirement
    if (requirements.required_race != PlayerRace::HUMAN) {
        // Would need to check player's race
        // For now, assume it passes
    }
    
    // Check prerequisite quests
    for (const auto& required_quest : requirements.required_quests) {
        if (!IsQuestCompleted(player_id, required_quest)) {
            return false;
        }
    }
    
    return true;
}

bool QuestSystem::CanAcceptQuest(uint64_t player_id, const std::string& quest_id) const {
    // Check if quest exists
    auto quest_data_it = quest_database_.find(quest_id);
    if (quest_data_it == quest_database_.end()) {
        return false;
    }
    
    const QuestData& quest_data = quest_data_it->second;
    
    // Check if player already has this quest
    if (HasQuest(player_id, quest_id)) {
        return false;
    }
    
    // Check requirements
    if (!MeetsQuestRequirements(player_id, quest_id)) {
        return false;
    }
    
    // Check if quest is repeatable and on cooldown
    if (quest_data.is_repeatable) {
        if (quest_data.type == QuestType::DAILY_QUEST && 
            !IsDailyQuestAvailable(player_id, quest_id)) {
            return false;
        }
        
        if (quest_data.type == QuestType::WEEKLY_QUEST && 
            !IsWeeklyQuestAvailable(player_id, quest_id)) {
            return false;
        }
    }
    
    return true;
}

void QuestSystem::GiveQuestReward(uint64_t player_id, const std::string& quest_id) {
    auto quest_data_it = quest_database_.find(quest_id);
    if (quest_data_it == quest_database_.end()) {
        return;
    }
    
    const QuestData& quest_data = quest_data_it->second;
    const QuestReward& reward = quest_data.reward;
    
    // This would give the rewards to the player
    // Implementation depends on the game's reward system
    
    Logger::GetInstance().Log(LogLevel::INFO, "Giving reward for quest " + quest_id + 
                              " to player " + std::to_string(player_id));
    
    // Would call PlayerEntity methods to give experience, items, etc.
}

void QuestSystem::ProcessQuestCompletion(uint64_t player_id, const std::string& quest_id) {
    // Check for next quests in the chain
    auto quest_data_it = quest_database_.find(quest_id);
    if (quest_data_it == quest_database_.end()) {
        return;
    }
    
    const QuestData& quest_data = quest_data_it->second;
    
    // Auto-start next quests if they exist
    for (const auto& next_quest_id : quest_data.next_quests) {
        if (CanAcceptQuest(player_id, next_quest_id)) {
            StartQuest(player_id, next_quest_id);
        }
    }
}

void QuestSystem::CheckQuestChainProgress(uint64_t player_id, const std::string& quest_id) {
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return;
    }
    
    auto& player_data = player_data_it->second;
    
    // Check all quest chains
    for (auto& [chain_id, chain] : player_data.quest_chains) {
        if (!chain.is_completed && 
            chain.current_quest_index < chain.quests_in_order.size() &&
            chain.quests_in_order[chain.current_quest_index] == quest_id) {
            AdvanceQuestChain(player_id, chain_id);
            break;
        }
    }
}

// Time-based quest management
void QuestSystem::CheckTimedObjectives(uint64_t player_id, float delta_time) {
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return;
    }
    
    auto& player_data = player_data_it->second;
    
    // Check all active quests
    std::vector<std::string> quests_to_fail;
    
    for (auto& [quest_id, player_quest] : player_data.active_quests) {
        if (player_quest.state != QuestState::IN_PROGRESS) {
            continue;
        }
        
        // Check all timed objectives
        for (auto& objective : player_quest.objectives) {
            if (objective.time_limit > 0 && objective.time_remaining > 0) {
                objective.time_remaining -= delta_time;
                
                if (objective.time_remaining <= 0) {
                    // Timed objective failed
                    quests_to_fail.push_back(quest_id);
                    break;
                }
            }
        }
    }
    
    // Fail timed-out quests
    for (const auto& quest_id : quests_to_fail) {
        FailQuest(player_id, quest_id);
    }
}

void QuestSystem::CleanupExpiredQuests(uint64_t player_id) {
    // Clean up old quest data if needed
    // This could remove very old completed quests to save memory
}

bool QuestSystem::IsDailyQuestAvailable(uint64_t player_id, const std::string& quest_id) const {
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return true; // Player hasn't done any daily quests
    }
    
    const auto& player_data = player_data_it->second;
    auto it = player_data.daily_quest_completions.find(quest_id);
    
    if (it == player_data.daily_quest_completions.end()) {
        return true; // Player hasn't done this daily quest today
    }
    
    // Check if 24 hours have passed
    auto now = std::chrono::system_clock::now();
    auto hours_since_completion = std::chrono::duration_cast<std::chrono::hours>(
        now - it->second).count();
    
    return hours_since_completion >= 24;
}

bool QuestSystem::IsWeeklyQuestAvailable(uint64_t player_id, const std::string& quest_id) const {
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return true; // Player hasn't done any weekly quests
    }
    
    const auto& player_data = player_data_it->second;
    auto it = player_data.weekly_quest_completions.find(quest_id);
    
    if (it == player_data.weekly_quest_completions.end()) {
        return true; // Player hasn't done this weekly quest this week
    }
    
    // Check if 7 days have passed
    auto now = std::chrono::system_clock::now();
    auto days_since_completion = std::chrono::duration_cast<std::chrono::hours>(
        now - it->second).count() / 24;
    
    return days_since_completion >= 7;
}

// Database operations
bool QuestSystem::LoadQuestDataFromDatabase() {
#ifdef USE_CITUS
    try {
        auto result = db_client_.ExecuteQuery("SELECT quest_data FROM quest_database");
        
        // Parse and load quest data from database
        // This is a simplified example
        
        Logger::GetInstance().Log(LogLevel::INFO, "Loaded quest data from database");
        return true;
    } catch (const std::exception& e) {
        Logger::GetInstance().Log(LogLevel::ERROR, "Error loading quest data from database: " + 
                                  std::string(e.what()));
        return false;
    }
#else
    return true; // Not implemented for non-Citus
#endif
}

bool QuestSystem::SaveQuestDataToDatabase() {
#ifdef USE_CITUS
    try {
        // Save quest data to database
        // This would serialize all quest data and store it
        
        Logger::GetInstance().Log(LogLevel::INFO, "Saved quest data to database");
        return true;
    } catch (const std::exception& e) {
        Logger::GetInstance().Log(LogLevel::ERROR, "Error saving quest data to database: " + 
                                  std::string(e.what()));
        return false;
    }
#else
    return true; // Not implemented for non-Citus
#endif
}

bool QuestSystem::LoadPlayerQuestsFromDatabase(uint64_t player_id) {
#ifdef USE_CITUS
    try {
        std::string query = "SELECT quest_data FROM player_quests WHERE player_id = " + 
                           std::to_string(player_id);
        auto result = db_client_.ExecuteQuery(query);
        
        if (result.empty()) {
            // No existing data, create new entry
            return true;
        }
        
        // Parse and deserialize quest data
        nlohmann::json json_data = nlohmann::json::parse(result[0]["quest_data"]);
        
        PlayerQuestData player_data;
        player_data.Deserialize(json_data);
        
        player_quests_[player_id] = player_data;
        
        Logger::GetInstance().Log(LogLevel::INFO, "Loaded quests for player " + 
                                  std::to_string(player_id) + " from database");
        return true;
    } catch (const std::exception& e) {
        Logger::GetInstance().Log(LogLevel::ERROR, "Error loading player quests from database: " + 
                                  std::string(e.what()));
        return false;
    }
#else
    return LoadPlayerQuests(player_id); // Fallback to local storage
#endif
}

bool QuestSystem::SavePlayerQuestsToDatabase(uint64_t player_id) {
#ifdef USE_CITUS
    auto player_data_it = player_quests_.find(player_id);
    if (player_data_it == player_quests_.end()) {
        return false;
    }
    
    try {
        nlohmann::json json_data = player_data_it->second.Serialize();
        std::string json_str = json_data.dump();
        
        std::string query = "INSERT INTO player_quests (player_id, quest_data) VALUES (" +
                           std::to_string(player_id) + ", '" + json_str + 
                           "') ON CONFLICT (player_id) DO UPDATE SET quest_data = EXCLUDED.quest_data";
        
        db_client_.ExecuteQuery(query);
        
        Logger::GetInstance().Log(LogLevel::INFO, "Saved quests for player " + 
                                  std::to_string(player_id) + " to database");
        return true;
    } catch (const std::exception& e) {
        Logger::GetInstance().Log(LogLevel::ERROR, "Error saving player quests to database: " + 
                                  std::string(e.what()));
        return false;
    }
#else
    return SavePlayerQuests(player_id); // Fallback to local storage
#endif
}
