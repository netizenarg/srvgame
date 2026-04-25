#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <memory>
#include <random>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>

#include <zlib.h>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <asio.hpp>
#include <asio/buffers_iterator.hpp>
#include <asio/ssl.hpp>
#include <openssl/ssl.h>
#include <openssl/sha.h>
#include <openssl/evp.h>

#include "logging/Logger.hpp"

namespace WebSocketProtocol {

    // WebSocket opcodes
    enum Opcode : uint8_t {
        OP_CONTINUATION = 0x0,
        OP_TEXT = 0x1,
        OP_BINARY = 0x2,
        OP_CLOSE = 0x8,
        OP_PING = 0x9,
        OP_PONG = 0xA
    };

    // WebSocket frame structure
    struct WebSocketFrame {
        bool fin{true};
        bool rsv1{false};
        bool rsv2{false};
        bool rsv3{false};
        Opcode opcode{OP_BINARY};
        bool masked{false};
        uint8_t masking_key[4]{0, 0, 0, 0};
        uint64_t payload_length{0};
        std::vector<uint8_t> payload_data;

        // Serialization
        std::vector<uint8_t> Serialize() const;
        static WebSocketFrame Deserialize(const uint8_t* data, size_t length);
        static WebSocketFrame Deserialize(const std::vector<uint8_t>& data);

        // Frame type helpers
        bool IsControlFrame() const {
            return opcode == OP_CLOSE || opcode == OP_PING || opcode == OP_PONG;
        }

        bool IsDataFrame() const {
            return opcode == OP_TEXT || opcode == OP_BINARY || opcode == OP_CONTINUATION;
        }

        // Create common frame types
        static WebSocketFrame CreateTextFrame(const std::string& text);
        static WebSocketFrame CreateBinaryFrame(const std::vector<uint8_t>& data);
        static WebSocketFrame CreatePingFrame(const std::vector<uint8_t>& data = {});
        static WebSocketFrame CreatePongFrame(const std::vector<uint8_t>& data = {});
        static WebSocketFrame CreateCloseFrame(uint16_t code = 1000, const std::string& reason = "");
    };

    // WebSocket message (can span multiple frames)
    struct WebSocketMessage {
        Opcode opcode{OP_BINARY};
        std::vector<uint8_t> data;
        bool complete{false};

        // For text messages
        std::string GetText() const {
            return std::string(data.begin(), data.end());
        }

        // Set text data
        void SetText(const std::string& text) {
            data.assign(text.begin(), text.end());
            opcode = OP_TEXT;
        }

        // Convert to JSON
        nlohmann::json ToJson() const {
            if (opcode == OP_TEXT) {
                return nlohmann::json::parse(GetText());
            }
            throw std::runtime_error("Cannot convert binary data to JSON");
        }

        // Create from JSON
        static WebSocketMessage FromJson(const nlohmann::json& json) {
            WebSocketMessage msg;
            msg.SetText(json.dump());
            return msg;
        }
    };

    // WebSocket handshake request
    struct HandshakeRequest {
        std::string method{"GET"};
        std::string path{"/"};
        std::string http_version{"HTTP/1.1"};
        std::unordered_map<std::string, std::string> headers;

        std::string Serialize() const;
        static HandshakeRequest Parse(const std::string& request);

        // Common headers
        std::string GetHeader(const std::string& name) const;
        void SetHeader(const std::string& name, const std::string& value);

        // WebSocket specific
        std::string GetKey() const { return GetHeader("Sec-WebSocket-Key"); }
        std::string GetVersion() const { return GetHeader("Sec-WebSocket-Version"); }
        std::string GetProtocol() const { return GetHeader("Sec-WebSocket-Protocol"); }
        std::string GetExtensions() const { return GetHeader("Sec-WebSocket-Extensions"); }
    };

    // WebSocket handshake response
    struct HandshakeResponse {
        std::string http_version{"HTTP/1.1"};
        uint16_t status_code{101};
        std::string status_text{"Switching Protocols"};
        std::unordered_map<std::string, std::string> headers;

        std::string Serialize() const;
        static HandshakeResponse Parse(const std::string& response);

        // Generate response from request
        static HandshakeResponse GenerateResponse(const HandshakeRequest& request,
                                                 const std::string& protocol = "",
                                                 const std::string& extensions = "");
        std::string GetHeader(const std::string& name) const;
        void SetHeader(const std::string& name, const std::string& value);
        // Validate response
        bool Validate(const HandshakeRequest& request) const;
    };

