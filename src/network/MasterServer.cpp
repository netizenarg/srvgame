#include "network/MasterServer.hpp"

extern std::atomic<bool> g_shutdown;

MasterServer::MasterServer(asio::io_context& io,
                           const std::vector<WorkerGroupConfig>& workerGroups,
                           const ConfigManager& config,
                           GameLogic& gameLogic,
                           DatabaseService& dbService,
                           const std::string& configPath)
: io_(io), gameLogic_(gameLogic), processPool_(io, workerGroups),
config_(config), configPath_(configPath)
{
    gameLogic_.SetDatabaseService(&dbService);
}

void MasterServer::Initialize()
{
    assignVirtualId_ = [this](uint64_t sessionId, int workerId) -> uint64_t {
        auto it = sessionToVirtual_.find(sessionId);
        if (it == sessionToVirtual_.end()) {
            uint64_t newId = (static_cast<uint64_t>(workerId) << 32) | (nextPersistentId_++);
            sessionToVirtual_[sessionId] = newId;
            return newId;
        }
        return it->second;
    };
    sendReplyCb_ = [this](uint64_t clientSessionId, const std::vector<uint8_t>& data) {
        auto it = sessionToVirtual_.find(clientSessionId);
        if (it != sessionToVirtual_.end()) {
            uint64_t virtualId = it->second;
            int workerId = static_cast<int>(virtualId >> 32);
            uint32_t corrId = static_cast<uint32_t>(virtualId & 0xFFFFFFFF);
            processPool_.SendReplyToWorker(workerId, corrId, data);
        } else {
            Logger::Error("No virtual mapping for session {}", clientSessionId);
        }
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
    processPool_.SetMasterMessageHandler([this](int workerId, uint32_t /*correlationId*/,
                                               uint64_t sessionId, uint16_t messageType,
                                               const std::vector<uint8_t>& body) {
        uint64_t virtualId = assignVirtualId_(sessionId, workerId);
        sessionToVirtual_[sessionId] = virtualId;
        switch (messageType) {
            case BinaryProtocol::MESSAGE_TYPE_AUTHENTICATION: {
                BinaryProtocol::BinaryReader r(body.data(), body.size());
                AuthenticationData auth;
                auth.username = r.ReadString();
                auth.password = r.ReadString();
                auth.session_id = sessionId;
                gameLogic_.OnAuthentication(auth);
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
                Logger::Warn("Unhandled master message type: {}", messageType);
                break;
        }
    });
    WireCallbacks();
    processPool_.SetWorker([this](int workerId, const WorkerGroupConfig& groupConfig, int masterReadFd) {
        WorkerClient(workerId, groupConfig, masterReadFd, configPath_);
    });
    processPool_.Initialize();
    gameLogic_.Initialize();
    StartShutdownWatcher();
}

void MasterServer::Run()
{
    io_.run();
}

void MasterServer::Shutdown()
{
    processPool_.Shutdown();
    io_.stop();
}

void MasterServer::WorkerClient(int workerId, const WorkerGroupConfig& groupConfig,
                              int masterReadFd, const std::string& configPath)
{
    try {
        asio::io_context ipc_io;
        asio::signal_set signals(ipc_io, SIGINT, SIGTERM);
        signals.async_wait([&](const asio::error_code& ec, int signo) {
            if (!ec) {
                Logger::Info("Worker {} received signal {}", workerId, signo);
                g_shutdown.store(true);
                ipc_io.stop();
            }
        });
        asio::posix::stream_descriptor masterPipe(ipc_io);
        if (masterReadFd != -1) masterPipe.assign(masterReadFd);
        std::thread ipc_thread([&]() { ipc_io.run(); });
        auto& config = ConfigManager::GetInstance();
        Logger::InitializeWithWorkerId(workerId);
        Logger::Info("Worker {} starting for group: {} ({}:{})", workerId,
                     groupConfig.protocol, groupConfig.host, groupConfig.port);
        if (!config.LoadConfig(configPath)) {
            Logger::Critical("Worker {} failed to load configuration", workerId);
            return;
        }
        ClientServer server(groupConfig, config);
        server.SetMasterSender([masterReadFd](const std::vector<uint8_t>& data) {
            uint32_t len = htonl(static_cast<uint32_t>(data.size()));
            std::vector<uint8_t> frame(sizeof(len) + data.size());
            std::memcpy(frame.data(), &len, sizeof(len));
            std::memcpy(frame.data() + sizeof(len), data.data(), data.size());
            ssize_t res = write(masterReadFd, frame.data(), frame.size());
            (void)res;
        });
        std::function<void()> start_read;
        auto read_buffer = std::make_shared<std::array<uint8_t, 4>>();
        start_read = [&, read_buffer]() {
            asio::async_read(masterPipe, asio::buffer(*read_buffer),
            [&, read_buffer](const std::error_code& ec, size_t) {
                if (ec) { start_read(); return; }
                uint32_t len = ntohl(*reinterpret_cast<uint32_t*>(read_buffer->data()));
                auto payload = std::make_shared<std::vector<uint8_t>>(len);
                asio::async_read(masterPipe, asio::buffer(*payload),
                [&, payload](const std::error_code& ec, size_t) {
                    if (!ec) {
                        BinaryProtocol::BinaryReader reader(payload->data(), payload->size());
                        uint32_t corrId = reader.ReadUInt32();
                        std::vector<uint8_t> replyData = reader.ReadBytes(payload->size() - sizeof(corrId));
                        server.OnMasterReply(corrId, replyData);
                    }
                    start_read();
                });
            });
        };
        asio::post(ipc_io, start_read);
        server.InitSessionFactory(workerId);
        if (server.Initialize()) {
            Logger::Info("Worker {} game server initialized on {}:{} (protocol: {})",
                         workerId, groupConfig.host, groupConfig.port, groupConfig.protocol);
            std::thread shutdown_trigger([&server]() {
                while (!g_shutdown.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
                server.Shutdown();
            });
            shutdown_trigger.detach();
            std::thread watchdog([workerId]() {
                while (!g_shutdown.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                std::this_thread::sleep_for(std::chrono::seconds(35));
                Logger::Error("Worker {} watchdog triggered – forcing exit", workerId);
                _exit(1);
            });
            watchdog.detach();
            Logger::Info("Worker {} entering server.Run()", workerId);
            server.Run();
            if (shutdown_trigger.joinable()) shutdown_trigger.join();
        } else {
            Logger::Critical("Worker {} failed to initialize server", workerId);
        }
        ipc_io.stop();
        if (ipc_thread.joinable()) ipc_thread.join();
        Logger::Info("Worker {} shutdown complete", workerId);
    } catch (const std::exception& err) {
        Logger::Critical("Worker {} caught unhandled exception: {}", workerId, err.what());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } catch (...) {
        Logger::Critical("Worker {} caught unknown exception", workerId);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void MasterServer::WireCallbacks()
{
    gameLogic_.SetSendAuthenticationResponseCallback([this](uint64_t session_id, const std::string& message, uint64_t player_id) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt64(gameLogic_.GetCurrentTimestamp());
        w.WriteUInt64(player_id);
        w.WriteString(message);
        SendResponse(session_id, w.GetBuffer());
    });
    gameLogic_.SetSendChunkParamsCallback([this](uint64_t session_id, const ChunkParams& data) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt64(data.timestamp);
        w.WriteUInt32(static_cast<uint32_t>(data.size));
        w.WriteFloat(data.spacing);
        SendResponse(session_id, w.GetBuffer());
    });
    gameLogic_.SetSendChunkCallback([this](uint64_t session_id, const ChunkData& data) {
        BinaryProtocol::BinaryWriter w;
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
        SendResponse(session_id, w.GetBuffer());
    });
    gameLogic_.SetSendCollisionResponseCallback([this](uint64_t session_id, const CollisionResult& result) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt64(gameLogic_.GetCurrentTimestamp());
        w.WriteUInt8(result.collided ? 1 : 0);
        w.WriteUInt64(result.collided_id);
        w.WriteFloat(result.penetration);
        w.WriteVector3(result.resolution);
        SendResponse(session_id, w.GetBuffer());
    });
    gameLogic_.SetPlayerStateCallback([this](const PlayerStateData& state) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt64(state.timestamp);
        w.WriteUInt64(state.player_id);
        w.WriteVector3(state.position);
        w.WriteVector3(state.velocity);
        w.WriteVector3(state.rotation);
        w.WriteUInt8(state.on_ground ? 1 : 0);
        auto sids = gameLogic_.GetSessionsInRadius(state.position);
        for (auto sid : sids) {
            if (sid == state.session_id) continue;
            SendResponse(sid, w.GetBuffer());
        }
    });
    gameLogic_.SetBroadcastPlayerPositionCallback([this](const PlayerPositionData& data, float /*radius*/) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt64(data.timestamp);
        w.WriteUInt64(data.player_id);
        w.WriteVector3(data.position);
        w.WriteVector3(data.velocity);
        auto sids = gameLogic_.GetSessionsInRadius(data.position);
        for (auto sid : sids) {
            if (sid == data.session_id) continue;
            SendResponse(sid, w.GetBuffer());
        }
    });
    gameLogic_.SetSendPlayerSpawnCallback([this](uint64_t session_id, const PlayerSpawnData& data) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt64(data.timestamp);
        w.WriteUInt64(data.player_id);
        w.WriteString(data.name);
        w.WriteVector3(data.position);
        w.WriteFloat(data.yaw);
        w.WriteFloat(data.health);
        w.WriteFloat(data.max_health);
        SendResponse(session_id, w.GetBuffer());
    });
    gameLogic_.SetSendPlayerDespawnCallback([this](uint64_t session_id, const PlayerDespawnData& data) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt64(data.timestamp);
        w.WriteUInt64(data.player_id);
        SendResponse(session_id, w.GetBuffer());
    });
    gameLogic_.SetSendPlayerUpdateCallback([this](uint64_t session_id, const PlayerUpdateData& data) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt64(data.timestamp);
        w.WriteUInt64(data.player_id);
        w.WriteVector3(data.position);
        w.WriteFloat(data.yaw);
        w.WriteFloat(data.health);
        w.WriteFloat(data.max_health);
        w.WriteString(data.name);
        SendResponse(session_id, w.GetBuffer());
    });
    gameLogic_.SetSendPlayersUpdateCallback([this](uint64_t session_id, const PlayerUpdateData& data) {
        BinaryProtocol::BinaryWriter w;
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
                SendResponse(sid, w.GetBuffer());
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
            SendResponse(session_id, w.GetBuffer());
        } else if (response.type == "dialogue") {
            w.WriteUInt64(response.npc_id);
            w.WriteJson(response.quests);
            w.WriteUInt64(response.timestamp);
            SendResponse(session_id, w.GetBuffer());
        } else {
            w.WriteUInt64(0);
            w.WriteString("NPC interaction failed");
            SendResponse(session_id, w.GetBuffer());
        }
    });
    gameLogic_.SetSendFamiliarCommandResponseCallback([this](uint64_t session_id, const FamiliarData& response) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt64(response.timestamp);
        w.WriteUInt64(response.familiar_id);
        w.WriteUInt64(response.target_id);
        w.WriteString(response.command);
        SendResponse(session_id, w.GetBuffer());
    });
    gameLogic_.SetSendEntitySpawnResponseCallback([this](uint64_t session_id, const EntitySpawnData& response) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt64(response.timestamp);
        w.WriteUInt64(response.entity_id);
        w.WriteInt32(response.type);
        w.WriteVector3(response.position);
        SendResponse(session_id, w.GetBuffer());
    });
    gameLogic_.SetSendLootPickupResponseCallback([this](uint64_t session_id, const LootPickupData& response) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt64(response.timestamp);
        w.WriteUInt64(response.loot_id);
        w.WriteUInt16(response.quantity);
        SendResponse(session_id, w.GetBuffer());
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
        SendResponse(session_id, w.GetBuffer());
    });
}

void MasterServer::SendResponse(uint64_t sessionId, const std::vector<uint8_t>& buffer)
{
    sendReplyCb_(sessionId, buffer);
}

void MasterServer::StartShutdownWatcher()
{
    std::thread([this]() {
        while (!g_shutdown.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        Logger::Info("Master shutdown triggered");
        Shutdown();
    }).detach();
}
