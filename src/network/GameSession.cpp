#include "network/GameSession.hpp"

// Static member initialization
std::atomic<uint64_t> GameSession::nextSessionId_{1};

// =============== Constructor and Destructor ===============

GameSession::GameSession(asio::ip::tcp::socket socket,
                         std::shared_ptr<asio::ssl::context> ssl_context)
    : socket_(std::move(socket))
    , ssl_context_(ssl_context)
    , sessionId_(nextSessionId_.fetch_add(1))
    , heartbeat_timer_(socket_.get_executor())
    , shutdown_timer_(socket_.get_executor())
    , network_adaptation_timer_(socket_.get_executor())
    , connected_(true)
    , closing_(false) {
    
    // Create SSL stream if context is provided
    if (ssl_context_) {
        ssl_stream_ = std::make_unique<asio::ssl::stream<asio::ip::tcp::socket>>(
            std::move(socket_), *ssl_context_);
    }
    
    // Set socket options for better performance
    try {
        asio::ip::tcp::no_delay no_delay_option(true);
        GetSocket().set_option(no_delay_option);
        
        asio::socket_base::keep_alive keep_alive_option(true);
        GetSocket().set_option(keep_alive_option);
        
        asio::socket_base::receive_buffer_size recv_buffer_option(65536);
        GetSocket().set_option(recv_buffer_option);
        
        asio::socket_base::send_buffer_size send_buffer_option(65536);
        GetSocket().set_option(send_buffer_option);
        
        asio::socket_base::linger linger_option(true, 30);
        GetSocket().set_option(linger_option);
        
    } catch (const std::exception& e) {
        Logger::Warn("Failed to set socket options for session {}: {}",
                     sessionId_, e.what());
    }
    
    connected_time_ = std::chrono::steady_clock::now();
    last_heartbeat_ = connected_time_;
    
    // Initialize rate limiting
    rate_limit_.tokens = rate_limit_.burst_size;
    rate_limit_.last_refill = std::chrono::steady_clock::now();
    
    Logger::Info("GameSession {} created for {}", 
                 sessionId_, GetRemoteEndpoint().address().to_string());
}

GameSession::~GameSession() {
    Stop();
    Logger::Debug("GameSession {} destroyed", sessionId_);
}

// =============== Core Session Management ===============

void GameSession::Start() {
    if (!connected_) {
        Logger::Warn("Session {} already closed", sessionId_);
        return;
    }
    
    Logger::Debug("Starting GameSession {}", sessionId_);
    
    if (ssl_stream_) {
        // Start TLS handshake for encrypted connections
        StartTLSHandshake();
    } else {
        // Start protocol negotiation for plain connections
        StartProtocolNegotiation();
    }
}

void GameSession::StartTLSHandshake() {
    if (!ssl_stream_) return;
    
    auto self = shared_from_this();
    ssl_stream_->async_handshake(asio::ssl::stream_base::server,
        [self](std::error_code ec) {
            if (ec) {
                Logger::Error("TLS handshake failed for session {}: {}",
                              self->sessionId_, ec.message());
                self->Stop();
                return;
            }
            
            Logger::Debug("TLS handshake completed for session {}", self->sessionId_);
            self->StartProtocolNegotiation();
        });
}

void GameSession::StartProtocolNegotiation() {
    // Send protocol capabilities to client
    SendProtocolCapabilities();
    
    // Start reading messages
    DoBinaryRead();
    
    // Start heartbeat monitoring
    StartHeartbeat();
    
    // Start network adaptation monitoring
    StartNetworkAdaptation();
    
    Logger::Info("GameSession {} started", sessionId_);
}

void GameSession::Stop() {
    if (closing_.exchange(true)) {
        return; // Already closing
    }

    Logger::Debug("Stopping GameSession {}", sessionId_);

    connected_ = false;

    // Cancel all timers
    try {
        heartbeat_timer_.cancel();
    } catch (const std::exception& e) {
        Logger::Debug("Error cancelling heartbeat timer: {}", e.what());
    }

    try {
        shutdown_timer_.cancel();
    } catch (const std::exception& e) {
        Logger::Debug("Error cancelling shutdown timer: {}", e.what());
    }

    try {
        network_adaptation_timer_.cancel();
    } catch (const std::exception& e) {
        Logger::Debug("Error cancelling network adaptation timer: {}", e.what());
    }

    std::error_code ec;
    if (ec) {
        Logger::Debug("Error cancelling timers: {}", ec.message());
    }

    // Close socket
    if (GetSocket().is_open()) {
        try {
            GetSocket().shutdown(asio::ip::tcp::socket::shutdown_both, ec);
            if (ec && ec != asio::error::not_connected) {
                Logger::Debug("Error shutting down socket: {}", ec.message());
            }

            GetSocket().close(ec);
            if (ec) {
                Logger::Debug("Error closing socket: {}", ec.message());
            }
        } catch (const std::exception& e) {
            Logger::Error("Exception closing socket: {}", e.what());
        }
    }

    // Notify close handler
    if (close_handler_) {
        try {
            close_handler_();
        } catch (const std::exception& e) {
            Logger::Error("Error in close handler: {}", e.what());
        }
    }

    Logger::Info("GameSession {} stopped", sessionId_);
}

void GameSession::Disconnect() {
    Stop();
}

bool GameSession::IsConnected() const {
    return connected_ && !closing_ && GetSocket().is_open();
}

asio::ip::tcp::endpoint GameSession::GetRemoteEndpoint() const {
    try {
        // if (GetSocket().is_open()) {
        //     return GetSocket().remote_endpoint();
        // }
        const asio::ip::tcp::socket& socket = ssl_stream_ ?
        ssl_stream_->next_layer() : socket_;
        if (socket.is_open()) {
            return socket.remote_endpoint();
        }
    } catch (const std::exception& e) {
        Logger::Debug("Failed to get remote endpoint: {}", e.what());
    }
    return asio::ip::tcp::endpoint();
}

// =============== Binary Protocol Implementation ===============

