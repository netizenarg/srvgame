#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <unordered_map>
#include <vector>
#include <thread>

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

#include "database/DbManager.hpp"
#include "game/RAIIThread.hpp"
#include "game/LogicWorld.hpp"
#include "game/LogicEntity.hpp"

struct RateLimitInfo {
    std::chrono::steady_clock::time_point lastMessageTime;
    int messageCount = 0;
    std::deque<std::chrono::steady_clock::time_point> messageTimes;
};

class LogicCore {
public:
    LogicCore();
    virtual ~LogicCore();

    using MessageHandler = std::function<void(uint64_t sessionId, const nlohmann::json&)>;
    using BinaryMessageHandler = std::function<void(uint64_t sessionId, uint16_t messageType, const std::vector<uint8_t>&)>;
    using EventCallback = std::function<void()>;

    static LogicCore& GetInstance();

    virtual void Initialize();
    virtual void Shutdown();
    bool IsRunning() const;

    void HandleMessage(uint64_t sessionId, const nlohmann::json& msg);
    void HandleBinaryMessage(uint64_t sessionId, uint16_t messageType, const std::vector<uint8_t>& data);
    void RegisterHandler(const std::string& messageType, MessageHandler handler);
    void RegisterDefaultHandlers();

    virtual void OnPlayerConnected(uint64_t sessionId, uint64_t playerId);
    virtual void OnPlayerDisconnected(uint64_t sessionId);
    uint64_t GetPlayerIdBySession(uint64_t sessionId) const;
    uint64_t GetSessionIdByPlayer(uint64_t playerId) const;

    virtual void SendError(uint64_t sessionId, const std::string& description, int code = 0);
    virtual void SendSuccess(uint64_t sessionId, const std::string& description, const std::vector<uint8_t>& data = {});
    virtual void SendToSession(uint64_t sessionId, uint16_t messageType, const std::vector<uint8_t>& data);

    virtual void FirePythonEvent(const std::string& eventName, const nlohmann::json& data);
    virtual nlohmann::json CallPythonFunction(const std::string& moduleName, const std::string& functionName, const nlohmann::json& args);
    virtual void RegisterPythonEventHandlers();

    void QueueEvent(EventCallback event);
    void ProcessEvents();

    int64_t GetCurrentTimestamp();
    bool CheckRateLimit(uint64_t sessionId);
    float CalculateDistance(const glm::vec3& a, const glm::vec3& b);

protected:
    RAIIThread gameLoopThread_;
    RAIIThread spawnerThread_;
    RAIIThread saveThread_;
    std::atomic<bool> running_;
    std::chrono::milliseconds gameLoopInterval_{16};
    std::condition_variable gameLoopCV_;
    std::condition_variable spawnerCV_;
    std::condition_variable saveCV_;
    std::condition_variable eventQueueCV_;
    std::mutex gameLoopMutex_;
    std::mutex spawnerMutex_;
    std::mutex saveMutex_;
    std::unordered_map<std::string, MessageHandler> messageHandlers_;
    std::unordered_map<uint16_t, BinaryMessageHandler> binaryHandlers_;
    std::mutex handlersMutex_;
    std::unordered_map<uint64_t, RateLimitInfo> rateLimits_;
    std::mutex rateLimitMutex_;
    const int MAX_MESSAGES_PER_SECOND = 100;
    std::unordered_map<uint64_t, uint64_t> sessionToPlayerMap_;
    std::unordered_map<uint64_t, uint64_t> playerToSessionMap_;
    mutable std::mutex sessionMutex_;
    std::queue<EventCallback> eventQueue_;
    std::mutex eventQueueMutex_;
    DbManager& dbManager_;
    bool pythonEnabled_ = false;
    std::mt19937 rng_;
    virtual void GameLoop();
    virtual void SpawnerLoop();
    virtual void SaveLoop();
    virtual void ProcessGameTick(float deltaTime);
    virtual void SpawnEnemies();
    virtual void RespawnNPCs();
    virtual void SpawnResources();
    virtual void SaveGameState();
    virtual void CleanupOldData();
    virtual void HandleLogin(uint64_t sessionId, const nlohmann::json& data);
    virtual void HandleChat(uint64_t sessionId, const nlohmann::json& data);
    virtual void HandleCombat(uint64_t sessionId, const nlohmann::json& data);
    virtual void HandleQuest(uint64_t sessionId, const nlohmann::json& data);

private:
    static std::mutex instanceMutex_;
    static LogicCore* instance_;
};
