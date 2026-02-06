#include <iostream>
#include <asio/connect.hpp>
#include <asio/write.hpp>
#include <asio/read_until.hpp>
#include <chrono>

#include "../include/client/NetworkClient.h"

NetworkClient::NetworkClient(Protocol protocol)
    : socket_(ioContext_),
      connected_(false),
      running_(false),
      preferredProtocol_(protocol),
      activeProtocol_(Protocol::TCP),
      wsClient_(nullptr) {
    
    stateManager_ = std::make_unique<ConnectionStateManager>();
    
    // Initialize WebSocket client if needed
    if (protocol == Protocol::WebSocket || protocol == Protocol::AutoDetect) {
        InitializeWebSocketClient();
    }
}

NetworkClient::~NetworkClient() {
    Disconnect();
}

void NetworkClient::InitializeWebSocketClient() {
    wsClient_ = std::make_unique<WebSocketClient>();
    
    // Setup WebSocket event handlers
    SetupWebSocketHandlers();
}

bool NetworkClient::Connect(const std::string& host, uint16_t port) {
    if (connected_) {
        Disconnect();
    }
    
    serverHost_ = host;
    serverPort_ = port;
    
    // Reset statistics
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_ = NetworkStats();
    }
    
    stateManager_->TransitionTo(ConnectionState::Connecting);
    
    // Protocol selection logic
    if (preferredProtocol_ == Protocol::WebSocket && wsClient_) {
        activeProtocol_ = Protocol::WebSocket;
        return ConnectWebSocket(host, port);
    }
    else if (preferredProtocol_ == Protocol::AutoDetect && wsClient_) {
        // Try WebSocket first, then fallback to TCP
        protocolNegotiating_ = true;
        activeProtocol_ = Protocol::WebSocket;
        
        if (ConnectWebSocket(host, port)) {
            // Set a timeout for WebSocket handshake
            std::thread([this, host, port]() {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                if (protocolNegotiating_) {
                    std::cout << "WebSocket handshake timeout, falling back to TCP" << std::endl;
                    Disconnect();
                    activeProtocol_ = Protocol::TCP;
                    protocolNegotiating_ = false;
                    
                    // Try TCP connection
                    Connect(host, port);
                }
            }).detach();
            return true;
        }
        return false;
    }
    else {
        // TCP connection
        activeProtocol_ = Protocol::TCP;
        
        try {
            asio::ip::tcp::resolver resolver(ioContext_);
            auto endpoints = resolver.resolve(host, std::to_string(port));
            
            // Start connection attempt
            DoConnect(endpoints);
            
            // Start IO thread
            running_ = true;
            ioThread_ = std::thread(&NetworkClient::RunIOContext, this);
            
            // Wait for connection to be established
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            return connected_;
        }
        catch (const std::exception& e) {
            std::cerr << "TCP Connection failed: " << e.what() << std::endl;
            stateManager_->TransitionTo(ConnectionState::Error, ConnectionError::NetworkUnavailable);
            return false;
        }
    }
}

bool NetworkClient::ConnectWebSocket(const std::string& host, uint16_t port) {
    if (!wsClient_) {
        InitializeWebSocketClient();
    }
    
    std::string wsUrl = "ws://" + host + ":" + std::to_string(port);
    std::cout << "Attempting WebSocket connection to: " << wsUrl << std::endl;
    
    return wsClient_->Connect(wsUrl, "game-protocol");
}

void NetworkClient::SetupWebSocketHandlers() {
    if (!wsClient_) return;
    
    // Register common message handlers
    wsClient_->RegisterHandler("heartbeat", [this](const nlohmann::json& msg) {
        HandleHeartbeat(msg);
    });
    
    wsClient_->RegisterHandler("ack", [this](const nlohmann::json& msg) {
        if (msg.contains("sequence")) {
            HandleAck(msg["sequence"].get<uint32_t>());
        }
    });
    
    // Wildcard handler for all other messages
    wsClient_->RegisterHandler("*", [this](const nlohmann::json& msg) {
        ForwardToHandlers(msg);
    });
    
    // Set connection callbacks
    wsClient_->SetConnectionCallback([this](bool success, ConnectionError error) {
        OnWebSocketConnected(success, error);
    });
}

