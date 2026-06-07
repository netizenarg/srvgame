#include "network/ClientListener.hpp"

ClientListener::ClientListener(const WorkerGroupConfig& groupConfig, int masterFd, int workerId)
    : workerId_(workerId)
    , io_()
    , channel_(std::make_shared<IPCChannel>(io_, masterFd))
    , manager_(std::make_shared<ConnectionManager>(groupConfig,
        [this](uint32_t corrId, uint64_t sessId, uint16_t msgType, const std::vector<uint8_t>& body) {
            sendToMaster(corrId, sessId, msgType, body);
        }, workerId))
{
}

ClientListener::~ClientListener() { Shutdown(); }

void ClientListener::Start() {
    std::thread([this]() { manager_->Start(); }).detach();
    channel_->Start([this](const IPCEnvelope& env) { onMasterMessage(env); });
    ioThread_ = std::thread([this]() {
        io_.run();
    });
}

void ClientListener::Shutdown() {
    if (stopping_.exchange(true)) return;
    manager_->Shutdown();
    channel_->Stop();
    io_.stop();
    io_.poll();
    if (ioThread_.joinable()) ioThread_.join();
}

void ClientListener::onMasterMessage(const IPCEnvelope& env) {
    if (env.correlationId == 0) {
        manager_->OnMasterPush(env.sessionId, env.payload);
    } else {
        manager_->OnMasterReply(env.correlationId, env.payload);
    }
}

void ClientListener::sendToMaster(uint32_t correlationId, uint64_t sessionId, uint16_t messageType, const std::vector<uint8_t>& body) {
    IPCEnvelope env;
    env.correlationId = correlationId;
    env.sessionId = sessionId;
    env.messageType = messageType;
    env.payload = body;
    channel_->SendAsync(env);
}
