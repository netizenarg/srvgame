#include "network/BinarySession.hpp"

// Static member initialization
std::atomic<uint64_t> BinarySession::nextSessionId_{1};

// =============== Constructor and Destructor ===============

BinarySession::BinarySession(asio::ip::tcp::socket socket,
                         std::shared_ptr<asio::ssl::context> ssl_context)
    : socket_(std::move(socket))
    , ssl_context_(ssl_context)
    , sessionId_(nextSessionId_.fetch_add(1))
    , connected_(true)
    , closing_(false)
    , heartbeat_timer_(socket_.get_executor())
    , shutdown_timer_(socket_.get_executor())
    , network_adaptation_timer_(socket_.get_executor()) {

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

    Logger::Info("BinarySession {} created for {}",
                 sessionId_, GetRemoteEndpoint().address().to_string());
}

BinarySession::~BinarySession() {
    Stop();
    Logger::Debug("BinarySession {} destroyed", sessionId_);
}

// =============== Core Session Management ===============

void BinarySession::Start() {
    if (!connected_) {
        Logger::Warn("Session {} already closed", sessionId_);
        return;
    }

    Logger::Debug("Starting BinarySession {}", sessionId_);

    if (ssl_stream_) {
        // Start TLS handshake for encrypted connections
        StartTLSHandshake();
    } else {
        // Start protocol negotiation for plain connections
        StartProtocolNegotiation();
    }
}

