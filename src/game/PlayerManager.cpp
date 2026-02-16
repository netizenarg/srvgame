#include "game/PlayerManager.hpp"

std::mutex PlayerManager::instanceMutex_;
PlayerManager* PlayerManager::instance_ = nullptr;

PlayerManager& PlayerManager::GetInstance() {
    std::lock_guard<std::mutex> lock(instanceMutex_);
    if (!instance_) {
        instance_ = new PlayerManager();
    }
    return *instance_;
}

PlayerManager::PlayerManager()
: dbClient_(CitusClient::GetInstance()),
lastCleanup_(std::chrono::steady_clock::now()),
running_(true),
saveInterval_(std::chrono::minutes(5)),
cleanupInterval_(std::chrono::minutes(10)) {

    Logger::Info("PlayerManager initialized");

    // Start background threads using RAIIThread
    saveThread_ = RAIIThread([this]() { SaveLoop(); });
    cleanupThread_ = RAIIThread([this]() { CleanupLoop(); });
}

PlayerManager::~PlayerManager() {
    Shutdown();
    Logger::Info("PlayerManager destroyed");
}

void PlayerManager::Shutdown() {
    if (!running_) {
        return;
    }

    Logger::Info("Shutting down PlayerManager...");

    running_ = false;

    // Notify threads
    saveCV_.notify_all();
    cleanupCV_.notify_all();

    // RAIIThread destructors will handle thread cleanup automatically
    saveThread_.Stop();
    cleanupThread_.Stop();

    // Save all players before shutdown
    SaveAllPlayers();

    Logger::Info("PlayerManager shutdown complete");
}

std::shared_ptr<Player> PlayerManager::CreatePlayer(const std::string& username) {
    // Generate player ID
    static std::atomic<int64_t> nextPlayerId{1000000};
    int64_t playerId = nextPlayerId++;

    auto player = std::make_shared<Player>(playerId, username);

    {
        std::unique_lock<std::shared_mutex> lock(playersMutex_);
        players_[playerId] = player;
    }

    {
        std::unique_lock<std::shared_mutex> lock(usernameMutex_);
        usernameToId_[username] = playerId;
    }

    // Save to database
    if (!player->SaveToDatabase()) {
        Logger::Error("Failed to save new player {} to database", username);

        // Clean up
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
    std::shared_lock<std::shared_mutex> lock(playersMutex_);

    auto it = players_.find(playerId);
    if (it != players_.end()) {
        return it->second;
    }

    // Try to load from database
    lock.unlock();
    return LoadPlayer(playerId);
}

std::shared_ptr<Player> PlayerManager::GetPlayerByUsername(const std::string& username) {
    int64_t playerId = 0;

    {
        std::shared_lock<std::shared_mutex> lock(usernameMutex_);
        auto it = usernameToId_.find(username);
        if (it != usernameToId_.end()) {
            playerId = it->second;
        }
    }

    if (playerId > 0) {
        return GetPlayer(playerId);
    }

    // Try to find in database
    auto player = LoadPlayerByUsername(username);
    if (player) {
        // Cache the mapping
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
        if (it != sessionToPlayer_.end()) {
            playerId = it->second;
        }
    }

    if (playerId > 0) {
        return GetPlayer(playerId);
    }

    return nullptr;
}

uint64_t PlayerManager::GetSessionIdByPlayerId(int64_t playerId) const {
    std::shared_lock<std::shared_mutex> lock(sessionsMutex_);

    auto it = playerToSession_.find(playerId);
    if (it != playerToSession_.end()) {
        return it->second;
    }

    return 0;
}

bool PlayerManager::AuthenticatePlayer(const std::string& username, const std::string& password) {
    try {
        // In production, use proper password hashing and database lookup
        auto& dbClient = CitusClient::GetInstance();

        // Query player from database
        auto playerData = dbClient.Query(
            "SELECT password_hash FROM players WHERE username = '" + username + "'");

        // This is simplified - in reality, you'd compare hashed passwords
        // and handle salt, etc.

        return true; // Simplified authentication

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

        // Update connection statistics
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

            auto playerIt = playerToSession_.find(playerId);
            if (playerIt != playerToSession_.end()) {
                playerToSession_.erase(playerIt);
            }
        }
    }

    if (playerId > 0) {
        auto player = GetPlayer(playerId);
        if (player) {
            player->SetOnline(false);
            player->SetSessionId(0);

            // Update connection statistics
            UpdateConnectionStats(playerId, false);

            // Save player state
            player->SaveToDatabase();
        }

        Logger::Info("Player {} disconnected from session {}", playerId, sessionId);
    }
}