void NetworkClient::OnWebSocketConnected(bool success, ConnectionError error) {
    protocolNegotiating_ = false;
    
    if (success) {
        connected_ = true;
        stateManager_->TransitionTo(ConnectionState::Connected);
        
        // Update stats
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.activeProtocol = Protocol::WebSocket;
        
        std::cout << "WebSocket connection established" << std::endl;
        
        // Call connection handler
        std::lock_guard<std::mutex> lock2(handlersMutex_);
        auto it = messageHandlers_.find("connected");
        if (it != messageHandlers_.end()) {
            it->second(nlohmann::json{{"type", "connected"}});
        }
    } else {
        stateManager_->TransitionTo(ConnectionState::Error, error);
        
        // Try TCP fallback if we were in AutoDetect mode
        if (preferredProtocol_ == Protocol::AutoDetect && error == ConnectionError::ProtocolError) {
            std::cout << "WebSocket failed, attempting TCP fallback..." << std::endl;
            Disconnect();
            activeProtocol_ = Protocol::TCP;
            Connect(serverHost_, serverPort_);
        }
    }
}

void NetworkClient::Disconnect() {
    if (!connected_ && stateManager_->GetState() != ConnectionState::Connecting) return;
    
    stateManager_->TransitionTo(ConnectionState::Disconnecting);
    
    if (activeProtocol_ == Protocol::WebSocket && wsClient_) {
        wsClient_->Disconnect();
    } else {
        // TCP disconnect
        running_ = false;
        connected_ = false;
        
        try {
            // Cancel all asynchronous operations
            socket_.cancel();
            
            // Close socket
            if (socket_.is_open()) {
                socket_.close();
            }
            
            // Stop IO context
            ioContext_.stop();
            
            // Wait for thread to finish
            if (ioThread_.joinable()) {
                ioThread_.join();
            }
            
            // Reset IO context
            ioContext_.restart();
        }
        catch (const std::exception& e) {
            std::cerr << "Disconnect error: " << e.what() << std::endl;
        }
    }
    
    stateManager_->TransitionTo(ConnectionState::Disconnected);
    
    // Call disconnection handler
    std::lock_guard<std::mutex> lock(handlersMutex_);
    auto it = messageHandlers_.find("disconnected");
    if (it != messageHandlers_.end()) {
        it->second(nlohmann::json{{"type", "disconnected"}});
    }
}

bool NetworkClient::IsConnected() const {
    if (activeProtocol_ == Protocol::WebSocket && wsClient_) {
        return wsClient_->IsConnected();
    }
    return connected_ && socket_.is_open();
}

void NetworkClient::Send(const nlohmann::json& message) {
    if (!IsConnected()) return;
    
    if (activeProtocol_ == Protocol::WebSocket && wsClient_) {
        wsClient_->Send(message);
        
        // Update stats
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.messagesSent++;
        stats_.totalBytesSent += message.dump().size();
    } else {
        // TCP send
        std::string data = message.dump() + "\n";
        SendRaw(data);
    }
}

void NetworkClient::SendRaw(const std::string& data) {
    if (!connected_) return;
    
    std::lock_guard<std::mutex> lock(writeMutex_);
    bool writeInProgress = !writeQueue_.empty();
    writeQueue_.push(data);
    
    if (!writeInProgress) {
        DoWrite();
    }
    
    // Update stats
    std::lock_guard<std::mutex> lock2(statsMutex_);
    stats_.messagesSent++;
    stats_.totalBytesSent += data.size();
}

void NetworkClient::RegisterHandler(const std::string& messageType, MessageHandler handler) {
    std::lock_guard<std::mutex> lock(handlersMutex_);
    messageHandlers_[messageType] = handler;
    
    // Also register with WebSocket client if active
    if (activeProtocol_ == Protocol::WebSocket && wsClient_) {
        wsClient_->RegisterHandler(messageType, handler);
    }
}

