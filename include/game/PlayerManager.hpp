#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include "logging/Logger.hpp"
#include "network/ConnectionManager.hpp"
#include "network/BinarySession.hpp"
#include "utils/Passwords.hpp"

#include "database/DbManager.hpp"
#include "game/RAIIThread.hpp"
#include "game/Player.hpp"

struct GlobalPlayerStats {
    int total_players = 0;
    int online_players = 0;
    int total_connections = 0;
    long long total_playtime = 0;
    double average_playtime = 0.0;
    int level_1_10 = 0;
    int level_11_20 = 0;
    int level_21_30 = 0;
    int level_31_40 = 0;
    int level_41_50 = 0;
    int level_50_plus = 0;
};

struct Party {
    int64_t id;
    std::string name;
    int64_t leader_id;
    std::set<int64_t> members;
    std::chrono::system_clock::time_point created_at;
};

class PlayerManager {
public:
    static PlayerManager& GetInstance();

    std::shared_ptr<Player> CreatePlayer(const std::string& username, const std::string& password="");
    std::shared_ptr<Player> GetPlayer(int64_t playerId);
    std::shared_ptr<Player> GetPlayerBySession(uint64_t sessionId);
    std::shared_ptr<Player> GetPlayerByUsername(const std::string& username);
    uint64_t GetSessionIdByPlayerId(int64_t playerId) const;

    bool AuthenticatePlayer(const std::string& username, const std::string& password="");
    void PlayerConnected(uint64_t sessionId, int64_t playerId);
    void PlayerDisconnected(uint64_t sessionId);

    void UpdatePosition(uint64_t playerId, float x, float y, float z);
    std::vector<uint64_t> GetDirtyPlayersAndClear();

    void BroadcastToNearbyPlayers(int64_t playerId, const nlohmann::json& message);
    std::vector<int64_t> GetNearbyPlayers(int64_t playerId, float radius);
    std::vector<std::shared_ptr<Player>> GetPlayersInRadius(const glm::vec3& center, float radius);

    void SaveAllPlayers();
    void CleanupInactivePlayers();

    void SendToPlayer(int64_t playerId, const nlohmann::json& message);
    void SendToPlayers(const std::vector<int64_t>& playerIds, const nlohmann::json& message);
    void BanPlayer(int64_t playerId, const std::string& reason, int64_t durationSeconds);
    void UnbanPlayer(int64_t playerId);
    void TeleportPlayer(int64_t playerId, float x, float y, float z);
    bool GiveItemToPlayer(int64_t playerId, const std::string& itemId, int count);
    bool TakeItemFromPlayer(int64_t playerId, const std::string& itemId, int count);
    void AddAchievementToPlayer(int64_t playerId, const std::string& achievementId);

    std::vector<std::shared_ptr<Player>> GetAllPlayers() const;
    std::vector<std::shared_ptr<Player>> GetOnlinePlayers() const;
    size_t GetPlayerCount() const;
    size_t GetOnlinePlayerCount() const;
    bool PlayerExists(const std::string& username) const;
    std::shared_ptr<Player> LoadPlayer(int64_t playerId);
    std::shared_ptr<Player> LoadPlayerByUsername(const std::string& username);
    PlayerStats GetPlayerStats(int64_t playerId) const;
    GlobalPlayerStats GetGlobalPlayerStats() const;
    void PrintStats();

    std::vector<int64_t> SearchPlayers(const std::string& query, int limit);
    std::vector<int64_t> GetPlayersByLevelRange(int minLevel, int maxLevel);

    void CreateParty(int64_t leaderId, const std::string& partyName);
    void AddPlayerToParty(int64_t partyId, int64_t playerId);
    void RemovePlayerFromParty(int64_t partyId, int64_t playerId);
    std::vector<int64_t> GetPartyMembers(int64_t partyId) const;
    int64_t GeneratePartyId();

    void Shutdown();

private:
    PlayerManager();
    ~PlayerManager();

    void SaveLoop();
    void CleanupLoop();
    void UpdateConnectionStats(int64_t playerId, bool connected);

    DbManager& dbManager_;

    mutable std::shared_mutex playersMutex_;
    std::unordered_map<int64_t, std::shared_ptr<Player>> players_;

    mutable std::shared_mutex sessionsMutex_;
    std::unordered_map<uint64_t, int64_t> sessionToPlayer_;
    std::unordered_map<int64_t, uint64_t> playerToSession_;

    mutable std::shared_mutex usernameMutex_;
    std::unordered_map<std::string, int64_t> usernameToId_;

    mutable std::mutex statsMutex_;
    std::unordered_map<int64_t, PlayerStats> playerStats_;

    mutable std::mutex partyMutex_;
    std::unordered_map<int64_t, Party> parties_;

    std::chrono::system_clock::time_point lastCleanup_;

    // Thread management with RAII
    RAIIThread saveThread_;
    RAIIThread cleanupThread_;

    std::atomic<bool> running_{true};
    std::chrono::seconds saveInterval_{10};
    std::chrono::seconds cleanupInterval_{10};

    std::condition_variable saveCV_;
    std::condition_variable cleanupCV_;
    std::mutex saveMutex_;
    std::mutex cleanupMutex_;

    static constexpr float DEFAULT_BROADCAST_RANGE = 100.0f;
    static constexpr size_t MAX_PARTY_SIZE = 5;

    mutable std::shared_mutex dirtyMutex_;
    std::unordered_set<uint64_t> dirtyPlayers_;

    void MarkDirty(uint64_t playerId);

};