void BinarySession::StartTLSHandshake() {
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

void BinarySession::StartProtocolNegotiation() {
    // Send protocol capabilities to client
    SendProtocolCapabilities();

    // Setup default binary handlers
    SetupDefaultHandlers();

    // Start reading messages
    DoBinaryRead();

    // Start heartbeat monitoring
    StartHeartbeat();

    // Start network adaptation monitoring
    StartNetworkAdaptation();

    Logger::Info("BinarySession {} started", sessionId_);
}

void BinarySession::Stop() {
    if (closing_.exchange(true)) {
        return;
    }
    Logger::Debug("Stopping BinarySession {}", sessionId_);
    connected_ = false;
    try {
        heartbeat_timer_.cancel();
    } catch (const std::exception& err) {
        Logger::Debug("Error cancelling heartbeat timer: {}", err.what());
    }
    try {
        shutdown_timer_.cancel();
    } catch (const std::exception& err) {
        Logger::Debug("Error cancelling shutdown timer: {}", err.what());
    }
    try {
        network_adaptation_timer_.cancel();
    } catch (const std::exception& err) {
        Logger::Debug("Error cancelling network adaptation timer: {}", err.what());
    }
    std::error_code ec;
    if (ec) {
        Logger::Debug("Error cancelling timers: {}", ec.message());
    }
    if (GetSocket().is_open()) {
        try {
            GetSocket().cancel(ec);
            if (ec) {
                Logger::Debug("Error cancel socket: {}", ec.message());
            }
            GetSocket().shutdown(asio::ip::tcp::socket::shutdown_both, ec);
            if (ec && ec != asio::error::not_connected) {
                Logger::Debug("Error shutting down socket: {}", ec.message());
            }
            GetSocket().close(ec);
            if (ec) {
                Logger::Debug("Error closing socket: {}", ec.message());
            }
        } catch (const std::exception& err) {
            Logger::Error("Exception closing socket: {}", err.what());
        }
    }
    if (close_handler_) {
        try {
            close_handler_();
        } catch (const std::exception& err) {
            Logger::Error("Error in close handler: {}", err.what());
        }
    }
    Logger::Info("BinarySession {} stopped", sessionId_);
}

void BinarySession::Disconnect() {
    Stop();
}

bool BinarySession::IsConnected() const {
    return connected_ && !closing_ && GetSocket().is_open();
}

asio::ip::tcp::endpoint BinarySession::GetRemoteEndpoint() const {
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
void BinarySession::DoBinaryRead() {
    if (!connected_ || closing_) return;

    auto self = shared_from_this();

    // Read message header (fixed size)
    BinaryProtocol::NetworkHeader header;

    asio::async_read(GetSocket(),
        asio::buffer(&header, sizeof(BinaryProtocol::NetworkHeader)),
        [self, header](std::error_code ec, std::size_t length) mutable {
            Logger::Debug("BinarySession::DoBinaryRead asio::async_read length = {}", length);
            if (ec) {
                self->HandleNetworkError(ec);
                return;
            }

            // Validate header
            if (header.version > BinaryProtocol::CURRENT_PROTOCOL_VERSION) {
                Logger::Warn("Session {}: incompatible protocol version {}",
                            self->sessionId_, header.version);
                self->SendError(BinaryProtocol::MESSAGE_TYPE_ERROR,
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

            // Create deadline timer for payload read
            auto deadline = std::make_shared<asio::steady_timer>(self->GetSocket().get_executor());
            deadline->expires_after(std::chrono::seconds(10));
            deadline->async_wait([self, deadline](std::error_code ec) {
                if (!ec && self->connected_) {
                    Logger::Warn("Session {}: payload read timeout", self->sessionId_);
                    self->Stop();
                }
            });

            // Read message body
            std::vector<uint8_t> body(header.length);

            asio::async_read(self->GetSocket(),
                asio::buffer(body),
                [self, header, body, deadline](std::error_code ec, std::size_t length) mutable {
                    deadline->cancel();  // Cancel timeout on successful read or error
                    if (ec) {
                        self->HandleNetworkError(ec);
                        return;
                    }

                    Logger::Debug("BinarySession::DoBinaryRead asio::async_read length = {}", length);

                    // Verify checksum
                    uint32_t calculated = BinaryProtocol::CalculateCRC32(body.data(), body.size());
                    if (calculated != header.checksum) {
                        Logger::Error("Session {}: checksum mismatch", self->sessionId_);
                        self->SendError(BinaryProtocol::MESSAGE_TYPE_ERROR,
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
                            self->SendError(BinaryProtocol::MESSAGE_TYPE_ERROR,
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

void BinarySession::HandleBinaryMessage(const BinaryProtocol::BinaryMessage& message) {
    last_heartbeat_ = std::chrono::steady_clock::now();
    RecordMessageReceived(message.data.size());
    if (!CheckRateLimit()) {
        SendError(BinaryProtocol::MESSAGE_TYPE_ERROR, "Rate limit exceeded", 429);
        return;
    }
    switch (message.header.message_type) {
        case BinaryProtocol::MESSAGE_TYPE_HEARTBEAT:
            Send(BinaryProtocol::MESSAGE_TYPE_HEARTBEAT, message.data);
            return;
        case BinaryProtocol::MESSAGE_TYPE_PROTOCOL_NEGOTIATION:
            HandleProtocolNegotiation(message.data);
            return;
        case BinaryProtocol::MESSAGE_TYPE_CHUNK_DATA: {
            BinaryProtocol::BinaryReader reader(message.data.data(), message.data.size());
            ChunkData req;
            req.x = reader.ReadInt32();
            req.z = reader.ReadInt32();
            req.lod = reader.ReadUInt8();
            req.player_x = reader.ReadFloat();
            req.player_y = reader.ReadFloat();
            req.player_z = reader.ReadFloat();
            req.session_id = sessionId_;
            GameLogic::GetInstance().OnChunkData(req);
            break;
        }
        case BinaryProtocol::MESSAGE_TYPE_ERROR:
            Logger::Warn("Session {} received error from client", sessionId_);
            return;
        case BinaryProtocol::MESSAGE_TYPE_SUCCESS:
            return;
    }

    std::lock_guard<std::mutex> lock(binary_handlers_mutex_);
    auto it = binary_handlers_.find(message.header.message_type);
    if (it != binary_handlers_.end()) {
        try {
            it->second(message.header.message_type, message.data);
        } catch (const std::exception& e) {
            Logger::Error("Session {} error in binary handler {}: {}",
                          sessionId_, message.header.message_type, e.what());
            SendError(BinaryProtocol::MESSAGE_TYPE_ERROR,
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
        SendError(BinaryProtocol::MESSAGE_TYPE_ERROR,
                       "Unknown message type", 400);
    }
}

void BinarySession::Send(uint16_t message_type, const std::vector<uint8_t>& data) {
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
    if (compression_enabled_ && data.size() > 1024) {
        try {
            auto compressed = BinaryProtocol::CompressData(data);
            if (compressed.size() < data.size() * 0.9) {
                message.data = compressed;
                message.header.length = static_cast<uint32_t>(compressed.size());
                message.header.flags |= BinaryProtocol::FLAG_COMPRESSED;
            }
        } catch (const std::exception& e) {
            Logger::Warn("Session {} compression failed: {}", sessionId_, e.what());
        }
    }
    if (ssl_stream_) {
        message.header.flags |= BinaryProtocol::FLAG_ENCRYPTED;
    }
    message.header.checksum = BinaryProtocol::CalculateCRC32(
        message.data.data(), message.data.size());
    auto serialized = message.Serialize();
    network_monitor_.RecordPacketSent(message.header.sequence, serialized.size());
    std::lock_guard<std::mutex> lock(write_mutex_);
    write_queue_.push(serialized);
    if (write_queue_.size() == 1) {
        DoBinaryWrite();
    }
}

void BinarySession::Send(uint16_t message_type, const void* data, size_t length) {
    Send(message_type, std::vector<uint8_t>(
        static_cast<const uint8_t*>(data),
        static_cast<const uint8_t*>(data) + length
    ));
}

void BinarySession::SendRaw(const std::string& data) {
    if (!connected_ || closing_) {
        Logger::Warn("Session {} not connected, cannot send", sessionId_);
    }
    else
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        bool write_in_progress = !write_queue_.empty();
        std::vector<uint8_t> binary_data(data.begin(), data.end());
        write_queue_.push(binary_data);
        if (!write_in_progress) {
            DoBinaryWrite();
        }
    }
}

void BinarySession::SendWithAck(uint16_t message_type, const std::vector<uint8_t>& data) {
    uint32_t sequence = next_sequence_.load();
    {
        std::lock_guard<std::mutex> lock(ack_mutex_);
        pending_acks_[sequence] = std::chrono::steady_clock::now();
    }
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
        network_monitor_.RecordPacketSent(sequence, serialized.size());
        std::lock_guard<std::mutex> lock(write_mutex_);
        write_queue_.push(serialized);
        if (write_queue_.size() == 1) {
            DoBinaryWrite();
        }
        next_sequence_++;
}

void BinarySession::SendError(uint16_t message_type, const std::string& error_message, int code) {
    BinaryProtocol::BinaryWriter writer;
    writer.WriteUInt32(static_cast<uint32_t>(code));
    writer.WriteString(error_message);
    writer.WriteUInt64(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());

    Send(message_type, writer.GetBuffer());
}

void BinarySession::SendJson(const nlohmann::json& message)
{
    SendRaw(message.dump());
}


void BinarySession::SendBinaryWithAck(uint16_t message_type, const std::vector<uint8_t>& data) {
    SendWithAck(message_type, data);
}

void BinarySession::SendPing() {
    Send(BinaryProtocol::MESSAGE_TYPE_HEARTBEAT, {});
}

void BinarySession::SendPong() {
    Send(BinaryProtocol::MESSAGE_TYPE_HEARTBEAT, {});
}

void BinarySession::DoBinaryWrite() {
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

void BinarySession::SendAcknowledgment(uint32_t sequence) {
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

void BinarySession::ProcessAcknowledgment(uint32_t sequence) {
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

// =============== Protocol Negotiation ===============

void BinarySession::SendProtocolCapabilities() {
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
    Send(BinaryProtocol::MESSAGE_TYPE_PROTOCOL_NEGOTIATION, caps_data);
}

void BinarySession::HandleProtocolNegotiation(const std::vector<uint8_t>& data) {
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

        Send(BinaryProtocol::MESSAGE_TYPE_SUCCESS, writer.GetBuffer());

    } catch (const std::exception& e) {
        Logger::Error("Session {}: protocol negotiation failed: {}",
                     sessionId_, e.what());
        SendError(BinaryProtocol::MESSAGE_TYPE_ERROR,
                       "Protocol negotiation failed", 400);
    }
}

// =============== Heartbeat Management ===============

void BinarySession::StartHeartbeat() {
    if (!connected_ || closing_) return;

    // Set initial heartbeat time
    last_heartbeat_ = std::chrono::steady_clock::now();

    // Start heartbeat check
    CheckHeartbeat();
}

void BinarySession::CheckHeartbeat() {
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

void BinarySession::UpdateHeartbeat() {
    last_heartbeat_ = std::chrono::steady_clock::now();
}

// =============== Network Quality Monitoring ===============

void BinarySession::StartNetworkAdaptation() {
    auto self = shared_from_this();

    network_adaptation_timer_.expires_after(std::chrono::seconds(10));
    network_adaptation_timer_.async_wait([self](std::error_code ec) {
        if (ec) return;

        self->CheckNetworkConditions();
        self->StartNetworkAdaptation();
    });
}

void BinarySession::CheckNetworkConditions() {
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
            {"msg", "network_warning"},
            {"desc", "Unstable network connection detected"},
            {"metrics", {
                {"latency", metrics.average_latency_ms},
                {"packet_loss", metrics.packet_loss_percent},
                {"jitter", metrics.jitter_ms}
            }},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };
        SendJson(warning);
    }
}

void BinarySession::AdaptToNetworkConditions() {
    CheckNetworkConditions();
}

// =============== Binary Message Handlers ===============

void BinarySession::SetBinaryMessageHandler(uint16_t message_type, BinaryMessageHandler handler) {
    std::lock_guard<std::mutex> lock(binary_handlers_mutex_);
    binary_handlers_[message_type] = std::move(handler);
}

void BinarySession::SetDefaultBinaryMessageHandler(BinaryMessageHandler handler) {
    std::lock_guard<std::mutex> lock(binary_handlers_mutex_);
    default_binary_handler_ = std::move(handler);
}

void BinarySession::SetMessageHandler(std::function<void(const nlohmann::json&)> handler) {
    message_handler_ = std::move(handler);
}

void BinarySession::SetCloseHandler(std::function<void()> handler) {
    close_handler_ = std::move(handler);
}

// =============== Session Statistics ===============

SessionStats BinarySession::GetStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void BinarySession::ResetStats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = SessionStats{};
}

void BinarySession::RecordMessageReceived(size_t size) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.messages_received++;
    stats_.bytes_received += size;
    stats_.last_message_received = std::chrono::steady_clock::now();

    // Also record for network monitoring
    network_monitor_.RecordBytesReceived(size);
}

void BinarySession::RecordMessageSent(size_t size) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.messages_sent++;
    stats_.bytes_sent += size;
    stats_.last_message_sent = std::chrono::steady_clock::now();

    // Also record for network monitoring
    network_monitor_.RecordBytesSent(size);
}

// =============== Compression ===============

void BinarySession::SetCompressionEnabled(bool enabled) {
    compression_enabled_ = enabled;
}

bool BinarySession::IsCompressionEnabled() const {
    return compression_enabled_;
}

// =============== Rate Limiting ===============

void BinarySession::SetRateLimit(int messages_per_second, int burst_size) {
    std::lock_guard<std::mutex> lock(rate_limit_mutex_);
    rate_limit_.messages_per_second = messages_per_second;
    rate_limit_.burst_size = burst_size;
    rate_limit_.tokens = burst_size;
    rate_limit_.last_refill = std::chrono::steady_clock::now();
}

bool BinarySession::CheckRateLimit() {
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

void BinarySession::SetRateLimitEnabled(bool enabled) {
    rate_limit_enabled_ = enabled;
}

// =============== Session Groups ===============

void BinarySession::JoinGroup(const std::string& groupId) {
    std::lock_guard<std::mutex> lock(groups_mutex_);
    joined_groups_.insert(groupId);
}

void BinarySession::LeaveGroup(const std::string& groupId) {
    std::lock_guard<std::mutex> lock(groups_mutex_);
    joined_groups_.erase(groupId);
}

void BinarySession::LeaveAllGroups() {
    std::lock_guard<std::mutex> lock(groups_mutex_);
    joined_groups_.clear();
}

std::set<std::string> BinarySession::GetJoinedGroups() const {
    std::lock_guard<std::mutex> lock(groups_mutex_);
    return joined_groups_;
}

bool BinarySession::IsInGroup(const std::string& groupId) const {
    std::lock_guard<std::mutex> lock(groups_mutex_);
    return joined_groups_.find(groupId) != joined_groups_.end();
}

// =============== Authentication and Security ===============

void BinarySession::Authenticate(const std::string& authToken) {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    auth_token_ = authToken;
    authenticated_ = true;
    authentication_time_ = std::chrono::steady_clock::now();

    Logger::Info("Session {} authenticated", sessionId_);
}

void BinarySession::Deauthenticate() {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    auth_token_.clear();
    authenticated_ = false;
    player_id_ = 0;

    Logger::Info("Session {} deauthenticated", sessionId_);
}

bool BinarySession::IsAuthenticated() const {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    return authenticated_;
}

std::string BinarySession::GetAuthToken() const {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    return auth_token_;
}

void BinarySession::SetPlayerId(int64_t playerId) {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    player_id_ = playerId;

    Logger::Debug("Session {} assigned to player {}", sessionId_, playerId);
}

int64_t BinarySession::GetPlayerId() const {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    return player_id_;
}

// =============== Session Data Storage ===============

void BinarySession::SetData(const std::string& key, const nlohmann::json& value) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    session_data_[key] = value;
}

nlohmann::json BinarySession::GetData(const std::string& key, const nlohmann::json& defaultValue) const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    auto it = session_data_.find(key);
    if (it != session_data_.end()) {
        return it->second;
    }
    return defaultValue;
}

bool BinarySession::HasData(const std::string& key) const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return session_data_.find(key) != session_data_.end();
}

void BinarySession::RemoveData(const std::string& key) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    session_data_.erase(key);
}

void BinarySession::ClearData() {
    std::lock_guard<std::mutex> lock(data_mutex_);
    session_data_.clear();
}

nlohmann::json BinarySession::GetAllData() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    nlohmann::json result;
    for (const auto& [key, value] : session_data_) {
        result[key] = value;
    }
    return result;
}

// =============== Session Properties ===============

void BinarySession::SetProperty(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(properties_mutex_);
    properties_[key] = value;
}

std::string BinarySession::GetProperty(const std::string& key, const std::string& defaultValue) const {
    std::lock_guard<std::mutex> lock(properties_mutex_);
    auto it = properties_.find(key);
    if (it != properties_.end()) {
        return it->second;
    }
    return defaultValue;
}

std::map<std::string, std::string> BinarySession::GetAllProperties() const {
    std::lock_guard<std::mutex> lock(properties_mutex_);
    return properties_;
}

// =============== Metrics and Monitoring ===============

SessionMetrics BinarySession::GetMetrics() const {
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

void BinarySession::PrintMetrics() const {
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

std::string BinarySession::ToString() const {
    std::stringstream ss;
    ss << "BinarySession[" << sessionId_ << "] ";

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

uint64_t BinarySession::GetUptimeSeconds() const {
    if (!connected_) {
        return 0;
    }

    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        now - connected_time_);

    return uptime.count();
}

// =============== Connection Quality Monitoring ===============

void BinarySession::RecordLatency(uint64_t latencyMs) {
    network_monitor_.RecordLatencySample(latencyMs);
}

uint64_t BinarySession::GetAverageLatency() const {
    auto metrics = network_monitor_.GetMetrics();
    return metrics.average_latency_ms;
}

uint64_t BinarySession::GetMinLatency() const {
    auto metrics = network_monitor_.GetMetrics();
    return metrics.min_latency_ms;
}

uint64_t BinarySession::GetMaxLatency() const {
    auto metrics = network_monitor_.GetMetrics();
    return metrics.max_latency_ms;
}

std::vector<uint64_t> BinarySession::GetLatencySamples() const {
    // Note: NetworkQualityMonitor doesn't expose samples directly
    // In a full implementation, we would add this method
    return {};
}

// =============== Custom Event Handlers ===============

void BinarySession::SetCustomEventHandler(const std::string& eventName,
                                        std::function<void(const nlohmann::json&)> handler) {
    std::lock_guard<std::mutex> lock(event_handlers_mutex_);
    custom_event_handlers_[eventName] = handler;
}

void BinarySession::RemoveCustomEventHandler(const std::string& eventName) {
    std::lock_guard<std::mutex> lock(event_handlers_mutex_);
    custom_event_handlers_.erase(eventName);
}

void BinarySession::HandleCustomEvent(const std::string& eventName, const nlohmann::json& data) {
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

size_t BinarySession::GetPendingMessageCount() const {
    std::lock_guard<std::mutex> lock(write_mutex_);
    return write_queue_.size();
}

void BinarySession::ClearPendingMessages() {
    std::lock_guard<std::mutex> lock(write_mutex_);
    std::queue<std::vector<uint8_t>> empty;
    std::swap(write_queue_, empty);
}

bool BinarySession::IsWriteQueueFull() const {
    std::lock_guard<std::mutex> lock(write_mutex_);
    return write_queue_.size() >= max_write_queue_size_;
}

void BinarySession::SetMaxWriteQueueSize(size_t maxSize) {
    max_write_queue_size_ = maxSize;
}

// =============== World and Entity Methods ===============

void BinarySession::SendWorldChunkBinary(int chunkX, int chunkZ, const std::vector<uint8_t>& chunkData) {
    BinaryProtocol::BinaryWriter writer;
    writer.WriteInt32(chunkX);
    writer.WriteInt32(chunkZ);

    // Combine metadata with chunk data
    auto metadata = writer.GetBuffer();
    std::vector<uint8_t> combined_data(metadata.size() + chunkData.size());

    std::copy(metadata.begin(), metadata.end(), combined_data.begin());
    std::copy(chunkData.begin(), chunkData.end(), combined_data.begin() + metadata.size());

    Send(BinaryProtocol::MESSAGE_TYPE_CHUNK_DATA, combined_data);
}

void BinarySession::SendEntityUpdateBinary(uint64_t entityId, const std::vector<uint8_t>& entityData) {
    BinaryProtocol::BinaryWriter writer;
    writer.WriteUInt64(entityId);

    // Combine entity ID with entity data
    auto id_data = writer.GetBuffer();
    std::vector<uint8_t> combined_data(id_data.size() + entityData.size());

    std::copy(id_data.begin(), id_data.end(), combined_data.begin());
    std::copy(entityData.begin(), entityData.end(), combined_data.begin() + id_data.size());

    Send(BinaryProtocol::MESSAGE_TYPE_ENTITY_UPDATE, combined_data);
}

void BinarySession::SendEntitySpawnBinary(uint64_t entityId, const std::vector<uint8_t>& spawnData) {
    BinaryProtocol::BinaryWriter writer;
    writer.WriteUInt64(entityId);

    auto id_data = writer.GetBuffer();
    std::vector<uint8_t> combined_data(id_data.size() + spawnData.size());

    std::copy(id_data.begin(), id_data.end(), combined_data.begin());
    std::copy(spawnData.begin(), spawnData.end(), combined_data.begin() + id_data.size());

    Send(BinaryProtocol::MESSAGE_TYPE_ENTITY_SPAWN, combined_data);
}

void BinarySession::SendEntityDespawnBinary(uint64_t entityId) {
    BinaryProtocol::BinaryWriter writer;
    writer.WriteUInt64(entityId);

    Send(BinaryProtocol::MESSAGE_TYPE_ENTITY_DESPAWN, writer.GetBuffer());
}

// =============== Player State Synchronization ===============

void BinarySession::SyncPlayerStateBinary(const glm::vec3& position, const glm::vec3& rotation,
                                        const glm::vec3& velocity, uint32_t last_input_id) {
    BinaryProtocol::BinaryWriter writer;

    writer.WriteVector3(position);
    writer.WriteVector3(rotation);
    writer.WriteVector3(velocity);
    writer.WriteUInt32(last_input_id);
    writer.WriteUInt64(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());

    Send(BinaryProtocol::MESSAGE_TYPE_PLAYER_STATE, writer.GetBuffer());
}

void BinarySession::SendPositionCorrection(const glm::vec3& position, const glm::vec3& velocity) {
    BinaryProtocol::BinaryWriter writer;

    writer.WriteVector3(position);
    writer.WriteVector3(velocity);
    writer.WriteUInt64(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());

    Send(BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION_CORRECTION, writer.GetBuffer());
}

// =============== Private Helper Methods ===============

asio::ip::tcp::socket& BinarySession::GetSocket() {
    if (ssl_stream_) {
        return ssl_stream_->next_layer();
    }
    return socket_;
}

const asio::ip::tcp::socket& BinarySession::GetSocket() const {
    if (ssl_stream_) {
        return ssl_stream_->next_layer();
    }
    return socket_;
}

void BinarySession::HandleNetworkError(std::error_code ec) {
    if (ec == asio::error::eof || ec == asio::error::connection_reset) {
        Logger::Debug("Session {} disconnected: {}",
                      sessionId_, ec.message());
    } else if (ec != asio::error::operation_aborted) {
        Logger::Error("Session {} network error: {}",
                      sessionId_, ec.message());
    }
    Stop();
}

void BinarySession::HandleMessage(const std::string& message) {
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
        SendError(BinaryProtocol::MESSAGE_TYPE_ERROR, "Invalid JSON format", 400);

    } catch (const std::exception& e) {
        Logger::Error("Session {} message handling error: {}",
                      sessionId_, e.what());
    }
}

PredictionSystem& BinarySession::GetPredictionSystem() {
    return prediction_system_;
}

void BinarySession::SetPlayerStateHandler(std::function<void(const ClientInput&)> handler) {
    player_state_handler_ = std::move(handler);
}

void BinarySession::SetupDefaultHandlers() {
    SetBinaryMessageHandler(BinaryProtocol::MESSAGE_TYPE_PLAYER_STATE,
        [this](uint16_t type, const std::vector<uint8_t>& data) {
            (void)type;
            try {
                ClientInput input = ClientInput::Deserialize(data.data(), data.size());
                if (player_state_handler_) {
                    player_state_handler_(input);
                } else {
                    Logger::Warn("Session {}: no player state handler registered", sessionId_);
                }
            } catch (const std::exception& e) {
                Logger::Error("Session {}: failed to deserialize player state: {}", sessionId_, e.what());
            }
        });
}
