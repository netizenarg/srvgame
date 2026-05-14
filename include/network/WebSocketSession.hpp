#pragma once

#include <set>
#include <map>
#include <mutex>

#include "logging/Logger.hpp"

#include "network/IConnection.hpp"
#include "network/BinaryProtocol.hpp"
#include "network/WebSocketProtocol.hpp"

#include "game/GameData.hpp"
//#include "game/GameLogic.hpp"

class WebSocketSession : public IConnection, public std::enable_shared_from_this<WebSocketSession> {
public:
    WebSocketSession(WebSocketProtocol::WebSocketConnection::Pointer wsConn, uint64_t sessionId=0);
    ~WebSocketSession();

    void SetProtocolMode(ProtocolMode mode) { protocolMode_ = mode; }
    ProtocolMode GetProtocolMode() const override { return protocolMode_; }

    void Start() override;
    void Stop() override;
    void Disconnect() override;
    bool IsConnected() const override;
    uint64_t GetSessionId() const override;

    void Send(uint16_t message_type, const std::vector<uint8_t>& data) override;
    void SendRaw(const std::string& data) override;
    void SendJson(const nlohmann::json& message) override;
    void SendError(uint16_t message_type, const std::string& error_message, int code) override;

    void SetMessageHandler(MessageHandler handler) override;
    void SetCloseHandler(CloseHandler handler) override;

    void Authenticate(const std::string& authToken) override;
    bool IsAuthenticated() const override;
    void SetPlayerId(int64_t playerId) override;
    int64_t GetPlayerId() const override;
    std::string GetAuthToken() const override;

    void SetProperty(const std::string& key, const std::string& value) override;
    std::string GetProperty(const std::string& key, const std::string& defaultValue = "") const override;
    std::map<std::string, std::string> GetAllProperties() const override;

    void JoinGroup(const std::string& groupId) override;
    void LeaveGroup(const std::string& groupId) override;
    void LeaveAllGroups() override;
    std::set<std::string> GetJoinedGroups() const override;
    bool IsInGroup(const std::string& groupId) const override;

    void SetData(const std::string& key, const nlohmann::json& value) override;
    nlohmann::json GetData(const std::string& key, const nlohmann::json& defaultValue = {}) const override;
    bool HasData(const std::string& key) const override;
    void RemoveData(const std::string& key) override;
    void ClearData() override;
    nlohmann::json GetAllData() const override;

    std::string GetRemoteAddress() const override;

    using BinaryMessageHandler = std::function<void(uint16_t, const std::vector<uint8_t>&)>;
    void SetBinaryMessageHandler(BinaryMessageHandler handler);
    void SetDefaultBinaryMessageHandler(BinaryMessageHandler handler);

private:
    ProtocolMode protocolMode_;

    WebSocketProtocol::WebSocketConnection::Pointer wsConn_;
    uint64_t sessionId_;
    static std::atomic<uint64_t> nextSessionId_;

    MessageHandler messageHandler_;
    CloseHandler closeHandler_;

    std::atomic<bool> authenticated_{false};
    int64_t playerId_{0};
    std::string authToken_;

    mutable std::mutex groupsMutex_;
    std::set<std::string> groups_;

    mutable std::mutex dataMutex_;
    std::map<std::string, nlohmann::json> data_;

    mutable std::mutex propertiesMutex_;
    std::map<std::string, std::string> properties_;

    BinaryMessageHandler binary_handler_;
    BinaryMessageHandler default_binary_handler_;

    void OnMessage(const WebSocketProtocol::WebSocketMessage& msg);
    void OnClose(uint16_t code, const std::string& reason);
};
