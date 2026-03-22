#include "network/WebSocketSession.hpp"

std::atomic<uint64_t> WebSocketSession::nextSessionId_{1};

WebSocketSession::WebSocketSession(WebSocketProtocol::WebSocketConnection::Pointer wsConn)
    : wsConn_(std::move(wsConn)), sessionId_(nextSessionId_++) {
    // Set up callbacks
    wsConn_->SetMessageHandler([this](const WebSocketProtocol::WebSocketMessage& msg) {
        OnMessage(msg);
    });
    wsConn_->SetCloseHandler([this](uint16_t code, const std::string& reason) {
        OnClose(code, reason);
    });
}

WebSocketSession::~WebSocketSession() = default;

void WebSocketSession::Start() {
    wsConn_->Start();
}

void WebSocketSession::Stop() {
    wsConn_->Close();
}

void WebSocketSession::Disconnect() {
    wsConn_->Close(1000, "Disconnect");
}

bool WebSocketSession::IsConnected() const {
    return wsConn_->IsOpen();
}

uint64_t WebSocketSession::GetSessionId() const {
    return sessionId_;
}

void WebSocketSession::Send(const nlohmann::json& message) {
    wsConn_->SendJson(message);
}

void WebSocketSession::SendBinary(uint16_t message_type, const std::vector<uint8_t>& data) {
    // For binary messages, we need to send the raw data as a WebSocket binary frame.
    // Optionally, we could embed the message type in the payload (e.g., first two bytes).
    // Here we assume the caller has already prepared a complete payload.
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
    // Could perform validation here
}

bool WebSocketSession::IsAuthenticated() const {
    return authenticated_;
}

void WebSocketSession::SetPlayerId(int64_t playerId) {
    playerId_ = playerId;
}

int64_t WebSocketSession::GetPlayerId() const {
    return playerId_;
}

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

void WebSocketSession::OnMessage(const WebSocketProtocol::WebSocketMessage& msg) {
    if (messageHandler_) {
        if (msg.opcode == WebSocketProtocol::OP_TEXT) {
            try {
                auto json = nlohmann::json::parse(msg.GetText());
                messageHandler_(json);
            } catch (const std::exception& e) {
                Logger::Error("WebSocketSession: invalid JSON: {}", e.what());
            }
        } else if (msg.opcode == WebSocketProtocol::OP_BINARY) {
            // For binary messages, we could convert to a JSON message or handle directly.
            // Here we'll just log and ignore; later we can parse binary protocol.
            Logger::Debug("WebSocket binary message received, size: {}", msg.data.size());
            // Optionally, we could delegate to a binary protocol handler.
        }
    }
}

void WebSocketSession::OnClose(uint16_t code, const std::string& reason) {
    Logger::Info("WebSocketSession closed: code={}, reason={}", code, reason);
    if (closeHandler_) {
        closeHandler_();
    }
}
