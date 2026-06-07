#include "network/MasterRouter.hpp"
#include "process/ProcessPool.hpp"
#include "game/GameLogic.hpp"

MasterRouter::MasterRouter(ProcessPool& pool, GameLogic& gameLogic)
    : pool_(pool), gameLogic_(gameLogic) {}

void MasterRouter::Initialize() {
    sendReplyCb_ = [this](uint64_t sessionId, const std::vector<uint8_t>& data) {
        int workerId = static_cast<int>(sessionId >> 48);
        pool_.PushToWorker(workerId, sessionId, data);
    };
    gameLogic_.SetSendReplyCallback(sendReplyCb_);
    gameLogic_.SetGetSessionIdsInRadiusCallback([](const glm::vec3& pos, float radius) -> std::vector<uint64_t> {
        std::vector<uint64_t> sids;
        auto& pm = PlayerManager::GetInstance();
        auto players = pm.GetPlayersInRadius(pos, radius);
        for (auto& p : players) {
            uint64_t sid = pm.GetSessionIdByPlayerId(p->GetId());
            if (sid != 0) sids.push_back(sid);
        }
        return sids;
    });
    WireCallbacks();
}

void MasterRouter::OnChildWorkerMessage(int workerId, uint32_t /*corrId*/,
                                         uint64_t sessionId, uint16_t msgType,
                                         const std::vector<uint8_t>& body) {
    RouteToGameLogic(sessionId, msgType, body);
}

void MasterRouter::OnGameLogicResponse(const IPCEnvelope& env) {
    int workerId = static_cast<int>(env.sessionId >> 48);
    pool_.PushToWorker(workerId, env.sessionId, env.payload);
}