void PlayerManager::UpdateConnectionStats(int64_t playerId, bool connected) {
    std::lock_guard<std::mutex> lock(statsMutex_);

    auto& stats = playerStats_[playerId];
    auto now = std::chrono::steady_clock::now();

    if (connected) {
        stats.connection_count++;
        stats.last_connect = now;
    } else {
        stats.last_disconnect = now;

        if (stats.last_connect.time_since_epoch().count() > 0) {
            auto sessionDuration = std::chrono::duration_cast<std::chrono::seconds>(
                now - stats.last_connect);
            stats.total_playtime += sessionDuration.count();
        }
    }
}

void PlayerManager::BroadcastToNearbyPlayers(int64_t playerId, const nlohmann::json& message) {
    auto nearbyPlayers = GetNearbyPlayers(playerId, DEFAULT_BROADCAST_RANGE);

    auto& connMgr = ConnectionManager::GetInstance();
    for (int64_t nearbyId : nearbyPlayers) {
        auto sessionId = GetSessionIdByPlayerId(nearbyId);
        if (sessionId > 0) {
            auto session = connMgr.GetSession(sessionId);
            if (session) {
                session->Send(message);
            }
        }
    }
}

std::vector<int64_t> PlayerManager::GetNearbyPlayers(int64_t playerId, float radius) {
    std::vector<int64_t> nearbyPlayers;

    auto sourcePlayer = GetPlayer(playerId);
    if (!sourcePlayer) {
        return nearbyPlayers;
    }

    auto sourcePos = sourcePlayer->GetPosition();
    Position sourcePosition = {
        sourcePos["x"].get<float>(),
        sourcePos["y"].get<float>(),
        sourcePos["z"].get<float>()
    };

    std::shared_lock<std::shared_mutex> lock(playersMutex_);

    for (const auto& [id, player] : players_) {
        if (id == playerId || !player->IsOnline()) {
            continue;
        }

        if (player->GetDistanceTo(sourcePosition) <= radius) {
            nearbyPlayers.push_back(id);
        }
    }

    return nearbyPlayers;
}

std::vector<std::shared_ptr<Player>> PlayerManager::GetPlayersInRadius(const Position& center, float radius) {
    std::vector<std::shared_ptr<Player>> playersInRadius;

    std::shared_lock<std::shared_mutex> lock(playersMutex_);

    for (const auto& [id, player] : players_) {
        if (player->IsOnline() && player->GetDistanceTo(center) <= radius) {
            playersInRadius.push_back(player);
        }
    }

    return playersInRadius;
}

void PlayerManager::SaveAllPlayers() {
    Logger::Info("Saving all players to database...");

    std::vector<std::shared_ptr<Player>> playersToSave;

    {
        std::shared_lock<std::shared_mutex> lock(playersMutex_);
        playersToSave.reserve(players_.size());
        for (const auto& [id, player] : players_) {
            playersToSave.push_back(player);
        }
    }

    int savedCount = 0;
    int failedCount = 0;

    for (const auto& player : playersToSave) {
        if (player->SaveToDatabase()) {
            savedCount++;
        } else {
            failedCount++;
        }
    }

    Logger::Info("Saved {} players to database ({} failed)", savedCount, failedCount);
}

