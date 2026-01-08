#pragma once

#include <deque>
#include <chrono>
#include <mutex>
#include <atomic>
#include <vector>
#include <algorithm>

struct NetworkMetrics {
    uint64_t average_latency_ms{0};
    uint64_t min_latency_ms{0};
    uint64_t max_latency_ms{0};
    uint64_t jitter_ms{0};
    float packet_loss_percent{0.0f};
    uint64_t bandwidth_bps{0};
    float connection_stability{1.0f};
    uint64_t packets_sent{0};
    uint64_t packets_received{0};
    uint64_t bytes_sent{0};
    uint64_t bytes_received{0};
    
    // Quality scores (0.0 - 1.0)
    float latency_score{1.0f};
    float stability_score{1.0f};
    float overall_score{1.0f};
    
    // Timestamp of last update
    std::chrono::steady_clock::time_point last_update;
    
    // Get human-readable description
    std::string ToString() const;
    
    // Calculate quality scores
    void CalculateScores();
};

class NetworkQualityMonitor {
public:
    NetworkQualityMonitor(size_t max_samples = 1000);
    
    // Record network events
    void RecordPacketSent(uint64_t packet_id, size_t size);
    void RecordPacketReceived(uint64_t packet_id, size_t size);
    void RecordAcknowledgment(uint64_t packet_id, uint64_t rtt_ms);
    
    // Record latency sample (for ping/pong)
    void RecordLatencySample(uint64_t latency_ms);
    
    // Record bandwidth usage
    void RecordBytesSent(size_t bytes);
    void RecordBytesReceived(size_t bytes);
    
    // Get current metrics
    NetworkMetrics GetMetrics() const;
    
    // Adaptive strategies
    uint32_t CalculateOptimalUpdateRate() const;
    bool ShouldEnableCompression() const;
    bool ShouldEnableRedundancy() const;
    uint32_t CalculateOptimalMTU() const;
    
    // Connection quality assessment
    enum class ConnectionQuality {
        EXCELLENT,  // < 50ms latency, < 1% packet loss
        GOOD,       // < 100ms latency, < 3% packet loss
        FAIR,       // < 200ms latency, < 5% packet loss
        POOR,       // < 500ms latency, < 10% packet loss
        UNSTABLE    // > 500ms latency or > 10% packet loss
    };
    
    ConnectionQuality GetConnectionQuality() const;
    
    // Reset all statistics
    void Reset();
    
    // Periodic update (call every second)
    void Update();
    
private:
    mutable std::mutex metrics_mutex_;
    
    // Packet tracking
    struct PacketRecord {
        uint64_t packet_id;
        std::chrono::steady_clock::time_point sent_time;
        size_t size;
        bool acknowledged{false};
        uint64_t rtt_ms{0};
    };
    
    std::deque<PacketRecord> packet_history_;
    std::atomic<uint64_t> next_packet_id_{1};
    
    // Latency samples
    std::deque<uint64_t> latency_samples_;
    size_t max_samples_;
    
    // Bandwidth tracking
    struct BandwidthSample {
        std::chrono::steady_clock::time_point timestamp;
        uint64_t bytes_sent;
        uint64_t bytes_received;
    };
    
    std::deque<BandwidthSample> bandwidth_samples_;
    
    // Current metrics
    NetworkMetrics current_metrics_;
    
    // Helper methods
    void CalculateLatency();
    void CalculatePacketLoss();
    void CalculateBandwidth();
    void CalculateJitter();
    
    // Cleanup old records
    void CleanupOldRecords();
    
    // Quality thresholds
    static constexpr uint64_t LATENCY_EXCELLENT = 50;    // ms
    static constexpr uint64_t LATENCY_GOOD = 100;
    static constexpr uint64_t LATENCY_FAIR = 200;
    static constexpr uint64_t LATENCY_POOR = 500;
    
    static constexpr float PACKET_LOSS_EXCELLENT = 0.01f;  // 1%
    static constexpr float PACKET_LOSS_GOOD = 0.03f;       // 3%
    static constexpr float PACKET_LOSS_FAIR = 0.05f;       // 5%
    static constexpr float PACKET_LOSS_POOR = 0.10f;       // 10%
};