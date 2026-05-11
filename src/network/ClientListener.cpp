#include "network/ClientListener.hpp"

ClientListener::ClientListener(const WorkerGroupConfig& groupConfig, int masterFd)
    : io_()
    , pipe_(io_)
    , manager_(std::make_shared<ConnectionManager>(groupConfig,
        [this](uint32_t corrId, uint64_t sessId, uint16_t msgType, const std::vector<uint8_t>& body) {
            sendToMaster(corrId, sessId, msgType, body);
        }))
{
    if (masterFd != -1) pipe_.assign(masterFd);
}

ClientListener::~ClientListener() { Shutdown(); }

void ClientListener::Start() {
    std::thread([this]() { manager_->Start(); }).detach();
    doRead();
    ioThread_ = std::thread([this]() { io_.run(); });
}

void ClientListener::Shutdown() {
    if (stopping_.exchange(true)) return;
    manager_->Shutdown();
    io_.stop();
    if (ioThread_.joinable()) ioThread_.join();
}

void ClientListener::sendToMaster(uint32_t correlationId, uint64_t sessionId, uint16_t messageType, const std::vector<uint8_t>& body) {
    BinaryProtocol::BinaryWriter w;
    w.WriteUInt32(correlationId);
    w.WriteUInt64(sessionId);
    w.WriteUInt16(messageType);
    w.WriteUInt32(static_cast<uint32_t>(body.size()));
    w.WriteBytes(body.data(), body.size());
    auto frame = w.GetBuffer();
    {
        std::lock_guard<std::mutex> lock(writeMutex_);
        writeQueue_.push_back(std::move(frame));
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
            if (!ec) doWrite();
        });
}

void ClientListener::doRead() {
    auto header = std::make_shared<std::vector<uint8_t>>(14);
    asio::async_read(pipe_, asio::buffer(*header),
        [this, header](std::error_code ec, size_t) {
            if (ec) {
                if (!stopping_) doRead();
                return;
            }
            BinaryProtocol::BinaryReader r(header->data(), header->size());
            uint32_t corrId = r.ReadUInt32();
            uint64_t sessionId = r.ReadUInt64();
            uint16_t msgType = r.ReadUInt16();
            uint32_t bodyLen = r.ReadUInt32();
            auto body = std::make_shared<std::vector<uint8_t>>(bodyLen);
            asio::async_read(pipe_, asio::buffer(*body),
                [this, corrId, sessionId, msgType, body](std::error_code ec, size_t) {
                    if (!ec) {
                        manager_->OnMasterReply(corrId, *body);
                    }
                    doRead();
                });
        });
}