void MasterRouter::RouteToGameLogic(uint64_t sessionId, uint16_t msgType, const std::vector<uint8_t>& body) {
    switch (msgType) {
        case BinaryProtocol::MESSAGE_TYPE_AUTHENTICATION: {
            BinaryProtocol::BinaryReader r(body.data(), body.size());
            AuthenticationData auth;
            auth.username = r.ReadString();
            auth.password = r.ReadString();
            auth.session_id = sessionId;
            gameLogic_.OnAuthentication(auth);
            break;
        }
        case BinaryProtocol::MESSAGE_TYPE_CHAT_MESSAGE: {
            BinaryProtocol::BinaryReader r(body.data(), body.size());
            std::string sender = r.ReadString();
            std::string message = r.ReadString();
            uint64_t timestamp = r.ReadUInt64();
            BinaryProtocol::BinaryWriter w;
            w.WriteUInt16(BinaryProtocol::MESSAGE_TYPE_CHAT_MESSAGE);
            w.WriteString(sender);
            w.WriteString(message);
            w.WriteUInt64(timestamp);
            std::vector<uint8_t> response = w.GetBuffer();
            pool_.BroadcastToAllWorkers(response);
            break;
        }
        case BinaryProtocol::MESSAGE_TYPE_CHUNK_PARAMS: {
            ChunkParams req;
            req.session_id = sessionId;
            gameLogic_.OnChunkParams(req);
            break;
        }
        case BinaryProtocol::MESSAGE_TYPE_CHUNK_DATA: {
            BinaryProtocol::BinaryReader r(body.data(), body.size());
            ChunkData req;
            req.x = r.ReadInt32();
            req.z = r.ReadInt32();
            req.lod = r.ReadUInt8();
            req.player_x = r.ReadFloat();
            req.player_y = r.ReadFloat();
            req.player_z = r.ReadFloat();
            req.session_id = sessionId;
            gameLogic_.OnChunkData(req);
            break;
        }
        case BinaryProtocol::MESSAGE_TYPE_COLLISION_CHECK: {
            BinaryProtocol::BinaryReader r(body.data(), body.size());
            CollisionData req;
            req.position = r.ReadVector3();
            req.radius = r.ReadFloat();
            req.session_id = sessionId;
            gameLogic_.OnCollisionCheck(req);
            break;
        }
        case BinaryProtocol::MESSAGE_TYPE_PLAYER_STATE: {
            BinaryProtocol::BinaryReader r(body.data(), body.size());
            PlayerStateData state;
            state.player_id = gameLogic_.GetPlayerIdBySession(sessionId);
            state.input_id = r.ReadUInt32();
            state.position = r.ReadVector3();
            state.velocity = r.ReadVector3();
            state.rotation = r.ReadVector3();
            state.on_ground = r.ReadUInt8() != 0;
            state.timestamp = r.ReadUInt64();
            state.session_id = sessionId;
            gameLogic_.OnPlayerState(state);
            break;
        }
        case BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION: {
            BinaryProtocol::BinaryReader r(body.data(), body.size());
            uint64_t player_id = r.ReadUInt64();
            glm::vec3 position = r.ReadVector3();
            glm::vec3 velocity = r.ReadVector3();
            uint64_t timestamp = r.ReadUInt64();
            PlayerPositionData pos;
            pos.player_id = player_id;
            pos.position = position;
            pos.velocity = velocity;
            pos.timestamp = timestamp;
            pos.session_id = sessionId;
            gameLogic_.OnPlayerPosition(pos);
            break;
        }
        case BinaryProtocol::MESSAGE_TYPE_NPC_INTERACTION: {
            BinaryProtocol::BinaryReader r(body.data(), body.size());
            NpcData req;
            req.npc_id = r.ReadUInt64();
            req.type = r.ReadString();
            req.session_id = sessionId;
            gameLogic_.OnNPCInteraction(req);
            break;
        }
        case BinaryProtocol::MESSAGE_TYPE_FAMILIAR_COMMAND: {
            BinaryProtocol::BinaryReader r(body.data(), body.size());
            FamiliarData req;
            req.session_id = sessionId;
            req.familiar_id = r.ReadUInt64();
            req.target_id = r.ReadUInt64();
            req.command = r.ReadString();
            gameLogic_.OnFamiliarCommand(req);
            break;
        }
        case BinaryProtocol::MESSAGE_TYPE_ENTITY_SPAWN: {
            BinaryProtocol::BinaryReader r(body.data(), body.size());
            EntitySpawnData req;
            req.session_id = sessionId;
            req.entity_id = r.ReadUInt64();
            req.type = r.ReadInt32();
            req.position = r.ReadVector3();
            gameLogic_.OnEntitySpawnRequest(req);
            break;
        }
        case BinaryProtocol::MESSAGE_TYPE_LOOT_PICKUP: {
            BinaryProtocol::BinaryReader r(body.data(), body.size());
            LootPickupData req;
            req.loot_id = r.ReadUInt64();
            req.quantity = r.ReadUInt16();
            req.session_id = sessionId;
            gameLogic_.OnLootPickup(req);
            break;
        }
        case BinaryProtocol::MESSAGE_TYPE_INVENTORY_MOVE: {
            BinaryProtocol::BinaryReader r(body.data(), body.size());
            InventoryData req;
            req.loot_id = r.ReadUInt64();
            req.target_id = r.ReadUInt64();
            req.move_type = static_cast<InventoryMoveType>(r.ReadUInt8());
            req.inv_slot_id = r.ReadInt32();
            req.use_slot_id = r.ReadInt32();
            req.quantity = r.ReadUInt16();
            req.session_id = sessionId;
            gameLogic_.OnInventory(req);
            break;
        }
        case BinaryProtocol::MESSAGE_TYPE_PLAYER_DISCONNECT: {
            gameLogic_.OnPlayerDisconnected(sessionId);
            break;
        }
        default:
            Logger::Warn("Unhandled message type from child worker: {}", msgType);
            break;
    }
}

void MasterRouter::SendToChildWorker(int workerId, uint64_t sessionId, const std::vector<uint8_t>& data) {
    pool_.PushToWorker(workerId, sessionId, data);
}

void MasterRouter::BroadcastToChildWorkers(const std::vector<uint8_t>& data) {
    pool_.BroadcastToAllWorkers(data);
}

