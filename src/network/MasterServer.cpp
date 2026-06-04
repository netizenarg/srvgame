#include "network/MasterServer.hpp"

extern std::atomic<bool> g_shutdown;

MasterServer::MasterServer(asio::io_context& io, const std::vector<WorkerGroupConfig>& workerGroups,
    const ConfigManager& config, GameLogic& gameLogic, DatabaseService& dbService, const std::string& configPath)
    : io_(io), signal_pipe_(io),
    gameLogic_(gameLogic), processPool_(io, workerGroups), router_(processPool_, gameLogic),
    config_(config), configPath_(configPath)
{
    gameLogic_.SetDatabaseService(&dbService);
}

void MasterServer::Initialize()
{
    router_.Initialize();
    processPool_.SetMasterMessageHandler([this](int workerId, uint32_t corrId,
        uint64_t sessionId, uint16_t msgType, const std::vector<uint8_t>& body) {
        router_.OnChildWorkerMessage(workerId, corrId, sessionId, msgType, body);
    });
    processPool_.SetWorker([this](int workerId, const WorkerGroupConfig& groupConfig, int masterReadFd) {
        WorkerClient(workerId, groupConfig, masterReadFd, configPath_);
    });
    processPool_.SetGameLogicWorker([this](int workerId, int masterFd) {
        GameLogicClient(workerId, masterFd, configPath_);
    });
    processPool_.Initialize();
    gameLogic_.Initialize();
    extern int g_signal_pipe[2];
    signal_pipe_.assign(g_signal_pipe[0]);
    start_signal_read();
}

void MasterServer::start_signal_read() {
    signal_pipe_.async_read_some(asio::buffer(signal_buffer_),
    [this](std::error_code ec, size_t /*bytes*/) {
        if (!ec) {
            Logger::Trace("MasterServer::start_signal_read shutdown signal received");
            Shutdown();
        }
        start_signal_read();
        Logger::Trace("MasterServer::start_signal_read completed, ec = {}", ec.message());
    });
}

void MasterServer::Run()
{
    io_.run();
    Logger::Trace("MasterServer::Run: io_context::run() returned");
}

void MasterServer::Shutdown() {
    static std::once_flag once;
    std::call_once(once, [this]{
        Logger::Info("MasterServer::Shutdown initiated");
        signal_pipe_.cancel();
        processPool_.Shutdown();
        gameLogic_.Shutdown();
        io_.stop();
        Logger::Info("MasterServer::Shutdown finished");
    });
}

void MasterServer::WorkerClient(int workerId, const WorkerGroupConfig& groupConfig,
                                int masterReadFd, const std::string& configPath)
{
    auto& config = ConfigManager::GetInstance();
    if (!config.LoadConfig(configPath)) {
        Logger::Critical("Worker {} failed to load configuration", workerId);
        return;
    }
    Logger::InitializeWithWorkerId(workerId, config.GetJson("logging"));
    uint16_t logPort = config.GetInt("logging.log_port", 15555);
    asio::io_context logIo;
    auto logSocket = std::make_shared<asio::ip::tcp::socket>(logIo);
    logSocket->connect(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), logPort));
    auto logSink = std::make_shared<LogSink>(logSocket);
    Logger::AddSink(logSink);
    Logger::GetLogger()->set_pattern(config.GetString("logging.pattern", "[%Y-%m-%d %H:%M:%S.%e] [%P] [%l] [%n] %v"));
    Logger::Info("Worker {} starting for group: {} ({}:{})", workerId, groupConfig.protocol, groupConfig.host, groupConfig.port);
    ClientListener listener(groupConfig, masterReadFd, workerId);
    listener.Start();
    asio::io_context signalIo;
    asio::signal_set signals(signalIo, SIGINT, SIGTERM);
    signals.async_wait([&listener](const std::error_code&, int) { listener.Shutdown(); });
    std::thread signalThread([&]() { signalIo.run(); });
    signalThread.join();
    listener.Shutdown();
}