void NetworkClient::UnregisterHandler(const std::string& messageType) {
    std::lock_guard<std::mutex> lock(handlersMutex_);
    messageHandlers_.erase(messageType);
    
    // Also unregister from WebSocket client if active
    if (activeProtocol_ == Protocol::WebSocket && wsClient_) {
        wsClient_->UnregisterHandler(messageType);
    }
}

void NetworkClient::ForwardToHandlers(const nlohmann::json& message) {
    try {
        // Extract message type
        std::string msgType = message.value("type", "unknown");
        
        // Call appropriate handler
        std::lock_guard<std::mutex> lock(handlersMutex_);
        auto it = messageHandlers_.find(msgType);
        if (it != messageHandlers_.end()) {
            it->second(message);
        }
        else {
            // Try wildcard handler
            auto wildcardIt = messageHandlers_.find("*");
            if (wildcardIt != messageHandlers_.end()) {
                wildcardIt->second(message);
            }
            else {
                std::cout << "Unhandled message type: " << msgType << std::endl;
            }
        }
        
        // Update stats
        std::lock_guard<std::mutex> lock2(statsMutex_);
        stats_.messagesReceived++;
        stats_.totalBytesReceived += message.dump().size();
    }
    catch (const nlohmann::json::parse_error& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Message handling error: " << e.what() << std::endl;
    }
}

void NetworkClient::RunIOContext() {
    while (running_) {
        try {
            ioContext_.run();
            // If we get here, io_context stopped
            break;
        }
        catch (const std::exception& e) {
            std::cerr << "IO Context error: " << e.what() << std::endl;
        }
    }
}

void NetworkClient::DoConnect(const asio::ip::tcp::resolver::results_type& endpoints) {
    asio::async_connect(socket_, endpoints,
        [this](std::error_code ec, asio::ip::tcp::endpoint) {
            if (!ec) {
                connected_ = true;
                std::cout << "TCP Connected to server" << std::endl;
                
                // Update stats
                std::lock_guard<std::mutex> lock(statsMutex_);
                stats_.activeProtocol = Protocol::TCP;
                
                stateManager_->TransitionTo(ConnectionState::Connected);
                
                // Start reading
                DoRead();
                
                // Call connection handler
                std::lock_guard<std::mutex> lock2(handlersMutex_);
                auto it = messageHandlers_.find("connected");
                if (it != messageHandlers_.end()) {
                    it->second(nlohmann::json{{"type", "connected"}});
                }
            }
            else {
                std::cerr << "TCP Connection failed: " << ec.message() << std::endl;
                connected_ = false;
                stateManager_->TransitionTo(ConnectionState::Error, ConnectionError::NetworkUnavailable);
            }
        });
}

void NetworkClient::DoRead() {
    asio::async_read_until(socket_, readBuffer_, '\n',
        [this](std::error_code ec, std::size_t length) {
            if (!ec) {
                std::istream is(&readBuffer_);
                std::string message;
                std::getline(is, message);
                
                // Handle the message
                HandleMessage(message);
                
                // Continue reading
                DoRead();
            }
            else {
                if (ec != asio::error::operation_aborted) {
                    std::cerr << "TCP Read error: " << ec.message() << std::endl;
                    Disconnect();
                    
                    // Call disconnection handler
                    std::lock_guard<std::mutex> lock(handlersMutex_);
                    auto it = messageHandlers_.find("disconnected");
                    if (it != messageHandlers_.end()) {
                        it->second(nlohmann::json{{"type", "disconnected"}});
                    }
                }
            }
        });
}

void NetworkClient::DoWrite() {
    if (!connected_) return;
    
    std::lock_guard<std::mutex> lock(writeMutex_);
    if (writeQueue_.empty()) return;
    
    std::string data = writeQueue_.front();
    
    asio::async_write(socket_, asio::buffer(data),
        [this](std::error_code ec, std::size_t /*length*/) {
            if (!ec) {
                std::lock_guard<std::mutex> lock(writeMutex_);
                writeQueue_.pop();
                
                // Write next message if any
                if (!writeQueue_.empty()) {
                    DoWrite();
                }
            }
            else {
                std::cerr << "TCP Write error: " << ec.message() << std::endl;
                Disconnect();
            }
        });
}

