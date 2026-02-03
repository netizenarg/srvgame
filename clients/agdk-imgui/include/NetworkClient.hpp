// NetworkClient.hpp - MODIFIED LINES
#pragma once

#include <asio.hpp>
#include <queue>
#include <mutex>
#include <atomic>
#include <nlohmann/json.hpp>
#include "BinaryProtocol.hpp"
#include "WebSocketProtocol.hpp"

enum class NetworkProtocol {
    JSON_TEXT,
    BINARY,
    WEBSOCKET
};

class NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();
    
    bool Connect(const std::string& host, int port);
    void Disconnect();
    bool IsConnected() const { return connected_; }
    
    // Protocol selection
    void SetProtocol(NetworkProtocol protocol);
    NetworkProtocol GetProtocol() const { return protocol_; }
    
    // Send methods for different protocols
    void SendJson(const nlohmann::json& message);
    void SendBinary(const BinaryProtocol::BinaryMessage& message);
    void SendWebSocket(const nlohmann::json& message);
    
    // Receive methods for different protocols
    std::vector<nlohmann::json> ReceiveJson();
    std::vector<BinaryProtocol::BinaryMessage> ReceiveBinary();
    std::vector<nlohmann::json> ReceiveWebSocket();
    
    // Common settings
    void SetTimeout(int milliseconds);
    void SetCompression(bool enabled);
    
private:
    void RunIOContext();
    void DoConnect(const asio::ip::tcp::resolver::results_type& endpoints);
    
    // JSON protocol
    void DoReadJson();
    void DoWriteJson(const std::string& message);
    
    // Binary protocol
    void DoReadBinary();
    void DoWriteBinary(const BinaryProtocol::BinaryMessage& message);
    
    // WebSocket protocol
    void InitializeWebSocket();
    void DoReadWebSocket();
    void DoWriteWebSocket(const std::string& message);
    
    asio::io_context ioContext_;
    asio::ip::tcp::socket socket_;
    std::unique_ptr<WebSocketProtocol::WebSocketClient> webSocketClient_;
    asio::streambuf readBuffer_;
    
    std::thread ioThread_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    
    // Message queues
    std::queue<std::string> jsonSendQueue_;
    std::queue<nlohmann::json> jsonReceiveQueue_;
    
    std::queue<BinaryProtocol::BinaryMessage> binarySendQueue_;
    std::queue<BinaryProtocol::BinaryMessage> binaryReceiveQueue_;
    
    mutable std::mutex jsonSendMutex_;
    mutable std::mutex jsonReceiveMutex_;
    mutable std::mutex binarySendMutex_;
    mutable std::mutex binaryReceiveMutex_;
    
    // Compression
    bool compressionEnabled_{false};
    
    // Timeout
    asio::steady_timer timeoutTimer_;
    int timeoutMs_{5000};
    
    // Protocol
    NetworkProtocol protocol_{NetworkProtocol::JSON_TEXT};
};