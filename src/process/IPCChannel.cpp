#include "process/IPCChannel.hpp"
#include "logging/Logger.hpp"

IPCChannel::IPCChannel(asio::io_context& io, int fd)
    : io_(io), stream_(io) {
    if (fd >= 0) {
        stream_.assign(fd);
    }
}

IPCChannel::~IPCChannel() {
    Stop();
}

void IPCChannel::Start(std::function<void(const IPCEnvelope&)> onMessage) {
    onMessage_ = std::move(onMessage);
    stopped_ = false;
    doRead();
}

void IPCChannel::Stop() {
    if (stopped_.exchange(true)) return;
    std::error_code ec;
    stream_.cancel(ec);
    stream_.close(ec);
}

bool IPCChannel::IsOpen() const {
    return stream_.is_open();
}

int IPCChannel::GetFd() {
    return stream_.native_handle();
}

std::vector<uint8_t> IPCChannel::encodeFrame(const IPCEnvelope& env) {
    uint32_t innerLen = 4 + 8 + 2 + 4 + static_cast<uint32_t>(env.payload.size());
    uint32_t totalLen = innerLen;
    std::vector<uint8_t> frame(4 + innerLen);
    uint32_t netTotal = htonl(totalLen);
    memcpy(frame.data(), &netTotal, sizeof(netTotal));
    BinaryProtocol::BinaryWriter w;
    w.WriteUInt32(env.correlationId);
    w.WriteUInt64(env.sessionId);
    w.WriteUInt16(env.messageType);
    w.WriteUInt32(static_cast<uint32_t>(env.payload.size()));
    w.WriteRaw(env.payload.data(), env.payload.size());
    auto inner = w.GetBuffer();
    memcpy(frame.data() + 4, inner.data(), inner.size());
    return frame;
}

IPCEnvelope IPCChannel::decodeFrame(const uint8_t* data, size_t len) {
    IPCEnvelope env;
    if (len < 18) return env;
    BinaryProtocol::BinaryReader r(data, len);
    env.correlationId = r.ReadUInt32();
    env.sessionId = r.ReadUInt64();
    env.messageType = r.ReadUInt16();
    uint32_t bodyLen = r.ReadUInt32();
    if (bodyLen > 0 && r.Remaining() >= bodyLen) {
        env.payload = r.ReadBytes(bodyLen);
    }
    return env;
}

void IPCChannel::doRead() {
    if (stopped_) return;
    auto self = shared_from_this();
    auto lengthBuf = std::make_shared<uint32_t>(0);
    Logger::Trace("IPCChannel::doRead: starting async_read on fd={}, stopped={}", stream_.native_handle(), stopped_.load());
    asio::async_read(stream_, asio::buffer(lengthBuf.get(), sizeof(uint32_t)),
    [self, lengthBuf](std::error_code ec, std::size_t) {
        if (ec || self->stopped_) {
            Logger::Trace("IPCChannel::doRead: async_read error ec={}, stopped={}", ec.value(), self->stopped_.load());
            return;
        }
        uint32_t msgLen = ntohl(*lengthBuf);
        if (msgLen == 0 || msgLen > BinaryProtocol::MAX_MESSAGE_SIZE) {
            self->doRead();
            return;
        }
        auto msgBuffer = std::make_shared<std::vector<uint8_t>>(msgLen);
        asio::async_read(self->stream_, asio::buffer(*msgBuffer),
        [self, msgBuffer](std::error_code ec, std::size_t) {
            if (ec || self->stopped_) return;
            IPCEnvelope env = decodeFrame(msgBuffer->data(), msgBuffer->size());
            Logger::Trace("IPCChannel::doRead: received envelope, corrId={}, session={}, type={}, payloadSize={}", env.correlationId, env.sessionId, env.messageType, env.payload.size());
            if (self->onMessage_) {
                self->onMessage_(env);
            }
            self->doRead();
        });
    });
}

void IPCChannel::doWrite() {
    std::vector<uint8_t> data;
    {
        std::lock_guard<std::mutex> lock(writeMutex_);
        if (writeQueue_.empty()) {
            writing_ = false;
            return;
        }
        data = std::move(writeQueue_.front());
        writeQueue_.pop_front();
    }
    auto self = shared_from_this();
    asio::async_write(stream_, asio::buffer(data),
    [self, data](std::error_code ec, std::size_t) {
        if (ec) {
            Logger::Error("IPCChannel::doWrite failed: {}", ec.message());
            return;
        }
        Logger::Trace("IPCChannel::doWrite: wrote {} bytes", data.size());
        self->doWrite();
    });
}

void IPCChannel::SendAsync(const IPCEnvelope& envelope) {
    auto frame = encodeFrame(envelope);
    {
        std::lock_guard<std::mutex> lock(writeMutex_);
        writeQueue_.push_back(std::move(frame));
        if (!writing_) {
            writing_ = true;
        } else {
            return;
        }
    }
    Logger::Trace("IPCChannel::SendAsync: posting doWrite, frameSize={}, fd={}", frame.size(), stream_.native_handle());
    asio::post(io_, [self = shared_from_this()]() {
        self->doWrite();
    });
}

void IPCChannel::SendSync(const IPCEnvelope& envelope) {
    auto frame = encodeFrame(envelope);
    std::error_code ec;
    asio::write(stream_, asio::buffer(frame), ec);
    if (ec) {
        Logger::Error("IPCChannel::SendSync failed: {}", ec.message());
    }
}
