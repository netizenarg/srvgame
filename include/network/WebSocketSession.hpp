#pragma once

#include <set>
#include <map>
#include <mutex>

#include "logging/Logger.hpp"

#include "network/IConnection.hpp"
#include "network/WebSocketProtocol.hpp"

class WebSocketSession : public IConnection, public std::enable_shared_from_this<WebSocketSession> {
public:
    WebSocketSession(WebSocketProtocol::WebSocketConnection::Pointer wsConn);
    ~WebSocketSession();

    // IConnection implementation
    void Start() override;
    void Stop() override;
    void Disconnect() override;
    bool IsConnected() const override;
    uint64_t GetSessionId() const override;

    void Send(const nlohmann::json& message) override;
    void SendRaw(const std::string& data) override;
    void SendBinary(uint16_t message_type, const std::vector<uint8_t>& data) override;

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
    WebSocketProtocol::WebSocketConnection::Pointer wsConn_;
    uint64_t sessionId_;
    static std::atomic<uint64_t> nextSessionId_;

    MessageHandler messageHandler_;
    CloseHandler closeHandler_;

    std::atomic<bool> authenticated_{false};
    int64_t playerId_{0};
    std::string authToken_;

    // Groups
    mutable std::mutex groupsMutex_;
    std::set<std::string> groups_;

    // Data
    mutable std::mutex dataMutex_;
    std::map<std::string, nlohmann::json> data_;

    // Properties
    mutable std::mutex propertiesMutex_;
    std::map<std::string, std::string> properties_;

    BinaryMessageHandler binary_handler_;
    BinaryMessageHandler default_binary_handler_;

    // Internal helpers
    void OnMessage(const WebSocketProtocol::WebSocketMessage& msg);
    void OnClose(uint16_t code, const std::string& reason);
};
