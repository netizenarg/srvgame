#include "game/PlayerManager.hpp"

PlayerManager& PlayerManager::GetInstance() {
    static PlayerManager instance;
    return instance;
}

PlayerManager::PlayerManager()
    : dbManager_(DbManager::GetInstance()),
      lastCleanup_(std::chrono::system_clock::now()),
      running_(true),
      saveInterval_(std::chrono::minutes(5)),
      cleanupInterval_(std::chrono::minutes(10)) {

    Logger::Info("PlayerManager initialized");

    saveThread_ = RAIIThread([this]() { SaveLoop(); });
    cleanupThread_ = RAIIThread([this]() { CleanupLoop(); });
}

PlayerManager::~PlayerManager() {
    Shutdown();
    Logger::Info("PlayerManager destroyed");
}

void PlayerManager::Shutdown() {
    if (!running_.exchange(false))
        return;

    Logger::Info("Shutting down PlayerManager...");

    saveCV_.notify_all();
    cleanupCV_.notify_all();

    saveThread_.Stop();
    cleanupThread_.Stop();

    SaveAllPlayers();
    Logger::Info("PlayerManager shutdown complete");
}

std::shared_ptr<Player> PlayerManager::CreatePlayer(const std::string& username, const std::string& password) {
    static std::atomic<int64_t> nextPlayerId{1000000};
    int64_t playerId = nextPlayerId++;

    auto player = std::make_shared<Player>(playerId, username, password);

    {
        std::unique_lock<std::shared_mutex> lock(playersMutex_);
        players_[playerId] = player;
    }
    {
        std::unique_lock<std::shared_mutex> lock(usernameMutex_);
        usernameToId_[username] = playerId;
    }

    if (!player->SaveToDatabase()) {
        Logger::Error("Failed to save new player {} to database", username);
        {
            std::unique_lock<std::shared_mutex> lock(playersMutex_);
            players_.erase(playerId);
        }
        {
            std::unique_lock<std::shared_mutex> lock(usernameMutex_);
            usernameToId_.erase(username);
        }
        return nullptr;
    }

    Logger::Info("Created new player: {} (ID: {})", username, playerId);
    return player;
}

std::shared_ptr<Player> PlayerManager::GetPlayer(int64_t playerId) {
    {
        std::shared_lock<std::shared_mutex> lock(playersMutex_);
        auto it = players_.find(playerId);
        if (it != players_.end())
            return it->second;
    }
    return LoadPlayer(playerId);
}

std::shared_ptr<Player> PlayerManager::GetPlayerByUsername(const std::string& username) {
    int64_t playerId = 0;
    {
        std::shared_lock<std::shared_mutex> lock(usernameMutex_);
        auto it = usernameToId_.find(username);
        if (it != usernameToId_.end())
            playerId = it->second;
    }
    if (playerId > 0)
        return GetPlayer(playerId);

    auto player = LoadPlayerByUsername(username);
    if (player) {
        std::unique_lock<std::shared_mutex> lock(usernameMutex_);
        usernameToId_[username] = player->GetId();
    }
    return player;
}

std::shared_ptr<Player> PlayerManager::GetPlayerBySession(uint64_t sessionId) {
    int64_t playerId = 0;
    {
        std::shared_lock<std::shared_mutex> lock(sessionsMutex_);
        auto it = sessionToPlayer_.find(sessionId);
        if (it != sessionToPlayer_.end())
            playerId = it->second;
    }
    return (playerId > 0) ? GetPlayer(playerId) : nullptr;
}

uint64_t PlayerManager::GetSessionIdByPlayerId(int64_t playerId) const {
    std::shared_lock<std::shared_mutex> lock(sessionsMutex_);
    auto it = playerToSession_.find(playerId);
    return (it != playerToSession_.end()) ? it->second : 0;
}

