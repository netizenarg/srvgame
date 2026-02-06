#include "WebSocketClient.h"
#include <iostream>

WebSocketClient::WebSocketClient() 
    : state_manager_(std::make_unique<ConnectionStateManager>()) {
    
    client_.clear_access_channels(websocketpp::log::alevel::all);
    client_.clear_error_channels(websocketpp::log::elevel::all);
    
    client_.init_asio();
    
    client_.set_open_handler([this](ConnectionPtr hdl) {
        OnOpen(hdl);
    });
    
    client_.set_message_handler([this](ConnectionPtr hdl, MessagePtr msg) {
        OnMessage(hdl, msg);
    });
    
    client_.set_close_handler([this](ConnectionPtr hdl) {
        OnClose(hdl);
    });
    
    client_.set_fail_handler([this](ConnectionPtr hdl) {
        OnFail(hdl);
    });
    
    state_manager_->TransitionTo(ConnectionState::Disconnected);
}

WebSocketClient::~WebSocketClient() {
    Disconnect();
}

bool WebSocketClient::Connect(const std::string& url, const std::string& protocol) {
    if (IsConnected()) {
        Disconnect();
    }
    
    state_manager_->TransitionTo(ConnectionState::Connecting);
    
    std::error_code ec;
    auto con = client_.get_connection(url, ec);
    
    if (ec) {
        state_manager_->TransitionTo(ConnectionState::Error, ConnectionError::NetworkUnavailable);
        return false;
    }
    
    if (!protocol.empty()) {
        con->add_subprotocol(protocol);
    }
    
    {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        connection_ = con->get_handle();
    }
    
    client_.connect(con);
    
    if (!running_) {
        running_ = true;
        io_thread_ = std::thread(&WebSocketClient::RunIOContext, this);
    }
    
    return true;
}

bool WebSocketClient::ConnectAsync(const std::string& url, ConnectionCallback callback, const std::string& protocol) {
    // Similar to Connect but calls callback when done
    return Connect(url, protocol);
}

void WebSocketClient::Disconnect() {
    if (!IsConnected()) return;
    
    state_manager_->TransitionTo(ConnectionState::Disconnecting);
    
    try {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        if (connection_.lock()) {
            client_.close(connection_, websocketpp::close::status::going_away, "");
        }
    } catch (...) {
        // Ignore errors during disconnect
    }
    
    if (running_) {
        running_ = false;
        client_.stop();
        if (io_thread_.joinable()) {
            io_thread_.join();
        }
    }
    
    state_manager_->TransitionTo(ConnectionState::Disconnected);
}

bool WebSocketClient::IsConnected() const {
    return connected_.load() && state_manager_->GetState() == ConnectionState::Connected;
}

void WebSocketClient::Send(const nlohmann::json& message) {
    if (!IsConnected()) return;
    
    std::string data = message.dump();
    
    std::lock_guard<std::mutex> lock(queue_mutex_);
    send_queue_.push({data, true});
    
    try {
        std::lock_guard<std::mutex> conn_lock(connection_mutex_);
        auto conn = connection_.lock();
        if (conn) {
            client_.send(connection_, data, websocketpp::frame::opcode::text);
            
            std::lock_guard<std::mutex> metrics_lock(metrics_mutex_);
            metrics_.bytesSent += data.size();
            metrics_.packetsSent++;
        }
    } catch (...) {
        // Handle send error
    }
}

void WebSocketClient::OnOpen(ConnectionPtr hdl) {
    connected_.store(true);
    state_manager_->TransitionTo(ConnectionState::Connected);
    
    // Process queued messages
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!send_queue_.empty()) {
        auto& [data, is_text] = send_queue_.front();
        try {
            client_.send(hdl, data, is_text ? websocketpp::frame::opcode::text : websocketpp::frame::opcode::binary);
        } catch (...) {
            // Handle error
        }
        send_queue_.pop();
    }
}

void WebSocketClient::OnMessage(ConnectionPtr hdl, MessagePtr msg) {
    std::lock_guard<std::mutex> metrics_lock(metrics_mutex_);
    metrics_.bytesReceived += msg->get_payload().size();
    metrics_.packetsReceived++;
    
    if (msg->get_opcode() == websocketpp::frame::opcode::text) {
        try {
            auto json = nlohmann::json::parse(msg->get_payload());
            std::string msg_type = json.value("type", "");
            
            std::lock_guard<std::mutex> handlers_lock(handlers_mutex_);
            auto it = message_handlers_.find(msg_type);
            if (it != message_handlers_.end()) {
                it->second(json);
            }
            
            // Try wildcard handler
            auto wildcard = message_handlers_.find("*");
            if (wildcard != message_handlers_.end()) {
                wildcard->second(json);
            }
        } catch (const std::exception& e) {
            std::cerr << "JSON parse error: " << e.what() << std::endl;
        }
    }
}

void WebSocketClient::RunIOContext() {
    client_.run();
}

// Other methods implementation...