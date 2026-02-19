#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <unordered_set>
#include <shared_mutex>
#include <mutex>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <set>
#include <unordered_map>

#include "logging/Logger.hpp"
#include "network/GameSession.hpp"
#include "nlohmann/json.hpp"

class ConnectionManager {
public:
    // Delete copy constructor and assignment operator
    ConnectionManager(const ConnectionManager&) = delete;
    ConnectionManager& operator=(const ConnectionManager&) = delete;

    // Get singleton instance
    static ConnectionManager& GetInstance();

    // Get shared_ptr to singleton (useful for passing to other components)
    static std::shared_ptr<ConnectionManager> GetInstancePtr();

    void Start(std::shared_ptr<GameSession> session);
    void Stop(std::shared_ptr<GameSession> session);
    void StopAll();

    size_t GetConnectionCount() const;
    std::shared_ptr<GameSession> GetSession(uint64_t sessionId) const;
    std::vector<std::shared_ptr<GameSession>> GetAllSessions() const;

    // Broadcast methods
    void Broadcast(const nlohmann::json& message);
    void BroadcastToGroup(const std::string& groupId, const nlohmann::json& message);
    
    // New broadcast methods
    void BroadcastWithFilter(const nlohmann::json& message,
                           std::function<bool(std::shared_ptr<GameSession>)> filter);
    void BroadcastExcept(uint64_t excludeSessionId, const nlohmann::json& message);
    void BroadcastToAuthenticated(const nlohmann::json& message);
    void BroadcastToUnauthenticated(const nlohmann::json& message);

    // Session groups
    void AddToGroup(const std::string& groupId, uint64_t sessionId);
    void RemoveFromGroup(const std::string& groupId, uint64_t sessionId);
    void RemoveFromAllGroups(uint64_t sessionId);

    // Session query methods
    std::vector<std::shared_ptr<GameSession>> GetSessionsByPlayerId(int64_t playerId) const;
    std::vector<uint64_t> GetSessionIdsInGroup(const std::string& groupId) const;
    std::vector<std::shared_ptr<GameSession>> GetSessionsInGroup(const std::string& groupId) const;
    std::set<std::string> GetGroupsForSession(uint64_t sessionId) const;
    bool IsSessionInGroup(uint64_t sessionId, const std::string& groupId) const;
    
    // Session search
    std::vector<uint64_t> FindSessionsByProperty(const std::string& key,
                                                const std::string& value) const;
    std::vector<uint64_t> FindSessionsByData(const std::string& key,
                                           const nlohmann::json& value) const;

    // Statistics
    struct SessionStatsInfo {
        std::chrono::system_clock::time_point start_time;
        std::chrono::system_clock::time_point last_activity;
        uint64_t messages_sent = 0;
        uint64_t messages_received = 0;
        uint64_t bytes_sent = 0;
        uint64_t bytes_received = 0;
    };

    struct GlobalStats {
        uint64_t total_sessions_created = 0;
        uint64_t total_connections = 0;
        uint64_t total_connection_time_seconds = 0;
        double average_connection_duration_seconds = 0.0;
        
        uint64_t total_bytes_received = 0;
        uint64_t total_bytes_sent = 0;
        uint64_t total_messages_received = 0;
        uint64_t total_messages_sent = 0;
        
        double bytes_received_per_second = 0.0;
        double bytes_sent_per_second = 0.0;
        double messages_received_per_second = 0.0;
        double messages_sent_per_second = 0.0;
        
        size_t total_groups = 0;
        size_t largest_group_size = 0;
        std::string largest_group_id;
    };

    GlobalStats GetGlobalStats() const;
    SessionStatsInfo GetSessionStats(uint64_t sessionId) const;
    void PrintGlobalStats() const;
    
    // Connection maintenance
    void CleanupInactiveSessions(int timeoutSeconds = 300);
    void DisconnectAllInGroup(const std::string& groupId);
    
    // Load balancing
    std::vector<std::shared_ptr<GameSession>> GetSessionsByWorkerId(int workerId) const;
    void RedistributeSessions(const std::vector<int>& workerIds);
    
    // Event system
    using EventHandler = std::function<void(const std::string&, const nlohmann::json&)>;
    void RegisterEventHandler(const std::string& eventType, EventHandler handler);
    void UnregisterEventHandler(const std::string& eventType, EventHandler handler);
    
    // Rate limiting
    void EnforceGlobalRateLimit(int maxMessagesPerSecond);
    
    // Session migration
    bool MigrateSession(uint64_t sessionId, std::shared_ptr<GameSession> newSession);
    
    // Monitoring
    void MonitorConnections();
    
    // Utility methods
    void DisconnectAll();
    void GracefulShutdown(int timeoutSeconds = 30);
    std::string GetStatusReport() const;

private:
    ConnectionManager();
    ~ConnectionManager();

    // Singleton instance
    static std::mutex instanceMutex_;
    static ConnectionManager* instance_;

    // Session storage
    mutable std::shared_mutex sessionsMutex_;
    std::unordered_map<uint64_t, std::shared_ptr<GameSession>> sessions_;

    // Group management
    mutable std::shared_mutex groupsMutex_;
    std::unordered_map<std::string, std::unordered_set<uint64_t>> groups_;
    std::unordered_map<uint64_t, std::set<std::string>> sessionGroups_;

    // Statistics
    mutable std::mutex statsMutex_;
    std::unordered_map<uint64_t, SessionStatsInfo> sessionStats_;
    std::atomic<uint64_t> totalConnections_{0};
    std::atomic<uint64_t> totalSessionsCreated_{0};
    std::atomic<uint64_t> totalConnectionTime_{0}; // in seconds

    // Event system
    mutable std::mutex eventHandlersMutex_;
    std::unordered_map<std::string, std::vector<EventHandler>> eventHandlers_;

    // Maintenance
    std::chrono::system_clock::time_point lastCleanup_;
    std::chrono::system_clock::time_point lastMonitor_;

    // Internal methods
    void AddToGroupInternal(const std::string& groupId, uint64_t sessionId);
    void RemoveFromGroupInternal(const std::string& groupId, uint64_t sessionId);
    void RemoveFromAllGroupsInternal(uint64_t sessionId);
    std::set<std::string> GetDefaultGroups() const;
    
    void UpdateSessionStats(uint64_t sessionId,
                          size_t bytesReceived = 0,
                          size_t bytesSent = 0);
    void EmitEvent(const std::string& eventType, const nlohmann::json& data);
};