void GameSession::DoBinaryRead() {
    if (!connected_ || closing_) return;
    
    auto self = shared_from_this();
    
    // Read message header (fixed size)
    BinaryProtocol::NetworkHeader header;
    
    asio::async_read(GetSocket(),
        asio::buffer(&header, sizeof(BinaryProtocol::NetworkHeader)),
        [self, header](std::error_code ec, std::size_t length) mutable {
            if (ec) {
                self->HandleNetworkError(ec);
                return;
            }
            
            // Validate header
            if (header.version > BinaryProtocol::CURRENT_PROTOCOL_VERSION) {
                Logger::Warn("Session {}: incompatible protocol version {}",
                            self->sessionId_, header.version);
                self->SendBinaryError(BinaryProtocol::MESSAGE_TYPE_ERROR, 
                                     "Incompatible protocol version", 400);
                self->DoBinaryRead();
                return;
            }
            
            if (header.length > BinaryProtocol::MAX_MESSAGE_SIZE) {
                Logger::Error("Session {}: message too large: {} bytes",
                            self->sessionId_, header.length);
                self->Stop();
                return;
            }
            
            if (header.length == 0) {
                // Empty message, just process header
                BinaryProtocol::BinaryMessage message;
                message.header = header;
                self->HandleBinaryMessage(message);
                self->DoBinaryRead();
                return;
            }
            
            // Read message body
            std::vector<uint8_t> body(header.length);
            
            asio::async_read(self->GetSocket(),
                asio::buffer(body),
                [self, header, body](std::error_code ec, std::size_t length) mutable {
                    if (ec) {
                        self->HandleNetworkError(ec);
                        return;
                    }
                    
                    // Verify checksum
                    uint32_t calculated = BinaryProtocol::CalculateCRC32(body.data(), body.size());
                    if (calculated != header.checksum) {
                        Logger::Error("Session {}: checksum mismatch", self->sessionId_);
                        self->SendBinaryError(BinaryProtocol::MESSAGE_TYPE_ERROR, 
                                             "Checksum error", 400);
                        self->DoBinaryRead();
                        return;
                    }
                    
                    // Decompress if needed
                    std::vector<uint8_t> processed_body = body;
                    if (header.flags & BinaryProtocol::FLAG_COMPRESSED) {
                        try {
                            processed_body = BinaryProtocol::DecompressData(body);
                        } catch (const std::exception& e) {
                            Logger::Error("Session {}: decompression failed: {}",
                                        self->sessionId_, e.what());
                            self->SendBinaryError(BinaryProtocol::MESSAGE_TYPE_ERROR,
                                                 "Decompression failed", 400);
                            self->DoBinaryRead();
                            return;
                        }
                    }
                    
                    // Record for network monitoring
                    self->network_monitor_.RecordPacketReceived(header.sequence, processed_body.size());
                    
                    // Handle the message
                    BinaryProtocol::BinaryMessage message;
                    message.header = header;
                    message.data = processed_body;
                    
                    self->HandleBinaryMessage(message);
                    
                    // Send acknowledgment for reliable messages
                    if (header.flags & BinaryProtocol::FLAG_RELIABLE) {
                        self->SendAcknowledgment(header.sequence);
                    }
                    
                    // Continue reading
                    if (self->connected_ && !self->closing_) {
                        self->DoBinaryRead();
                    }
                });
        });
}

void GameSession::HandleBinaryMessage(const BinaryProtocol::BinaryMessage& message) {
    // Update heartbeat on any valid message
    last_heartbeat_ = std::chrono::steady_clock::now();
    
    // Record message for statistics
    RecordMessageReceived(message.data.size());
    
    // Check rate limiting
    if (!CheckRateLimit()) {
        SendBinaryError(BinaryProtocol::MESSAGE_TYPE_ERROR, "Rate limit exceeded", 429);
        return;
    }
    
    // Handle special message types
    switch (message.header.message_type) {
        case BinaryProtocol::MESSAGE_TYPE_HEARTBEAT:
            // This is a ping, send pong
            SendBinary(BinaryProtocol::MESSAGE_TYPE_HEARTBEAT, message.data);
            return;
            
        case BinaryProtocol::MESSAGE_TYPE_PROTOCOL_NEGOTIATION:
            HandleProtocolNegotiation(message.data);
            return;
            
        case BinaryProtocol::MESSAGE_TYPE_ERROR:
            Logger::Warn("Session {} received error from client", sessionId_);
            return;
            
        case BinaryProtocol::MESSAGE_TYPE_SUCCESS:
            // Process success acknowledgment
            return;
    }
    
    // Look for registered binary handler
    std::lock_guard<std::mutex> lock(binary_handlers_mutex_);
    auto it = binary_handlers_.find(message.header.message_type);
    
    if (it != binary_handlers_.end()) {
        try {
            it->second(message.header.message_type, message.data);
        } catch (const std::exception& e) {
            Logger::Error("Session {} error in binary handler {}: {}",
                          sessionId_, message.header.message_type, e.what());
            SendBinaryError(BinaryProtocol::MESSAGE_TYPE_ERROR, 
                           "Handler error", 500);
        }
    } else if (default_binary_handler_) {
        try {
            default_binary_handler_(message.header.message_type, message.data);
        } catch (const std::exception& e) {
            Logger::Error("Session {} error in default binary handler: {}",
                          sessionId_, e.what());
        }
    } else {
        Logger::Warn("Session {}: no handler for binary message type {}",
                     sessionId_, message.header.message_type);
        SendBinaryError(BinaryProtocol::MESSAGE_TYPE_ERROR,
                       "Unknown message type", 400);
    }
}

void GameSession::SendBinary(uint16_t message_type, const std::vector<uint8_t>& data) {
    if (!connected_ || closing_) {
        Logger::Warn("Session {} not connected, cannot send binary", sessionId_);
        return;
    }
    
    BinaryProtocol::BinaryMessage message;
    message.header.version = BinaryProtocol::CURRENT_PROTOCOL_VERSION;
    message.header.message_type = message_type;
    message.header.sequence = next_sequence_++;
    message.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    message.data = data;
    message.header.length = static_cast<uint32_t>(data.size());
    
    // Apply compression if enabled and beneficial
    if (compression_enabled_ && data.size() > 1024) {
        try {
            auto compressed = BinaryProtocol::CompressData(data);
            if (compressed.size() < data.size() * 0.9) { // Only compress if we save at least 10%
                message.data = compressed;
                message.header.length = static_cast<uint32_t>(compressed.size());
                message.header.flags |= BinaryProtocol::FLAG_COMPRESSED;
            }
        } catch (const std::exception& e) {
            Logger::Warn("Session {} compression failed: {}", sessionId_, e.what());
        }
    }
    
    // Add encryption flag if using TLS
    if (ssl_stream_) {
        message.header.flags |= BinaryProtocol::FLAG_ENCRYPTED;
    }
    
    // Calculate checksum
    message.header.checksum = BinaryProtocol::CalculateCRC32(
        message.data.data(), message.data.size());
    
    auto serialized = message.Serialize();
    
    // Record for network monitoring
    network_monitor_.RecordPacketSent(message.header.sequence, serialized.size());
    
    // Add to write queue
    std::lock_guard<std::mutex> lock(write_mutex_);
    write_queue_.push(serialized);
    
    if (write_queue_.size() == 1) {
        DoBinaryWrite();
    }
}

