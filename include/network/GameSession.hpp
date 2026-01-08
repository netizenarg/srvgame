#pragma once

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <memory>
#include <string>
#include <functional>
#include <nlohmann/json.hpp>
#include <atomic>
#include <mutex>
#include <queue>
#include <set>
#include <map>
#include <deque>
#include <chrono>
#include <vector>

#include "BinaryProtocol.hpp"
#include "NetworkQualityMonitor.hpp"
#include "PredictionSystem.hpp"

// Supporting struct definitions (unchanged)
struct SessionStats { /* ... */ };
struct SessionMetrics { /* ... */ };
struct RateLimitConfig { /* ... */ };

class GameSession : public std::enable_shared_from_this<GameSession> {
public:
    using Pointer = std::shared_ptr<GameSession>;

    // Constructor with SSL context option
    explicit GameSession(asio::ip::tcp::socket socket, 
                         std::shared_ptr<asio::ssl::context> ssl_context = nullptr);
    ~GameSession();

    // Core session management
    void Start();
    void Stop();
    void Disconnect();

    bool IsConnected() const;
    uint64_t GetSessionId() const { return sessionId_; }
    asio::ip::tcp::endpoint GetRemoteEndpoint() const;
    bool IsEncrypted() const { return ssl_stream_ != nullptr; }

    // Binary protocol methods
    void SendBinary(uint16_t message_type, const std::vector<uint8_t>& data);
    void SendBinary(uint16_t message_type, const void* data, size_t length);
    void SendBinaryWithAck(uint16_t message_type, const std::vector<uint8_t>& data);
    
    // JSON compatibility (for backward compatibility)
    void Send(const nlohmann::json& message);
    void SendRaw(const std::string& data);
    void SendError(const std::string& message, int code);
    void SendSuccess(const std::string& message, const nlohmann::json& data = {});
    
    // Protocol negotiation
    void StartProtocolNegotiation();
    
    // Network quality adaptation
    void AdaptToNetworkConditions();
    NetworkQualityMonitor& GetNetworkMonitor() { return network_monitor_; }
    
    // Prediction system
    PredictionSystem& GetPredictionSystem() { return prediction_system_; }
    
    // Message handling with binary support
    using BinaryMessageHandler = std::function<void(uint16_t, const std::vector<uint8_t>&)>;
    void SetBinaryMessageHandler(uint16_t message_type, BinaryMessageHandler handler);
    void SetDefaultBinaryMessageHandler(BinaryMessageHandler handler);
    
    // Callback setters
    void SetMessageHandler(std::function<void(const nlohmann::json&)> handler);
    void SetCloseHandler(std::function<void()> handler);
    
    // Authentication and security
    void Authenticate(const std::string& authToken);
    void Deauthenticate();
    bool IsAuthenticated() const;
    std::string GetAuthToken() const;
    void SetPlayerId(int64_t playerId);
    int64_t GetPlayerId() const;
    
    // Session data storage (unchanged)
    void SetData(const std::string& key, const nlohmann::json& value);
    nlohmann::json GetData(const std::string& key, const nlohmann::json& defaultValue = {}) const;
    bool HasData(const std::string& key) const;
    void RemoveData(const std::string& key);
    void ClearData();
    nlohmann::json GetAllData() const;
    
    // Session properties (unchanged)
    void SetProperty(const std::string& key, const std::string& value);
    std::string GetProperty(const std::string& key, const std::string& defaultValue = "") const;
    std::map<std::string, std::string> GetAllProperties() const;
    
    // Session groups (unchanged)
    void JoinGroup(const std::string& groupId);
    void LeaveGroup(const std::string& groupId);
    void LeaveAllGroups();
    std::set<std::string> GetJoinedGroups() const;
    bool IsInGroup(const std::string& groupId) const;
    
    // Statistics and metrics
    SessionStats GetStats() const;
    void ResetStats();
    void RecordMessageReceived(size_t size);
    void RecordMessageSent(size_t size);
    
    SessionMetrics GetMetrics() const;
    void PrintMetrics() const;
    
    // Compression
    void SetCompressionEnabled(bool enabled);
    bool IsCompressionEnabled() const;
    
    // Rate limiting
    void SetRateLimit(int messagesPerSecond, int burstSize);
    void SetRateLimitEnabled(bool enabled);
    bool CheckRateLimit();
    
    // Connection quality monitoring
    void RecordLatency(uint64_t latencyMs);
    uint64_t GetAverageLatency() const;
    uint64_t GetMinLatency() const;
    uint64_t GetMaxLatency() const;
    std::vector<uint64_t> GetLatencySamples() const;
    
    // Custom event handlers
    void SetCustomEventHandler(const std::string& eventName,
                               std::function<void(const nlohmann::json&)> handler);
    void RemoveCustomEventHandler(const std::string& eventName);
    void HandleCustomEvent(const std::string& eventName, const nlohmann::json& data);
    