void MasterRouter::WireCallbacks() {
    gameLogic_.SetSendAuthenticationResponseCallback([this](uint64_t session_id, const std::string& message, uint64_t player_id) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt16(BinaryProtocol::MESSAGE_TYPE_AUTHENTICATION);
        w.WriteUInt64(gameLogic_.GetCurrentTimestamp());
        w.WriteUInt64(player_id);
        w.WriteString(message);
        auto buf = w.GetBuffer();
        pool_.PushToWorker(static_cast<int>(session_id >> 48), session_id, buf);
    });
    gameLogic_.SetSendChunkParamsCallback([this](uint64_t session_id, const ChunkParams& data) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt16(BinaryProtocol::MESSAGE_TYPE_CHUNK_PARAMS);
        w.WriteUInt64(data.timestamp);
        w.WriteUInt32(static_cast<uint32_t>(data.size));
        w.WriteFloat(data.spacing);
        auto buf = w.GetBuffer();
        pool_.PushToWorker(static_cast<int>(session_id >> 48), session_id, buf);
    });
    gameLogic_.SetSendChunkCallback([this](uint64_t session_id, const ChunkData& data) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt16(BinaryProtocol::MESSAGE_TYPE_CHUNK_DATA);
        w.WriteUInt64(data.timestamp);
        w.WriteInt32(data.x);
        w.WriteInt32(data.z);
        uint32_t vertSize = static_cast<uint32_t>(data.vertices.size() * sizeof(float));
        w.WriteUInt32(vertSize);
        w.WriteBytes(reinterpret_cast<const uint8_t*>(data.vertices.data()), vertSize);
        uint32_t idxSize = static_cast<uint32_t>(data.indices.size() * sizeof(uint32_t));
        w.WriteUInt32(idxSize);
        w.WriteBytes(reinterpret_cast<const uint8_t*>(data.indices.data()), idxSize);
        w.WriteUInt32(data.lod);
        auto buf = w.GetBuffer();
        pool_.PushToWorker(static_cast<int>(session_id >> 48), session_id, buf);
    });
    gameLogic_.SetSendCollisionResponseCallback([this](uint64_t session_id, const CollisionResult& result) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt16(BinaryProtocol::MESSAGE_TYPE_COLLISION_CHECK);
        w.WriteUInt64(gameLogic_.GetCurrentTimestamp());
        w.WriteUInt8(result.collided ? 1 : 0);
        w.WriteUInt64(result.collided_id);
        w.WriteFloat(result.penetration);
        w.WriteVector3(result.resolution);
        pool_.PushToWorker(static_cast<int>(session_id >> 48), session_id, w.GetBuffer());
    });
    gameLogic_.SetPlayerStateCallback([this](const PlayerStateData& state) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt16(BinaryProtocol::MESSAGE_TYPE_PLAYER_STATE);
        w.WriteUInt64(state.timestamp);
        w.WriteUInt64(state.player_id);
        w.WriteVector3(state.position);
        w.WriteVector3(state.velocity);
        w.WriteVector3(state.rotation);
        w.WriteUInt8(state.on_ground ? 1 : 0);
        auto sids = gameLogic_.GetSessionsInRadius(state.position);
        for (auto sid : sids) {
            if (sid == state.session_id) continue;
            pool_.PushToWorker(static_cast<int>(sid >> 48), sid, w.GetBuffer());
        }
    });
    gameLogic_.SetBroadcastPlayerPositionCallback([this](const PlayerPositionData& data, float /*radius*/) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt16(BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION);
        w.WriteUInt64(data.timestamp);
        w.WriteUInt64(data.player_id);
        w.WriteVector3(data.position);
        w.WriteVector3(data.velocity);
        auto sids = gameLogic_.GetSessionsInRadius(data.position);
        for (auto sid : sids) {
            if (sid == data.session_id) continue;
            pool_.PushToWorker(static_cast<int>(sid >> 48), sid, w.GetBuffer());
        }
    });
    gameLogic_.SetSendPlayerSpawnCallback([this](uint64_t session_id, const PlayerSpawnData& data) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt16(BinaryProtocol::MESSAGE_TYPE_PLAYER_SPAWN);
        w.WriteUInt64(gameLogic_.GetCurrentTimestamp());
        w.WriteUInt64(data.player_id);
        w.WriteString(data.name);
        w.WriteVector3(data.position);
        w.WriteFloat(data.yaw);
        w.WriteFloat(data.health);
        w.WriteFloat(data.max_health);
        pool_.PushToWorker(static_cast<int>(session_id >> 48), session_id, w.GetBuffer());
    });
    gameLogic_.SetSendPlayerDespawnCallback([this](uint64_t session_id, const PlayerDespawnData& data) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt16(BinaryProtocol::MESSAGE_TYPE_PLAYER_DESPAWN);
        w.WriteUInt64(data.timestamp);
        w.WriteUInt64(data.player_id);
        pool_.PushToWorker(static_cast<int>(session_id >> 48), session_id, w.GetBuffer());
    });
    gameLogic_.SetSendPlayerUpdateCallback([this](uint64_t session_id, const PlayerUpdateData& data) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt16(BinaryProtocol::MESSAGE_TYPE_PLAYER_UPDATE);
        w.WriteUInt64(data.timestamp);
        w.WriteUInt64(data.player_id);
        w.WriteVector3(data.position);
        w.WriteFloat(data.yaw);
        w.WriteFloat(data.health);
        w.WriteFloat(data.max_health);
        w.WriteString(data.name);
        pool_.PushToWorker(static_cast<int>(session_id >> 48), session_id, w.GetBuffer());
    });
    gameLogic_.SetSendPlayersUpdateCallback([this](uint64_t session_id, const PlayerUpdateData& data) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt16(BinaryProtocol::MESSAGE_TYPE_PLAYERS_UPDATE);
        w.WriteUInt64(data.timestamp);
        w.WriteUInt64(data.player_id);
        w.WriteVector3(data.position);
        w.WriteFloat(data.yaw);
        w.WriteFloat(data.health);
        w.WriteFloat(data.max_health);
        w.WriteString(data.name);
        auto sids = gameLogic_.GetSessionsInRadius(data.position);
        for (auto sid : sids) {
            if (sid != session_id) {
                pool_.PushToWorker(static_cast<int>(sid >> 48), sid, w.GetBuffer());
            }
        }
    });
    gameLogic_.SetSendNPCInteractionResponseCallback([this](uint64_t session_id, const NpcData& response) {
        BinaryProtocol::BinaryWriter w;
        if (response.type == "combat") {
            w.WriteUInt64(response.timestamp);
            w.WriteUInt64(response.player_id);
            w.WriteUInt64(response.npc_id);
            w.WriteFloat(response.damage);
            w.WriteFloat(response.health);
            w.WriteUInt8(response.is_dead ? 1 : 0);
            pool_.PushToWorker(static_cast<int>(session_id >> 48), session_id, w.GetBuffer());
        } else if (response.type == "dialogue") {
            w.WriteUInt64(response.npc_id);
            w.WriteJson(response.quests);
            w.WriteUInt64(response.timestamp);
            pool_.PushToWorker(static_cast<int>(session_id >> 48), session_id, w.GetBuffer());
        } else {
            w.WriteUInt64(0);
            w.WriteString("NPC interaction failed");
            pool_.PushToWorker(static_cast<int>(session_id >> 48), session_id, w.GetBuffer());
        }
    });
    gameLogic_.SetSendFamiliarCommandResponseCallback([this](uint64_t session_id, const FamiliarData& response) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt64(response.timestamp);
        w.WriteUInt64(response.familiar_id);
        w.WriteUInt64(response.target_id);
        w.WriteString(response.command);
        pool_.PushToWorker(static_cast<int>(session_id >> 48), session_id, w.GetBuffer());
    });
    gameLogic_.SetSendEntitySpawnResponseCallback([this](uint64_t session_id, const EntitySpawnData& response) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt64(response.timestamp);
        w.WriteUInt64(response.entity_id);
        w.WriteInt32(response.type);
        w.WriteVector3(response.position);
        pool_.PushToWorker(static_cast<int>(session_id >> 48), session_id, w.GetBuffer());
    });
    gameLogic_.SetSendLootPickupResponseCallback([this](uint64_t session_id, const LootPickupData& response) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt64(response.timestamp);
        w.WriteUInt64(response.loot_id);
        w.WriteUInt16(response.quantity);
        pool_.PushToWorker(static_cast<int>(session_id >> 48), session_id, w.GetBuffer());
    });
    gameLogic_.SetSendInventoryResponseCallback([this](uint64_t session_id, const InventoryData& response) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt64(response.timestamp);
        w.WriteUInt64(response.loot_id);
        w.WriteUInt64(response.target_id);
        w.WriteUInt8(static_cast<uint8_t>(response.move_type));
        w.WriteInt32(response.inv_slot_id);
        w.WriteInt32(response.use_slot_id);
        w.WriteUInt16(response.quantity);
        pool_.PushToWorker(static_cast<int>(session_id >> 48), session_id, w.GetBuffer());
    });
}
