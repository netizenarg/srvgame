#include "network/WebSocketProtocol.hpp"

namespace WebSocketProtocol {

// =============== WebSocketFrame Implementation ===============

std::vector<uint8_t> WebSocketFrame::Serialize() const {
    std::vector<uint8_t> buffer;

    // First byte: FIN(1) + RSV1(1) + RSV2(1) + RSV3(1) + Opcode(4)
    uint8_t first_byte = 0;
    if (fin) first_byte |= 0x80;
    if (rsv1) first_byte |= 0x40;
    if (rsv2) first_byte |= 0x20;
    if (rsv3) first_byte |= 0x10;
    first_byte |= (opcode & 0x0F);
    buffer.push_back(first_byte);

    // Second byte: MASK(1) + Payload Length(7)
    uint8_t second_byte = masked ? 0x80 : 0x00;

    if (payload_length <= 125) {
        second_byte |= (payload_length & 0x7F);
        buffer.push_back(second_byte);
    } else if (payload_length <= 65535) {
        second_byte |= 126;
        buffer.push_back(second_byte);
        buffer.push_back((payload_length >> 8) & 0xFF);
        buffer.push_back(payload_length & 0xFF);
    } else {
        second_byte |= 127;
        buffer.push_back(second_byte);
        for (int i = 7; i >= 0; --i) {
            buffer.push_back((payload_length >> (i * 8)) & 0xFF);
        }
    }

    // Masking key
    if (masked) {
        buffer.push_back(masking_key[0]);
        buffer.push_back(masking_key[1]);
        buffer.push_back(masking_key[2]);
        buffer.push_back(masking_key[3]);
    }

    // Payload data (apply mask if needed)
    if (!payload_data.empty()) {
        if (masked) {
            std::vector<uint8_t> masked_data = payload_data;
            for (size_t i = 0; i < masked_data.size(); ++i) {
                masked_data[i] ^= masking_key[i % 4];
            }
            buffer.insert(buffer.end(), masked_data.begin(), masked_data.end());
        } else {
            buffer.insert(buffer.end(), payload_data.begin(), payload_data.end());
        }
    }

    return buffer;
}

WebSocketFrame WebSocketFrame::Deserialize(const uint8_t* data, size_t length) {
    if (length < 2) {
        throw std::runtime_error("Frame too short");
    }

    WebSocketFrame frame;

    // First byte
    uint8_t first_byte = data[0];
    frame.fin = (first_byte & 0x80) != 0;
    frame.rsv1 = (first_byte & 0x40) != 0;
    frame.rsv2 = (first_byte & 0x20) != 0;
    frame.rsv3 = (first_byte & 0x10) != 0;
    frame.opcode = static_cast<Opcode>(first_byte & 0x0F);

    // Second byte
    uint8_t second_byte = data[1];
    frame.masked = (second_byte & 0x80) != 0;
    uint64_t payload_length = second_byte & 0x7F;

    size_t header_size = 2;
    size_t masking_key_offset = 0;

    if (payload_length == 126) {
        if (length < 4) throw std::runtime_error("Frame too short for extended payload length");
        payload_length = (data[2] << 8) | data[3];
        header_size = 4;
        masking_key_offset = 4;
    } else if (payload_length == 127) {
        if (length < 10) throw std::runtime_error("Frame too short for 64-bit payload length");
        payload_length = 0;
        for (int i = 0; i < 8; ++i) {
            payload_length = (payload_length << 8) | data[2 + i];
        }
        header_size = 10;
        masking_key_offset = 10;
    } else {
        masking_key_offset = 2;
    }

    frame.payload_length = payload_length;

    // Read masking key if present
    if (frame.masked) {
        if (length < masking_key_offset + 4) {
            throw std::runtime_error("Frame too short for masking key");
        }
        std::copy(data + masking_key_offset, data + masking_key_offset + 4, frame.masking_key);
        header_size += 4;
    }

    // Read payload data
    if (payload_length > 0) {
        if (length < header_size + payload_length) {
            throw std::runtime_error("Frame too short for payload");
        }

        frame.payload_data.resize(payload_length);
        std::copy(data + header_size, data + header_size + payload_length, frame.payload_data.begin());

        // Unmask if needed
        if (frame.masked) {
            for (size_t i = 0; i < frame.payload_data.size(); ++i) {
                frame.payload_data[i] ^= frame.masking_key[i % 4];
            }
        }
    }

    return frame;
}

WebSocketFrame WebSocketFrame::Deserialize(const std::vector<uint8_t>& data) {
    return Deserialize(data.data(), data.size());
}

WebSocketFrame WebSocketFrame::CreateTextFrame(const std::string& text) {
    WebSocketFrame frame;
    frame.opcode = OP_TEXT;
    frame.fin = true;
    frame.payload_length = text.size();
    frame.payload_data.assign(text.begin(), text.end());
    return frame;
}

WebSocketFrame WebSocketFrame::CreateBinaryFrame(const std::vector<uint8_t>& data) {
    WebSocketFrame frame;
    frame.opcode = OP_BINARY;
    frame.fin = true;
    frame.payload_length = data.size();
    frame.payload_data = data;
    return frame;
}

WebSocketFrame WebSocketFrame::CreatePingFrame(const std::vector<uint8_t>& data) {
    WebSocketFrame frame;
    frame.opcode = OP_PING;
    frame.fin = true;
    frame.payload_length = data.size();
    frame.payload_data = data;
    return frame;
}

WebSocketFrame WebSocketFrame::CreatePongFrame(const std::vector<uint8_t>& data) {
    WebSocketFrame frame;
    frame.opcode = OP_PONG;
    frame.fin = true;
    frame.payload_length = data.size();
    frame.payload_data = data;
    return frame;
}

WebSocketFrame WebSocketFrame::CreateCloseFrame(uint16_t code, const std::string& reason) {
    WebSocketFrame frame;
    frame.opcode = OP_CLOSE;
    frame.fin = true;

    std::vector<uint8_t> payload;
    if (code != 0) {
        payload.push_back((code >> 8) & 0xFF);
        payload.push_back(code & 0xFF);
        payload.insert(payload.end(), reason.begin(), reason.end());
    }

    frame.payload_length = payload.size();
    frame.payload_data = payload;
    return frame;
}

// =============== HandshakeRequest Implementation ===============

std::string HandshakeRequest::Serialize() const {
    std::stringstream ss;
    ss << method << " " << path << " " << http_version << "\r\n";

    for (const auto& [name, value] : headers) {
        ss << name << ": " << value << "\r\n";
    }

    ss << "\r\n";
    return ss.str();
}

HandshakeRequest HandshakeRequest::Parse(const std::string& request) {
    HandshakeRequest req;
    std::istringstream stream(request);
    std::string line;

    // Parse request line
    if (std::getline(stream, line)) {
        std::istringstream line_stream(line);
        line_stream >> req.method >> req.path >> req.http_version;
    }

    // Parse headers
    while (std::getline(stream, line) && line != "\r" && !line.empty()) {
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string name = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);

            // Trim whitespace
            name.erase(0, name.find_first_not_of(" \t\r\n"));
            name.erase(name.find_last_not_of(" \t\r\n") + 1);
            value.erase(0, value.find_first_not_of(" \t\r\n"));
            value.erase(value.find_last_not_of(" \t\r\n") + 1);

            req.headers[name] = value;
        }
    }

    return req;
}