bool PlayerManager::AuthenticatePlayer(const std::string& username, const std::string& password) {
    try {
        std::string sql = dbManager_.GetSQLProvider().GetQuery("get_player_by_username");
        if (sql.empty()) {
            Logger::Error("Missing SQL: get_player_by_username");
            return false;
        }
        auto result = dbManager_.QueryWithParams(sql, { username });
        if (result.empty()) {
            Logger::Debug("Authentication failed: username '{}' not found", username);
            return false;
        }
        std::string storedHash = result[0]["password_hash"].get<std::string>();
        if (Passwords::VerifyPassword(password, storedHash)) {
            Logger::Debug("Authentication successful for user '{}'", username);
            return true;
        } else {
            Logger::Debug("Authentication failed: invalid password for user '{}'", username);
            return false;
        }
    } catch (const std::exception& e) {
        Logger::Error("Authentication error for {}: {}", username, e.what());
        return false;
    }
}

void PlayerManager::PlayerConnected(uint64_t sessionId, int64_t playerId) {
    {
        std::unique_lock<std::shared_mutex> lock(sessionsMutex_);
        sessionToPlayer_[sessionId] = playerId;
        playerToSession_[playerId] = sessionId;
    }

    auto player = GetPlayer(playerId);
    if (player) {
        player->SetOnline(true);
        player->UpdateHeartbeat();
        player->SetSessionId(sessionId);
        UpdateConnectionStats(playerId, true);
    }
    Logger::Info("Player {} connected on session {}", playerId, sessionId);
}

void PlayerManager::PlayerDisconnected(uint64_t sessionId) {
    int64_t playerId = 0;
    {
        std::unique_lock<std::shared_mutex> lock(sessionsMutex_);
        auto it = sessionToPlayer_.find(sessionId);
        if (it != sessionToPlayer_.end()) {
            playerId = it->second;
            sessionToPlayer_.erase(it);
            playerToSession_.erase(playerId);
        }
    }

    if (playerId > 0) {
        auto player = GetPlayer(playerId);
        if (player) {
            player->SetOnline(false);
            player->SetSessionId(0);
            UpdateConnectionStats(playerId, false);
            player->SaveToDatabase();
        }
        Logger::Info("Player {} disconnected from session {}", playerId, sessionId);
    }
}

void PlayerManager::UpdateConnectionStats(int64_t playerId, bool connected) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    auto& stats = playerStats_[playerId];
    auto now = std::chrono::system_clock::now();

    if (connected) {
        stats.connection_count++;
        stats.last_connect = now;
    } else {
        stats.last_disconnect = now;
        if (stats.last_connect.time_since_epoch().count() > 0) {
            auto sessionDuration = std::chrono::duration_cast<std::chrono::seconds>(now - stats.last_connect);
            stats.total_playtime += sessionDuration.count();
        }
    }
}

void PlayerManager::MarkDirty(uint64_t playerId) {
    std::unique_lock<std::shared_mutex> lock(dirtyMutex_);
    dirtyPlayers_.insert(playerId);
}

std::vector<uint64_t> PlayerManager::GetDirtyPlayersAndClear() {
    std::unique_lock<std::shared_mutex> lock(dirtyMutex_);
    std::vector<uint64_t> result(dirtyPlayers_.begin(), dirtyPlayers_.end());
    dirtyPlayers_.clear();
    return result;
}

void PlayerManager::UpdatePosition(uint64_t playerId, float x, float y, float z) {
    auto player = GetPlayer(playerId);
    if (player) {
        player->UpdatePosition(x, y, z);
        MarkDirty(playerId);
    }
}

void PlayerManager::BroadcastToNearbyPlayers(int64_t playerId, const nlohmann::json& message) {
    auto nearby = GetNearbyPlayers(playerId, DEFAULT_BROADCAST_RANGE);
    auto& connMgr = ConnectionManager::GetInstance();
    for (int64_t id : nearby) {
        if (auto sessionId = GetSessionIdByPlayerId(id)) {
            if (auto session = connMgr.GetSession(sessionId))
                session->SendJson(message);
        }
    }
}

std::vector<int64_t> PlayerManager::GetNearbyPlayers(int64_t playerId, float radius) {
    std::vector<int64_t> result;
    auto source = GetPlayer(playerId);
    if (!source) return result;

    glm::vec3 sourcePos = source->GetPosition();
    std::shared_lock<std::shared_mutex> lock(playersMutex_);
    for (const auto& [id, player] : players_) {
        if (id != playerId && player->IsOnline() && player->GetDistanceTo(sourcePos) <= radius)
            result.push_back(id);
    }
    return result;
}

