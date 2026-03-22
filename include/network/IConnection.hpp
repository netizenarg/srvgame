#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

class IConnection {
public:
    virtual ~IConnection() = default;

    // Core methods
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual void Disconnect() = 0;
    virtual bool IsConnected() const = 0;
    virtual uint64_t GetSessionId() const = 0;

    // Send methods
    virtual void Send(const nlohmann::json& message) = 0;
    virtual void SendBinary(uint16_t message_type, const std::vector<uint8_t>& data) = 0;

    // Callback setters
    using MessageHandler = std::function<void(const nlohmann::json&)>;
    virtual void SetMessageHandler(MessageHandler handler) = 0;

    using CloseHandler = std::function<void()>;
    virtual void SetCloseHandler(CloseHandler handler) = 0;

    // Authentication and properties
    virtual void Authenticate(const std::string& authToken) = 0;
    virtual bool IsAuthenticated() const = 0;
    virtual void SetPlayerId(int64_t playerId) = 0;
    virtual int64_t GetPlayerId() const = 0;

    // Groups
    virtual void JoinGroup(const std::string& groupId) = 0;
    virtual void LeaveGroup(const std::string& groupId) = 0;
    virtual void LeaveAllGroups() = 0;
    virtual std::set<std::string> GetJoinedGroups() const = 0;
    virtual bool IsInGroup(const std::string& groupId) const = 0;

    // Data storage
    virtual void SetData(const std::string& key, const nlohmann::json& value) = 0;
    virtual nlohmann::json GetData(const std::string& key, const nlohmann::json& defaultValue = {}) const = 0;
    virtual bool HasData(const std::string& key) const = 0;
    virtual void RemoveData(const std::string& key) = 0;
    virtual void ClearData() = 0;
    virtual nlohmann::json GetAllData() const = 0;

    // Statistics (optional, can be empty implementations)
    virtual void RecordMessageReceived(size_t size) {}
    virtual void RecordMessageSent(size_t size) {}
};