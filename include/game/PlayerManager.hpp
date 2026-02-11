#pragma once

#include <algorithm>
#include <cmath>
#include <chrono>
#include <memory>
#include <unordered_map>
#include <shared_mutex>

#include <nlohmann/json.hpp>

#include "logging/Logger.hpp"
#include "database/DbManager.hpp"
#include "game/RAIIThread.hpp"
#include "game/PlayerEntity.hpp"

class Player : public PlayerEntity {
public:
    Player(int64_t id, const std::string& username);

    nlohmann::json ToJson() const;
    void AddExperience(int64_t amount);

    int64_t GetId() const { return id_; }
    const std::string& GetUsername() const { return username_; }

    void UpdatePosition(float x, float y, float z);
    nlohmann::json GetPosition() const;

    void AddItem(const std::string& itemId, int count = 1);
    void RemoveItem(const std::string& itemId, int count = 1);
    nlohmann::json GetInventory() const;

    void SetAttribute(const std::string& key, const nlohmann::json& value);
    nlohmann::json GetAttributes() const;

    void SetHealth(int health);
    void SetMana(int mana);

    void SaveToDatabase();
    bool LoadFromDatabase();

private:
    int64_t id_;
    std::string username_;

    struct Position {
        float x, y, z;
    } position_;

    std::unordered_map<std::string, int> inventory_;
    nlohmann::json attributes_;

    mutable std::shared_mutex mutex_;
};

struct GlobalPlayerStats {
    int total_players = 0;
    int online_players = 0;
    int total_connections = 0;
    int total_playtime = 0;
    int average_playtime = 0;
    int level_1_10 = 0;
    int level_11_20 = 0;
    int level_21_30 = 0;
    int level_31_40 = 0;
    int level_41_50 = 0;
    int level_50_plus = 0;
};

class PlayerManager {
public:
    static PlayerManager& GetInstance();

    std::shared_ptr<Player> CreatePlayer(const std::string& username);
    std::shared_ptr<Player> GetPlayer(int64_t playerId);
    std::shared_ptr<Player> GetPlayerBySession(uint64_t sessionId);

    bool AuthenticatePlayer(const std::string& username, const std::string& password);
    void PlayerConnected(uint64_t sessionId, int64_t playerId);
    void PlayerDisconnected(uint64_t sessionId);

    // Session management
    void BroadcastToNearbyPlayers(int64_t playerId, const nlohmann::json& message);
    std::vector<int64_t> GetNearbyPlayers(int64_t playerId, float radius);

    // Periodic tasks
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

private:
    PlayerManager();

    mutable std::shared_mutex playersMutex_;
    std::unordered_map<int64_t, std::shared_ptr<Player>> players_;

    mutable std::shared_mutex sessionsMutex_;
    std::unordered_map<uint64_t, int64_t> sessionToPlayer_;
    std::unordered_map<int64_t, uint64_t> playerToSession_;

    // Thread management with RAII
    RAIIThread saveThread_;      // Added
    RAIIThread cleanupThread_;   // Added
    
    std::atomic<bool> running_{true};
    std::chrono::minutes saveInterval_{5};
    std::chrono::minutes cleanupInterval_{10};
    
    std::condition_variable saveCV_;
    std::condition_variable cleanupCV_;
    std::mutex saveMutex_;
    std::mutex cleanupMutex_;

    CitusClient& dbClient_;

    // Thread functions
    void SaveLoop();
    void CleanupLoop();
};
