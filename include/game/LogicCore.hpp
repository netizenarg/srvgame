#pragma once

#include <nlohmann/json.hpp>
#include <functional>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <queue>
#include <chrono>

#include "../../include/game/PlayerManager.hpp"
#include "../../include/scripting/PythonScripting.hpp"
#include "../../include/network/ConnectionManager.hpp"
#include "../../include/network/BinaryProtocol.hpp"
#include "../../include/game/RAIIThread.hpp"

class LogicCore {
public:
    using MessageHandler = std::function<void(uint64_t sessionId, const nlohmann::json&)>;
    using BinaryMessageHandler = std::function<void(uint64_t sessionId, uint16_t messageType, const std::vector<uint8_t>&)>;
    using EventCallback = std::function<void()>;

    // Rate limiting structure
    struct RateLimitInfo {
        std::chrono::steady_clock::time_point lastMessageTime;
        int messageCount = 0;
        std::deque<std::chrono::steady_clock::time_point> messageTimes;
    };

    static LogicCore& GetInstance();

    // Core lifecycle
    void Initialize();
    void Shutdown();
    bool IsRunning() const { return running_; }

    // Message handling
    void HandleMessage(uint64_t sessionId, const nlohmann::json& msg);
    void HandleBinaryMessage(uint64_t sessionId, uint16_t messageType, const std::vector<uint8_t>& data);
    void RegisterHandler(const std::string& messageType, MessageHandler handler);
    void RegisterBinaryHandler(uint16_t messageType, BinaryMessageHandler handler);
    void RegisterDefaultHandlers();

    // Session management
    void OnPlayerConnected(uint64_t sessionId, uint64_t playerId);
    void OnPlayerDisconnected(uint64_t sessionId);
    uint64_t GetPlayerIdBySession(uint64_t sessionId) const;
    uint64_t GetSessionIdByPlayer(uint64_t playerId) const;

    // Response methods
    void SendError(uint64_t sessionId, const std::string& message, int code = 0);
    void SendSuccess(uint64_t sessionId, const std::string& message, const nlohmann::json& data = {});
    void SendToSession(uint64_t sessionId, const nlohmann::json& message);
    void SendBinaryToSession(uint64_t sessionId, uint16_t messageType, const std::vector<uint8_t>& data);

    // Python scripting
    void FirePythonEvent(const std::string& eventName, const nlohmann::json& data);
    nlohmann::json CallPythonFunction(const std::string& moduleName, const std::string& functionName, 
                                      const nlohmann::json& args);
    void RegisterPythonEventHandlers();

    // Event queue
    void QueueEvent(EventCallback event);
    void ProcessEvents();

    // Utility
    int64_t GetCurrentTimestamp();
    bool CheckRateLimit(uint64_t sessionId);

protected:
    LogicCore();
    virtual ~LogicCore();

    // Threading
    RAIIThread gameLoopThread_;
    RAIIThread spawnerThread_;
    RAIIThread saveThread_;
    
    std::atomic<bool> running_{false};
    std::chrono::milliseconds gameLoopInterval_{16};
    
    std::condition_variable gameLoopCV_;
    std::condition_variable spawnerCV_;
    std::condition_variable saveCV_;
    std::mutex gameLoopMutex_;
    std::mutex spawnerMutex_;
    std::mutex saveMutex_;

    // Message handling
    std::unordered_map<std::string, MessageHandler> messageHandlers_;
    std::unordered_map<uint16_t, BinaryMessageHandler> binaryHandlers_;
    std::mutex handlersMutex_;

    // Rate limiting
    std::unordered_map<uint64_t, RateLimitInfo> rateLimits_;
    std::mutex rateLimitMutex_;
    const int MAX_MESSAGES_PER_SECOND = 100;

    // Session management
    std::unordered_map<uint64_t, uint64_t> sessionToPlayerMap_;
    std::unordered_map<uint64_t, uint64_t> playerToSessionMap_;
    std::mutex sessionMutex_;

    // Event queue
    std::queue<EventCallback> eventQueue_;
    std::mutex eventQueueMutex_;

    // References to other systems
    PlayerManager& playerManager_;
    CitusClient& dbClient_;
    PythonScripting::PythonScripting& pythonScripting_;
    std::unique_ptr<PythonScripting::ScriptHotReloader> scriptHotReloader_;
    bool pythonEnabled_{false};

    // Random generator
    std::mt19937 rng_;

    // Thread functions
    virtual void GameLoop();
    virtual void SpawnerLoop();
    virtual void SaveLoop();

private:
    static std::mutex instanceMutex_;
    static LogicCore* instance_;
};