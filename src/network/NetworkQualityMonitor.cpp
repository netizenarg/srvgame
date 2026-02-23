#include "network/NetworkQualityMonitor.hpp"

NetworkQualityMonitor::NetworkQualityMonitor(size_t max_samples)
    : max_samples_(max_samples) {
    current_metrics_.last_update = std::chrono::steady_clock::now();
}

void NetworkQualityMonitor::RecordPacketSent(uint64_t packet_id, size_t size) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    packet_history_.push_back({
        packet_id,
        std::chrono::steady_clock::now(),
        size,
        false,
        0
    });
    
    current_metrics_.packets_sent++;
    current_metrics_.bytes_sent += size;
    
    // Keep history size manageable
    if (packet_history_.size() > max_samples_) {
        packet_history_.pop_front();
    }
}

void NetworkQualityMonitor::RecordPacketReceived(uint64_t packet_id, size_t size) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    current_metrics_.packets_received++;
    current_metrics_.bytes_received += size;
    Logger::Debug("NetworkQualityMonitor.RecordPacketReceived({}, {})", packet_id, size);
}

void NetworkQualityMonitor::RecordAcknowledgment(uint64_t packet_id, uint64_t rtt_ms) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    for (auto& record : packet_history_) {
        if (record.packet_id == packet_id && !record.acknowledged) {
            record.acknowledged = true;
            record.rtt_ms = rtt_ms;
            latency_samples_.push_back(rtt_ms);
            break;
        }
    }
    
    // Keep latency samples size manageable
    if (latency_samples_.size() > max_samples_) {
        latency_samples_.pop_front();
    }
}

void NetworkQualityMonitor::RecordLatencySample(uint64_t latency_ms) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    latency_samples_.push_back(latency_ms);
    
    if (latency_samples_.size() > max_samples_) {
        latency_samples_.pop_front();
    }
}

void NetworkQualityMonitor::RecordBytesSent(size_t bytes) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    current_metrics_.bytes_sent += bytes;
}

void NetworkQualityMonitor::RecordBytesReceived(size_t bytes) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    current_metrics_.bytes_received += bytes;
}

NetworkMetrics NetworkQualityMonitor::GetMetrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    // Calculate derived metrics
    NetworkMetrics metrics = current_metrics_;
    
    if (!latency_samples_.empty()) {
        // Calculate average latency
        uint64_t sum = 0;
        metrics.min_latency_ms = UINT64_MAX;
        metrics.max_latency_ms = 0;
        
        for (uint64_t latency : latency_samples_) {
            sum += latency;
            metrics.min_latency_ms = std::min(metrics.min_latency_ms, latency);
            metrics.max_latency_ms = std::max(metrics.max_latency_ms, latency);
        }
        
        metrics.average_latency_ms = sum / latency_samples_.size();
        
        // Calculate jitter (standard deviation)
        uint64_t variance_sum = 0;
        for (uint64_t latency : latency_samples_) {
            int64_t diff = static_cast<int64_t>(latency) - static_cast<int64_t>(metrics.average_latency_ms);
            variance_sum += diff * diff;
        }
        
        metrics.jitter_ms = static_cast<uint64_t>(std::sqrt(variance_sum / latency_samples_.size()));
    }
    
    // Calculate packet loss
    if (current_metrics_.packets_sent > 0) {
        size_t acked_packets = 0;
        for (const auto& record : packet_history_) {
            if (record.acknowledged) acked_packets++;
        }
        
        if (!packet_history_.empty()) {
            metrics.packet_loss_percent = (1.0f - (static_cast<float>(acked_packets) / packet_history_.size())) * 100.0f;
        }
    }
    
    // Calculate bandwidth (bytes per second over last 5 samples)
    if (bandwidth_samples_.size() >= 2) {
        auto& first = bandwidth_samples_.front();
        auto& last = bandwidth_samples_.back();
        
        auto time_diff = std::chrono::duration_cast<std::chrono::seconds>(
            last.timestamp - first.timestamp).count();
        
        if (time_diff > 0) {
            uint64_t bytes_sent_diff = last.bytes_sent - first.bytes_sent;
            uint64_t bytes_recv_diff = last.bytes_received - first.bytes_received;
            
            metrics.bandwidth_bps = ((bytes_sent_diff + bytes_recv_diff) * 8) / time_diff;
        }
    }
    
    metrics.CalculateScores();
    metrics.last_update = std::chrono::steady_clock::now();
    
    return metrics;
}

uint32_t NetworkQualityMonitor::CalculateOptimalUpdateRate() const {
    auto metrics = GetMetrics();
    
    if (metrics.packet_loss_percent > 10.0f) {
        return 10; // 10 Hz for high packet loss
    } else if (metrics.average_latency_ms > 200) {
        return 15; // 15 Hz for high latency
    } else if (metrics.average_latency_ms > 100) {
        return 20; // 20 Hz for moderate latency
    } else {
        return 30; // 30 Hz for good conditions
    }
}

