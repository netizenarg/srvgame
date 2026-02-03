// NetworkClient.cpp - MODIFIED AND ADDED LINES
#include "NetworkClient.hpp"
#include <android/log.h>

#define LOG_TAG "NetworkClient"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

NetworkClient::NetworkClient() 
    : socket_(ioContext_), timeoutTimer_(ioContext_) {
}

NetworkClient::~NetworkClient() {
    Disconnect();
}

bool NetworkClient::Connect(const std::string& host, int port) {
    try {
        asio::ip::tcp::resolver resolver(ioContext_);
        auto endpoints = resolver.resolve(host, std::to_string(port));
        
        connected_ = false;
        running_ = true;
        
        // Start IO context thread
        ioThread_ = std::thread(&NetworkClient::RunIOContext, this);
        
        // Start connection
        asio::post(ioContext_, [this, endpoints]() {
            DoConnect(endpoints);
        });
        
        // Wait for connection with timeout
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        while (!connected_ && 
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start).count() < timeoutMs_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        if (connected_) {
            LOGI("Connected to %s:%d", host.c_str(), port);
            
            // Initialize WebSocket if needed
            if (protocol_ == NetworkProtocol::WEBSOCKET) {
                InitializeWebSocket();
            }
            
            return true;
        } else {
            LOGE("Connection timeout");
            return false;
        }
    }
    catch (const std::exception& e) {
        LOGE("Connection error: %s", e.what());
        return false;
    }
}

void NetworkClient::Disconnect() {
    running_ = false;
    connected_ = false;
    
    try {
        if (readBuffer_.size() > 0) {
            readBuffer_.consume(readBuffer_.size());
        }
    } catch (const std::exception& e) {
        LOGE("Clear the read buffer error: %s", e.what());
    }
    
    if (socket_.is_open()) {
        asio::post(ioContext_, [this]() {
            socket_.close();
        });
    }
    
    if (webSocketClient_) {
        webSocketClient_->Close(1000, "Client disconnect");
    }
    
    ioContext_.stop();
    if (ioThread_.joinable()) {
        ioThread_.join();
    }
    
    // Clear queues
    {
        std::lock_guard<std::mutex> lock(jsonSendMutex_);
        std::queue<std::string> empty;
        jsonSendQueue_.swap(empty);
    }
    {
        std::lock_guard<std::mutex> lock(jsonReceiveMutex_);
        std::queue<nlohmann::json> empty;
        jsonReceiveQueue_.swap(empty);
    }
    {
        std::lock_guard<std::mutex> lock(binarySendMutex_);
        std::queue<BinaryProtocol::BinaryMessage> empty;
        binarySendQueue_.swap(empty);
    }
    {
        std::lock_guard<std::mutex> lock(binaryReceiveMutex_);
        std::queue<BinaryProtocol::BinaryMessage> empty;
        binaryReceiveQueue_.swap(empty);
    }
}

void NetworkClient::SetProtocol(NetworkProtocol protocol) {
    protocol_ = protocol;
}

void NetworkClient::SendJson(const nlohmann::json& message) {
    if (!connected_) return;
    
    std::string data = message.dump() + "\n";
    
    std::lock_guard<std::mutex> lock(jsonSendMutex_);
    jsonSendQueue_.push(data);
    
    // Trigger write if queue was empty
    if (jsonSendQueue_.size() == 1) {
        asio::post(ioContext_, [this]() {
            DoWriteJson(jsonSendQueue_.front());
        });
    }
}

void NetworkClient::SendBinary(const BinaryProtocol::BinaryMessage& message) {
    if (!connected_) return;
    
    std::lock_guard<std::mutex> lock(binarySendMutex_);
    binarySendQueue_.push(message);
    
    if (binarySendQueue_.size() == 1) {
        asio::post(ioContext_, [this]() {
            DoWriteBinary(binarySendQueue_.front());
        });
    }
}

void NetworkClient::SendWebSocket(const nlohmann::json& message) {
    if (!connected_ || !webSocketClient_) return;
    
    webSocketClient_->SendJson(message);
}

std::vector<nlohmann::json> NetworkClient::ReceiveJson() {
    std::vector<nlohmann::json> messages;
    
    std::lock_guard<std::mutex> lock(jsonReceiveMutex_);
    while (!jsonReceiveQueue_.empty()) {
        messages.push_back(std::move(jsonReceiveQueue_.front()));
        jsonReceiveQueue_.pop();
    }
    
    return messages;
}