std::vector<std::shared_ptr<Player>> PlayerManager::GetPlayersInRadius(const glm::vec3& center, float radius) {
    std::vector<std::shared_ptr<Player>> result;
    std::shared_lock<std::shared_mutex> lock(playersMutex_);
    for (const auto& [id, player] : players_) {
        if (player->IsOnline() && player->GetDistanceTo(center) <= radius)
            result.push_back(player);
    }
    return result;
}

void PlayerManager::SaveAllPlayers() {
    Logger::Info("Saving all players to database...");
    std::vector<std::shared_ptr<Player>> toSave;
    {
        std::shared_lock<std::shared_mutex> lock(playersMutex_);
        for (const auto& [id, player] : players_)
            toSave.push_back(player);
    }
    int saved = 0, failed = 0;
    for (auto& player : toSave) {
        if (player->SaveToDatabase())
            saved++;
        else
            failed++;
    }
    Logger::Info("Saved {} players ({} failed)", saved, failed);
}

std::shared_ptr<Player> PlayerManager::LoadPlayer(int64_t playerId) {
    try {
        auto playerData = dbManager_.GetPlayer(playerId);
        if (playerData.empty()) {
            Logger::Warn("Player {} not found in database", playerId);
            return nullptr;
        }
        std::string username = playerData.value("username", "");
        if (username.empty()) {
            Logger::Error("Player {} has no username", playerId);
            return nullptr;
        }
        auto player = std::make_shared<Player>(playerId, username);
        player->LoadFromDatabase();
        {
            std::unique_lock<std::shared_mutex> lock(playersMutex_);
            players_[playerId] = player;
        }
        {
            std::unique_lock<std::shared_mutex> lock(usernameMutex_);
            usernameToId_[username] = playerId;
        }
        Logger::Debug("Loaded player {} from database", playerId);
        return player;
    } catch (const std::exception& e) {
        Logger::Error("Failed to load player {}: {}", playerId, e.what());
        return nullptr;
    }
}

std::shared_ptr<Player> PlayerManager::LoadPlayerByUsername(const std::string& username) {
    try {
        std::string sql = dbManager_.GetSQLProvider().GetQuery("get_player_by_username");
        if (sql.empty()) {
            Logger::Error("Missing SQL: get_player_by_username");
            return nullptr;
        }
        auto result = dbManager_.QueryWithParams(sql, { username });
        if (result.empty()) return nullptr;
        int64_t playerId = result[0]["id"].get<int64_t>();
        return LoadPlayer(playerId);
    } catch (const std::exception& e) {
        Logger::Error("Failed to load player by username {}: {}", username, e.what());
        return nullptr;
    }
}

void PlayerManager::SaveLoop() {
    Logger::Info("Player save loop started");
    while (running_) {
        std::unique_lock<std::mutex> lock(saveMutex_);
        saveCV_.wait_for(lock, saveInterval_, [this] { return !running_; });
        if (!running_) break;
        SaveAllPlayers();
    }
    Logger::Info("Player save loop stopped");
}

void PlayerManager::CleanupLoop() {
    Logger::Info("Player cleanup loop started");
    while (running_) {
        std::unique_lock<std::mutex> lock(cleanupMutex_);
        cleanupCV_.wait_for(lock, cleanupInterval_, [this] { return !running_; });
        if (!running_) break;
        CleanupInactivePlayers();
    }
    Logger::Info("Player cleanup loop stopped");
}

