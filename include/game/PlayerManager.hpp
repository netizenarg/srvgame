#pragma once

#include <memory>
#include <unordered_map>
#include <shared_mutex>
#include <nlohmann/json.hpp>

#include "../../include/database/CitusClient.hpp"
#include "../../include/game/RAIIThread.hpp"

class Player {
public:
    Player(int64_t id, const std::string& username);

    int64_t GetId() const { return id_; }
    const std::string& GetUsername() const { return username_; }

    void UpdatePosition(float x, float y, float z);
    nlohmann::json GetPosition() const;

    void AddItem(const std::string& itemId, int count = 1);
    void RemoveItem(const std::string& itemId, int count = 1);
    nlohmann::json GetInventory() const;

    void SetAttribute(const std::string& key, const nlohmann::json& value);
    nlohmann::json GetAttributes() const;

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