std::string HandshakeRequest::GetHeader(const std::string& name) const {
    auto it = headers.find(name);
    if (it != headers.end()) {
        return it->second;
    }

    // Case-insensitive search
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

    for (const auto& [header_name, value] : headers) {
        std::string lower_header = header_name;
        std::transform(lower_header.begin(), lower_header.end(), lower_header.begin(), ::tolower);
        if (lower_header == lower_name) {
            return value;
        }
    }

    return "";
}

void HandshakeRequest::SetHeader(const std::string& name, const std::string& value) {
    headers[name] = value;
}

// =============== HandshakeResponse Implementation ===============

std::string HandshakeResponse::Serialize() const {
    std::stringstream ss;
    ss << http_version << " " << status_code << " " << status_text << "\r\n";

    for (const auto& [name, value] : headers) {
        ss << name << ": " << value << "\r\n";
    }

    ss << "\r\n";
    return ss.str();
}

HandshakeResponse HandshakeResponse::Parse(const std::string& response) {
    HandshakeResponse resp;
    std::istringstream stream(response);
    std::string line;

    // Parse status line
    if (std::getline(stream, line)) {
        std::istringstream line_stream(line);
        line_stream >> resp.http_version >> resp.status_code;
        std::getline(line_stream, resp.status_text);

        // Trim status text
        resp.status_text.erase(0, resp.status_text.find_first_not_of(" \t\r\n"));
        resp.status_text.erase(resp.status_text.find_last_not_of(" \t\r\n") + 1);
    }

    // Parse headers
    while (std::getline(stream, line) && line != "\r" && !line.empty()) {
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string name = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);

            // Trim whitespace
            name.erase(0, name.find_first_not_of(" \t\r\n"));
            name.erase(name.find_last_not_of(" \t\r\n") + 1);
            value.erase(0, value.find_first_not_of(" \t\r\n"));
            value.erase(value.find_last_not_of(" \t\r\n") + 1);

            resp.headers[name] = value;
        }
    }

    return resp;
}

HandshakeResponse HandshakeResponse::GenerateResponse(const HandshakeRequest& request,
                                                     const std::string& protocol,
                                                     const std::string& extensions) {
    HandshakeResponse response;
    response.status_code = 101;
    response.status_text = "Switching Protocols";

    // Required headers
    response.headers["Upgrade"] = "websocket";
    response.headers["Connection"] = "Upgrade";

    // Sec-WebSocket-Accept
    std::string key = request.GetKey();
    if (!key.empty()) {
        response.headers["Sec-WebSocket-Accept"] = GenerateAcceptKey(key);
    }

    // Optional protocol
    if (!protocol.empty()) {
        response.headers["Sec-WebSocket-Protocol"] = protocol;
    }

    // Optional extensions
    if (!extensions.empty()) {
        response.headers["Sec-WebSocket-Extensions"] = extensions;
    }

    return response;
}

bool HandshakeResponse::Validate(const HandshakeRequest& request) const {
    if (status_code != 101) {
        return false;
    }

    // Check Upgrade header
    std::string upgrade = GetHeader("Upgrade");
    std::transform(upgrade.begin(), upgrade.end(), upgrade.begin(), ::tolower);
    if (upgrade != "websocket") {
        return false;
    }

    // Check Connection header
    std::string connection = GetHeader("Connection");
    std::transform(connection.begin(), connection.end(), connection.begin(), ::tolower);
    if (connection.find("upgrade") == std::string::npos) {
        return false;
    }

    // Validate Sec-WebSocket-Accept
    std::string expected_accept = GenerateAcceptKey(request.GetKey());
    std::string actual_accept = GetHeader("Sec-WebSocket-Accept");

    return expected_accept == actual_accept;
}