void GameSession::SendBinary(uint16_t message_type, const void* data, size_t length) {
    SendBinary(message_type, std::vector<uint8_t>(
        static_cast<const uint8_t*>(data),
        static_cast<const uint8_t*>(data) + length
    ));
}

void GameSession::SendBinaryWithAck(uint16_t message_type, const std::vector<uint8_t>& data) {
    // Store in pending acks for reliability
    uint32_t sequence = next_sequence_.load();
    
    {
        std::lock_guard<std::mutex> lock(ack_mutex_);
        pending_acks_[sequence] = std::chrono::steady_clock::now();
    }
    
    // Send with reliable flag
    BinaryProtocol::BinaryMessage message;
    message.header.version = BinaryProtocol::CURRENT_PROTOCOL_VERSION;
    message.header.message_type = message_type;
    message.header.sequence = sequence;
    message.header.flags = BinaryProtocol::FLAG_RELIABLE;
    message.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    message.data = data;
    message.header.length = static_cast<uint32_t>(data.size());
    message.header.checksum = BinaryProtocol::CalculateCRC32(data.data(), data.size());
    
    auto serialized = message.Serialize();
    
    // Record for network monitoring
    network_monitor_.RecordPacketSent(sequence, serialized.size());
    
    std::lock_guard<std::mutex> lock(write_mutex_);
    write_queue_.push(serialized);
    
    if (write_queue_.size() == 1) {
        DoBinaryWrite();
    }
    
    // Increment sequence number
    next_sequence_++;
}

void GameSession::DoBinaryWrite() {
    if (!connected_ || closing_ || write_queue_.empty()) return;
    
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (write_queue_.empty()) return;
    
    const auto& data = write_queue_.front();
    
    auto self = shared_from_this();
    asio::async_write(GetSocket(),
        asio::buffer(data),
        [self](std::error_code ec, std::size_t length) {
            std::lock_guard<std::mutex> lock(self->write_mutex_);
            
            if (ec) {
                Logger::Error("Session {} write error: {}",
                            self->sessionId_, ec.message());
                self->Stop();
                return;
            }
            
            // Record for statistics
            self->RecordMessageSent(length);
            
            // Remove sent message
            if (!self->write_queue_.empty()) {
                self->write_queue_.pop();
            }
            
            // Continue writing if more messages
            if (!self->write_queue_.empty()) {
                self->DoBinaryWrite();
            }
        });
}

void GameSession::SendAcknowledgment(uint32_t sequence) {
    BinaryProtocol::BinaryWriter writer;
    writer.WriteUInt32(sequence);
    
    BinaryProtocol::BinaryMessage ack;
    ack.header.message_type = BinaryProtocol::MESSAGE_TYPE_SUCCESS;
    ack.header.sequence = next_sequence_++;
    ack.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    ack.data = writer.GetBuffer();
    ack.header.length = static_cast<uint32_t>(ack.data.size());
    ack.header.checksum = BinaryProtocol::CalculateCRC32(ack.data.data(), ack.data.size());
    
    auto serialized = ack.Serialize();
    
    std::lock_guard<std::mutex> lock(write_mutex_);
    write_queue_.push(serialized);
    
    if (write_queue_.size() == 1) {
        DoBinaryWrite();
    }
}

void GameSession::ProcessAcknowledgment(uint32_t sequence) {
    std::lock_guard<std::mutex> lock(ack_mutex_);
    
    auto it = pending_acks_.find(sequence);
    if (it != pending_acks_.end()) {
        auto now = std::chrono::steady_clock::now();
        auto rtt = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - it->second).count();
        
        // Record RTT for network monitoring
        network_monitor_.RecordAcknowledgment(sequence, static_cast<uint64_t>(rtt));
        
        // Remove from pending acks
        pending_acks_.erase(it);
    }
}

