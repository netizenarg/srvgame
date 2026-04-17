#include "network/WebSocketSession.hpp"

std::atomic<uint64_t> WebSocketSession::nextSessionId_{1};

WebSocketSession::WebSocketSession(WebSocketProtocol::WebSocketConnection::Pointer wsConn)
    : wsConn_(std::move(wsConn)), sessionId_(nextSessionId_++) {
    wsConn_->SetMessageHandler([this](const WebSocketProtocol::WebSocketMessage& msg) {
        OnMessage(msg);
    });
    wsConn_->SetCloseHandler([this](uint16_t code, const std::string& reason) {
        OnClose(code, reason);
    });
}

WebSocketSession::~WebSocketSession() = default;

void WebSocketSession::Start() { wsConn_->Start(); }
void WebSocketSession::Stop() { wsConn_->Close(); }
void WebSocketSession::Disconnect() { wsConn_->Close(1000, "Disconnect"); }
bool WebSocketSession::IsConnected() const { return wsConn_->IsOpen(); }
uint64_t WebSocketSession::GetSessionId() const { return sessionId_; }

void WebSocketSession::Send(const nlohmann::json& message) {
    wsConn_->SendJson(message);
}

void WebSocketSession::SendRaw(const std::string& data) {
    wsConn_->SendText(data);
}

void WebSocketSession::SendBinary(uint16_t /*message_type*/, const std::vector<uint8_t>& data) {
    wsConn_->SendBinary(data);
}

void WebSocketSession::SetMessageHandler(MessageHandler handler) {
    messageHandler_ = std::move(handler);
}

void WebSocketSession::SetCloseHandler(CloseHandler handler) {
    closeHandler_ = std::move(handler);
}

void WebSocketSession::Authenticate(const std::string& authToken) {
    authToken_ = authToken;
    authenticated_ = true;
    // Optionally validate token
}

bool WebSocketSession::IsAuthenticated() const { return authenticated_; }
void WebSocketSession::SetPlayerId(int64_t playerId) { playerId_ = playerId; }
int64_t WebSocketSession::GetPlayerId() const { return playerId_; }
std::string WebSocketSession::GetAuthToken() const { return authToken_; }

// Properties
void WebSocketSession::SetProperty(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(propertiesMutex_);
    properties_[key] = value;
}

std::string WebSocketSession::GetProperty(const std::string& key, const std::string& defaultValue) const {
    std::lock_guard<std::mutex> lock(propertiesMutex_);
    auto it = properties_.find(key);
    return it != properties_.end() ? it->second : defaultValue;
}

std::map<std::string, std::string> WebSocketSession::GetAllProperties() const {
    std::lock_guard<std::mutex> lock(propertiesMutex_);
    return properties_;
}

// Groups
void WebSocketSession::JoinGroup(const std::string& groupId) {
    std::lock_guard<std::mutex> lock(groupsMutex_);
    groups_.insert(groupId);
}

void WebSocketSession::LeaveGroup(const std::string& groupId) {
    std::lock_guard<std::mutex> lock(groupsMutex_);
    groups_.erase(groupId);
}

void WebSocketSession::LeaveAllGroups() {
    std::lock_guard<std::mutex> lock(groupsMutex_);
    groups_.clear();
}

std::set<std::string> WebSocketSession::GetJoinedGroups() const {
    std::lock_guard<std::mutex> lock(groupsMutex_);
    return groups_;
}

bool WebSocketSession::IsInGroup(const std::string& groupId) const {
    std::lock_guard<std::mutex> lock(groupsMutex_);
    return groups_.find(groupId) != groups_.end();
}

// Data
void WebSocketSession::SetData(const std::string& key, const nlohmann::json& value) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    data_[key] = value;
}

nlohmann::json WebSocketSession::GetData(const std::string& key, const nlohmann::json& defaultValue) const {
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto it = data_.find(key);
    return it != data_.end() ? it->second : defaultValue;
}

bool WebSocketSession::HasData(const std::string& key) const {
    std::lock_guard<std::mutex> lock(dataMutex_);
    return data_.find(key) != data_.end();
}

void WebSocketSession::RemoveData(const std::string& key) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    data_.erase(key);
}

void WebSocketSession::ClearData() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    data_.clear();
}

nlohmann::json WebSocketSession::GetAllData() const {
    std::lock_guard<std::mutex> lock(dataMutex_);
    return data_;
}

std::string WebSocketSession::GetRemoteAddress() const {
    try {
        return wsConn_->GetRemoteEndpoint().address().to_string();
    } catch (...) {
        return "unknown";
    }
}

void WebSocketSession::OnMessage(const WebSocketProtocol::WebSocketMessage& msg) {
    Logger::Debug("WebSocket received {} bytes, opcode: {}", msg.data.size(), (int)msg.opcode);
    Logger::Debug("WebSocketSession {} received text: {}", sessionId_, msg.GetText());
    if (msg.opcode == WebSocketProtocol::OP_TEXT) {
        if (messageHandler_) {
            try {
                auto json = nlohmann::json::parse(msg.GetText());
                messageHandler_(json);
            } catch (const std::exception& e) {
                Logger::Error("WebSocketSession: invalid JSON: {}", e.what());
            }
        }
    } else if (msg.opcode == WebSocketProtocol::OP_BINARY) {
        try {
            auto binaryMsg = BinaryProtocol::BinaryMessage::Deserialize(
                msg.data.data(), msg.data.size());
            if (binary_handler_) {
                binary_handler_(binaryMsg.header.message_type, binaryMsg.data);
            } else if (default_binary_handler_) {
                default_binary_handler_(binaryMsg.header.message_type, binaryMsg.data);
            } else {
                Logger::Warn("WebSocketSession {}: no binary handler for message type {}",
                             sessionId_, binaryMsg.header.message_type);
            }
        } catch (const std::exception& e) {
            Logger::Error("WebSocketSession {}: failed to deserialize binary message: {}",
                          sessionId_, e.what());
        }
    }
}

void WebSocketSession::OnClose(uint16_t code, const std::string& reason) {
    Logger::Info("WebSocketSession closed: code={}, reason={}", code, reason);
    if (closeHandler_) {
        closeHandler_();
    }
}

void WebSocketSession::SetBinaryMessageHandler(BinaryMessageHandler handler) {
    binary_handler_ = std::move(handler);
}

void WebSocketSession::SetDefaultBinaryMessageHandler(BinaryMessageHandler handler) {
    default_binary_handler_ = std::move(handler);
}