std::string HandshakeResponse::GetHeader(const std::string& name) const {
    auto it = headers.find(name);
    if (it != headers.end()) {
        return it->second;
    }

    // Case-insensitive search
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

    for (const auto& [header_name, value] : headers) {
        std::string lower_header = header_name;
        std::transform(lower_header.begin(), lower_header.end(), lower_header.begin(), ::tolower);
        if (lower_header == lower_name) {
            return value;
        }
    }

    return "";
}

std::atomic<uint64_t> WebSocketConnection::next_connection_id_{1};

WebSocketConnection::WebSocketConnection(asio::ip::tcp::socket socket)
    : socket_(std::move(socket))
    , connection_id_(next_connection_id_++) {
    Logger::Debug("WebSocketConnection {} created", connection_id_);
}

WebSocketConnection::~WebSocketConnection() {
    Logger::Debug("WebSocketConnection {} destroyed", connection_id_);
}

void WebSocketConnection::Start() {
    if (state_ != State::HANDSHAKE) {
        return;
    }
    HandleHandshake();
}

void WebSocketConnection::HandleHandshake() {
    ReadHandshake();
}

void WebSocketConnection::ReadHandshake() {
    auto self = shared_from_this();
    asio::async_read_until(socket_, read_buffer_, "\r\n\r\n",
        [self](std::error_code ec, size_t /*bytes_transferred*/) {
            //Logger::Debug("WebSocketConnection::ReadHandshake asio::async_read_until {}", bytes_transferred);
            if (ec) {
                self->HandleError(ec);
                return;
            }
            std::istream stream(&self->read_buffer_);
            std::string request_str;
            std::getline(stream, request_str, '\0');
            try {
                HandshakeRequest request = HandshakeRequest::Parse(request_str);
                HandshakeResponse response = HandshakeResponse::GenerateResponse(request);
                self->WriteHandshakeResponse(response);
            } catch (const std::exception& e) {
                Logger::Error("WebSocket handshake error: {}", e.what());
                self->Close(1002, "Protocol error");
            }
        });
}

void WebSocketConnection::WriteHandshakeResponse(const HandshakeResponse& response) {
    auto self = shared_from_this();
    std::string response_str = response.Serialize();
    asio::async_write(socket_, asio::buffer(response_str),
        [self](std::error_code ec, size_t /*bytes_transferred*/) {
            //Logger::Debug("WebSocketConnection::WriteHandshakeResponse asio::async_write {}", bytes_transferred);
            if (ec) {
                self->HandleError(ec);
                return;
            }
            self->state_ = State::OPEN;
            Logger::Info("WebSocketConnection {} handshake complete", self->connection_id_);
            self->ReadFrame();
        });
}

void WebSocketConnection::ReadFrame() {
    if (state_ != State::OPEN && state_ != State::CLOSING) return;
    auto self = shared_from_this();
    asio::async_read(socket_, read_buffer_, asio::transfer_exactly(2),
    [self](std::error_code ec, size_t) {
        if (ec) {
            if (ec != asio::error::operation_aborted) self->HandleError(ec);
            return;
        }
        if (self->state_ == State::CLOSED || self->state_ == State::CLOSING) return;
        auto bufs = self->read_buffer_.data();
        auto it = asio::buffers_begin(bufs);
        uint8_t b0 = *it; ++it; uint8_t b1 = *it;
        bool fin = (b0 & 0x80) != 0;
        uint8_t opcode = b0 & 0x0F;
        bool masked = (b1 & 0x80) != 0;
        uint64_t len7 = b1 & 0x7F;
        self->read_buffer_.consume(2);
        size_t ext_bytes = 0;
        if (len7 == 126) ext_bytes = 2;
        else if (len7 == 127) ext_bytes = 8;
        if (ext_bytes > 0) {
            asio::async_read(self->socket_, self->read_buffer_,
                asio::transfer_exactly(ext_bytes),
                [self, fin, opcode, masked, len7](std::error_code ec, size_t) {
                    if (ec) { if (ec != asio::error::operation_aborted) self->HandleError(ec); return; }
                    std::vector<uint8_t> ext(len7 == 126 ? 2 : 8);
                    self->read_buffer_.sgetn(reinterpret_cast<char*>(ext.data()), ext.size());
                    uint64_t actual_len = 0;
                    for (auto v : ext) actual_len = (actual_len << 8) | v;
                    self->ReadFramePayload(fin, opcode, masked, actual_len);
                });
        } else {
            self->ReadFramePayload(fin, opcode, masked, len7);
        }
    });
}

void WebSocketConnection::ReadFramePayload(bool fin, uint8_t opcode, bool masked, uint64_t payload_length)
{
    size_t to_read = payload_length + (masked ? 4 : 0);
    if (to_read == 0) {
        WebSocketFrame frame;
        frame.fin = fin; frame.opcode = static_cast<Opcode>(opcode);
        frame.masked = false; frame.payload_length = 0;
        HandleFrame(frame);
        if (state_ == State::OPEN || state_ == State::CLOSING) ReadFrame();
        return;
    }
    auto self = shared_from_this();
    asio::async_read(socket_, read_buffer_, asio::transfer_exactly(to_read),
    [self, fin, opcode, masked, payload_length](std::error_code ec, size_t to_read) {
        if (ec) { if (ec != asio::error::operation_aborted) self->HandleError(ec); return; }
        std::vector<uint8_t> data(to_read);
        self->read_buffer_.sgetn(reinterpret_cast<char*>(data.data()), to_read);
        uint8_t mask[4] = {0};
        size_t offset = 0;
        if (masked) {
            std::copy_n(data.begin(), 4, mask);
            offset = 4;
        }
        std::vector<uint8_t> payload(data.begin() + offset, data.end());
        if (masked) {
            for (size_t i = 0; i < payload.size(); ++i)
                payload[i] ^= mask[i % 4];
        }
        WebSocketFrame frame;
        frame.fin = fin; frame.opcode = static_cast<Opcode>(opcode);
        frame.masked = masked; frame.payload_length = payload_length;
        frame.payload_data = std::move(payload);
        self->HandleFrame(frame);
        if (self->state_ == State::OPEN || self->state_ == State::CLOSING)
            self->ReadFrame();
    });
}