std::vector<BinaryProtocol::BinaryMessage> NetworkClient::ReceiveBinary() {
    std::vector<BinaryProtocol::BinaryMessage> messages;
    
    std::lock_guard<std::mutex> lock(binaryReceiveMutex_);
    while (!binaryReceiveQueue_.empty()) {
        messages.push_back(std::move(binaryReceiveQueue_.front()));
        binaryReceiveQueue_.pop();
    }
    
    return messages;
}

std::vector<nlohmann::json> NetworkClient::ReceiveWebSocket() {
    std::vector<nlohmann::json> messages;
    
    if (webSocketClient_) {
        auto rawMessages = webSocketClient_->Receive();
        for (const auto& msg : rawMessages) {
            try {
                messages.push_back(nlohmann::json::parse(msg.GetText()));
            } catch (...) {
                LOGE("Failed to parse WebSocket message as JSON");
            }
        }
    }
    
    return messages;
}

void NetworkClient::RunIOContext() {
    try {
        ioContext_.run();
    }
    catch (const std::exception& e) {
        LOGE("IO context error: %s", e.what());
    }
}

void NetworkClient::DoConnect(const asio::ip::tcp::resolver::results_type& endpoints) {
    try {
        asio::async_connect(socket_, endpoints,
            [this](std::error_code ec, asio::ip::tcp::endpoint) {
                if (!ec) {
                    connected_ = true;
                    LOGI("Async connection successful");
                    
                    // Start reading based on protocol
                    switch (protocol_) {
                        case NetworkProtocol::JSON_TEXT:
                            DoReadJson();
                            break;
                        case NetworkProtocol::BINARY:
                            DoReadBinary();
                            break;
                        case NetworkProtocol::WEBSOCKET:
                            // WebSocket handshake will be handled by WebSocketClient
                            break;
                    }
                } else {
                    LOGE("Async connection failed: %s", ec.message().c_str());
                }
            });
    }
    catch (const std::exception& e) {
        LOGE("DoConnect error: %s", e.what());
    }
}

void NetworkClient::DoReadJson() {
    if (!connected_) return;
    
    asio::async_read_until(socket_, readBuffer_, '\n',
        [this](std::error_code ec, size_t length) {
            if (!ec) {
                std::istream is(&readBuffer_);
                std::string line;
                std::getline(is, line);
                
                try {
                    auto json = nlohmann::json::parse(line);
                    
                    std::lock_guard<std::mutex> lock(jsonReceiveMutex_);
                    jsonReceiveQueue_.push(json);
                }
                catch (const std::exception& e) {
                    LOGE("JSON parse error: %s", e.what());
                }
                
                // Continue reading
                DoReadJson();
            }
            else {
                if (ec != asio::error::operation_aborted) {
                    LOGE("Read error: %s", ec.message().c_str());
                    Disconnect();
                }
            }
        });
}

void NetworkClient::DoWriteJson(const std::string& message) {
    if (!connected_) return;
    
    asio::async_write(socket_, asio::buffer(message),
        [this](std::error_code ec, size_t /*length*/) {
            if (!ec) {
                std::lock_guard<std::mutex> lock(jsonSendMutex_);
                jsonSendQueue_.pop();
                
                // Write next message if available
                if (!jsonSendQueue_.empty()) {
                    DoWriteJson(jsonSendQueue_.front());
                }
            }
            else {
                LOGE("Write error: %s", ec.message().c_str());
                Disconnect();
            }
        });
}

