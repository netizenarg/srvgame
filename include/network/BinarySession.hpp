#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <queue>
#include <set>
#include <vector>

#include <openssl/err.h>
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <nlohmann/json.hpp>

#include "logging/Logger.hpp"
#include "config/ConfigManager.hpp"

#include "network/BinaryProtocol.hpp"
#include "network/NetworkQualityMonitor.hpp"
#include "network/PredictionSystem.hpp"
#include "network/IConnection.hpp"

#include "game/GameData.hpp"
#include "game/GameLogic.hpp"

struct SessionStats {
    // Message statistics
    uint64_t messages_received{0};
    uint64_t messages_sent{0};
    uint64_t bytes_received{0};
    uint64_t bytes_sent{0};

    // Error statistics
    uint64_t connection_errors{0};
    uint64_t authentication_failures{0};
    uint64_t rate_limit_exceeded{0};
    uint64_t protocol_errors{0};
    uint64_t checksum_errors{0};
    uint64_t compression_errors{0};

    // Time tracking
    std::chrono::steady_clock::time_point last_message_received;
    std::chrono::steady_clock::time_point last_message_sent;
    std::chrono::steady_clock::time_point connection_start_time;

    // Network statistics
    uint64_t retransmissions{0};
    uint64_t acknowledgments_received{0};
    uint64_t acknowledgments_sent{0};
    uint64_t packets_lost{0};

    // Performance statistics
    uint64_t max_write_queue_size_reached{0};
    uint64_t binary_messages_processed{0};
    uint64_t json_messages_processed{0};

    // Session lifecycle statistics
    uint64_t reconnections{0};
    uint64_t graceful_shutdowns{0};
    uint64_t force_disconnects{0};
};

struct SessionMetrics {
    // Basic metrics
    uint64_t session_id{0};
    std::string remote_endpoint;
    uint64_t connected_time_seconds{0};
    bool is_connected{false};
    bool is_authenticated{false};
    int64_t player_id{0};

    // Throughput metrics
    uint64_t messages_received{0};
    uint64_t messages_sent{0};
    uint64_t bytes_received{0};
    uint64_t bytes_sent{0};
    double receive_rate{0.0};        // messages per second
    double send_rate{0.0};           // messages per second
    double data_receive_rate{0.0};   // bytes per second
    double data_send_rate{0.0};      // bytes per second

    // Network quality metrics
    uint64_t average_latency{0};
    uint64_t min_latency{0};
    uint64_t max_latency{0};
    double packet_loss{0.0};         // percentage
    double jitter{0.0};              // milliseconds

    // Resource usage metrics
    size_t pending_message_count{0};
    size_t memory_usage_bytes{0};
    double cpu_usage_percent{0.0};

    // Quality metrics
    double compression_ratio{0.0};   // (1 - compressed/original)
    uint64_t joined_groups{0};
    uint64_t custom_events_processed{0};

    // Error metrics
    uint64_t rate_limit_exceeded{0};
    uint64_t authentication_attempts{0};
    uint64_t protocol_negotiation_time_ms{0};

    // Configuration metrics
    int max_write_queue_size{0};
    int heartbeat_interval_seconds{0};
    int session_timeout_seconds{0};
    bool compression_enabled{false};
    bool encryption_enabled{false};
};

struct RateLimitConfig {
    // Token bucket parameters
    int messages_per_second{100};      // Rate limit: messages per second
    int burst_size{200};               // Maximum burst size
    int tokens{0};                     // Current tokens available
    int refill_rate_ms{1000};          // Token refill interval in milliseconds

    // Tracking
    std::chrono::steady_clock::time_point last_refill;
    std::chrono::steady_clock::time_point last_violation;

    // Enforcement parameters
    bool enabled{true};                // Whether rate limiting is enabled
    int violation_threshold{10};       // Number of violations before action
    int violation_window_seconds{60};  // Time window for violation counting
    int cool_down_period_seconds{300}; // Cool down period after limit
    bool graceful_enforcement{true};   // Whether to use graceful enforcement

    // Statistics
    uint64_t total_requests{0};
    uint64_t allowed_requests{0};
    uint64_t denied_requests{0};
    uint64_t consecutive_violations{0};

    // Enforcement levels
    std::vector<std::string> enforcement_levels{
        "warn", "slowdown", "disconnect"
    };
    std::string current_level{"warn"};

    // Client-specific adjustments
    double quality_factor{1.0};        // Adjust rate based on network quality
    bool adaptive_rate_limit{true};    // Whether to adapt based on conditions
    int min_rate_limit{10};            // Minimum messages per second
    int max_rate_limit{1000};          // Maximum messages per second

    // Exemption parameters
    std::set<uint16_t> exempt_message_types{}; // Message types exempt from rate limiting
    bool exempt_authenticated_users{false};    // Whether authenticated users are exempt
};

class BinarySession : public IConnection, public std::enable_shared_from_this<BinarySession> {
public:
    using Pointer = std::shared_ptr<BinarySession>;

    // Constructor with SSL context option
    explicit BinarySession(asio::ip::tcp::socket socket,
                         std::shared_ptr<asio::ssl::context> ssl_context = nullptr);
    ~BinarySession();