void PlayerManager::CleanupInactivePlayers() {
    auto now = std::chrono::system_clock::now();
    if (std::chrono::duration_cast<std::chrono::minutes>(now - lastCleanup_).count() < 10)
        return;
    lastCleanup_ = now;

    std::vector<int64_t> toRemove;
    {
        std::shared_lock<std::shared_mutex> lock(playersMutex_);
        for (const auto& [id, player] : players_) {
            if (!player->IsOnline() && player->IsHeartbeatExpired(3600))
                toRemove.push_back(id);
        }
    }

    if (toRemove.empty()) return;

    std::unique_lock<std::shared_mutex> lock(playersMutex_);
    for (int64_t id : toRemove) {
        auto it = players_.find(id);
        if (it == players_.end()) continue;
        it->second->SaveToDatabase();

        {
            std::unique_lock<std::shared_mutex> ulock(usernameMutex_);
            for (auto uit = usernameToId_.begin(); uit != usernameToId_.end(); ) {
                if (uit->second == id) uit = usernameToId_.erase(uit);
                else ++uit;
            }
        }
        {
            std::unique_lock<std::shared_mutex> slock(sessionsMutex_);
            for (auto sit = sessionToPlayer_.begin(); sit != sessionToPlayer_.end(); ) {
                if (sit->second == id) sit = sessionToPlayer_.erase(sit);
                else ++sit;
            }
            playerToSession_.erase(id);
        }
        {
            std::lock_guard<std::mutex> slock(statsMutex_);
            playerStats_.erase(id);
        }
        players_.erase(it);
        Logger::Debug("Removed inactive player {}", id);
    }
    Logger::Info("Cleaned up {} inactive players", toRemove.size());
}

PlayerStats PlayerManager::GetPlayerStats(int64_t playerId) const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    auto it = playerStats_.find(playerId);
    return (it != playerStats_.end()) ? it->second : PlayerStats{};
}

GlobalPlayerStats PlayerManager::GetGlobalPlayerStats() const {
    GlobalPlayerStats stats;
    stats.total_players = GetPlayerCount();
    stats.online_players = GetOnlinePlayerCount();

    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        for (const auto& [id, ps] : playerStats_) {
            stats.total_connections += ps.connection_count;
            stats.total_playtime += ps.total_playtime;
        }
        if (stats.total_players > 0)
            stats.average_playtime = static_cast<double>(stats.total_playtime) / stats.total_players;
    }

    {
        std::shared_lock<std::shared_mutex> lock(playersMutex_);
        for (const auto& [id, player] : players_) {
            int level = player->GetLevel();
            if (level >= 1 && level <= 10) stats.level_1_10++;
            else if (level <= 20) stats.level_11_20++;
            else if (level <= 30) stats.level_21_30++;
            else if (level <= 40) stats.level_31_40++;
            else if (level <= 50) stats.level_41_50++;
            else stats.level_50_plus++;
        }
    }
    return stats;
}

void PlayerManager::PrintStats() {
    auto stats = GetGlobalPlayerStats();
    Logger::Info("=== Player Manager Statistics ===");
    Logger::Info("  Total Players: {}", stats.total_players);
    Logger::Info("  Online Players: {}", stats.online_players);
    Logger::Info("  Total Connections: {}", stats.total_connections);
    Logger::Info("  Total Playtime: {} seconds", stats.total_playtime);
    Logger::Info("  Average Playtime: {:.1f} seconds", stats.average_playtime);
    Logger::Info("  Player Distribution by Level:");
    Logger::Info("    Level 1-10: {}", stats.level_1_10);
    Logger::Info("    Level 11-20: {}", stats.level_11_20);
    Logger::Info("    Level 21-30: {}", stats.level_21_30);
    Logger::Info("    Level 31-40: {}", stats.level_31_40);
    Logger::Info("    Level 41-50: {}", stats.level_41_50);
    Logger::Info("    Level 50+: {}", stats.level_50_plus);
    Logger::Info("=================================");
}

std::vector<int64_t> PlayerManager::SearchPlayers(const std::string& query, int limit) {
    std::vector<int64_t> result;
    std::shared_lock<std::shared_mutex> lock(usernameMutex_);
    for (const auto& [name, id] : usernameToId_) {
        if (name.find(query) != std::string::npos) {
            result.push_back(id);
            if (limit > 0 && result.size() >= static_cast<size_t>(limit))
                break;
        }
    }
    return result;
}

std::vector<int64_t> PlayerManager::GetPlayersByLevelRange(int minLevel, int maxLevel) {
    std::vector<int64_t> result;
    std::shared_lock<std::shared_mutex> lock(playersMutex_);
    for (const auto& [id, player] : players_) {
        int level = player->GetLevel();
        if (level >= minLevel && level <= maxLevel)
            result.push_back(id);
    }
    return result;
}