void NetworkClient::DoReadBinary() {
    if (!connected_) return;
    
    // Read header first (32 bytes)
    asio::async_read(socket_, asio::buffer(readBuffer_.prepare(32)),
        [this](std::error_code ec, size_t bytes_transferred) {
            if (!ec && bytes_transferred == 32) {
                readBuffer_.commit(bytes_transferred);
                
                // Parse header
                std::istream is(&readBuffer_);
                BinaryProtocol::NetworkHeader header;
                
                // FIX: Check buffer has enough data before reading
                if (readBuffer_.size() < sizeof(header)) {
                    LOGE("Buffer underflow reading header");
                    Disconnect();
                    return;
                }
                
                is.read(reinterpret_cast<char*>(&header), sizeof(header));
                
                // FIX: Validate header length to prevent excessive allocation
                const size_t MAX_MESSAGE_SIZE = 10 * 1024 * 1024; // 10MB limit
                if (header.length > MAX_MESSAGE_SIZE) {
                    LOGE("Message size too large: %u", header.length);
                    Disconnect();
                    return;
                }
                
                if (header.length == 0) {
                    // Empty payload - process and continue reading
                    BinaryProtocol::BinaryMessage msg;
                    msg.header = header;
                    
                    std::lock_guard<std::mutex> lock(binaryReceiveMutex_);
                    binaryReceiveQueue_.push(msg);
                    
                    // Continue reading
                    DoReadBinary();
                    return;
                }
                
                // Read payload with size validation
                asio::async_read(socket_, asio::buffer(readBuffer_.prepare(header.length)),
                    [this, header](std::error_code ec, size_t payload_length) {
                        if (!ec) {
                            // FIX: Validate we read expected amount
                            if (payload_length != header.length) {
                                LOGE("Payload size mismatch: expected %u, got %zu", 
                                     header.length, payload_length);
                                Disconnect();
                                return;
                            }
                            
                            readBuffer_.commit(payload_length);
                            
                            // FIX: Verify buffer has enough data
                            if (readBuffer_.size() < payload_length) {
                                LOGE("Buffer underflow reading payload");
                                Disconnect();
                                return;
                            }
                            
                            std::istream is(&readBuffer_);
                            std::vector<uint8_t> data(payload_length);
                            
                            // FIX: Check read operation success
                            is.read(reinterpret_cast<char*>(data.data()), payload_length);
                            if (is.gcount() != static_cast<std::streamsize>(payload_length)) {
                                LOGE("Failed to read entire payload from buffer");
                                Disconnect();
                                return;
                            }
                            
                            BinaryProtocol::BinaryMessage msg;
                            msg.header = header;
                            msg.data = std::move(data);
                            
                            std::lock_guard<std::mutex> lock(binaryReceiveMutex_);
                            binaryReceiveQueue_.push(msg);
                            
                            // Continue reading - BUT with safety check
                            // to prevent infinite recursion in case of rapid data
                            if (connected_ && running_) {
                                DoReadBinary();
                            }
                        }
                        else {
                            if (ec != asio::error::operation_aborted) {
                                LOGE("Binary payload read error: %s", ec.message().c_str());
                                Disconnect();
                            }
                        }
                    });
            }
            else {
                if (ec && ec != asio::error::operation_aborted) {
                    LOGE("Binary header read error: %s", ec.message().c_str());
                    Disconnect();
                }
                // FIX: Added else-if for partial read
                else if (bytes_transferred > 0 && bytes_transferred < 32) {
                    LOGE("Incomplete header read: %zu bytes", bytes_transferred);
                    Disconnect();
                }
            }
        });
}

void NetworkClient::DoWriteBinary(const BinaryProtocol::BinaryMessage& message) {
    if (!connected_) return;
    
    // FIX: Validate message size before serialization
    const size_t MAX_MESSAGE_SIZE = 10 * 1024 * 1024; // 10MB limit
    if (message.data.size() > MAX_MESSAGE_SIZE) {
        LOGE("Message size too large for sending: %zu bytes", message.data.size());
        return;
    }
    
    // Serialize message
    auto serialized = message.Serialize();
    
    // FIX: Validate serialized size
    if (serialized.size() > MAX_MESSAGE_SIZE + sizeof(BinaryProtocol::NetworkHeader)) {
        LOGE("Serialized message size too large: %zu bytes", serialized.size());
        return;
    }
    
    asio::async_write(socket_, asio::buffer(serialized),
        [this](std::error_code ec, size_t /*length*/) {
            if (!ec) {
                std::lock_guard<std::mutex> lock(binarySendMutex_);
                binarySendQueue_.pop();
                
                // Write next message if available
                if (!binarySendQueue_.empty()) {
                    DoWriteBinary(binarySendQueue_.front());
                }
            }
            else {
                LOGE("Binary write error: %s", ec.message().c_str());
                Disconnect();
            }
        });
}

void NetworkClient::InitializeWebSocket() {
    webSocketClient_ = std::make_unique<WebSocketProtocol::WebSocketClient>(ioContext_);
    
    // Set up handlers
    webSocketClient_->SetMessageHandler([this](const WebSocketProtocol::WebSocketMessage& msg) {
        std::lock_guard<std::mutex> lock(jsonReceiveMutex_);
        try {
            jsonReceiveQueue_.push(msg.ToJson());
        } catch (...) {
            LOGE("Failed to parse WebSocket message");
        }
    });
    
    // Connect WebSocket
    // Note: WebSocket handshake requires upgrade from HTTP
    // This is simplified - in reality need proper WebSocket handshake
}