void WebSocketConnection::HandleFrame(const WebSocketFrame& frame) {
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.messages_received++;
        stats_.bytes_received += frame.payload_data.size();
    }
    if (frame.IsControlFrame()) {
        switch (frame.opcode) {
            case OP_CLOSE: {
                uint16_t close_code = 1000;
                std::string close_reason;
                if (frame.payload_length >= 2) {
                    close_code = (frame.payload_data[0] << 8) | frame.payload_data[1];
                    if (frame.payload_length > 2) {
                        close_reason = std::string(
                            frame.payload_data.begin() + 2,
                            frame.payload_data.end()
                        );
                    }
                }
                HandleClose(close_code, close_reason);
                break;
            }
            case OP_PING: {
                SendPong(frame.payload_data);
                {
                    std::lock_guard<std::mutex> lock(stats_mutex_);
                    stats_.ping_count++;
                }
                break;
            }
            case OP_PONG: {
                {
                    std::lock_guard<std::mutex> lock(stats_mutex_);
                    stats_.pong_count++;
                }
                break;
            }
            default:
                Close(1002, "Protocol error");
                break;
        }
        return;
    }
    ProcessMessageData(frame);
}

void WebSocketConnection::ProcessMessageData(const WebSocketFrame& frame) {
    if (frame.opcode == OP_TEXT || frame.opcode == OP_BINARY) {
        current_message_ = WebSocketMessage();
        current_message_.opcode = frame.opcode;
    }
    current_message_.data.insert(
        current_message_.data.end(),
        frame.payload_data.begin(),
        frame.payload_data.end()
    );
    if (frame.fin) {
        current_message_.complete = true;
        CompleteCurrentMessage();
    }
}

void WebSocketConnection::CompleteCurrentMessage() {
    if (message_handler_) {
        message_handler_(current_message_);
    }
    if (current_message_.opcode == OP_TEXT && text_handler_) {
        text_handler_(current_message_.GetText());
    } else if (current_message_.opcode == OP_BINARY && binary_handler_) {
        binary_handler_(current_message_.data);
    }
    current_message_ = WebSocketMessage();
}

void WebSocketConnection::SendFrame(const WebSocketFrame& frame) {
    std::vector<uint8_t> frame_data = frame.Serialize();
    {
        std::lock_guard<std::recursive_mutex> lock(write_mutex_);
        write_buffer_.insert(write_buffer_.end(), frame_data.begin(), frame_data.end());
        stats_.messages_sent++;
        stats_.bytes_sent += frame.payload_data.size();
    }
    DoWrite();
}

void WebSocketConnection::SendFrameAsync(const WebSocketFrame& frame) {
    auto self = shared_from_this();
    std::vector<uint8_t> frame_data = frame.Serialize();
    asio::async_write(socket_, asio::buffer(frame_data),
        [self, frame_data](std::error_code ec, size_t /*bytes_transferred*/) {
            //Logger::Debug("WebSocketConnection::SendFrameAsync asio::async_write {}", bytes_transferred);
            if (ec) {
                self->HandleError(ec);
            } else {
                std::lock_guard<std::mutex> lock(self->stats_mutex_);
                self->stats_.messages_sent++;
                self->stats_.bytes_sent += frame_data.size();
            }
        });
}

void WebSocketConnection::DoWrite() {
    std::lock_guard<std::recursive_mutex> lock(write_mutex_);
    if (write_buffer_.empty() || state_ != State::OPEN)
        return;
    auto self = shared_from_this();
    Logger::Trace("WebSocketConnection {} DoWrite: starting async_write", connection_id_);
    asio::async_write(socket_, asio::buffer(write_buffer_),
    [self](std::error_code ec, size_t bytes) {
        Logger::Trace("WebSocketConnection::DoWrite asio::async_write {}", bytes);
        if (ec) {
            self->HandleError(ec);
            return;
        }
        {
            std::lock_guard<std::recursive_mutex> lock(self->write_mutex_);
            self->write_buffer_.erase(self->write_buffer_.begin(), self->write_buffer_.begin() + bytes);
            self->stats_.bytes_sent += bytes;
        }
        if (!self->write_buffer_.empty()) {
            asio::post(self->socket_.get_executor(), [self]() {
                self->DoWrite();
            });
        }
    });
}

void WebSocketConnection::SendText(const std::string& text) {
    WebSocketFrame frame = WebSocketFrame::CreateTextFrame(text);
    SendFrame(frame);
}

void WebSocketConnection::SendBinary(const std::vector<uint8_t>& data) {
    WebSocketFrame frame = WebSocketFrame::CreateBinaryFrame(data);
    SendFrame(frame);
}

void WebSocketConnection::SendJson(const nlohmann::json& json) {
    SendText(json.dump());
}

void WebSocketConnection::SendPing(const std::vector<uint8_t>& data) {
    WebSocketFrame frame = WebSocketFrame::CreatePingFrame(data);
    SendFrameAsync(frame);
}

void WebSocketConnection::SendPong(const std::vector<uint8_t>& data) {
    WebSocketFrame frame = WebSocketFrame::CreatePongFrame(data);
    SendFrameAsync(frame);
}