    // WebSocket connection base class
    class WebSocketConnection : public std::enable_shared_from_this<WebSocketConnection> {
    public:
        using Pointer = std::shared_ptr<WebSocketConnection>;

        // Event callbacks
        using MessageHandler = std::function<void(const WebSocketMessage&)>;
        using TextHandler = std::function<void(const std::string&)>;
        using BinaryHandler = std::function<void(const std::vector<uint8_t>&)>;
        using CloseHandler = std::function<void(uint16_t code, const std::string& reason)>;
        using ErrorHandler = std::function<void(const std::error_code& ec)>;

        WebSocketConnection(asio::ip::tcp::socket socket);
        virtual ~WebSocketConnection();

        // Connection management
        virtual void Start();
        virtual void Close(uint16_t code = 1000, const std::string& reason = "");
        bool IsOpen() const { return state_ == State::OPEN; }
        bool IsClosing() const { return state_ == State::CLOSING; }

        // Message sending
        void SendText(const std::string& text);
        void SendBinary(const std::vector<uint8_t>& data);
        void SendJson(const nlohmann::json& json);
        void SendPing(const std::vector<uint8_t>& data = {});
        void SendPong(const std::vector<uint8_t>& data = {});

        // Event handlers
        void SetMessageHandler(MessageHandler handler) { message_handler_ = std::move(handler); }
        void SetTextHandler(TextHandler handler) { text_handler_ = std::move(handler); }
        void SetBinaryHandler(BinaryHandler handler) { binary_handler_ = std::move(handler); }
        void SetCloseHandler(CloseHandler handler) { close_handler_ = std::move(handler); }
        void SetErrorHandler(ErrorHandler handler) { error_handler_ = std::move(handler); }

        // Connection info
        asio::ip::tcp::endpoint GetRemoteEndpoint() const;
        uint64_t GetConnectionId() const { return connection_id_; }

        // Statistics
        struct Statistics {
            uint64_t messages_sent{0};
            uint64_t messages_received{0};
            uint64_t bytes_sent{0};
            uint64_t bytes_received{0};
            uint64_t ping_count{0};
            uint64_t pong_count{0};
        };

        Statistics GetStatistics() const;

        void ReadFramePayload(bool fin, uint8_t opcode, bool masked, uint64_t payload_length);

    protected:
        enum class State {
            HANDSHAKE,
            OPEN,
            CLOSING,
            CLOSED
        };

        asio::ip::tcp::socket socket_;
        State state_{State::HANDSHAKE};
        uint64_t connection_id_;
        static std::atomic<uint64_t> next_connection_id_;

        // Buffers
        asio::streambuf read_buffer_;
        std::vector<uint8_t> write_buffer_;
        std::recursive_mutex write_mutex_;

        // Message assembly
        WebSocketMessage current_message_;

        // Event handlers
        MessageHandler message_handler_;
        TextHandler text_handler_;
        BinaryHandler binary_handler_;
        CloseHandler close_handler_;
        ErrorHandler error_handler_;

        // Statistics
        mutable std::mutex stats_mutex_;
        Statistics stats_;

        // Handshake
        virtual void HandleHandshake();
        void ReadHandshake();
        void WriteHandshakeResponse(const HandshakeResponse& response);

        // Frame handling
        void ReadFrame();
        void HandleFrame(const WebSocketFrame& frame);
        void SendFrame(const WebSocketFrame& frame);
        void SendFrameAsync(const WebSocketFrame& frame);

        // Message assembly
        void ProcessMessageData(const WebSocketFrame& frame);
        void CompleteCurrentMessage();

        // Error handling
        void HandleError(const std::error_code& ec);
        void HandleClose(uint16_t code, const std::string& reason);

    private:
        void DoWrite();
    };

    // WebSocket server
    class WebSocketServer {
    public:
        using ConnectionFactory = std::function<WebSocketConnection::Pointer(asio::ip::tcp::socket)>;

        WebSocketServer(asio::io_context& io_context, uint16_t port);
        ~WebSocketServer();

        void Start();
        void Stop();

        void SetConnectionFactory(ConnectionFactory factory) { connection_factory_ = std::move(factory); }

        // Broadcast
        void BroadcastText(const std::string& text);
        void BroadcastBinary(const std::vector<uint8_t>& data);
        void BroadcastJson(const nlohmann::json& json);

        // Connection management
        std::vector<WebSocketConnection::Pointer> GetConnections() const;
        size_t GetConnectionCount() const;