void GameSession::SendBinaryError(uint16_t message_type, const std::string& error_message, int code) {
    BinaryProtocol::BinaryWriter writer;
    writer.WriteUInt32(static_cast<uint32_t>(code));
    writer.WriteString(error_message);
    writer.WriteUInt64(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    
    SendBinary(message_type, writer.GetBuffer());
}

// =============== Protocol Negotiation ===============

void GameSession::SendProtocolCapabilities() {
    BinaryProtocol::ProtocolCapabilities caps;
    caps.version = BinaryProtocol::CURRENT_PROTOCOL_VERSION;
    caps.supports_compression = true;
    caps.supports_encryption = IsEncrypted();
    caps.max_message_size = BinaryProtocol::MAX_MESSAGE_SIZE;
    
    // Add all supported message types
    for (int type = 1; type <= 1000; ++type) {
        caps.supported_message_types.push_back(static_cast<uint16_t>(type));
    }
    
    auto caps_data = caps.Serialize();
    SendBinary(BinaryProtocol::MESSAGE_TYPE_PROTOCOL_NEGOTIATION, caps_data);
}

void GameSession::HandleProtocolNegotiation(const std::vector<uint8_t>& data) {
    try {
        auto client_caps = BinaryProtocol::ProtocolCapabilities::Deserialize(
            data.data(), data.size());
        
        Logger::Debug("Session {}: client protocol capabilities: version={}, compression={}, encryption={}",
                     sessionId_, client_caps.version, 
                     client_caps.supports_compression,
                     client_caps.supports_encryption);
        
        // Enable compression if both support it
        if (client_caps.supports_compression) {
            SetCompressionEnabled(true);
            Logger::Debug("Session {}: compression enabled", sessionId_);
        }
        
        protocol_negotiated_ = true;
        
        // Send negotiation success
        BinaryProtocol::BinaryWriter writer;
        writer.WriteUInt8(1); // success
        writer.WriteString("Protocol negotiation successful");
        
        SendBinary(BinaryProtocol::MESSAGE_TYPE_SUCCESS, writer.GetBuffer());
        
    } catch (const std::exception& e) {
        Logger::Error("Session {}: protocol negotiation failed: {}",
                     sessionId_, e.what());
        SendBinaryError(BinaryProtocol::MESSAGE_TYPE_ERROR,
                       "Protocol negotiation failed", 400);
    }
}

// =============== JSON Compatibility Methods ===============

void GameSession::Send(const nlohmann::json& message) {
    try {
        std::string data = message.dump() + "\n";
        SendRaw(data);
    } catch (const std::exception& e) {
        Logger::Error("Session {} failed to serialize JSON: {}",
                     sessionId_, e.what());
    }
}

void GameSession::SendRaw(const std::string& data) {
    if (!connected_ || closing_) {
        Logger::Warn("Session {} not connected, cannot send", sessionId_);
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        bool write_in_progress = !write_queue_.empty();
        
        // Convert string to vector<uint8_t>
        std::vector<uint8_t> binary_data(data.begin(), data.end());
        write_queue_.push(binary_data);
        
        if (!write_in_progress) {
            DoBinaryWrite();
        }
    }
}

void GameSession::SendError(const std::string& message, int code) {
    nlohmann::json error = {
        {"type", "error"},
        {"code", code},
        {"message", message},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };
    Send(error);
}

void GameSession::SendSuccess(const std::string& message, const nlohmann::json& data) {
    nlohmann::json success = {
        {"type", "success"},
        {"message", message},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };
    
    if (!data.empty()) {
        success["data"] = data;
    }
    
    Send(success);
}

void GameSession::SendPing() {
    nlohmann::json ping = {
        {"type", "ping"},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };
    Send(ping);
}

void GameSession::SendPong() {
    nlohmann::json pong = {
        {"type", "pong"},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };
    Send(pong);
}

// =============== Heartbeat Management ===============

void GameSession::StartHeartbeat() {
    if (!connected_ || closing_) return;
    
    // Set initial heartbeat time
    last_heartbeat_ = std::chrono::steady_clock::now();
    
    // Start heartbeat check
    CheckHeartbeat();
}

void GameSession::CheckHeartbeat() {
    if (!connected_ || closing_) return;
    
    heartbeat_timer_.expires_after(std::chrono::seconds(30));
    heartbeat_timer_.async_wait(
        [self = shared_from_this()](std::error_code ec) {
            if (ec == asio::error::operation_aborted) {
                return;
            }
            
            if (!self->connected_ || self->closing_) {
                return;
            }
            
            // Check time since last heartbeat
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - self->last_heartbeat_);
            
            if (elapsed.count() > 60) { // 60 second timeout
                Logger::Warn("Session {} heartbeat timeout ({} seconds)",
                             self->sessionId_, elapsed.count());
                self->Stop();
                return;
            }
            
            // Send heartbeat ping if no activity for 30 seconds
            if (elapsed.count() > 30) {
                self->SendPing();
            }
            
            // Schedule next heartbeat check
            self->CheckHeartbeat();
        });
}

void GameSession::UpdateHeartbeat() {
    last_heartbeat_ = std::chrono::steady_clock::now();
}

// =============== Network Quality Monitoring ===============

void GameSession::StartNetworkAdaptation() {
    auto self = shared_from_this();
    
    network_adaptation_timer_.expires_after(std::chrono::seconds(10));
    network_adaptation_timer_.async_wait([self](std::error_code ec) {
        if (ec) return;
        
        self->CheckNetworkConditions();
        self->StartNetworkAdaptation();
    });
}

void GameSession::CheckNetworkConditions() {
    // Update network monitor statistics
    network_monitor_.Update();
    
    auto metrics = network_monitor_.GetMetrics();
    auto quality = network_monitor_.GetConnectionQuality();
    
    // Adapt compression
    bool should_compress = network_monitor_.ShouldEnableCompression();
    if (should_compress != compression_enabled_) {
        SetCompressionEnabled(should_compress);
        Logger::Debug("Session {}: compression {}",
                     sessionId_, should_compress ? "enabled" : "disabled");
    }
    
    // Adapt update rate based on connection quality
    uint32_t optimal_rate = network_monitor_.CalculateOptimalUpdateRate();
    Logger::Debug("Session optimal_rate: {}",optimal_rate);

    // Log connection quality changes
    static auto last_quality = NetworkQualityMonitor::ConnectionQuality::EXCELLENT;
    if (quality != last_quality) {
        const char* quality_names[] = {"EXCELLENT", "GOOD", "FAIR", "POOR", "UNSTABLE"};
        Logger::Info("Session {} connection quality changed: {} -> {}",
                    sessionId_, 
                    quality_names[static_cast<int>(last_quality)],
                    quality_names[static_cast<int>(quality)]);
        last_quality = quality;
    }
    
    // Send network metrics to client for adaptation
    if (quality == NetworkQualityMonitor::ConnectionQuality::UNSTABLE) {
        Logger::Warn("Session {}: unstable connection detected", sessionId_);
        
        // Send warning to client
        nlohmann::json warning = {
            {"type", "network_warning"},
            {"message", "Unstable network connection detected"},
            {"metrics", {
                {"latency", metrics.average_latency_ms},
                {"packet_loss", metrics.packet_loss_percent},
                {"jitter", metrics.jitter_ms}
            }},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };
        Send(warning);
    }
}

void GameSession::AdaptToNetworkConditions() {
    CheckNetworkConditions();
}

// =============== Binary Message Handlers ===============

void GameSession::SetBinaryMessageHandler(uint16_t message_type, BinaryMessageHandler handler) {
    std::lock_guard<std::mutex> lock(binary_handlers_mutex_);
    binary_handlers_[message_type] = std::move(handler);
}

void GameSession::SetDefaultBinaryMessageHandler(BinaryMessageHandler handler) {
    std::lock_guard<std::mutex> lock(binary_handlers_mutex_);
    default_binary_handler_ = std::move(handler);
}