void PlayerManager::CleanupInactivePlayers() {
    auto now = std::chrono::steady_clock::now();

    // Only run cleanup every 10 minutes
    if (std::chrono::duration_cast<std::chrono::minutes>(now - lastCleanup_).count() < 10) {
        return;
    }

    lastCleanup_ = now;

    std::vector<int64_t> playersToRemove;

    {
        std::shared_lock<std::shared_mutex> lock(playersMutex_);

        for (const auto& [id, player] : players_) {
            // Remove players who have been offline for over 1 hour
            if (!player->IsOnline() && player->IsHeartbeatExpired(3600)) {
                playersToRemove.push_back(id);
            }
        }
    }

    if (!playersToRemove.empty()) {
        std::unique_lock<std::shared_mutex> lock(playersMutex_);

        for (int64_t playerId : playersToRemove) {
            auto it = players_.find(playerId);
            if (it != players_.end()) {
                // Save before removing
                it->second->SaveToDatabase();

                // Remove from username map
                {
                    std::unique_lock<std::shared_mutex> usernameLock(usernameMutex_);
                    for (auto usernameIt = usernameToId_.begin(); usernameIt != usernameToId_.end();) {
                        if (usernameIt->second == playerId) {
                            usernameIt = usernameToId_.erase(usernameIt);
                        } else {
                            ++usernameIt;
                        }
                    }
                }

                // Remove from sessions map
                {
                    std::unique_lock<std::shared_mutex> sessionsLock(sessionsMutex_);
                    for (auto sessionIt = sessionToPlayer_.begin(); sessionIt != sessionToPlayer_.end();) {
                        if (sessionIt->second == playerId) {
                            sessionIt = sessionToPlayer_.erase(sessionIt);
                        } else {
                            ++sessionIt;
                        }
                    }
                    playerToSession_.erase(playerId);
                }

                // Remove from stats
                {
                    std::lock_guard<std::mutex> statsLock(statsMutex_);
                    playerStats_.erase(playerId);
                }

                players_.erase(it);
                Logger::Debug("Removed inactive player {}", playerId);
            }
        }

        Logger::Info("Cleaned up {} inactive players", playersToRemove.size());
    }
}

std::vector<std::shared_ptr<Player>> PlayerManager::GetAllPlayers() const {
    std::vector<std::shared_ptr<Player>> allPlayers;

    std::shared_lock<std::shared_mutex> lock(playersMutex_);
    allPlayers.reserve(players_.size());

    for (const auto& [id, player] : players_) {
        allPlayers.push_back(player);
    }

    return allPlayers;
}

std::vector<std::shared_ptr<Player>> PlayerManager::GetOnlinePlayers() const {
    std::vector<std::shared_ptr<Player>> onlinePlayers;

    std::shared_lock<std::shared_mutex> lock(playersMutex_);

    for (const auto& [id, player] : players_) {
        if (player->IsOnline()) {
            onlinePlayers.push_back(player);
        }
    }

    return onlinePlayers;
}

size_t PlayerManager::GetPlayerCount() const {
    std::shared_lock<std::shared_mutex> lock(playersMutex_);
    return players_.size();
}

size_t PlayerManager::GetOnlinePlayerCount() const {
    size_t count = 0;

    std::shared_lock<std::shared_mutex> lock(playersMutex_);

    for (const auto& [id, player] : players_) {
        if (player->IsOnline()) {
            count++;
        }
    }

    return count;
}

bool PlayerManager::PlayerExists(const std::string& username) const {
    std::shared_lock<std::shared_mutex> lock(usernameMutex_);
    return usernameToId_.find(username) != usernameToId_.end();
}

