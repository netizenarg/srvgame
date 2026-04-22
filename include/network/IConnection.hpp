#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>

#include <nlohmann/json.hpp>

enum class ProtocolMode { Binary, Json, Unknown };

static const std::unordered_map<std::string, int> IPCMessageTypes = {
    {"welcome", 1},
    {"heartbeat", 2},
    {"broadcast", 3},
    {"shutdown", 4},
    {"reload_config", 5}
};

class IConnection {
public:
    virtual ~IConnection() = default;

    virtual ProtocolMode GetProtocolMode() const = 0;

    // Core methods
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual void Disconnect() = 0;
    virtual bool IsConnected() const = 0;
    virtual uint64_t GetSessionId() const = 0;

    // Send methods
    virtual void Send(uint16_t message_type, const std::vector<uint8_t>& data) = 0;
    virtual void SendRaw(const std::string& data) = 0;
    virtual void SendJson(const nlohmann::json& message) = 0;
    virtual void SendError(uint16_t message_type, const std::string& error_message, int code) = 0;

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
    virtual std::string GetAuthToken() const = 0;

    // Properties (key‑value pairs for metadata)
    virtual void SetProperty(const std::string& key, const std::string& value) = 0;
    virtual std::string GetProperty(const std::string& key, const std::string& defaultValue = "") const = 0;
    virtual std::map<std::string, std::string> GetAllProperties() const = 0;

    // Groups
    virtual void JoinGroup(const std::string& groupId) = 0;
    virtual void LeaveGroup(const std::string& groupId) = 0;
    virtual void LeaveAllGroups() = 0;
    virtual std::set<std::string> GetJoinedGroups() const = 0;
    virtual bool IsInGroup(const std::string& groupId) const = 0;

    // Data storage (JSON)
    virtual void SetData(const std::string& key, const nlohmann::json& value) = 0;
    virtual nlohmann::json GetData(const std::string& key, const nlohmann::json& defaultValue = {}) const = 0;
    virtual bool HasData(const std::string& key) const = 0;
    virtual void RemoveData(const std::string& key) = 0;
    virtual void ClearData() = 0;
    virtual nlohmann::json GetAllData() const = 0;

    // Remote info
    virtual std::string GetRemoteAddress() const = 0;

    // Statistics (optional, can be empty implementations)
    virtual void RecordMessageReceived(size_t size) {(void)size;}
    virtual void RecordMessageSent(size_t size) {(void)size;}
};