void GameSession::SetMessageHandler(std::function<void(const nlohmann::json&)> handler) {
    message_handler_ = std::move(handler);
}

void GameSession::SetCloseHandler(std::function<void()> handler) {
    close_handler_ = std::move(handler);
}

// =============== Session Statistics ===============

SessionStats GameSession::GetStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void GameSession::ResetStats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = SessionStats{};
}

void GameSession::RecordMessageReceived(size_t size) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.messages_received++;
    stats_.bytes_received += size;
    stats_.last_message_received = std::chrono::steady_clock::now();
    
    // Also record for network monitoring
    network_monitor_.RecordBytesReceived(size);
}

void GameSession::RecordMessageSent(size_t size) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.messages_sent++;
    stats_.bytes_sent += size;
    stats_.last_message_sent = std::chrono::steady_clock::now();
    
    // Also record for network monitoring
    network_monitor_.RecordBytesSent(size);
}

// =============== Compression ===============

void GameSession::SetCompressionEnabled(bool enabled) {
    compression_enabled_ = enabled;
}

bool GameSession::IsCompressionEnabled() const {
    return compression_enabled_;
}

// =============== Rate Limiting ===============

void GameSession::SetRateLimit(int messages_per_second, int burst_size) {
    std::lock_guard<std::mutex> lock(rate_limit_mutex_);
    rate_limit_.messages_per_second = messages_per_second;
    rate_limit_.burst_size = burst_size;
    rate_limit_.tokens = burst_size;
    rate_limit_.last_refill = std::chrono::steady_clock::now();
}

bool GameSession::CheckRateLimit() {
    if (!rate_limit_enabled_) {
        return true;
    }
    
    std::lock_guard<std::mutex> lock(rate_limit_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - rate_limit_.last_refill);
    
    // Refill tokens based on elapsed time
    int refill = (elapsed.count() * rate_limit_.messages_per_second) / 1000;
    if (refill > 0) {
        rate_limit_.tokens = std::min(rate_limit_.burst_size,
                                     rate_limit_.tokens + refill);
        rate_limit_.last_refill = now;
    }
    
    // Check if we have tokens
    if (rate_limit_.tokens > 0) {
        rate_limit_.tokens--;
        return true;
    }
    
    // Rate limit exceeded
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.rate_limit_exceeded++;
    }
    
    return false;
}

void GameSession::SetRateLimitEnabled(bool enabled) {
    rate_limit_enabled_ = enabled;
}

// =============== Session Groups ===============

void GameSession::JoinGroup(const std::string& groupId) {
    std::lock_guard<std::mutex> lock(groups_mutex_);
    joined_groups_.insert(groupId);
}

void GameSession::LeaveGroup(const std::string& groupId) {
    std::lock_guard<std::mutex> lock(groups_mutex_);
    joined_groups_.erase(groupId);
}

void GameSession::LeaveAllGroups() {
    std::lock_guard<std::mutex> lock(groups_mutex_);
    joined_groups_.clear();
}

std::set<std::string> GameSession::GetJoinedGroups() const {
    std::lock_guard<std::mutex> lock(groups_mutex_);
    return joined_groups_;
}

bool GameSession::IsInGroup(const std::string& groupId) const {
    std::lock_guard<std::mutex> lock(groups_mutex_);
    return joined_groups_.find(groupId) != joined_groups_.end();
}

// =============== Authentication and Security ===============

void GameSession::Authenticate(const std::string& authToken) {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    auth_token_ = authToken;
    authenticated_ = true;
    authentication_time_ = std::chrono::steady_clock::now();
    
    Logger::Info("Session {} authenticated", sessionId_);
}

void GameSession::Deauthenticate() {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    auth_token_.clear();
    authenticated_ = false;
    player_id_ = 0;
    
    Logger::Info("Session {} deauthenticated", sessionId_);
}

bool GameSession::IsAuthenticated() const {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    return authenticated_;
}

std::string GameSession::GetAuthToken() const {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    return auth_token_;
}

void GameSession::SetPlayerId(int64_t playerId) {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    player_id_ = playerId;
    
    Logger::Debug("Session {} assigned to player {}", sessionId_, playerId);
}

int64_t GameSession::GetPlayerId() const {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    return player_id_;
}

// =============== Session Data Storage ===============

void GameSession::SetData(const std::string& key, const nlohmann::json& value) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    session_data_[key] = value;
}

nlohmann::json GameSession::GetData(const std::string& key, const nlohmann::json& defaultValue) const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    auto it = session_data_.find(key);
    if (it != session_data_.end()) {
        return it->second;
    }
    return defaultValue;
}

bool GameSession::HasData(const std::string& key) const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return session_data_.find(key) != session_data_.end();
}

void GameSession::RemoveData(const std::string& key) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    session_data_.erase(key);
}

void GameSession::ClearData() {
    std::lock_guard<std::mutex> lock(data_mutex_);
    session_data_.clear();
}

nlohmann::json GameSession::GetAllData() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    nlohmann::json result;
    for (const auto& [key, value] : session_data_) {
        result[key] = value;
    }
    return result;
}

// =============== Session Properties ===============

void GameSession::SetProperty(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(properties_mutex_);
    properties_[key] = value;
}

std::string GameSession::GetProperty(const std::string& key, const std::string& defaultValue) const {
    std::lock_guard<std::mutex> lock(properties_mutex_);
    auto it = properties_.find(key);
    if (it != properties_.end()) {
        return it->second;
    }
    return defaultValue;
}

std::map<std::string, std::string> GameSession::GetAllProperties() const {
    std::lock_guard<std::mutex> lock(properties_mutex_);
    return properties_;
}

// =============== Metrics and Monitoring ===============