std::shared_ptr<Player> PlayerManager::LoadPlayer(int64_t playerId) {
    try {
        // Check if player already exists
        {
            std::shared_lock<std::shared_mutex> lock(playersMutex_);
            auto it = players_.find(playerId);
            if (it != players_.end()) {
                return it->second;
            }
        }

        // Load from database
        auto& dbClient = CitusClient::GetInstance();
        auto playerData = dbClient.GetPlayer(playerId);

        if (playerData.empty()) {
            Logger::Warn("Player {} not found in database", playerId);
            return nullptr;
        }

        // Create player object
        std::string username = playerData.value("username", "");
        if (username.empty()) {
            Logger::Error("Player {} has no username", playerId);
            return nullptr;
        }

        auto player = std::make_shared<Player>(playerId, username);
        player->LoadFromDatabase();

        // Add to cache
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
        auto& dbClient = CitusClient::GetInstance();

        // Query player ID from database
        auto result = dbClient.Query(
            "SELECT player_id FROM players WHERE username = '" + username + "'");

        if (result.empty()) {
            return nullptr;
        }

        int64_t playerId = result[0]["player_id"].get<int64_t>();
        return LoadPlayer(playerId);

    } catch (const std::exception& e) {
        Logger::Error("Failed to load player by username {}: {}", username, e.what());
        return nullptr;
    }
}

void PlayerManager::SaveLoop() {
    Logger::Info("Player save loop started");

    while (running_) {
        try {
            std::unique_lock<std::mutex> lock(saveMutex_);
            saveCV_.wait_for(lock, saveInterval_, [this] { return !running_; });

            if (!running_) {
                break;
            }

            SaveAllPlayers();

        } catch (const std::exception& e) {
            Logger::Error("Error in save loop: {}", e.what());
        }
    }

    Logger::Info("Player save loop stopped");
}

void PlayerManager::CleanupLoop() {
    Logger::Info("Player cleanup loop started");

    while (running_) {
        try {
            std::unique_lock<std::mutex> lock(cleanupMutex_);
            cleanupCV_.wait_for(lock, cleanupInterval_, [this] { return !running_; });

            if (!running_) {
                break;
            }

            CleanupInactivePlayers();

        } catch (const std::exception& e) {
            Logger::Error("Error in cleanup loop: {}", e.what());
        }
    }

    Logger::Info("Player cleanup loop stopped");
}

// =============== Player Statistics ===============

PlayerStats PlayerManager::GetPlayerStats(int64_t playerId) const {
    std::lock_guard<std::mutex> lock(statsMutex_);

    auto it = playerStats_.find(playerId);
    if (it != playerStats_.end()) {
        return it->second;
    }

    return PlayerStats{};
}