void WebSocketConnection::Close(uint16_t code, const std::string& reason) {
    if (state_ == State::CLOSED) {
        Logger::Trace("WebSocketConnection {} already closed", connection_id_);
        return;
    }
    if (state_ == State::CLOSING) {
        Logger::Trace("WebSocketConnection {} already closing", connection_id_);
        return;
    }
    Logger::Trace("WebSocketConnection {} Close code {}, reason {}, state={}", connection_id_, code, reason, (int)state_);
    state_ = State::CLOSING;
    std::error_code ec;
    if (socket_.is_open()) {
        socket_.cancel(ec);
        if (ec) {
            Logger::Debug("WebSocketConnection {} cancel error: {}", connection_id_, ec.message());
        }
        socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        if (ec && ec != asio::error::not_connected) {
            Logger::Debug("WebSocketConnection {} shutdown error: {}", connection_id_, ec.message());
        }
        socket_.close(ec);
        if (ec && ec != asio::error::not_connected) {
            Logger::Debug("WebSocketConnection {} close error: {}", connection_id_, ec.message());
        }
    }
    state_ = State::CLOSED;
    if (close_handler_) {
        close_handler_(code, reason.empty() ? "Connection closed" : reason);
    }
    Logger::Trace("WebSocketConnection {} closed synchronously", connection_id_);
}

void WebSocketConnection::HandleError(const std::error_code& ec) {
    if (state_ == State::CLOSED || state_ == State::CLOSING) {
        return;
    }
    if (ec == asio::error::eof || ec == asio::error::connection_reset || ec == asio::error::broken_pipe) {
        Logger::Trace("WebSocketConnection {} disconnected by client", connection_id_);
    } else if (ec == asio::error::operation_aborted) {
        Logger::Debug("WebSocketConnection {} operation aborted", connection_id_);
    } else if (ec == asio::error::bad_descriptor) {
        Logger::Debug("WebSocketConnection {} bad descriptor - already closed", connection_id_);
    } else {
        Logger::Error("WebSocketConnection {} error: {}", connection_id_, ec.message());
    }
    if (state_ == State::OPEN) {
        Close(1006, "Connection error");
    }
}

void WebSocketConnection::HandleClose(uint16_t code, const std::string& reason) {
    if (state_ == State::OPEN) {
        SendFrameAsync(WebSocketFrame::CreateCloseFrame(code, reason));
    }
    Close(code, reason);
}

asio::ip::tcp::endpoint WebSocketConnection::GetRemoteEndpoint() const {
    try {
        if (socket_.is_open()) {
            return socket_.remote_endpoint();
        }
    } catch (const std::exception& e) {
        Logger::Debug("Failed to get remote endpoint: {}", e.what());
    }
    return asio::ip::tcp::endpoint();
}

WebSocketConnection::Statistics WebSocketConnection::GetStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

WebSocketServer::WebSocketServer(asio::io_context& io_context, uint16_t port)
    : io_context_(io_context)
    , acceptor_(io_context_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
    , port_(port) {
    Logger::Info("WebSocketServer created on port {}", port);
}

WebSocketServer::~WebSocketServer() {
    Stop();
}

void WebSocketServer::Start() {
    if (running_) {
        return;
    }
    running_ = true;
    DoAccept();
    Logger::Info("WebSocketServer started on port {}", port_);
}

void WebSocketServer::Stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    std::error_code ec;
    acceptor_.close(ec);
    std::lock_guard<std::mutex> lock(connections_mutex_);
    for (auto& connection : connections_) {
        connection->Close(1001, "Server going away");
    }
    connections_.clear();
    Logger::Info("WebSocketServer stopped");
}

void WebSocketServer::DoAccept() {
    if (!running_) {
        return;
    }
    acceptor_.async_accept([this](std::error_code ec, asio::ip::tcp::socket socket) {
        if (!ec) {
            WebSocketConnection::Pointer connection;
            if (connection_factory_) {
                connection = connection_factory_(std::move(socket));
            } else {
                connection = std::make_shared<WebSocketConnection>(std::move(socket));
            }
            AddConnection(connection);
            connection->Start();
            Logger::Debug("WebSocket connection accepted");
        }
        if (running_) {
            DoAccept();
        }
    });
}

void WebSocketServer::AddConnection(WebSocketConnection::Pointer connection) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    connections_.push_back(connection);
    auto weak_connection = std::weak_ptr<WebSocketConnection>(connection);
    connection->SetCloseHandler([this, weak_connection](uint16_t code, const std::string& reason) {
        Logger::Debug("WebSocketConnection::AddConnection WebSocketConnection::SetCloseHandler({}, {})", code, reason);
        auto connection = weak_connection.lock();
        if (connection) {
            RemoveConnection(connection);
        }
    });
}

void WebSocketServer::RemoveConnection(WebSocketConnection::Pointer connection) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    auto it = std::find(connections_.begin(), connections_.end(), connection);
    if (it != connections_.end()) {
        connections_.erase(it);
        Logger::Debug("WebSocket connection removed");
    }
}

void WebSocketServer::BroadcastText(const std::string& text) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    for (auto& connection : connections_) {
        if (connection->IsOpen()) {
            connection->SendText(text);
        }
    }
}

void WebSocketServer::BroadcastBinary(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    for (auto& connection : connections_) {
        if (connection->IsOpen()) {
            connection->SendBinary(data);
        }
    }
}

void WebSocketServer::BroadcastJson(const nlohmann::json& json) {
    BroadcastText(json.dump());
}

std::vector<WebSocketConnection::Pointer> WebSocketServer::GetConnections() const {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    return connections_;
}

size_t WebSocketServer::GetConnectionCount() const {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    return connections_.size();
}