SessionMetrics GameSession::GetMetrics() const {
    auto now = std::chrono::steady_clock::now();
    auto connected_time = std::chrono::duration_cast<std::chrono::seconds>(
        now - connected_time_);
    
    SessionMetrics metrics;
    
    // Get stats with lock
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        metrics.messages_received = stats_.messages_received;
        metrics.messages_sent = stats_.messages_sent;
        metrics.bytes_received = stats_.bytes_received;
        metrics.bytes_sent = stats_.bytes_sent;
        metrics.rate_limit_exceeded = stats_.rate_limit_exceeded;
    }
    
    metrics.session_id = sessionId_;
    metrics.connected_time_seconds = connected_time.count();
    metrics.is_connected = IsConnected();
    metrics.is_authenticated = IsAuthenticated();
    metrics.player_id = GetPlayerId();
    
    try {
        auto endpoint = GetRemoteEndpoint();
        metrics.remote_endpoint = endpoint.address().to_string() + ":" +
                                 std::to_string(endpoint.port());
    } catch (...) {
        metrics.remote_endpoint = "unknown";
    }
    
    // Calculate message rates
    if (connected_time.count() > 0) {
        metrics.receive_rate = static_cast<double>(metrics.messages_received) / connected_time.count();
        metrics.send_rate = static_cast<double>(metrics.messages_sent) / connected_time.count();
    }
    
    // Group membership
    metrics.joined_groups = GetJoinedGroups().size();
    
    // Network quality metrics
    auto network_metrics = network_monitor_.GetMetrics();
    metrics.average_latency = network_metrics.average_latency_ms;
    metrics.packet_loss = network_metrics.packet_loss_percent;
    
    return metrics;
}

void GameSession::PrintMetrics() const {
    auto metrics = GetMetrics();
    
    Logger::Info("Session {} Metrics:", metrics.session_id);
    Logger::Info("  Remote Endpoint: {}", metrics.remote_endpoint);
    Logger::Info("  Connected Time: {} seconds", metrics.connected_time_seconds);
    Logger::Info("  Status: {} (Auth: {})",
                 metrics.is_connected ? "Connected" : "Disconnected",
                 metrics.is_authenticated ? "Yes" : "No");
    Logger::Info("  Player ID: {}", metrics.player_id);
    Logger::Info("  Messages: Received={}, Sent={}",
                 metrics.messages_received, metrics.messages_sent);
    Logger::Info("  Bytes: Received={}, Sent={}",
                 metrics.bytes_received, metrics.bytes_sent);
    Logger::Info("  Rates: Receive={:.2f}/s, Send={:.2f}/s",
                 metrics.receive_rate, metrics.send_rate);
    Logger::Info("  Rate Limit Exceeded: {}", metrics.rate_limit_exceeded);
    Logger::Info("  Joined Groups: {}", metrics.joined_groups);
    Logger::Info("  Network: Latency={}ms, Packet Loss={:.1f}%",
                 metrics.average_latency, metrics.packet_loss);
}

// =============== Utility Methods ===============

std::string GameSession::ToString() const {
    std::stringstream ss;
    ss << "GameSession[" << sessionId_ << "] ";
    
    try {
        auto endpoint = GetRemoteEndpoint();
        ss << endpoint.address().to_string() << ":" << endpoint.port();
    } catch (...) {
        ss << "unknown endpoint";
    }
    
    if (IsAuthenticated()) {
        ss << " (Player: " << GetPlayerId() << ")";
    }
    
    if (ssl_stream_) {
        ss << " [TLS]";
    }
    
    return ss.str();
}

uint64_t GameSession::GetUptimeSeconds() const {
    if (!connected_) {
        return 0;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        now - connected_time_);
    
    return uptime.count();
}

// =============== Connection Quality Monitoring ===============

void GameSession::RecordLatency(uint64_t latencyMs) {
    network_monitor_.RecordLatencySample(latencyMs);
}

uint64_t GameSession::GetAverageLatency() const {
    auto metrics = network_monitor_.GetMetrics();
    return metrics.average_latency_ms;
}

uint64_t GameSession::GetMinLatency() const {
    auto metrics = network_monitor_.GetMetrics();
    return metrics.min_latency_ms;
}

uint64_t GameSession::GetMaxLatency() const {
    auto metrics = network_monitor_.GetMetrics();
    return metrics.max_latency_ms;
}

std::vector<uint64_t> GameSession::GetLatencySamples() const {
    // Note: NetworkQualityMonitor doesn't expose samples directly
    // In a full implementation, we would add this method
    return {};
}

// =============== Custom Event Handlers ===============

void GameSession::SetCustomEventHandler(const std::string& eventName,
                                        std::function<void(const nlohmann::json&)> handler) {
    std::lock_guard<std::mutex> lock(event_handlers_mutex_);
    custom_event_handlers_[eventName] = handler;
}

void GameSession::RemoveCustomEventHandler(const std::string& eventName) {
    std::lock_guard<std::mutex> lock(event_handlers_mutex_);
    custom_event_handlers_.erase(eventName);
}

void GameSession::HandleCustomEvent(const std::string& eventName, const nlohmann::json& data) {
    std::lock_guard<std::mutex> lock(event_handlers_mutex_);
    auto it = custom_event_handlers_.find(eventName);
    if (it != custom_event_handlers_.end()) {
        try {
            it->second(data);
        } catch (const std::exception& e) {
            Logger::Error("Error in custom event handler '{}': {}", eventName, e.what());
        }
    }
}

// =============== Message Queue Management ===============

size_t GameSession::GetPendingMessageCount() const {
    std::lock_guard<std::mutex> lock(write_mutex_);
    return write_queue_.size();
}

void GameSession::ClearPendingMessages() {
    std::lock_guard<std::mutex> lock(write_mutex_);
    std::queue<std::vector<uint8_t>> empty;
    std::swap(write_queue_, empty);
}

bool GameSession::IsWriteQueueFull() const {
    std::lock_guard<std::mutex> lock(write_mutex_);
    return write_queue_.size() >= max_write_queue_size_;
}

void GameSession::SetMaxWriteQueueSize(size_t maxSize) {
    max_write_queue_size_ = maxSize;
}

// =============== Graceful Shutdown ===============

void GameSession::BeginGracefulShutdown() {
    if (graceful_shutdown_) {
        return;
    }
    
    Logger::Info("Beginning graceful shutdown for session {}", sessionId_);
    graceful_shutdown_ = true;
    
    // Send shutdown notification to client
    nlohmann::json shutdown_msg = {
        {"type", "shutdown_notice"},
        {"message", "Server shutting down"},
        {"timeout_seconds", 30},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };
    Send(shutdown_msg);
    
    // Start shutdown timer
    shutdown_timer_.expires_after(std::chrono::seconds(30));
    shutdown_timer_.async_wait([self = shared_from_this()](std::error_code ec) {
        if (!ec) {
            Logger::Info("Graceful shutdown timeout for session {}", self->sessionId_);
            self->Stop();
        }
    });
}