void NetworkClient::HandleMessage(const std::string& message) {
    try {
        nlohmann::json jsonMsg = nlohmann::json::parse(message);
        ForwardToHandlers(jsonMsg);
    }
    catch (const nlohmann::json::parse_error& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Message handling error: " << e.what() << std::endl;
    }
}

NetworkClient::NetworkStats NetworkClient::GetStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    NetworkStats result = stats_;
    
    // Add WebSocket-specific stats if active
    if (activeProtocol_ == Protocol::WebSocket && wsClient_) {
        auto wsStats = wsClient_->GetConnectionMetrics();
        result.averageLatency = wsStats.latency;
        result.packetLoss = wsStats.packetLoss;
    }
    
    return result;
}

ConnectionState NetworkClient::GetConnectionState() const {
    return stateManager_->GetState();
}

ConnectionMetrics NetworkClient::GetConnectionMetrics() const {
    if (activeProtocol_ == Protocol::WebSocket && wsClient_) {
        return wsClient_->GetConnectionMetrics();
    }
    return stateManager_->GetMetrics();
}

// Message builder methods (unchanged from original implementation)
nlohmann::json NetworkClient::BuildLoginMessage(const std::string& username, const std::string& password) {
    return {
        {"type", "login"},
        {"username", username},
        {"password", password},
        {"version", "1.0.0"},
        {"platform", "desktop"}
    };
}

nlohmann::json NetworkClient::BuildMoveMessage(const glm::vec3& position, const glm::vec3& rotation) {
    return {
        {"type", "move"},
        {"position", {
            {"x", position.x},
            {"y", position.y},
            {"z", position.z}
        }},
        {"rotation", {
            {"x", rotation.x},
            {"y", rotation.y},
            {"z", rotation.z}
        }},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };
}

nlohmann::json NetworkClient::BuildChatMessage(const std::string& message) {
    return {
        {"type", "chat"},
        {"message", message},
        {"channel", "global"}
    };
}

nlohmann::json NetworkClient::BuildInteractionMessage(uint64_t entityId, const std::string& action) {
    return {
        {"type", "interact"},
        {"entity_id", entityId},
        {"action", action}
    };
}

nlohmann::json NetworkClient::BuildInventoryAction(const std::string& itemId, int quantity, const std::string& action) {
    return {
        {"type", "inventory"},
        {"item_id", itemId},
        {"quantity", quantity},
        {"action", action}
    };
}

void NetworkClient::HandleConnectionResult(bool success, ConnectionError error) {
    if (success) {
        stateManager_->TransitionTo(ConnectionState::Connected);
        stateManager_->RecordConnectAttempt();
    } else {
        stateManager_->TransitionTo(ConnectionState::Error, error);

        // Check if we should reconnect
        if (stateManager_->ShouldAttemptReconnect()) {
            auto delay = stateManager_->GetNextReconnectDelay();
            // Schedule reconnection after delay
        }
    }
}

// Additional TCP methods from original implementation
void NetworkClient::EnableHeartbeat(bool enable, uint32_t intervalMs) {
    config_.enableHeartbeat = enable;
    config_.heartbeatInterval = intervalMs;
}

void NetworkClient::SetKeepAlive(bool enable, uint32_t idleTime, uint32_t interval) {
    // TCP keep-alive settings
}

bool NetworkClient::SwitchProtocol(Protocol newProtocol) {
    if (IsConnected()) {
        std::cout << "Cannot switch protocol while connected" << std::endl;
        return false;
    }
    
    preferredProtocol_ = newProtocol;
    
    // Re-initialize WebSocket client if needed
    if ((newProtocol == Protocol::WebSocket || newProtocol == Protocol::AutoDetect) && !wsClient_) {
        InitializeWebSocketClient();
    }
    
    return true;
}

bool NetworkClient::ConnectAsync(const std::string& host, uint16_t port, ConnectionCallback callback) {
    // Asynchronous connection implementation
    // This would typically run connection in a separate thread
    std::thread([this, host, port, callback]() {
        bool success = Connect(host, port);
        ConnectionError error = success ? ConnectionError::None : ConnectionError::Unknown;
        
        if (callback) {
            callback(success, error);
        }
    }).detach();
    
    return true;
}