WebSocketClient::WebSocketClient(asio::io_context& io_context)
    : WebSocketConnection(asio::ip::tcp::socket(io_context))
    , io_context_(io_context) {
}

void WebSocketClient::Connect(const std::string& host, uint16_t port, const std::string& path) {
    host_ = host;
    port_ = port;
    path_ = path;
    ResolveAndConnect();
}

void WebSocketClient::Connect(const std::string& url) {
    WebSocketURL parsed_url = WebSocketURL::Parse(url);
    host_ = parsed_url.host;
    port_ = parsed_url.port;
    path_ = parsed_url.path;
    if (parsed_url.protocol == "wss") {
        UseSSL(true);
    }
    ResolveAndConnect();
}

void WebSocketClient::UseSSL(bool enable) {
    if (enable) {
        ssl_context_ = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_client);
        ssl_context_->set_default_verify_paths();
        ssl_context_->set_verify_mode(asio::ssl::verify_peer);
    } else {
        ssl_context_.reset();
    }
}

void WebSocketClient::ResolveAndConnect() {
    auto resolver = std::make_shared<asio::ip::tcp::resolver>(io_context_);
    resolver->async_resolve(host_, std::to_string(port_),
        [this, resolver](const std::error_code& ec,
                        asio::ip::tcp::resolver::results_type endpoints) {
            HandleResolve(ec, endpoints);
        });
}

void WebSocketClient::HandleResolve(const std::error_code& ec,
                                   asio::ip::tcp::resolver::results_type endpoints) {
    if (ec) {
        HandleError(ec);
        return;
    }
    if (ssl_context_) {
        ssl_stream_ = std::make_unique<asio::ssl::stream<asio::ip::tcp::socket>>(
            io_context_, *ssl_context_);
        asio::async_connect(ssl_stream_->lowest_layer(), endpoints,
            [this](const std::error_code& ec, const asio::ip::tcp::endpoint& endpoint) {
                HandleConnect(ec, endpoint);
            });
    } else {
        asio::async_connect(socket_, endpoints,
            [this](const std::error_code& ec, const asio::ip::tcp::endpoint& endpoint) {
                HandleConnect(ec, endpoint);
            });
    }
}

void WebSocketClient::HandleConnect(const std::error_code& ec, const asio::ip::tcp::endpoint& endpoint) {
    if (ec) {
        HandleError(ec);
        return;
    }
    Logger::Debug("WebSocketClient connected to {}:{}", endpoint.address().to_string(), endpoint.port());
    if (ssl_stream_) {
        ssl_stream_->async_handshake(asio::ssl::stream_base::client,
            [this](const std::error_code& ec) {
                if (ec) {
                    HandleError(ec);
                    return;
                }
                HandleHandshake();
            });
    } else {
        HandleHandshake();
    }
}

void WebSocketClient::HandleHandshake() {
    SendHandshakeRequest();
}

void WebSocketClient::SendHandshakeRequest() {
    HandshakeRequest request;
    request.path = path_;
    request.SetHeader("Host", host_ + ":" + std::to_string(port_));
    request.SetHeader("Upgrade", "websocket");
    request.SetHeader("Connection", "Upgrade");
    request.SetHeader("Sec-WebSocket-Key", GenerateWebSocketKey());
    request.SetHeader("Sec-WebSocket-Version", "13");
    std::string request_str = request.Serialize();
    auto self = std::static_pointer_cast<WebSocketClient>(shared_from_this());
    auto write_handler = [self, request](std::error_code ec, size_t bytes_transferred) {
        Logger::Debug("WebSocketConnection::SendHandshakeRequest write_handler {}", bytes_transferred);
        if (ec) {
            self->HandleError(ec);
            return;
        }
        asio::async_read_until(self->socket_, self->read_buffer_, "\r\n\r\n",
            [self, request](std::error_code ec, size_t bytes_transferred) {
                Logger::Debug("WebSocketConnection::SendHandshakeRequest asio::async_read_until {}", bytes_transferred);
                if (ec) {
                    self->HandleError(ec);
                    return;
                }
                std::istream stream(&self->read_buffer_);
                std::string response_str;
                std::getline(stream, response_str, '\0');
                try {
                    HandshakeResponse response = HandshakeResponse::Parse(response_str);
                    if (!response.Validate(request)) {
                        throw std::runtime_error("Invalid handshake response");
                    }
                    self->state_ = State::OPEN;
                    Logger::Info("WebSocketClient handshake complete");
                    self->ReadFrame();
                } catch (const std::exception& e) {
                    Logger::Error("WebSocket handshake error: {}", e.what());
                    self->Close(1002, "Protocol error");
                }
            });
    };
    if (ssl_stream_) {
        asio::async_write(*ssl_stream_, asio::buffer(request_str), write_handler);
    } else {
        asio::async_write(socket_, asio::buffer(request_str), write_handler);
    }
}

std::string GenerateWebSocketKey() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    std::array<uint8_t, 16> random_bytes;
    for (int i = 0; i < 16; ++i) {
        random_bytes[i] = static_cast<uint8_t>(dis(gen));
    }
    const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    int i = 0;
    while (i < 16) {
        uint32_t octet_a = i < 16 ? random_bytes[i++] : 0;
        uint32_t octet_b = i < 16 ? random_bytes[i++] : 0;
        uint32_t octet_c = i < 16 ? random_bytes[i++] : 0;
        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
        result += base64_chars[(triple >> 18) & 0x3F];
        result += base64_chars[(triple >> 12) & 0x3F];
        result += base64_chars[(triple >> 6) & 0x3F];
        result += base64_chars[triple & 0x3F];
    }
    result = result.substr(0, 24);
    return result;
}