    // Message queue management
    size_t GetPendingMessageCount() const;
    void ClearPendingMessages();
    bool IsWriteQueueFull() const;
    void SetMaxWriteQueueSize(size_t maxSize);
    
    // Heartbeat management
    void UpdateHeartbeat();
    
    // Utility methods
    std::string ToString() const;
    uint64_t GetUptimeSeconds() const;
    
    // Graceful shutdown
    void BeginGracefulShutdown();
    void CancelGracefulShutdown();
    
    // World and entity methods (binary versions)
    void SendWorldChunkBinary(int chunkX, int chunkZ, const std::vector<uint8_t>& chunkData);
    void SendEntityUpdateBinary(uint64_t entityId, const std::vector<uint8_t>& entityData);
    void SendEntitySpawnBinary(uint64_t entityId, const std::vector<uint8_t>& spawnData);
    void SendEntityDespawnBinary(uint64_t entityId);
    
    // Player state synchronization (with prediction)
    void SyncPlayerStateBinary(const glm::vec3& position, const glm::vec3& rotation, 
                               const glm::vec3& velocity, uint32_t last_input_id);
    void SendPositionCorrection(const glm::vec3& position, const glm::vec3& velocity);
    
private:
    // Core networking
    asio::ip::tcp::socket socket_;
    std::shared_ptr<asio::ssl::context> ssl_context_;
    std::unique_ptr<asio::ssl::stream<asio::ip::tcp::socket>> ssl_stream_;
    
    asio::streambuf read_buffer_;
    std::queue<std::vector<uint8_t>> write_queue_;
    std::mutex write_mutex_;
    
    // Binary protocol state
    std::unordered_map<uint16_t, BinaryMessageHandler> binary_handlers_;
    BinaryMessageHandler default_binary_handler_;
    std::mutex binary_handlers_mutex_;
    
    std::atomic<uint32_t> next_sequence_{1};
    std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> pending_acks_;
    std::mutex ack_mutex_;
    
    // Session identification
    uint64_t sessionId_;
    static std::atomic<uint64_t> nextSessionId_;
    
    // Callbacks
    std::function<void(const nlohmann::json&)> message_handler_;
    std::function<void()> close_handler_;
    
    // State management
    std::atomic<bool> connected_{false};
    std::atomic<bool> closing_{false};
    std::atomic<bool> graceful_shutdown_{false};
    std::atomic<bool> protocol_negotiated_{false};
    
    // Heartbeat
    asio::steady_timer heartbeat_timer_;
    asio::steady_timer shutdown_timer_;
    asio::steady_timer network_adaptation_timer_;
    std::chrono::steady_clock::time_point last_heartbeat_;
    std::chrono::steady_clock::time_point connected_time_;
    
    // Network quality monitoring
    NetworkQualityMonitor network_monitor_;
    
    // Prediction system
    PredictionSystem prediction_system_;
    
    // Statistics
    mutable std::mutex stats_mutex_;
    SessionStats stats_;
    
    // Compression
    std::atomic<bool> compression_enabled_{false};
    
    // Rate limiting
    mutable std::mutex rate_limit_mutex_;
    RateLimitConfig rate_limit_;
    std::atomic<bool> rate_limit_enabled_{false};
    
    // Groups
    mutable std::mutex groups_mutex_;
    std::set<std::string> joined_groups_;
    
    // Authentication
    mutable std::mutex auth_mutex_;
    std::string auth_token_;
    std::atomic<bool> authenticated_{false};
    std::atomic<int64_t> player_id_{0};
    std::chrono::steady_clock::time_point authentication_time_;
    
    // Session data
    mutable std::mutex data_mutex_;
    std::map<std::string, nlohmann::json> session_data_;
    
    // Properties
    mutable std::mutex properties_mutex_;
    std::map<std::string, std::string> properties_;
    
    // Custom event handlers
    mutable std::mutex event_handlers_mutex_;
    std::map<std::string, std::function<void(const nlohmann::json&)>> custom_event_handlers_;
    
    // Queue management
    size_t max_write_queue_size_{1000};
    
    // Private methods
    void StartHeartbeat();
    void CheckHeartbeat();
    void DoRead();
    void DoWrite();
    void HandleMessage(const std::string& message);
    
    // Binary protocol methods
    void DoBinaryRead();
    void HandleBinaryMessage(const BinaryProtocol::BinaryMessage& message);
    void DoBinaryWrite();
    void ProcessAcknowledgment(uint32_t sequence);
    void SendAcknowledgment(uint32_t sequence);
    
    // SSL/TLS methods
    void StartTLSHandshake();
    void HandleTLSHandshake(std::error_code ec);
    
    // Network adaptation
    void StartNetworkAdaptation();
    void CheckNetworkConditions();
    
    // Protocol negotiation
    void SendProtocolCapabilities();
    void HandleProtocolNegotiation(const std::vector<uint8_t>& data);
    
    // Helper methods
    asio::ip::tcp::socket& GetSocket();
    void HandleNetworkError(std::error_code ec);
};