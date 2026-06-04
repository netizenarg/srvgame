#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "logging/Logger.hpp"
#include "network/BinaryProtocol.hpp"
#include "process/IPCChannel.hpp"
#include "game/GameData.hpp"

class ProcessPool;
class GameLogic;

struct PendingRequest {
    int workerId;
    uint32_t correlationId;
};

class MasterRouter {
public:
    MasterRouter(ProcessPool& pool, GameLogic& gameLogic);
    void Initialize();

    void OnChildWorkerMessage(int workerId, uint32_t corrId,
                              uint64_t sessionId, uint16_t msgType,
                              const std::vector<uint8_t>& body);

    void OnGameLogicResponse(const IPCEnvelope& env);

    void SendToChildWorker(int workerId, uint64_t sessionId, const std::vector<uint8_t>& data);
    void BroadcastToChildWorkers(const std::vector<uint8_t>& data);

private:
    ProcessPool& pool_;
    GameLogic& gameLogic_;

    std::function<void(uint64_t, const std::vector<uint8_t>&)> sendReplyCb_;

    void WireCallbacks();
    void RouteToGameLogic(uint64_t sessionId, uint16_t msgType, const std::vector<uint8_t>& body);
};