void MasterServer::GameLogicClient(int workerId, int masterFd, const std::string& configPath)
{
    auto& config = ConfigManager::GetInstance();
    if (!config.LoadConfig(configPath)) {
        Logger::Critical("GameLogicWorker {} failed to load configuration", workerId);
        return;
    }
    Logger::InitializeWithWorkerId(workerId, config.GetJson("logging"));
    uint16_t logPort = config.GetInt("logging.log_port", 15555);
    asio::io_context logIo;
    auto logSocket = std::make_shared<asio::ip::tcp::socket>(logIo);
    logSocket->connect(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), logPort));
    auto logSink = std::make_shared<LogSink>(logSocket);
    Logger::AddSink(logSink);
    Logger::GetLogger()->set_pattern(config.GetString("logging.pattern", "[%Y-%m-%d %H:%M:%S.%e] [%P] [%l] [%n] %v"));
    Logger::Info("GameLogicWorker {} starting", workerId);

    asio::io_context workerIo;
    auto channel = std::make_shared<IPCChannel>(workerIo, masterFd);

    DatabaseService dbService(workerIo, config.GetInt("database.pool.threads", 2));
    auto gameLogic = std::make_unique<GameLogic>();
    gameLogic->SetDatabaseService(&dbService);

    channel->Start([&gameLogic, &channel](const IPCEnvelope& env) {
        switch (env.messageType) {
            case BinaryProtocol::MESSAGE_TYPE_AUTHENTICATION: {
                BinaryProtocol::BinaryReader r(env.payload.data(), env.payload.size());
                AuthenticationData auth;
                auth.username = r.ReadString();
                auth.password = r.ReadString();
                auth.session_id = env.sessionId;
                gameLogic->OnAuthentication(auth);
                break;
            }
            case BinaryProtocol::MESSAGE_TYPE_CHAT_MESSAGE: {
                BinaryProtocol::BinaryReader r(env.payload.data(), env.payload.size());
                std::string sender = r.ReadString();
                std::string message = r.ReadString();
                uint64_t timestamp = r.ReadUInt64();
                BinaryProtocol::BinaryWriter w;
                w.WriteUInt16(BinaryProtocol::MESSAGE_TYPE_CHAT_MESSAGE);
                w.WriteString(sender);
                w.WriteString(message);
                w.WriteUInt64(timestamp);
                IPCEnvelope resp;
                resp.correlationId = 0;
                resp.sessionId = env.sessionId;
                resp.messageType = BinaryProtocol::MESSAGE_TYPE_CHAT_MESSAGE;
                resp.payload = w.GetBuffer();
                channel->SendAsync(resp);
                break;
            }
            case BinaryProtocol::MESSAGE_TYPE_CHUNK_PARAMS: {
                ChunkParams req;
                req.session_id = env.sessionId;
                gameLogic->OnChunkParams(req);
                break;
            }
            case BinaryProtocol::MESSAGE_TYPE_CHUNK_DATA: {
                BinaryProtocol::BinaryReader r(env.payload.data(), env.payload.size());
                ChunkData req;
                req.x = r.ReadInt32();
                req.z = r.ReadInt32();
                req.lod = r.ReadUInt8();
                req.player_x = r.ReadFloat();
                req.player_y = r.ReadFloat();
                req.player_z = r.ReadFloat();
                req.session_id = env.sessionId;
                gameLogic->OnChunkData(req);
                break;
            }
            case BinaryProtocol::MESSAGE_TYPE_COLLISION_CHECK: {
                BinaryProtocol::BinaryReader r(env.payload.data(), env.payload.size());
                CollisionData req;
                req.position = r.ReadVector3();
                req.radius = r.ReadFloat();
                req.session_id = env.sessionId;
                gameLogic->OnCollisionCheck(req);
                break;
            }
            case BinaryProtocol::MESSAGE_TYPE_PLAYER_STATE: {
                BinaryProtocol::BinaryReader r(env.payload.data(), env.payload.size());
                PlayerStateData state;
                state.player_id = gameLogic->GetPlayerIdBySession(env.sessionId);
                state.input_id = r.ReadUInt32();
                state.position = r.ReadVector3();
                state.velocity = r.ReadVector3();
                state.rotation = r.ReadVector3();
                state.on_ground = r.ReadUInt8() != 0;
                state.timestamp = r.ReadUInt64();
                state.session_id = env.sessionId;
                gameLogic->OnPlayerState(state);
                break;
            }
            case BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION: {
                BinaryProtocol::BinaryReader r(env.payload.data(), env.payload.size());
                uint64_t player_id = r.ReadUInt64();
                glm::vec3 position = r.ReadVector3();
                glm::vec3 velocity = r.ReadVector3();
                uint64_t timestamp = r.ReadUInt64();
                PlayerPositionData pos;
                pos.player_id = player_id;
                pos.position = position;
                pos.velocity = velocity;
                pos.timestamp = timestamp;
                pos.session_id = env.sessionId;
                gameLogic->OnPlayerPosition(pos);
                break;
            }
            case BinaryProtocol::MESSAGE_TYPE_NPC_INTERACTION: {
                BinaryProtocol::BinaryReader r(env.payload.data(), env.payload.size());
                NpcData req;
                req.npc_id = r.ReadUInt64();
                req.type = r.ReadString();
                req.session_id = env.sessionId;
                gameLogic->OnNPCInteraction(req);
                break;
            }
            case BinaryProtocol::MESSAGE_TYPE_FAMILIAR_COMMAND: {
                BinaryProtocol::BinaryReader r(env.payload.data(), env.payload.size());
                FamiliarData req;
                req.session_id = env.sessionId;
                req.familiar_id = r.ReadUInt64();
                req.target_id = r.ReadUInt64();
                req.command = r.ReadString();
                gameLogic->OnFamiliarCommand(req);
                break;
            }
            case BinaryProtocol::MESSAGE_TYPE_ENTITY_SPAWN: {
                BinaryProtocol::BinaryReader r(env.payload.data(), env.payload.size());
                EntitySpawnData req;
                req.session_id = env.sessionId;
                req.entity_id = r.ReadUInt64();
                req.type = r.ReadInt32();
                req.position = r.ReadVector3();
                gameLogic->OnEntitySpawnRequest(req);
                break;
            }
            case BinaryProtocol::MESSAGE_TYPE_LOOT_PICKUP: {
                BinaryProtocol::BinaryReader r(env.payload.data(), env.payload.size());
                LootPickupData req;
                req.loot_id = r.ReadUInt64();
                req.quantity = r.ReadUInt16();
                req.session_id = env.sessionId;
                gameLogic->OnLootPickup(req);
                break;
            }
            case BinaryProtocol::MESSAGE_TYPE_INVENTORY_MOVE: {
                BinaryProtocol::BinaryReader r(env.payload.data(), env.payload.size());
                InventoryData req;
                req.loot_id = r.ReadUInt64();
                req.target_id = r.ReadUInt64();
                req.move_type = static_cast<InventoryMoveType>(r.ReadUInt8());
                req.inv_slot_id = r.ReadInt32();
                req.use_slot_id = r.ReadInt32();
                req.quantity = r.ReadUInt16();
                req.session_id = env.sessionId;
                gameLogic->OnInventory(req);
                break;
            }
            case BinaryProtocol::MESSAGE_TYPE_PLAYER_DISCONNECT:
                gameLogic->OnPlayerDisconnected(env.sessionId);
                break;
            default:
                Logger::Warn("GameLogicWorker: unhandled message type {}", env.messageType);
                break;
        }
    });

    auto sendToMaster = [&channel](uint64_t sessionId, uint16_t msgType, const std::vector<uint8_t>& data) {
        IPCEnvelope resp;
        resp.correlationId = 0;
        resp.sessionId = sessionId;
        resp.messageType = msgType;
        resp.payload = data;
        channel->SendAsync(resp);
    };

    gameLogic->SetSendReplyCallback([&sendToMaster](uint64_t sessionId, const std::vector<uint8_t>& data) {
        sendToMaster(sessionId, 0, data);
    });

    gameLogic->SetGetSessionIdsInRadiusCallback([](const glm::vec3& pos, float radius) -> std::vector<uint64_t> {
        std::vector<uint64_t> sids;
        auto& pm = PlayerManager::GetInstance();
        auto players = pm.GetPlayersInRadius(pos, radius);
        for (auto& p : players) {
            uint64_t sid = pm.GetSessionIdByPlayerId(p->GetId());
            if (sid != 0) sids.push_back(sid);
        }
        return sids;
    });

    gameLogic->SetSendAuthenticationResponseCallback([&sendToMaster, &gameLogic](uint64_t session_id, const std::string& message, uint64_t player_id) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt16(BinaryProtocol::MESSAGE_TYPE_AUTHENTICATION);
        w.WriteUInt64(gameLogic->GetCurrentTimestamp());
        w.WriteUInt64(player_id);
        w.WriteString(message);
        sendToMaster(session_id, BinaryProtocol::MESSAGE_TYPE_AUTHENTICATION, w.GetBuffer());
    });

    gameLogic->SetSendChunkParamsCallback([&sendToMaster](uint64_t session_id, const ChunkParams& data) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt16(BinaryProtocol::MESSAGE_TYPE_CHUNK_PARAMS);
        w.WriteUInt64(data.timestamp);
        w.WriteUInt32(static_cast<uint32_t>(data.size));
        w.WriteFloat(data.spacing);
        sendToMaster(session_id, BinaryProtocol::MESSAGE_TYPE_CHUNK_PARAMS, w.GetBuffer());
    });

    gameLogic->SetSendChunkCallback([&sendToMaster](uint64_t session_id, const ChunkData& data) {
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
        sendToMaster(session_id, BinaryProtocol::MESSAGE_TYPE_CHUNK_DATA, w.GetBuffer());
    });

    gameLogic->SetSendCollisionResponseCallback([&sendToMaster, &gameLogic](uint64_t session_id, const CollisionResult& result) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt16(BinaryProtocol::MESSAGE_TYPE_COLLISION_CHECK);
        w.WriteUInt64(gameLogic->GetCurrentTimestamp());
        w.WriteUInt8(result.collided ? 1 : 0);
        w.WriteUInt64(result.collided_id);
        w.WriteFloat(result.penetration);
        w.WriteVector3(result.resolution);
        sendToMaster(session_id, BinaryProtocol::MESSAGE_TYPE_COLLISION_CHECK, w.GetBuffer());
    });

    gameLogic->SetPlayerStateCallback([&sendToMaster, &gameLogic](const PlayerStateData& state) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt16(BinaryProtocol::MESSAGE_TYPE_PLAYER_STATE);
        w.WriteUInt64(state.timestamp);
        w.WriteUInt64(state.player_id);
        w.WriteVector3(state.position);
        w.WriteVector3(state.velocity);
        w.WriteVector3(state.rotation);
        w.WriteUInt8(state.on_ground ? 1 : 0);
        auto sids = gameLogic->GetSessionsInRadius(state.position);
        for (auto sid : sids) {
            if (sid == state.session_id) continue;
            sendToMaster(sid, BinaryProtocol::MESSAGE_TYPE_PLAYER_STATE, w.GetBuffer());
        }
    });

    gameLogic->SetBroadcastPlayerPositionCallback([&sendToMaster, &gameLogic](const PlayerPositionData& data, float /*radius*/) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt16(BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION);
        w.WriteUInt64(data.timestamp);
        w.WriteUInt64(data.player_id);
        w.WriteVector3(data.position);
        w.WriteVector3(data.velocity);
        auto sids = gameLogic->GetSessionsInRadius(data.position);
        for (auto sid : sids) {
            if (sid == data.session_id) continue;
            sendToMaster(sid, BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION, w.GetBuffer());
        }
    });

    gameLogic->SetSendPlayerSpawnCallback([&sendToMaster, &gameLogic](uint64_t session_id, const PlayerSpawnData& data) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt16(BinaryProtocol::MESSAGE_TYPE_PLAYER_SPAWN);
        w.WriteUInt64(gameLogic->GetCurrentTimestamp());
        w.WriteUInt64(data.player_id);
        w.WriteString(data.name);
        w.WriteVector3(data.position);
        w.WriteFloat(data.yaw);
        w.WriteFloat(data.health);
        w.WriteFloat(data.max_health);
        sendToMaster(session_id, BinaryProtocol::MESSAGE_TYPE_PLAYER_SPAWN, w.GetBuffer());
    });

    gameLogic->SetSendPlayerDespawnCallback([&sendToMaster](uint64_t session_id, const PlayerDespawnData& data) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt16(BinaryProtocol::MESSAGE_TYPE_PLAYER_DESPAWN);
        w.WriteUInt64(data.timestamp);
        w.WriteUInt64(data.player_id);
        sendToMaster(session_id, BinaryProtocol::MESSAGE_TYPE_PLAYER_DESPAWN, w.GetBuffer());
    });

    gameLogic->SetSendPlayerUpdateCallback([&sendToMaster](uint64_t session_id, const PlayerUpdateData& data) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt16(BinaryProtocol::MESSAGE_TYPE_PLAYER_UPDATE);
        w.WriteUInt64(data.timestamp);
        w.WriteUInt64(data.player_id);
        w.WriteVector3(data.position);
        w.WriteFloat(data.yaw);
        w.WriteFloat(data.health);
        w.WriteFloat(data.max_health);
        w.WriteString(data.name);
        sendToMaster(session_id, BinaryProtocol::MESSAGE_TYPE_PLAYER_UPDATE, w.GetBuffer());
    });

    gameLogic->SetSendPlayersUpdateCallback([&sendToMaster, &gameLogic](uint64_t session_id, const PlayerUpdateData& data) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt16(BinaryProtocol::MESSAGE_TYPE_PLAYERS_UPDATE);
        w.WriteUInt64(data.timestamp);
        w.WriteUInt64(data.player_id);
        w.WriteVector3(data.position);
        w.WriteFloat(data.yaw);
        w.WriteFloat(data.health);
        w.WriteFloat(data.max_health);
        w.WriteString(data.name);
        auto sids = gameLogic->GetSessionsInRadius(data.position);
        for (auto sid : sids) {
            if (sid != session_id) {
                sendToMaster(sid, BinaryProtocol::MESSAGE_TYPE_PLAYERS_UPDATE, w.GetBuffer());
            }
        }
    });

    gameLogic->SetSendNPCInteractionResponseCallback([&sendToMaster](uint64_t session_id, const NpcData& response) {
        BinaryProtocol::BinaryWriter w;
        if (response.type == "combat") {
            w.WriteUInt64(response.timestamp);
            w.WriteUInt64(response.player_id);
            w.WriteUInt64(response.npc_id);
            w.WriteFloat(response.damage);
            w.WriteFloat(response.health);
            w.WriteUInt8(response.is_dead ? 1 : 0);
            sendToMaster(session_id, BinaryProtocol::MESSAGE_TYPE_NPC_INTERACTION, w.GetBuffer());
        } else if (response.type == "dialogue") {
            w.WriteUInt64(response.npc_id);
            w.WriteJson(response.quests);
            w.WriteUInt64(response.timestamp);
            sendToMaster(session_id, BinaryProtocol::MESSAGE_TYPE_NPC_INTERACTION, w.GetBuffer());
        } else {
            w.WriteUInt64(0);
            w.WriteString("NPC interaction failed");
            sendToMaster(session_id, BinaryProtocol::MESSAGE_TYPE_NPC_INTERACTION, w.GetBuffer());
        }
    });

    gameLogic->SetSendFamiliarCommandResponseCallback([&sendToMaster](uint64_t session_id, const FamiliarData& response) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt64(response.timestamp);
        w.WriteUInt64(response.familiar_id);
        w.WriteUInt64(response.target_id);
        w.WriteString(response.command);
        sendToMaster(session_id, BinaryProtocol::MESSAGE_TYPE_FAMILIAR_COMMAND, w.GetBuffer());
    });

    gameLogic->SetSendEntitySpawnResponseCallback([&sendToMaster](uint64_t session_id, const EntitySpawnData& response) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt64(response.timestamp);
        w.WriteUInt64(response.entity_id);
        w.WriteInt32(response.type);
        w.WriteVector3(response.position);
        sendToMaster(session_id, BinaryProtocol::MESSAGE_TYPE_ENTITY_SPAWN, w.GetBuffer());
    });

    gameLogic->SetSendLootPickupResponseCallback([&sendToMaster](uint64_t session_id, const LootPickupData& response) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt64(response.timestamp);
        w.WriteUInt64(response.loot_id);
        w.WriteUInt16(response.quantity);
        sendToMaster(session_id, BinaryProtocol::MESSAGE_TYPE_LOOT_PICKUP, w.GetBuffer());
    });

    gameLogic->SetSendInventoryResponseCallback([&sendToMaster](uint64_t session_id, const InventoryData& response) {
        BinaryProtocol::BinaryWriter w;
        w.WriteUInt64(response.timestamp);
        w.WriteUInt64(response.loot_id);
        w.WriteUInt64(response.target_id);
        w.WriteUInt8(static_cast<uint8_t>(response.move_type));
        w.WriteInt32(response.inv_slot_id);
        w.WriteInt32(response.use_slot_id);
        w.WriteUInt16(response.quantity);
        sendToMaster(session_id, BinaryProtocol::MESSAGE_TYPE_INVENTORY_MOVE, w.GetBuffer());
    });

    gameLogic->Initialize();

    asio::io_context signalIo;
    asio::signal_set signals(signalIo, SIGINT, SIGTERM);
    signals.async_wait([&](const std::error_code&, int) {
        channel->Stop();
        gameLogic->Shutdown();
        workerIo.stop();
    });
    std::thread signalThread([&]() { signalIo.run(); });

    workerIo.run();
    signalThread.join();
    Logger::Info("GameLogicWorker {} shutting down", workerId);
}