    private:
        asio::io_context& io_context_;
        asio::ip::tcp::acceptor acceptor_;
        uint16_t port_;

        ConnectionFactory connection_factory_;
        std::vector<WebSocketConnection::Pointer> connections_;
        mutable std::mutex connections_mutex_;

        std::atomic<bool> running_{false};

        void DoAccept();
        void AddConnection(WebSocketConnection::Pointer connection);
        void RemoveConnection(WebSocketConnection::Pointer connection);
    };

    // WebSocket client
    class WebSocketClient : public WebSocketConnection {
    public:
        WebSocketClient(asio::io_context& io_context);

        void Connect(const std::string& host, uint16_t port, const std::string& path = "/");
        void Connect(const std::string& url); // Supports ws:// and wss://

        // SSL/TLS support
        void UseSSL(bool enable = true);
        bool IsSecure() const { return ssl_context_ != nullptr; }

    private:
        asio::io_context& io_context_;
        std::shared_ptr<asio::ssl::context> ssl_context_;
        std::unique_ptr<asio::ssl::stream<asio::ip::tcp::socket>> ssl_stream_;

        std::string host_;
        uint16_t port_;
        std::string path_;

        void ResolveAndConnect();
        void HandleResolve(const std::error_code& ec, asio::ip::tcp::resolver::results_type endpoints);
        void HandleConnect(const std::error_code& ec, const asio::ip::tcp::endpoint& endpoint);

        // Override handshake for client
        void HandleHandshake() override;
        void SendHandshakeRequest();
    };

    // Utility functions
    std::string GenerateWebSocketKey();
    std::string GenerateAcceptKey(const std::string& key);
    uint16_t GenerateMaskingKey(uint8_t key[4]);

    bool IsValidOpcode(uint8_t opcode);
    bool IsControlOpcode(uint8_t opcode);

    // Frame parsing utilities
    size_t GetFrameHeaderSize(const WebSocketFrame& frame);
    size_t GetFrameSize(const WebSocketFrame& frame);

    // Masking utilities
    void ApplyMask(uint8_t* data, size_t length, const uint8_t masking_key[4]);
    void ApplyMask(std::vector<uint8_t>& data, const uint8_t masking_key[4]);

    // Close code utilities
    bool IsValidCloseCode(uint16_t code);
    std::string GetCloseReason(uint16_t code);

    // URL parsing
    struct WebSocketURL {
        std::string protocol; // "ws" or "wss"
        std::string host;
        uint16_t port{80};
        std::string path{"/"};
        std::string query;

        static WebSocketURL Parse(const std::string& url);
        std::string ToString() const;
    };

    // Compression support (permessage-deflate extension)
    class CompressionContext {
    public:
        CompressionContext();
        ~CompressionContext();

        bool Initialize(bool server, int compression_level = 6);
        std::vector<uint8_t> Compress(const std::vector<uint8_t>& data);
        std::vector<uint8_t> Decompress(const std::vector<uint8_t>& compressed_data);

        bool IsInitialized() const { return initialized_; }

    private:
        void* deflate_context_{nullptr};
        void* inflate_context_{nullptr};
        bool initialized_{false};
        bool server_{false};

        void Cleanup();
    };

    // Message fragmentation
    class MessageFragmenter {
    public:
        MessageFragmenter(size_t max_frame_size = 16384); // 16KB default

        std::vector<WebSocketFrame> FragmentMessage(const WebSocketMessage& message);
        std::vector<WebSocketFrame> FragmentText(const std::string& text);
        std::vector<WebSocketFrame> FragmentBinary(const std::vector<uint8_t>& data);

        size_t GetMaxFrameSize() const { return max_frame_size_; }
        void SetMaxFrameSize(size_t size) { max_frame_size_ = size; }

    private:
        size_t max_frame_size_;
    };

    // Rate limiter for WebSocket connections
    class WebSocketRateLimiter {
    public:
        WebSocketRateLimiter(size_t messages_per_second = 100, size_t burst_size = 1000);

        bool CheckLimit();
        void Update();

        void SetLimit(size_t messages_per_second, size_t burst_size);
        size_t GetMessagesPerSecond() const { return messages_per_second_; }
        size_t GetBurstSize() const { return burst_size_; }

    private:
        size_t messages_per_second_;
        size_t burst_size_;
        size_t tokens_;
        std::chrono::steady_clock::time_point last_refill_;
        mutable std::mutex mutex_;
    };

} // namespace WebSocketProtocol