std::string GenerateAcceptKey(const std::string& key) {
    const std::string magic_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = key + magic_guid;
    unsigned char hash[SHA_DIGEST_LENGTH]; // 20 bytes
    SHA1(reinterpret_cast<const unsigned char*>(combined.c_str()), combined.size(), hash);
    const std::string base64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    int i = 0;
    while (i < SHA_DIGEST_LENGTH - 2) {
        uint32_t triple = (hash[i] << 16) | (hash[i+1] << 8) | hash[i+2];
        result += base64_chars[(triple >> 18) & 0x3F];
        result += base64_chars[(triple >> 12) & 0x3F];
        result += base64_chars[(triple >> 6) & 0x3F];
        result += base64_chars[triple & 0x3F];
        i += 3;
    }
    int remaining = SHA_DIGEST_LENGTH - i;
    if (remaining == 1) {
        uint32_t triple = (hash[i] << 16);
        result += base64_chars[(triple >> 18) & 0x3F];
        result += base64_chars[(triple >> 12) & 0x3F];
        result += "==";
    } else if (remaining == 2) {
        uint32_t triple = (hash[i] << 16) | (hash[i+1] << 8);
        result += base64_chars[(triple >> 18) & 0x3F];
        result += base64_chars[(triple >> 12) & 0x3F];
        result += base64_chars[(triple >> 6) & 0x3F];
        result += "=";
    }
    return result;
}

bool IsValidOpcode(uint8_t opcode) {
    return opcode == 0x0 || opcode == 0x1 || opcode == 0x2 ||
    opcode == 0x8 || opcode == 0x9 || opcode == 0xA;
}

bool IsControlOpcode(uint8_t opcode) {
    return opcode >= 0x8 && opcode <= 0xA;
}

size_t GetFrameHeaderSize(const WebSocketFrame& frame) {
    size_t size = 2;
    if (frame.payload_length <= 125) {
    } else if (frame.payload_length <= 65535) {
        size += 2;
    } else {
        size += 8;
    }
    if (frame.masked) {
        size += 4;
    }
    return size;
}

size_t GetFrameSize(const WebSocketFrame& frame) {
    return GetFrameHeaderSize(frame) + frame.payload_length;
}

void ApplyMask(uint8_t* data, size_t length, const uint8_t masking_key[4]) {
    for (size_t i = 0; i < length; ++i) {
        data[i] ^= masking_key[i % 4];
    }
}

void ApplyMask(std::vector<uint8_t>& data, const uint8_t masking_key[4]) {
    ApplyMask(data.data(), data.size(), masking_key);
}

bool IsValidCloseCode(uint16_t code) {
    if (code >= 1000 && code <= 1011) {
        return code != 1004 && code != 1005 && code != 1006;
    }
    if (code >= 3000 && code <= 4999) {
        return true;
    }
    return false;
}

std::string GetCloseReason(uint16_t code) {
    switch (code) {
        case 1000: return "Normal closure";
        case 1001: return "Going away";
        case 1002: return "Protocol error";
        case 1003: return "Unsupported data";
        case 1007: return "Invalid frame payload data";
        case 1008: return "Policy violation";
        case 1009: return "Message too big";
        case 1010: return "Missing extension";
        case 1011: return "Internal error";
        default: return "Unknown error";
    }
}

WebSocketURL WebSocketURL::Parse(const std::string& url) {
    WebSocketURL result;
    size_t protocol_end = url.find("://");
    if (protocol_end != std::string::npos) {
        result.protocol = url.substr(0, protocol_end);
        protocol_end += 3;
    } else {
        protocol_end = 0;
        result.protocol = "ws";
    }
    size_t path_start = url.find('/', protocol_end);
    if (path_start == std::string::npos) {
        path_start = url.length();
        result.path = "/";
    } else {
        result.path = url.substr(path_start);
    }
    std::string host_port = url.substr(protocol_end, path_start - protocol_end);
    size_t colon_pos = host_port.find(':');
    if (colon_pos != std::string::npos) {
        result.host = host_port.substr(0, colon_pos);
        std::string port_str = host_port.substr(colon_pos + 1);
        result.port = static_cast<uint16_t>(std::stoi(port_str));
    } else {
        result.host = host_port;
        result.port = (result.protocol == "wss") ? 443 : 80;
    }
    size_t query_start = result.path.find('?');
    if (query_start != std::string::npos) {
        result.query = result.path.substr(query_start + 1);
        result.path = result.path.substr(0, query_start);
    }
    return result;
}

std::string WebSocketURL::ToString() const {
    std::stringstream ss;
    ss << protocol << "://" << host;
    if ((protocol == "ws" && port != 80) || (protocol == "wss" && port != 443)) {
        ss << ":" << port;
    }
    ss << path;
    if (!query.empty()) {
        ss << "?" << query;
    }
    return ss.str();
}

CompressionContext::CompressionContext() = default;

CompressionContext::~CompressionContext() {
    Cleanup();
}