void PlayerManager::CreateParty(int64_t leaderId, const std::string& partyName) {
    std::lock_guard<std::mutex> lock(partyMutex_);
    Party party;
    party.id = GeneratePartyId();
    party.name = partyName;
    party.leader_id = leaderId;
    party.members.insert(leaderId);
    party.created_at = std::chrono::system_clock::now();
    parties_[party.id] = party;

    auto& connMgr = ConnectionManager::GetInstance();
    if (auto sessionId = GetSessionIdByPlayerId(leaderId))
        connMgr.AddToGroup("party_" + std::to_string(party.id), sessionId);

    Logger::Info("Party created: {} (ID: {}) by player {}", partyName, party.id, leaderId);
}

void PlayerManager::AddPlayerToParty(int64_t partyId, int64_t playerId) {
    std::lock_guard<std::mutex> lock(partyMutex_);
    auto it = parties_.find(partyId);
    if (it == parties_.end()) {
        Logger::Warn("Party {} not found", partyId);
        return;
    }
    auto& party = it->second;
    if (party.members.size() >= MAX_PARTY_SIZE) {
        Logger::Warn("Party {} is full", partyId);
        return;
    }
    party.members.insert(playerId);

    auto& connMgr = ConnectionManager::GetInstance();
    if (auto sessionId = GetSessionIdByPlayerId(playerId))
        connMgr.AddToGroup("party_" + std::to_string(partyId), sessionId);

    Logger::Info("Player {} added to party {}", playerId, partyId);
}

void PlayerManager::RemovePlayerFromParty(int64_t partyId, int64_t playerId) {
    std::lock_guard<std::mutex> lock(partyMutex_);
    auto it = parties_.find(partyId);
    if (it == parties_.end()) return;

    auto& party = it->second;
    party.members.erase(playerId);

    auto& connMgr = ConnectionManager::GetInstance();
    if (auto sessionId = GetSessionIdByPlayerId(playerId))
        connMgr.RemoveFromGroup("party_" + std::to_string(partyId), sessionId);

    if (party.members.empty() || playerId == party.leader_id) {
        parties_.erase(it);
        Logger::Info("Party {} disbanded", partyId);
    } else {
        Logger::Info("Player {} removed from party {}", playerId, partyId);
    }
}

std::vector<int64_t> PlayerManager::GetPartyMembers(int64_t partyId) const {
    std::lock_guard<std::mutex> lock(partyMutex_);
    auto it = parties_.find(partyId);
    if (it == parties_.end())
        return {};
    return std::vector<int64_t>(it->second.members.begin(), it->second.members.end());
}

int64_t PlayerManager::GeneratePartyId() {
    static std::atomic<int64_t> nextPartyId{1000};
    return nextPartyId++;
}

void PlayerManager::SendToPlayer(int64_t playerId, const nlohmann::json& message) {
    if (auto sessionId = GetSessionIdByPlayerId(playerId)) {
        auto& connMgr = ConnectionManager::GetInstance();
        if (auto session = connMgr.GetSession(sessionId))
            session->SendJson(message);
    } else {
        Logger::Warn("Player {} is not online", playerId);
    }
}

void PlayerManager::SendToPlayers(const std::vector<int64_t>& playerIds, const nlohmann::json& message) {
    auto& connMgr = ConnectionManager::GetInstance();
    for (int64_t id : playerIds) {
        if (auto sessionId = GetSessionIdByPlayerId(id)) {
            if (auto session = connMgr.GetSession(sessionId))
                session->SendJson(message);
        }
    }
}

void PlayerManager::BanPlayer(int64_t playerId, const std::string& reason, int64_t durationSeconds) {
    auto player = GetPlayer(playerId);
    if (!player) {
        Logger::Error("Cannot ban non-existent player {}", playerId);
        return;
    }
    player->SetBanned(true);
    player->SetBanReason(reason);
    if (durationSeconds > 0) {
        auto expires = std::chrono::system_clock::now() + std::chrono::seconds(durationSeconds);
        player->SetBanExpires(expires);
    }

    if (auto sessionId = GetSessionIdByPlayerId(playerId)) {
        auto& connMgr = ConnectionManager::GetInstance();
        if (auto session = connMgr.GetSession(sessionId)) {
            nlohmann::json msg = {{"msg", "banned"}, {"reason", reason}, {"duration", durationSeconds}};
            session->SendJson(msg);
            session->Stop();
        }
    }
    player->SaveToDatabase();
    Logger::Info("Player {} banned: {} ({}s)", playerId, reason, durationSeconds);
}

