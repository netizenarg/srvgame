#include <android/log.h>
#include "WebSocketNetworkClient.hpp"

#define LOG_TAG "WebSocketNetworkClient"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

WebSocketNetworkClient::WebSocketNetworkClient() 
    : client_(std::make_unique<WebSocketProtocol::WebSocketClient>(ioContext_)) {
}

WebSocketNetworkClient::~WebSocketNetworkClient() {
    Disconnect();
}

bool WebSocketNetworkClient::Connect(const std::string& host, int port, const std::string& path) {
    try {
        running_ = true;
        
        // Start IO context thread
        ioThread_ = std::thread([this]() { Run(); });
        
        // Setup handlers
        client_->SetMessageHandler([this](const WebSocketProtocol::WebSocketMessage& msg) {
            HandleMessage(msg);
        });
        
        client_->SetErrorHandler([this](const std::error_code& ec) {
            HandleError(ec);
        });
        
        // Connect
        client_->Connect(host, port, path);
        
        // Wait for connection
        for (int i = 0; i < 50 && !connected_; ++i) { // 5 second timeout
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        return connected_;
    }
    catch (const std::exception& e) {
        LOGE("WebSocket connection error: %s", e.what());
        return false;
    }
}

void WebSocketNetworkClient::Disconnect() {
    running_ = false;
    connected_ = false;
    
    if (client_) {
        client_->Close(1000, "Client disconnect");
    }
    
    ioContext_.stop();
    if (ioThread_.joinable()) {
        ioThread_.join();
    }
}

bool WebSocketNetworkClient::IsConnected() const {
    return connected_ && client_ && client_->IsOpen();
}

void WebSocketNetworkClient::Send(const nlohmann::json& message) {
    if (!IsConnected() || !client_) return;
    
    client_->SendJson(message);
}

void WebSocketNetworkClient::SendBinary(const std::vector<uint8_t>& data) {
    if (!IsConnected() || !client_) return;
    
    client_->SendBinary(data);
}

std::vector<nlohmann::json> WebSocketNetworkClient::Receive() {
    std::vector<nlohmann::json> messages;
    
    std::lock_guard<std::mutex> lock(queueMutex_);
    while (!messageQueue_.empty()) {
        messages.push_back(std::move(messageQueue_.front()));
        messageQueue_.pop();
    }
    
    return messages;
}

std::vector<std::vector<uint8_t>> WebSocketNetworkClient::ReceiveBinary() {
    std::vector<std::vector<uint8_t>> messages;
    
    std::lock_guard<std::mutex> lock(binaryQueueMutex_);
    while (!binaryQueue_.empty()) {
        messages.push_back(std::move(binaryQueue_.front()));
        binaryQueue_.pop();
    }
    
    return messages;
}

void WebSocketNetworkClient::SetMessageHandler(std::function<void(const nlohmann::json&)> handler) {
    messageHandler_ = std::move(handler);
}

void WebSocketNetworkClient::SetBinaryHandler(std::function<void(const std::vector<uint8_t>&)> handler) {
    binaryHandler_ = std::move(handler);
}

void WebSocketNetworkClient::SetErrorHandler(std::function<void(const std::string&)> handler) {
    errorHandler_ = std::move(handler);
}

void WebSocketNetworkClient::Run() {
    try {
        ioContext_.run();
    }
    catch (const std::exception& e) {
        LOGE("WebSocket IO context error: %s", e.what());
    }
}

void WebSocketNetworkClient::HandleMessage(const WebSocketProtocol::WebSocketMessage& msg) {
    if (msg.opcode == WebSocketProtocol::OP_TEXT) {
        try {
            auto json = nlohmann::json::parse(msg.GetText());
            
            if (messageHandler_) {
                messageHandler_(json);
            } else {
                std::lock_guard<std::mutex> lock(queueMutex_);
                messageQueue_.push(json);
            }
        }
        catch (const std::exception& e) {
            LOGE("Failed to parse WebSocket message: %s", e.what());
        }
    }
    else if (msg.opcode == WebSocketProtocol::OP_BINARY) {
        if (binaryHandler_) {
            binaryHandler_(msg.data);
        } else {
            std::lock_guard<std::mutex> lock(binaryQueueMutex_);
            binaryQueue_.push(msg.data);
        }
    }
}

void WebSocketNetworkClient::HandleError(const std::error_code& ec) {
    connected_ = false;
    
    if (errorHandler_) {
        errorHandler_(ec.message());
    } else {
        LOGE("WebSocket error: %s", ec.message().c_str());
    }
}