GlobalPlayerStats PlayerManager::GetGlobalPlayerStats() const {
    GlobalPlayerStats stats;

    stats.total_players = GetPlayerCount();
    stats.online_players = GetOnlinePlayerCount();

    {
        std::lock_guard<std::mutex> lock(statsMutex_);

        for (const auto& [playerId, playerStat] : playerStats_) {
            stats.total_connections += playerStat.connection_count;
            stats.total_playtime += playerStat.total_playtime;
        }

        if (stats.total_players > 0) {
            stats.average_playtime = static_cast<double>(stats.total_playtime) / stats.total_players;
        }
    }

    // Calculate player distribution by level
    {
        std::shared_lock<std::shared_mutex> lock(playersMutex_);

        for (const auto& [playerId, player] : players_) {
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
    auto stats = GetGlobalStats();

    Logger::Info("=== Player Manager Statistics ===");
    Logger::Info("  Total Players: {}", stats.total_players);
    Logger::Info("  Online Players: {}", stats.online_players);
    Logger::Info("  Total Connections: {}", stats.total_connections);
    Logger::Info("  Total Playtime: {} seconds", stats.total_playtime);
    Logger::Info("  Average Playtime: {:.1f} seconds", stats.average_playtime);
    Logger::Info("  ");
    Logger::Info("  Player Distribution by Level:");
    Logger::Info("    Level 1-10: {}", stats.level_1_10);
    Logger::Info("    Level 11-20: {}", stats.level_11_20);
    Logger::Info("    Level 21-30: {}", stats.level_21_30);
    Logger::Info("    Level 31-40: {}", stats.level_31_40);
    Logger::Info("    Level 41-50: {}", stats.level_41_50);
    Logger::Info("    Level 50+: {}", stats.level_50_plus);
    Logger::Info("=================================");
}

// =============== Search and Query Methods ===============

std::vector<int64_t> PlayerManager::SearchPlayers(const std::string& query, int limit) {
    std::vector<int64_t> results;

    {
        std::shared_lock<std::shared_mutex> lock(usernameMutex_);

        for (const auto& [username, playerId] : usernameToId_) {
            if (username.find(query) != std::string::npos) {
                results.push_back(playerId);

                if (limit > 0 && results.size() >= static_cast<size_t>(limit)) {
                    break;
                }
            }
        }
    }

    return results;
}

std::vector<int64_t> PlayerManager::GetPlayersByLevelRange(int minLevel, int maxLevel) {
    std::vector<int64_t> results;

    std::shared_lock<std::shared_mutex> lock(playersMutex_);

    for (const auto& [playerId, player] : players_) {
        int level = player->GetLevel();
        if (level >= minLevel && level <= maxLevel) {
            results.push_back(playerId);
        }
    }

    return results;
}

// =============== Player Groups and Parties ===============

void PlayerManager::CreateParty(int64_t leaderId, const std::string& partyName) {
    std::lock_guard<std::mutex> lock(partyMutex_);

    Party party;
    party.id = GeneratePartyId();
    party.name = partyName;
    party.leader_id = leaderId;
    party.members.insert(leaderId);
    party.created_at = std::chrono::steady_clock::now();

    parties_[party.id] = party;

    // Add leader to party group
    {
        auto& connMgr = ConnectionManager::GetInstance();
        auto sessionId = GetSessionIdByPlayerId(leaderId);
        if (sessionId > 0) {
            connMgr.AddToGroup("party_" + std::to_string(party.id), sessionId);
        }
    }

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

    // Add player to party group
    {
        auto& connMgr = ConnectionManager::GetInstance();
        auto sessionId = GetSessionIdByPlayerId(playerId);
        if (sessionId > 0) {
            connMgr.AddToGroup("party_" + std::to_string(partyId), sessionId);
        }
    }

    Logger::Info("Player {} added to party {}", playerId, partyId);
}

void PlayerManager::RemovePlayerFromParty(int64_t partyId, int64_t playerId) {
    std::lock_guard<std::mutex> lock(partyMutex_);

    auto it = parties_.find(partyId);
    if (it == parties_.end()) {
        return;
    }

    auto& party = it->second;
    party.members.erase(playerId);

    // Remove player from party group
    {
        auto& connMgr = ConnectionManager::GetInstance();
        auto sessionId = GetSessionIdByPlayerId(playerId);
        if (sessionId > 0) {
            connMgr.RemoveFromGroup("party_" + std::to_string(partyId), sessionId);
        }
    }

    // If party is empty or leader left, disband party
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
    if (it == parties_.end()) {
        return {};
    }

    return std::vector<int64_t>(it->second.members.begin(), it->second.members.end());
}

int64_t PlayerManager::GeneratePartyId() {
    static std::atomic<int64_t> nextPartyId{1000};
    return nextPartyId++;
}

// =============== Player Messaging ===============

void PlayerManager::SendToPlayer(int64_t playerId, const nlohmann::json& message) {
    auto sessionId = GetSessionIdByPlayerId(playerId);
    if (sessionId == 0) {
        Logger::Warn("Player {} is not online", playerId);
        return;
    }

    auto& connMgr = ConnectionManager::GetInstance();
    auto session = connMgr.GetSession(sessionId);
    if (session) {
        session->Send(message);
    }
}

void PlayerManager::SendToPlayers(const std::vector<int64_t>& playerIds, const nlohmann::json& message) {
    auto& connMgr = ConnectionManager::GetInstance();

    for (int64_t playerId : playerIds) {
        auto sessionId = GetSessionIdByPlayerId(playerId);
        if (sessionId > 0) {
            auto session = connMgr.GetSession(sessionId);
            if (session) {
                session->Send(message);
            }
        }
    }
}

// =============== Player Moderation ===============

void PlayerManager::BanPlayer(int64_t playerId, const std::string& reason, int64_t durationSeconds) {
    auto player = GetPlayer(playerId);
    if (!player) {
        Logger::Error("Cannot ban non-existent player {}", playerId);
        return;
    }

    player->SetBanned(true);
    player->SetBanReason(reason);

    if (durationSeconds > 0) {
        auto expires = std::chrono::system_clock::now() +
        std::chrono::seconds(durationSeconds);
        player->SetBanExpires(expires);
    }

    // Disconnect player if online
    auto sessionId = GetSessionIdByPlayerId(playerId);
    if (sessionId > 0) {
        auto& connMgr = ConnectionManager::GetInstance();
        auto session = connMgr.GetSession(sessionId);
        if (session) {
            nlohmann::json banMessage = {
                {"type", "banned"},
                {"reason", reason},
                {"duration", durationSeconds}
            };
            session->Send(banMessage);
            session->Stop();
        }
    }

    player->SaveToDatabase();

    Logger::Info("Player {} banned: {} (duration: {} seconds)",
                 playerId, reason, durationSeconds);
}

void PlayerManager::UnbanPlayer(int64_t playerId) {
    auto player = GetPlayer(playerId);
    if (player) {
        player->SetBanned(false);
        player->SetBanReason("");
        player->SetBanExpires(std::chrono::system_clock::time_point());
        player->SaveToDatabase();

        Logger::Info("Player {} unbanned", playerId);
    }
}

// =============== Player Teleportation ===============

void PlayerManager::TeleportPlayer(int64_t playerId, float x, float y, float z) {
    auto player = GetPlayer(playerId);
    if (player) {
        player->UpdatePosition(x, y, z);

        // Notify player
        nlohmann::json teleportMessage = {
            {"type", "teleported"},
            {"x", x},
            {"y", y},
            {"z", z},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };

        SendToPlayer(playerId, teleportMessage);

        // Update database
        auto& dbClient = CitusClient::GetInstance();
        dbClient.UpdatePlayerPosition(playerId, x, y, z);

        Logger::Debug("Player {} teleported to ({}, {}, {})", playerId, x, y, z);
    }
}

// =============== Player Inventory Management ===============

bool PlayerManager::GiveItemToPlayer(int64_t playerId, const std::string& itemId, int count) {
    auto player = GetPlayer(playerId);
    if (!player) {
        return false;
    }

    player->AddItem(itemId, count);

    // Notify player
    nlohmann::json itemMessage = {
        {"type", "item_received"},
        {"item_id", itemId},
        {"count", count},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };

    SendToPlayer(playerId, itemMessage);

    player->SaveToDatabase();

    Logger::Debug("Gave {} x{} to player {}", itemId, count, playerId);
    return true;
}

bool PlayerManager::TakeItemFromPlayer(int64_t playerId, const std::string& itemId, int count) {
    auto player = GetPlayer(playerId);
    if (!player) {
        return false;
    }

    int currentCount = player->GetItemCount(itemId);
    if (currentCount < count) {
        return false;
    }

    player->RemoveItem(itemId, count);

    // Notify player
    nlohmann::json itemMessage = {
        {"type", "item_removed"},
        {"item_id", itemId},
        {"count", count},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };

    SendToPlayer(playerId, itemMessage);

    player->SaveToDatabase();

    Logger::Debug("Took {} x{} from player {}", itemId, count, playerId);
    return true;
}

// =============== Player Achievement Tracking ===============

void PlayerManager::AddAchievementToPlayer(int64_t playerId, const std::string& achievementId) {
    auto player = GetPlayer(playerId);
    if (player) {
        player->AddAchievement(achievementId);

        // Notify player
        nlohmann::json achievementMessage = {
            {"type", "achievement_earned"},
            {"achievement_id", achievementId},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };

        SendToPlayer(playerId, achievementMessage);

        player->SaveToDatabase();

        Logger::Info("Player {} earned achievement {}", playerId, achievementId);
    }
}
