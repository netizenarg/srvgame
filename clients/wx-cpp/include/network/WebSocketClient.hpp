#pragma once

#include <nlohmann/json.hpp>
#include <asio.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <functional>
#include <memory>
#include <string>
#include <queue>
#include <mutex>
#include <atomic>

#include "ConnectionState.hpp"
#include "WebSocketProtocol.hpp"

class WebSocketClient {
public:
    using MessageHandler = std::function<void(const nlohmann::json&)>;
    using ConnectionCallback = std::function<void(bool, ConnectionError)>;
    
    WebSocketClient();
    ~WebSocketClient();
    
    bool Connect(const std::string& url, const std::string& protocol = "");
    bool ConnectAsync(const std::string& url, ConnectionCallback callback, const std::string& protocol = "");
    void Disconnect();
    bool IsConnected() const;
    
    void Send(const nlohmann::json& message);
    void SendBinary(const std::vector<uint8_t>& data);
    void Ping();
    
    void RegisterHandler(const std::string& messageType, MessageHandler handler);
    void UnregisterHandler(const std::string& messageType);
    
    ConnectionState GetConnectionState() const;
    ConnectionMetrics GetConnectionMetrics() const;
    
private:
    using Client = websocketpp::client<websocketpp::config::asio_tls_client>;
    using ConnectionPtr = websocketpp::connection_hdl;
    using MessagePtr = Client::message_ptr;
    
    void RunIOContext();
    void OnOpen(ConnectionPtr hdl);
    void OnMessage(ConnectionPtr hdl, MessagePtr msg);
    void OnClose(ConnectionPtr hdl);
    void OnFail(ConnectionPtr hdl);
    
    Client client_;
    std::thread io_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    
    ConnectionPtr connection_;
    mutable std::mutex connection_mutex_;
    
    std::unordered_map<std::string, MessageHandler> message_handlers_;
    mutable std::mutex handlers_mutex_;
    
    std::unique_ptr<ConnectionStateManager> state_manager_;
    ConnectionMetrics metrics_;
    mutable std::mutex metrics_mutex_;
    
    std::queue<std::pair<std::string, bool>> send_queue_; // message, is_text
    mutable std::mutex queue_mutex_;
};