    ProtocolMode GetProtocolMode() const override { return ProtocolMode::Binary; }

    // Core session management
    void Start() override;
    void Stop() override;
    void Disconnect() override;
    bool IsConnected() const override;
    uint64_t GetSessionId() const override { return sessionId_; }


    asio::ip::tcp::endpoint GetRemoteEndpoint() const;
    bool IsEncrypted() const { return ssl_stream_ != nullptr; }
    std::string GetRemoteAddress() const override {
        try {
            return GetRemoteEndpoint().address().to_string();
        } catch (const std::exception& e) {
            Logger::Error("GetRemoteAddress: {}", e.what());
            return "unknown";
        }
    }
    // Binary protocol methods
    void SendPing();
    void SendPong();
    void SendBinary(uint16_t message_type, const std::vector<uint8_t>& data) override;
    void SendBinary(uint16_t message_type, const void* data, size_t length);
    void SendBinaryWithAck(uint16_t message_type, const std::vector<uint8_t>& data);
    void SendBinaryError(uint16_t message_type, const std::string& error_message, int code);

    // JSON compatibility (for backward compatibility)
    void Send(const nlohmann::json& message) override;
    void SendRaw(const std::string& data);
    void SendError(const std::string& message, int code);
    void SendSuccess(const std::string& message, const nlohmann::json& data = {});
    void SendWorldChunk(int chunkX, int chunkZ, const nlohmann::json& chunkData);
    void SendEntityUpdate(uint64_t entityId, const nlohmann::json& entityData);
    void SendEntitySpawn(uint64_t entityId, const nlohmann::json& spawnData);
    void SendEntityDespawn(uint64_t entityId);
    void SendCollisionEvent(uint64_t entityId1, uint64_t entityId2, const glm::vec3& point);
    void SyncPlayerState(const glm::vec3& position, const glm::vec3& rotation, const glm::vec3& velocity);
    void SendNearbyEntities(const std::vector<nlohmann::json>& entities);
    void SendNPCInteraction(uint64_t npcId, const std::string& interactionType, const nlohmann::json& data);
    void SendCompressedWorldData(const std::vector<uint8_t>& compressedData);
    void HandleWorldRequest(const nlohmann::json& data);
    void HandleEntityInteraction(const nlohmann::json& data);
    void HandleMovementUpdate(const nlohmann::json& data);
    void HandleFamiliarCommand(const nlohmann::json& data);

    // Protocol negotiation
    void StartProtocolNegotiation();

    // Network quality adaptation
    void AdaptToNetworkConditions();
    NetworkQualityMonitor& GetNetworkMonitor() { return network_monitor_; }

    // Prediction system
    PredictionSystem& GetPredictionSystem();

    // Message handling with binary support
    using BinaryMessageHandler = std::function<void(uint16_t, const std::vector<uint8_t>&)>;
    void SetBinaryMessageHandler(uint16_t message_type, BinaryMessageHandler handler);
    void SetDefaultBinaryMessageHandler(BinaryMessageHandler handler);

    // Callback setters
    //void SetMessageHandler(std::function<void(const nlohmann::json&)> handler);
    //void SetCloseHandler(std::function<void()> handler);
    void SetMessageHandler(MessageHandler handler) override;
    void SetCloseHandler(CloseHandler handler) override;

    // Authentication and security
    void Authenticate(const std::string& authToken) override;
    void Deauthenticate();
    bool IsAuthenticated() const override;
    std::string GetAuthToken() const;
    void SetPlayerId(int64_t playerId) override;
    int64_t GetPlayerId() const override;

    // Session data storage (unchanged)
    void SetData(const std::string& key, const nlohmann::json& value) override;
    nlohmann::json GetData(const std::string& key, const nlohmann::json& defaultValue = {}) const override;
    bool HasData(const std::string& key) const override;
    void RemoveData(const std::string& key) override;
    void ClearData() override;
    nlohmann::json GetAllData() const override;

    // Session properties (unchanged)
    void SetProperty(const std::string& key, const std::string& value);
    std::string GetProperty(const std::string& key, const std::string& defaultValue = "") const;
    std::map<std::string, std::string> GetAllProperties() const;

    // Session groups (unchanged)
    void JoinGroup(const std::string& groupId) override;
    void LeaveGroup(const std::string& groupId) override;
    void LeaveAllGroups() override;
    std::set<std::string> GetJoinedGroups() const override;
    bool IsInGroup(const std::string& groupId) const override;

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

    void SetPlayerStateHandler(std::function<void(const ClientInput&)> handler);

private:
    // Core networking
    asio::ip::tcp::socket socket_;
    std::shared_ptr<asio::ssl::context> ssl_context_;
    mutable std::unique_ptr<asio::ssl::stream<asio::ip::tcp::socket>> ssl_stream_;

    asio::streambuf read_buffer_;
    std::queue<std::vector<uint8_t>> write_queue_;
    mutable std::mutex write_mutex_;

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

    std::function<void(const ClientInput&)> player_state_handler_;

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
    const asio::ip::tcp::socket& GetSocket() const;
    void HandleNetworkError(std::error_code ec);
    void SetupDefaultHandlers();
};

