#pragma once

#include <asio.hpp>
#include <queue>
#include <mutex>
#include <atomic>
#include <memory>
#include <nlohmann/json.hpp>
#include "WebSocketProtocol.hpp"

class WebSocketNetworkClient {
public:
    WebSocketNetworkClient();
    ~WebSocketNetworkClient();
    
    bool Connect(const std::string& host, int port, const std::string& path = "/");
    void Disconnect();
    bool IsConnected() const;
    
    void Send(const nlohmann::json& message);
    void SendBinary(const std::vector<uint8_t>& data);
    
    std::vector<nlohmann::json> Receive();
    std::vector<std::vector<uint8_t>> ReceiveBinary();
    
    void SetMessageHandler(std::function<void(const nlohmann::json&)> handler);
    void SetBinaryHandler(std::function<void(const std::vector<uint8_t>&)> handler);
    void SetErrorHandler(std::function<void(const std::string&)> handler);
    
private:
    void Run();
    void HandleMessage(const WebSocketProtocol::WebSocketMessage& msg);
    void HandleError(const std::error_code& ec);
    
    asio::io_context ioContext_;
    std::unique_ptr<WebSocketProtocol::WebSocketClient> client_;
    std::thread ioThread_;
    
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    
    // Message queues
    std::queue<nlohmann::json> messageQueue_;
    std::queue<std::vector<uint8_t>> binaryQueue_;
    mutable std::mutex queueMutex_;
    mutable std::mutex binaryQueueMutex_;
    
    // Handlers
    std::function<void(const nlohmann::json&)> messageHandler_;
    std::function<void(const std::vector<uint8_t>&)> binaryHandler_;
    std::function<void(const std::string&)> errorHandler_;
};