void PlayerManager::UnbanPlayer(int64_t playerId) {
    auto player = GetPlayer(playerId);
    if (player) {
        player->SetBanned(false);
        player->SetBanReason("");
        player->SetBanExpires({});
        player->SaveToDatabase();
        Logger::Info("Player {} unbanned", playerId);
    }
}

void PlayerManager::TeleportPlayer(int64_t playerId, float x, float y, float z) {
    auto player = GetPlayer(playerId);
    if (player) {
        player->UpdatePosition(x, y, z);
        nlohmann::json msg = {{"type", "teleported"}, {"x", x}, {"y", y}, {"z", z},
                              {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch()).count()}};
        SendToPlayer(playerId, msg);
        dbManager_.UpdatePlayerPosition(playerId, x, y, z);
        Logger::Debug("Player {} teleported to ({}, {}, {})", playerId, x, y, z);
    }
}

bool PlayerManager::GiveItemToPlayer(int64_t playerId, const std::string& itemId, int count) {
    auto player = GetPlayer(playerId);
    if (!player) return false;
    player->AddItem(itemId, count);
    nlohmann::json msg = {{"type", "item_received"}, {"item_id", itemId}, {"count", count},
                          {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch()).count()}};
    SendToPlayer(playerId, msg);
    player->SaveToDatabase();
    Logger::Debug("Gave {} x{} to player {}", itemId, count, playerId);
    return true;
}

bool PlayerManager::TakeItemFromPlayer(int64_t playerId, const std::string& itemId, int count) {
    auto player = GetPlayer(playerId);
    if (!player) return false;
    if (player->GetItemCount(itemId) < count) return false;
    player->RemoveItem(itemId, count);
    nlohmann::json msg = {{"type", "item_removed"}, {"item_id", itemId}, {"count", count},
                          {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch()).count()}};
    SendToPlayer(playerId, msg);
    player->SaveToDatabase();
    Logger::Debug("Took {} x{} from player {}", itemId, count, playerId);
    return true;
}

void PlayerManager::AddAchievementToPlayer(int64_t playerId, const std::string& achievementId) {
    auto player = GetPlayer(playerId);
    if (player) {
        player->AddAchievement(achievementId);
        nlohmann::json msg = {{"type", "achievement_earned"}, {"achievement_id", achievementId},
                              {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch()).count()}};
        SendToPlayer(playerId, msg);
        player->SaveToDatabase();
        Logger::Info("Player {} earned achievement {}", playerId, achievementId);
    }
}

std::vector<std::shared_ptr<Player>> PlayerManager::GetAllPlayers() const {
    std::shared_lock<std::shared_mutex> lock(playersMutex_);
    std::vector<std::shared_ptr<Player>> result;
    result.reserve(players_.size());
    for (const auto& [id, player] : players_)
        result.push_back(player);
    return result;
}

std::vector<std::shared_ptr<Player>> PlayerManager::GetOnlinePlayers() const {
    std::shared_lock<std::shared_mutex> lock(playersMutex_);
    std::vector<std::shared_ptr<Player>> result;
    for (const auto& [id, player] : players_)
        if (player->IsOnline())
            result.push_back(player);
    return result;
}

size_t PlayerManager::GetPlayerCount() const {
    std::shared_lock<std::shared_mutex> lock(playersMutex_);
    return players_.size();
}

size_t PlayerManager::GetOnlinePlayerCount() const {
    size_t count = 0;
    std::shared_lock<std::shared_mutex> lock(playersMutex_);
    for (const auto& [id, player] : players_)
        if (player->IsOnline()) count++;
    return count;
}

bool PlayerManager::PlayerExists(const std::string& username) const {
    std::shared_lock<std::shared_mutex> lock(usernameMutex_);
    return usernameToId_.find(username) != usernameToId_.end();
}