bool NetworkQualityMonitor::ShouldEnableCompression() const {
    auto metrics = GetMetrics();
    
    // Enable compression if bandwidth is low or packet loss is high
    return metrics.bandwidth_bps < (1024 * 1024) || // < 1 Mbps
           metrics.packet_loss_percent > 5.0f;
}

bool NetworkQualityMonitor::ShouldEnableRedundancy() const {
    auto metrics = GetMetrics();
    
    // Enable redundancy for critical packets if packet loss is high
    return metrics.packet_loss_percent > 2.0f;
}

uint32_t NetworkQualityMonitor::CalculateOptimalMTU() const {
    auto metrics = GetMetrics();
    
    if (metrics.packet_loss_percent > 5.0f) {
        return 512; // Smaller MTU for high packet loss
    } else if (metrics.average_latency_ms > 150) {
        return 1024; // Medium MTU for high latency
    } else {
        return 1500; // Standard MTU for good conditions
    }
}

NetworkQualityMonitor::ConnectionQuality NetworkQualityMonitor::GetConnectionQuality() const {
    auto metrics = GetMetrics();
    
    if (metrics.average_latency_ms < LATENCY_EXCELLENT &&
        metrics.packet_loss_percent < PACKET_LOSS_EXCELLENT * 100) {
        return ConnectionQuality::EXCELLENT;
    } else if (metrics.average_latency_ms < LATENCY_GOOD &&
               metrics.packet_loss_percent < PACKET_LOSS_GOOD * 100) {
        return ConnectionQuality::GOOD;
    } else if (metrics.average_latency_ms < LATENCY_FAIR &&
               metrics.packet_loss_percent < PACKET_LOSS_FAIR * 100) {
        return ConnectionQuality::FAIR;
    } else if (metrics.average_latency_ms < LATENCY_POOR &&
               metrics.packet_loss_percent < PACKET_LOSS_POOR * 100) {
        return ConnectionQuality::POOR;
    } else {
        return ConnectionQuality::UNSTABLE;
    }
}

void NetworkQualityMonitor::Reset() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    packet_history_.clear();
    latency_samples_.clear();
    bandwidth_samples_.clear();
    current_metrics_ = NetworkMetrics();
    current_metrics_.last_update = std::chrono::steady_clock::now();
}

void NetworkQualityMonitor::Update() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    // Record bandwidth sample
    bandwidth_samples_.push_back({
        std::chrono::steady_clock::now(),
        current_metrics_.bytes_sent,
        current_metrics_.bytes_received
    });
    
    // Keep last 60 seconds of bandwidth data
    auto now = std::chrono::steady_clock::now();
    while (!bandwidth_samples_.empty() &&
           std::chrono::duration_cast<std::chrono::seconds>(
               now - bandwidth_samples_.front().timestamp).count() > 60) {
        bandwidth_samples_.pop_front();
    }
    
    // Cleanup old packet records
    CleanupOldRecords();
}

void NetworkQualityMonitor::CleanupOldRecords() {
    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - std::chrono::seconds(60); // Keep 60 seconds of history
    
    while (!packet_history_.empty() &&
           packet_history_.front().sent_time < cutoff) {
        packet_history_.pop_front();
    }
}

std::string NetworkMetrics::ToString() const {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2);
    
    ss << "Network Metrics:\n";
    ss << "  Latency: " << average_latency_ms << "ms (min: " << min_latency_ms 
       << "ms, max: " << max_latency_ms << "ms, jitter: " << jitter_ms << "ms)\n";
    ss << "  Packet Loss: " << packet_loss_percent << "%\n";
    ss << "  Bandwidth: " << (bandwidth_bps / 1024.0) << " Kbps\n";
    ss << "  Packets: Sent=" << packets_sent << ", Received=" << packets_received << "\n";
    ss << "  Bytes: Sent=" << (bytes_sent / 1024.0) << "KB, Received=" 
       << (bytes_received / 1024.0) << "KB\n";
    ss << "  Quality Scores: Latency=" << latency_score 
       << ", Stability=" << stability_score << ", Overall=" << overall_score;
    
    return ss.str();
}

void NetworkMetrics::CalculateScores() {
    // Latency score (exponential decay from 50ms to 500ms)
    if (average_latency_ms <= 50) {
        latency_score = 1.0f;
    } else if (average_latency_ms >= 500) {
        latency_score = 0.1f;
    } else {
        // Linear interpolation between 50ms (1.0) and 500ms (0.1)
        float t = (average_latency_ms - 50) / 450.0f;
        latency_score = 1.0f - (t * 0.9f);
    }
    
    // Stability score based on packet loss and jitter
    float packet_loss_score = std::max(0.0f, 1.0f - (packet_loss_percent / 10.0f));
    float jitter_score = jitter_ms > 100 ? 0.5f : 1.0f;
    
    stability_score = (packet_loss_score * 0.7f) + (jitter_score * 0.3f);
    
    // Overall score weighted average
    overall_score = (latency_score * 0.4f) + (stability_score * 0.6f);
}