void GameSession::CancelGracefulShutdown() {
    if (!graceful_shutdown_) {
        return;
    }
    
    Logger::Info("Cancelling graceful shutdown for session {}", sessionId_);
    graceful_shutdown_ = false;
    
    try {
        shutdown_timer_.cancel();
    } catch (const std::exception& e) {
        Logger::Debug("Error cancelling shutdown timer: {}", e.what());
    }
}

// =============== World and Entity Methods ===============

void GameSession::SendWorldChunkBinary(int chunkX, int chunkZ, const std::vector<uint8_t>& chunkData) {
    BinaryProtocol::BinaryWriter writer;
    writer.WriteInt32(chunkX);
    writer.WriteInt32(chunkZ);
    
    // Combine metadata with chunk data
    auto metadata = writer.GetBuffer();
    std::vector<uint8_t> combined_data(metadata.size() + chunkData.size());
    
    std::copy(metadata.begin(), metadata.end(), combined_data.begin());
    std::copy(chunkData.begin(), chunkData.end(), combined_data.begin() + metadata.size());
    
    SendBinary(BinaryProtocol::MESSAGE_TYPE_CHUNK_DATA, combined_data);
}

void GameSession::SendEntityUpdateBinary(uint64_t entityId, const std::vector<uint8_t>& entityData) {
    BinaryProtocol::BinaryWriter writer;
    writer.WriteUInt64(entityId);
    
    // Combine entity ID with entity data
    auto id_data = writer.GetBuffer();
    std::vector<uint8_t> combined_data(id_data.size() + entityData.size());
    
    std::copy(id_data.begin(), id_data.end(), combined_data.begin());
    std::copy(entityData.begin(), entityData.end(), combined_data.begin() + id_data.size());
    
    SendBinary(BinaryProtocol::MESSAGE_TYPE_ENTITY_UPDATE, combined_data);
}

void GameSession::SendEntitySpawnBinary(uint64_t entityId, const std::vector<uint8_t>& spawnData) {
    BinaryProtocol::BinaryWriter writer;
    writer.WriteUInt64(entityId);
    
    auto id_data = writer.GetBuffer();
    std::vector<uint8_t> combined_data(id_data.size() + spawnData.size());
    
    std::copy(id_data.begin(), id_data.end(), combined_data.begin());
    std::copy(spawnData.begin(), spawnData.end(), combined_data.begin() + id_data.size());
    
    SendBinary(BinaryProtocol::MESSAGE_TYPE_ENTITY_SPAWN, combined_data);
}

void GameSession::SendEntityDespawnBinary(uint64_t entityId) {
    BinaryProtocol::BinaryWriter writer;
    writer.WriteUInt64(entityId);
    
    SendBinary(BinaryProtocol::MESSAGE_TYPE_ENTITY_DESPAWN, writer.GetBuffer());
}

// =============== Player State Synchronization ===============