bool CompressionContext::Initialize(bool server, int compression_level) {
    if (initialized_) {
        Cleanup();
    }
    server_ = server;
    deflate_context_ = malloc(sizeof(z_stream));
    if (!deflate_context_) {
        return false;
    }
    z_stream* deflate_stream = static_cast<z_stream*>(deflate_context_);
    deflate_stream->zalloc = Z_NULL;
    deflate_stream->zfree = Z_NULL;
    deflate_stream->opaque = Z_NULL;
    if (deflateInit2(deflate_stream, compression_level, Z_DEFLATED,
                     -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        free(deflate_context_);
        deflate_context_ = nullptr;
        return false;
    }
    inflate_context_ = malloc(sizeof(z_stream));
    if (!inflate_context_) {
        deflateEnd(deflate_stream);
        free(deflate_context_);
        deflate_context_ = nullptr;
        return false;
    }
    z_stream* inflate_stream = static_cast<z_stream*>(inflate_context_);
    inflate_stream->zalloc = Z_NULL;
    inflate_stream->zfree = Z_NULL;
    inflate_stream->opaque = Z_NULL;
    if (inflateInit2(inflate_stream, -MAX_WBITS) != Z_OK) {
        free(inflate_context_);
        inflate_context_ = nullptr;
        deflateEnd(deflate_stream);
        free(deflate_context_);
        deflate_context_ = nullptr;
        return false;
    }
    initialized_ = true;
    return true;
}

std::vector<uint8_t> CompressionContext::Compress(const std::vector<uint8_t>& data) {
    if (!initialized_ || !deflate_context_) {
        return data;
    }
    z_stream* stream = static_cast<z_stream*>(deflate_context_);
    stream->next_in = const_cast<Bytef*>(data.data());
    stream->avail_in = static_cast<uInt>(data.size());
    std::vector<uint8_t> compressed(data.size()); // Start with same size
    stream->next_out = compressed.data();
    stream->avail_out = static_cast<uInt>(compressed.size());
    if (deflate(stream, Z_SYNC_FLUSH) != Z_OK) {
        return data;
    }
    compressed.resize(compressed.size() - stream->avail_out);
    return compressed;
}

std::vector<uint8_t> CompressionContext::Decompress(const std::vector<uint8_t>& compressed_data) {
    if (!initialized_ || !inflate_context_) {
        return compressed_data;
    }
    z_stream* stream = static_cast<z_stream*>(inflate_context_);
    stream->next_in = const_cast<Bytef*>(compressed_data.data());
    stream->avail_in = static_cast<uInt>(compressed_data.size());
    std::vector<uint8_t> decompressed(compressed_data.size() * 2);
    stream->next_out = decompressed.data();
    stream->avail_out = static_cast<uInt>(decompressed.size());
    if (inflate(stream, Z_SYNC_FLUSH) != Z_OK) {
        return compressed_data;
    }
    decompressed.resize(decompressed.size() - stream->avail_out);
    return decompressed;
}

void CompressionContext::Cleanup() {
    if (deflate_context_) {
        z_stream* stream = static_cast<z_stream*>(deflate_context_);
        deflateEnd(stream);
        free(deflate_context_);
        deflate_context_ = nullptr;
    }
    if (inflate_context_) {
        z_stream* stream = static_cast<z_stream*>(inflate_context_);
        inflateEnd(stream);
        free(inflate_context_);
        inflate_context_ = nullptr;
    }
    initialized_ = false;
}

MessageFragmenter::MessageFragmenter(size_t max_frame_size)
    : max_frame_size_(max_frame_size) {}

std::vector<WebSocketFrame> MessageFragmenter::FragmentMessage(const WebSocketMessage& message) {
    if (message.data.size() <= max_frame_size_) {
        WebSocketFrame frame;
        frame.opcode = message.opcode;
        frame.fin = true;
        frame.payload_length = message.data.size();
        frame.payload_data = message.data;
        return {frame};
    }
    std::vector<WebSocketFrame> frames;
    size_t offset = 0;
    bool first_frame = true;
    while (offset < message.data.size()) {
        WebSocketFrame frame;
        if (first_frame) {
            frame.opcode = message.opcode;
            first_frame = false;
        } else {
            frame.opcode = OP_CONTINUATION;
        }
        size_t chunk_size = std::min(max_frame_size_, message.data.size() - offset);
        frame.payload_length = chunk_size;
        frame.payload_data.assign(message.data.begin() + offset,
                                 message.data.begin() + offset + chunk_size);
        offset += chunk_size;
        frame.fin = (offset >= message.data.size());
        frames.push_back(frame);
    }
    return frames;
}

std::vector<WebSocketFrame> MessageFragmenter::FragmentText(const std::string& text) {
    WebSocketMessage msg;
    msg.SetText(text);
    return FragmentMessage(msg);
}

std::vector<WebSocketFrame> MessageFragmenter::FragmentBinary(const std::vector<uint8_t>& data) {
    WebSocketMessage msg;
    msg.opcode = OP_BINARY;
    msg.data = data;
    return FragmentMessage(msg);
}

WebSocketRateLimiter::WebSocketRateLimiter(size_t messages_per_second, size_t burst_size)
    : messages_per_second_(messages_per_second)
    , burst_size_(burst_size)
    , tokens_(burst_size)
    , last_refill_(std::chrono::steady_clock::now()) {}

bool WebSocketRateLimiter::CheckLimit() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_refill_);
    size_t refill = (elapsed.count() * messages_per_second_) / 1000;
    if (refill > 0) {
        tokens_ = std::min(burst_size_, tokens_ + refill);
        last_refill_ = now;
    }
    if (tokens_ > 0) {
        tokens_--;
        return true;
    }
    return false;
}

void WebSocketRateLimiter::Update() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_refill_);
    size_t refill = (elapsed.count() * messages_per_second_) / 1000;
    if (refill > 0) {
        tokens_ = std::min(burst_size_, tokens_ + refill);
        last_refill_ = now;
    }
}

void WebSocketRateLimiter::SetLimit(size_t messages_per_second, size_t burst_size) {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_per_second_ = messages_per_second;
    burst_size_ = burst_size;
    tokens_ = burst_size;
    last_refill_ = std::chrono::steady_clock::now();
}

} // namespace WebSocketProtocol
