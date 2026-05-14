#include "network/ClientListener.hpp"

ClientListener::ClientListener(const WorkerGroupConfig& groupConfig, int masterFd, int workerId)
    : workerId_(workerId)
    , io_()
    , pipe_(io_)
    , manager_(std::make_shared<ConnectionManager>(groupConfig,
        [this](uint32_t corrId, uint64_t sessId, uint16_t msgType, const std::vector<uint8_t>& body) {
            sendToMaster(corrId, sessId, msgType, body);
        }, workerId))
{
    if (masterFd != -1) pipe_.assign(masterFd);
}

ClientListener::~ClientListener() { Shutdown(); }

void ClientListener::Start() {
    std::thread([this]() { manager_->Start(); }).detach();
    doRead();
    ioThread_ = std::thread([this]() {
        Logger::Trace("ClientListener::Start: worker {} entering io_context::run()", workerId_);
        io_.run();
        Logger::Trace("ClientListener::Start: worker {} exiting io_context::run()", workerId_);
    });
}

void ClientListener::Shutdown() {
    if (stopping_.exchange(true)) return;
    manager_->Shutdown();
    io_.stop();
    io_.poll();
    if (ioThread_.joinable()) ioThread_.join();
}

void ClientListener::sendToMaster(uint32_t correlationId, uint64_t sessionId, uint16_t messageType, const std::vector<uint8_t>& body) {
    BinaryProtocol::BinaryWriter w;
    w.WriteUInt32(correlationId);
    w.WriteUInt64(sessionId);
    w.WriteUInt16(messageType);
    uint32_t len = static_cast<uint32_t>(body.size());
    w.WriteUInt32(len);
    w.WriteRaw(body.data(), len);
    auto frame = w.GetBuffer();
    uint32_t frameLen = htonl(static_cast<uint32_t>(frame.size()));
    std::vector<uint8_t> packet(sizeof(frameLen) + frame.size());
    memcpy(packet.data(), &frameLen, sizeof(frameLen));
    memcpy(packet.data() + sizeof(frameLen), frame.data(), frame.size());
    Logger::Trace("Worker sending to master: corrId={}, sessId={}, msgType={}, bodySize={}",
                 correlationId, sessionId, messageType, body.size());
    {
        std::lock_guard<std::mutex> lock(writeMutex_);
        writeQueue_.push_back(std::move(packet));
        if (!writing_) {
            writing_ = true;
            startWrite();
        }
    }
}

void ClientListener::startWrite() {
    asio::post(io_, [this]() { doWrite(); });
}

void ClientListener::doWrite() {
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
    asio::async_write(pipe_, asio::buffer(data),
    [this](std::error_code ec, size_t) {
        if (ec)
            Logger::Error("Worker write to master failed: {}", ec.message());
        else
            doWrite();
    });
}

void ClientListener::doRead() {
    auto lengthBuf = std::make_shared<uint32_t>(0);
    asio::async_read(pipe_, asio::buffer(lengthBuf.get(), sizeof(uint32_t)),
    [this, lengthBuf](std::error_code ec, size_t) {
        if (ec) {
            Logger::Error("ClientListener::doRead asio::async_read: {}", ec.message());
            if (!stopping_) doRead();
            return;
        }
        uint32_t msgLen = ntohl(*lengthBuf);
        if (msgLen == 0 || msgLen > 10 * 1024 * 1024) {
            doRead();
            return;
        }
        auto msgBuffer = std::make_shared<std::vector<uint8_t>>(msgLen);
        asio::async_read(pipe_, asio::buffer(*msgBuffer),
        [this, msgBuffer](std::error_code ec, size_t) {
            if (ec) {
                Logger::Error("ClientListener::doRead asio::async_read(msgBuffer): {}", ec.message());
                if (!stopping_) doRead();
                return;
            }
            if (msgBuffer->size() < 18) {
                doRead();
                return;
            }
            BinaryProtocol::BinaryReader r(msgBuffer->data(), msgBuffer->size());
            uint32_t corrId   = r.ReadUInt32();
            uint64_t sessionId = r.ReadUInt64();(void)sessionId;
            uint16_t msgType  = r.ReadUInt16();(void)msgType;
            uint32_t bodyLen  = r.ReadUInt32();
            std::vector<uint8_t> body;
            if (bodyLen > 0 && r.Remaining() >= bodyLen) {
                body = r.ReadBytes(bodyLen);
            }
            if (corrId == 0) {
                manager_->OnMasterPush(sessionId, body);
                doRead();
                return;
            }
            manager_->OnMasterReply(corrId, body);
            doRead();
        });
    });
}