void GameSession::SyncPlayerStateBinary(const glm::vec3& position, const glm::vec3& rotation,
                                        const glm::vec3& velocity, uint32_t last_input_id) {
    BinaryProtocol::BinaryWriter writer;
    
    writer.WriteVector3(position);
    writer.WriteVector3(rotation);
    writer.WriteVector3(velocity);
    writer.WriteUInt32(last_input_id);
    writer.WriteUInt64(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    
    SendBinary(BinaryProtocol::MESSAGE_TYPE_PLAYER_STATE, writer.GetBuffer());
}

void GameSession::SendPositionCorrection(const glm::vec3& position, const glm::vec3& velocity) {
    BinaryProtocol::BinaryWriter writer;
    
    writer.WriteVector3(position);
    writer.WriteVector3(velocity);
    writer.WriteUInt64(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    
    SendBinary(BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION_CORRECTION, writer.GetBuffer());
}

// =============== Private Helper Methods ===============

asio::ip::tcp::socket& GameSession::GetSocket() {
    if (ssl_stream_) {
        return ssl_stream_->next_layer();
    }
    return socket_;
}

const asio::ip::tcp::socket& GameSession::GetSocket() const {
    if (ssl_stream_) {
        return ssl_stream_->next_layer();
    }
    return socket_;
}

void GameSession::HandleNetworkError(std::error_code ec) {
    if (ec == asio::error::eof || ec == asio::error::connection_reset) {
        Logger::Debug("Session {} disconnected: {}",
                      sessionId_, ec.message());
    } else if (ec != asio::error::operation_aborted) {
        Logger::Error("Session {} network error: {}",
                      sessionId_, ec.message());
    }
    Stop();
}

void GameSession::HandleMessage(const std::string& message) {
    if (message.empty()) {
        return;
    }
    
    try {
        // Parse JSON message
        auto json_message = nlohmann::json::parse(message);
        
        // Update heartbeat on any valid message
        UpdateHeartbeat();
        
        // Handle the message
        if (message_handler_) {
            message_handler_(json_message);
        } else {
            Logger::Warn("No message handler set for session {}", sessionId_);
        }
        
    } catch (const nlohmann::json::parse_error& e) {
        Logger::Error("Session {} JSON parse error: {} - Message: {}",
                      sessionId_, e.what(), message);
        
        // Send error response for malformed JSON
        SendError("Invalid JSON format", 400);
        
    } catch (const std::exception& e) {
        Logger::Error("Session {} message handling error: {}",
                      sessionId_, e.what());
    }
}

// =============== Prediction System Integration ===============

PredictionSystem& GameSession::GetPredictionSystem() {
    return prediction_system_;
}

// =============== Legacy JSON Handlers (for backward compatibility) ===============

// These methods handle the old JSON-based protocol for backward compatibility
void GameSession::SendWorldChunk(int chunkX, int chunkZ, const nlohmann::json& chunkData) {
    nlohmann::json message = {
        {"type", "world_chunk"},
        {"chunkX", chunkX},
        {"chunkZ", chunkZ},
        {"data", chunkData},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };
    Send(message);
}

void GameSession::SendEntityUpdate(uint64_t entityId, const nlohmann::json& entityData) {
    nlohmann::json message = {
        {"type", "entity_update"},
        {"entityId", entityId},
        {"data", entityData},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };
    Send(message);
}

void GameSession::SendEntitySpawn(uint64_t entityId, const nlohmann::json& spawnData) {
    nlohmann::json message = {
        {"type", "entity_spawn"},
        {"entityId", entityId},
        {"data", spawnData},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };
    Send(message);
}

void GameSession::SendEntityDespawn(uint64_t entityId) {
    nlohmann::json message = {
        {"type", "entity_despawn"},
        {"entityId", entityId},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };
    Send(message);
}

void GameSession::SendCollisionEvent(uint64_t entityId1, uint64_t entityId2, const glm::vec3& point) {
    nlohmann::json message = {
        {"type", "collision_event"},
        {"entityId1", entityId1},
        {"entityId2", entityId2},
        {"point", {point.x, point.y, point.z}},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };
    Send(message);
}

void GameSession::SyncPlayerState(const glm::vec3& position, const glm::vec3& rotation, const glm::vec3& velocity) {
    nlohmann::json message = {
        {"type", "player_state_sync"},
        {"position", {position.x, position.y, position.z}},
        {"rotation", {rotation.x, rotation.y, rotation.z}},
        {"velocity", {velocity.x, velocity.y, velocity.z}},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };
    Send(message);
}

void GameSession::SendNearbyEntities(const std::vector<nlohmann::json>& entities) {
    nlohmann::json message = {
        {"type", "nearby_entities"},
        {"entities", entities},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };
    Send(message);
}

void GameSession::SendNPCInteraction(uint64_t npcId, const std::string& interactionType, const nlohmann::json& data) {
    nlohmann::json message = {
        {"type", "npc_interaction"},
        {"npcId", npcId},
        {"interaction", interactionType},
        {"data", data},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };
    Send(message);
}

void GameSession::SendCompressedWorldData(const std::vector<uint8_t>& compressedData) {
    // Convert binary data to base64 for JSON transmission
    std::string base64_data;
    const char base64_chars[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    size_t i = 0;
    while (i < compressedData.size()) {
        uint32_t octet_a = i < compressedData.size() ? compressedData[i++] : 0;
        uint32_t octet_b = i < compressedData.size() ? compressedData[i++] : 0;
        uint32_t octet_c = i < compressedData.size() ? compressedData[i++] : 0;
        
        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
        
        base64_data += base64_chars[(triple >> 18) & 0x3F];
        base64_data += base64_chars[(triple >> 12) & 0x3F];
        base64_data += base64_chars[(triple >> 6) & 0x3F];
        base64_data += base64_chars[triple & 0x3F];
    }
    
    // Add padding
    size_t padding = compressedData.size() % 3;
    if (padding > 0) {
        for (size_t j = 0; j < 3 - padding; ++j) {
            base64_data[base64_data.size() - 1 - j] = '=';
        }
    }
    
    nlohmann::json message = {
        {"type", "compressed_world_data"},
        {"data", base64_data},
        {"compression", "base64"},
        {"original_size", compressedData.size()},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };
    Send(message);
}

// =============== Legacy Handlers (Kept for backward compatibility) ===============

void GameSession::HandleWorldRequest(const nlohmann::json& data) {
    // Legacy handler - convert to binary protocol
    Logger::Debug("Session {}: converting legacy world request to binary", sessionId_);
    
    BinaryProtocol::BinaryWriter writer;
    writer.WriteInt32(data.value("chunkX", 0));
    writer.WriteInt32(data.value("chunkZ", 0));
    writer.WriteUInt8(data.value("lod", 0));
    
    // Forward to binary handler
    auto handler_it = binary_handlers_.find(BinaryProtocol::MESSAGE_TYPE_CHUNK_REQUEST);
    if (handler_it != binary_handlers_.end()) {
        handler_it->second(BinaryProtocol::MESSAGE_TYPE_CHUNK_REQUEST, writer.GetBuffer());
    }
}

void GameSession::HandleEntityInteraction(const nlohmann::json& data) {
    // Legacy handler - convert to binary protocol
    BinaryProtocol::BinaryWriter writer;
    writer.WriteUInt64(data.value("entityId", 0ULL));
    writer.WriteString(data.value("interaction", ""));
    writer.WriteJson(data.value("data", nlohmann::json()));
    
    // Forward to appropriate binary handler
    auto handler_it = binary_handlers_.find(BinaryProtocol::MESSAGE_TYPE_NPC_INTERACTION);
    if (handler_it != binary_handlers_.end()) {
        handler_it->second(BinaryProtocol::MESSAGE_TYPE_NPC_INTERACTION, writer.GetBuffer());
    }
}

void GameSession::HandleMovementUpdate(const nlohmann::json& data) {
    // Legacy handler - convert to binary protocol
    BinaryProtocol::BinaryWriter writer;
    
    glm::vec3 position(
        data.value("x", 0.0f),
        data.value("y", 0.0f),
        data.value("z", 0.0f)
    );
    
    glm::vec3 velocity(
        data.value("vx", 0.0f),
        data.value("vy", 0.0f),
        data.value("vz", 0.0f)
    );
    
    writer.WriteVector3(position);
    writer.WriteVector3(velocity);
    writer.WriteUInt32(data.value("input_id", 0U));
    
    // Forward to binary handler
    auto handler_it = binary_handlers_.find(BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION);
    if (handler_it != binary_handlers_.end()) {
        handler_it->second(BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION, writer.GetBuffer());
    }
}

void GameSession::HandleFamiliarCommand(const nlohmann::json& data) {
    // Legacy handler - convert to binary protocol
    BinaryProtocol::BinaryWriter writer;
    writer.WriteUInt64(data.value("familiarId", 0ULL));
    writer.WriteString(data.value("command", ""));
    writer.WriteUInt64(data.value("targetId", 0ULL));
    
    // Forward to binary handler (using entity interaction as fallback)
    auto handler_it = binary_handlers_.find(BinaryProtocol::MESSAGE_TYPE_NPC_INTERACTION);
    if (handler_it != binary_handlers_.end()) {
        handler_it->second(BinaryProtocol::MESSAGE_TYPE_NPC_INTERACTION, writer.GetBuffer());
    }
}
