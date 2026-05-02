#include "network/GameServer.hpp"

GameServer::GameServer(const WorkerGroupConfig& groupConfig, const ConfigManager& config)
    : ioContext_(groupConfig.threads),
      acceptor_(ioContext_),
      groupConfig_(groupConfig),
      config_(config),
      host_(groupConfig.host),
      port_(groupConfig.port),
      reuse_(groupConfig.reuse),
      ioThreads_(groupConfig.threads)
{
    if (groupConfig.ssl.has_value()) {
        sslContext_ = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_server);
        sslContext_->use_certificate_chain_file(groupConfig.ssl->certificate);
        sslContext_->use_private_key_file(groupConfig.ssl->private_key, asio::ssl::context::pem);
        if (!groupConfig.ssl->dh_params.empty()) {
            sslContext_->use_tmp_dh_file(groupConfig.ssl->dh_params);
        }
    }
}

GameServer::~GameServer() = default;

asio::io_context& GameServer::GetIoContext() { return ioContext_; }

bool GameServer::Initialize() {
    try {
        asio::ip::tcp::endpoint endpoint(
            asio::ip::make_address(host_),
            port_
        );
        acceptor_.open(endpoint.protocol());
        if (reuse_) {
            acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
            int optval = 1;
            if (setsockopt(acceptor_.native_handle(),
                SOL_SOCKET,
                SO_REUSEPORT,
                &optval,
                sizeof(optval)) < 0) {
                Logger::Error("Failed to set SO_REUSEPORT: {}", strerror(errno));
            }
        }
        acceptor_.bind(endpoint);
        acceptor_.listen(groupConfig_.max_connections);
        Logger::Info("GameServer initialized for protocol '{}' on {}:{}",
                     groupConfig_.protocol, host_, port_);
        return true;
    } catch (const std::exception& err) {
        Logger::Critical("Failed to initialize server for protocol '{}': {}",
                         groupConfig_.protocol, err.what());
        return false;
    }
}

void GameServer::Run() {
    running_ = true;
    DoAccept();
    StartWorkerThreads();
    work_guard_.emplace(asio::make_work_guard(ioContext_));
    Logger::Info("GameServer started with {} IO threads for protocol '{}'",
                 ioThreads_, groupConfig_.protocol);
    try {
        ioContext_.run();
    } catch (const std::exception& err) {
        Logger::Critical("Unhandled exception in IO context: {}", err.what());
    }
    for (auto& thread : workerThreads_) {
        if (thread.joinable()) thread.join();
    }
    Logger::Info("GameServer run finished for protocol '{}'", groupConfig_.protocol);
}

// void GameServer::HandleIPCMessage(const nlohmann::json& data, GameLogic& game_logic) {
//     try {
//         std::string msgType = data.value("msg", "");
//         if (msgType == "shutdown") {
//             Logger::Info("Received shutdown command via IPC");
//             game_logic.Shutdown();
//             Shutdown();
//         } else if (msgType == "reload_config") {
//             Logger::Info("Received config reload command via IPC");
//         } else if (msgType == "broadcast") {
//             if (data.contains("data")) {
//                 game_logic.BroadcastToAllPlayers(data["data"]);
//             }
//         } else {
//             Logger::Warn("Unknown IPC message type: {}", msgType);
//         }
//     } catch (const std::exception& err) {
//         Logger::Error("Error handling IPC message: {}", err.what());
//     }
// }

void GameServer::HandleIPCMessage(const nlohmann::json& data, GameLogic& game_logic) {
    try {
        std::string msgType = data.value("msg", "");
        auto it = IPCMessageTypes.find(msgType);
        if (it == IPCMessageTypes.end()) {
            Logger::Warn("Unknown IPC message: {}", msgType);
            return;
        }
        int typeCode = it->second;
        switch (typeCode) {
            case 1://welcome
                break;
            case 2://heartbeat
                break;
            case 3://broadcast
                if (data.contains("data")) {
                    game_logic.BroadcastToAllPlayers(data["data"]);
                }
                break;
            case 4://shutdown
                Logger::Info("Received shutdown command from master");
                game_logic.Shutdown();
                Shutdown();
                break;
            case 5://reload_config
                Logger::Info("Received config reload command from master");
                break;
            case 201: { // player_spawn_relay
                auto pdata = data["data"];
                PlayerSpawnData spawn;
                spawn.timestamp = pdata["timestamp"];
                spawn.player_id = pdata["player_id"];
                spawn.name = pdata["name"];
                spawn.position = { pdata["x"], pdata["y"], pdata["z"] };
                spawn.yaw = pdata["yaw"];
                spawn.health = pdata["health"];
                spawn.max_health = pdata["max_health"];
                game_logic.BroadcastRemotePlayerSpawn(spawn);
                break;
            }
            case 202: { // player_despawn_relay
                auto pid = data["data"]["player_id"];
                game_logic.BroadcastRemotePlayerDespawn(pid);
                break;
            }
            case 206: { // player_position_relay
                auto pdata = data["data"];
                PlayerPositionData pos;
                pos.timestamp = game_logic.GetCurrentTimestamp();
                pos.player_id = pdata["player_id"];
                pos.position = { pdata["x"], pdata["y"], pdata["z"] };
                pos.velocity = { pdata["vx"], pdata["vy"], pdata["vz"] };
                game_logic.OnRemotePlayerPosition(pos);
                break;
            }
            default:
                Logger::Warn("Unhandled IPC message: {}({})", typeCode, msgType);
                break;
        }
    } catch (const std::exception& err) {
        Logger::Error("Error handling IPC message: {}", err.what());
    }
}

void GameServer::InitSessionFactory(int workerId, ProcessPool* processPool, GameLogic& game_logic) {
    if (groupConfig_.protocol == "binary") {
        sessionFactory_ = [workerId, processPool, &game_logic, this]
        (asio::ip::tcp::socket socket, std::shared_ptr<asio::ssl::context> sslCtx) mutable{
            auto session = std::make_shared<BinarySession>(std::move(socket), sslCtx);
            Logger::Trace("Worker {} created new game session {}; protocol: binary", workerId, session->GetSessionId());
            session->SetMessageHandler([session, workerId, processPool, &game_logic]
            (const nlohmann::json& msg) mutable{
                try {
                    std::string msgType = msg.value("msg", "");
                    Logger::Trace("Worker {} processing message: {}", workerId, msgType);
                    if (msgType == "ipc_message" && processPool) {
                        if (msg.contains("target_worker") && msg.contains("payload")) {
                            int targetWorker = msg["target_worker"];
                            std::string payload = msg["payload"].dump();
                            if (processPool->SendToWorker(targetWorker, payload)) {
                                Logger::Trace("Worker {} sent IPC message {} to worker {}", workerId, msgType, targetWorker);
                            } else {
                                Logger::Error("Worker {} failed to send IPC message {} to worker {}", workerId, msgType, targetWorker);
                            }
                        }
                    } else {
                        game_logic.HandleMessage(session->GetSessionId(), msg);
                    }
                } catch (const std::exception& e) {
                    Logger::Error("Worker {} error processing message: {}", workerId, e.what());
                    session->SendError(BinaryProtocol::MESSAGE_TYPE_ERROR, "Internal server error", 500);
                }
            });
            session->SetDefaultBinaryMessageHandler([session, workerId, &game_logic, this]
            (uint16_t type, const std::vector<uint8_t>& data) mutable{
                switch (type) {
                    case BinaryProtocol::MESSAGE_TYPE_AUTHENTICATION: {
                        BinaryProtocol::BinaryReader reader(data.data(), data.size());
                        AuthenticationData authData;
                        authData.username = reader.ReadString();
                        authData.password = reader.ReadString();
                        authData.session_id = session->GetSessionId();
                        game_logic.OnAuthentication(authData);
                        break;
                    }
                    case BinaryProtocol::MESSAGE_TYPE_CHUNK_PARAMS: {
                        BinaryProtocol::BinaryReader reader(data.data(), data.size());
                        ChunkParams req;
                        req.session_id = session->GetSessionId();
                        game_logic.OnChunkParams(req);
                        break;
                    }
                    case BinaryProtocol::MESSAGE_TYPE_CHUNK_DATA: {
                        BinaryProtocol::BinaryReader reader(data.data(), data.size());
                        ChunkData req;
                        req.x = reader.ReadInt32();
                        req.z = reader.ReadInt32();
                        req.lod = reader.ReadUInt8();
                        req.player_x = reader.ReadFloat();
                        req.player_y = reader.ReadFloat();
                        req.player_z = reader.ReadFloat();
                        req.session_id = session->GetSessionId();
                        game_logic.OnChunkData(req);
                        break;
                    }
                    case BinaryProtocol::MESSAGE_TYPE_COLLISION_CHECK: {
                        BinaryProtocol::BinaryReader reader(data.data(), data.size());
                        CollisionData req;
                        req.position = reader.ReadVector3();
                        req.radius = reader.ReadFloat();
                        req.session_id = session->GetSessionId();
                        game_logic.OnCollisionCheck(req);
                        break;
                    }
                    case BinaryProtocol::MESSAGE_TYPE_PLAYER_STATE: {
                        BinaryProtocol::BinaryReader reader(data.data(), data.size());
                        PlayerStateData stateData;
                        stateData.player_id = game_logic.GetPlayerIdBySession(session->GetSessionId());
                        stateData.input_id = reader.ReadUInt32();
                        stateData.position = reader.ReadVector3();
                        stateData.velocity = reader.ReadVector3();
                        stateData.rotation = reader.ReadVector3();
                        stateData.on_ground = reader.ReadUInt8() != 0;
                        stateData.timestamp = reader.ReadUInt64();
                        stateData.session_id = session->GetSessionId();
                        game_logic.OnPlayerState(stateData);
                        break;
                    }
                    case BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION: {
                        BinaryProtocol::BinaryReader reader(data.data(), data.size());
                        uint64_t player_id = reader.ReadUInt64();
                        glm::vec3 position = reader.ReadVector3();
                        glm::vec3 velocity = reader.ReadVector3();
                        uint64_t timestamp = reader.ReadUInt64();
                        uint64_t session_id = session->GetSessionId();
                        asio::post(ioContext_, [&game_logic, session_id, player_id, position, velocity, timestamp]() {
                            PlayerPositionData posData;
                            posData.player_id = player_id;
                            posData.position = position;
                            posData.velocity = velocity;
                            posData.timestamp = timestamp;
                            posData.session_id = session_id;
                            game_logic.OnPlayerPosition(posData);
                        });
                        break;
                    }
                    case BinaryProtocol::MESSAGE_TYPE_NPC_INTERACTION: {
                        BinaryProtocol::BinaryReader reader(data.data(), data.size());
                        NpcData req;
                        req.npc_id = reader.ReadUInt64();
                        req.type = reader.ReadString();
                        req.session_id = session->GetSessionId();
                        game_logic.OnNPCInteraction(req);
                        break;
                    }
                    case BinaryProtocol::MESSAGE_TYPE_FAMILIAR_COMMAND: {
                        BinaryProtocol::BinaryReader reader(data.data(), data.size());
                        FamiliarData req;
                        req.session_id = session->GetSessionId();
                        req.familiar_id = reader.ReadUInt64();
                        req.target_id = reader.ReadUInt64();
                        req.command = reader.ReadString();
                        game_logic.OnFamiliarCommand(req);
                        break;
                    }
                    case BinaryProtocol::MESSAGE_TYPE_ENTITY_SPAWN: {
                        BinaryProtocol::BinaryReader reader(data.data(), data.size());
                        EntitySpawnData req;
                        req.session_id = session->GetSessionId();
                        req.entity_id = reader.ReadUInt64();
                        req.type = reader.ReadInt32();
                        req.position = reader.ReadVector3();
                        game_logic.OnEntitySpawnRequest(req);
                        break;
                    }
                    case BinaryProtocol::MESSAGE_TYPE_LOOT_PICKUP: {
                        BinaryProtocol::BinaryReader reader(data.data(), data.size());
                        LootPickupData req;
                        req.loot_id = reader.ReadUInt64();
                        req.quantity = reader.ReadUInt16();
                        req.session_id = session->GetSessionId();
                        game_logic.OnLootPickup(req);
                        break;
                    }
                    case BinaryProtocol::MESSAGE_TYPE_INVENTORY_MOVE: {
                        BinaryProtocol::BinaryReader reader(data.data(), data.size());
                        InventoryData req;
                        req.loot_id = reader.ReadUInt64();
                        req.target_id = reader.ReadUInt64();
                        req.move_type = static_cast<InventoryMoveType>(reader.ReadUInt8());
                        req.inv_slot_id = reader.ReadInt32();
                        req.use_slot_id = reader.ReadInt32();
                        req.quantity = reader.ReadUInt16();
                        req.session_id = session->GetSessionId();
                        game_logic.OnInventory(req);
                        break;
                    }
                    //TODO: add all game_logic MESSAGE_TYPE_XXX
                    default:
                        game_logic.HandleBinaryMessage(session->GetSessionId(), type, data);
                        break;
                }
            });
            session->SetCloseHandler([session, workerId, &game_logic]() mutable{
                Logger::Trace("Worker {} session {} closing", workerId, session->GetSessionId());
                game_logic.OnPlayerDisconnected(session->GetSessionId());
                ConnectionManager::GetInstance().Stop(session);
                Logger::Trace("Worker {} session {} cleanup complete", workerId, session->GetSessionId());
            });
            return session;
        };
    }
    else if (groupConfig_.protocol == "websocket") {
        webSocketFactory_ = [workerId, processPool, &game_logic, this]
        (asio::ip::tcp::socket socket, std::shared_ptr<asio::ssl::context> /*sslCtx*/) mutable{
            auto wsConn = std::make_shared<WebSocketProtocol::WebSocketConnection>(std::move(socket));
            auto session = std::make_shared<WebSocketSession>(wsConn);
            Logger::Trace("Worker {} created new game session {}; protocol: websocket", workerId, session->GetSessionId());
            session->SetMessageHandler([session, workerId, processPool, &game_logic, this]
            (const nlohmann::json& msg) mutable{
                std::string msgType = msg.value("msg", "");
                if (msgType == "protocol_negotiation") {
                    return;
                }
                else if (msgType == "authentication") {
                    AuthenticationData authData;
                    authData.username = msg.value("login", "");
                    authData.password = msg.value("password", "");
                    authData.session_id = session->GetSessionId();
                    game_logic.OnAuthentication(authData);
                }
                else if (msgType == "chunk_params") {
                    ChunkParams req;
                    req.session_id = session->GetSessionId();
                    game_logic.OnChunkParams(req);
                }
                else if (msgType == "get_chunk") {
                    ChunkData req;
                    req.x = msg.value("x", 0);
                    req.z = msg.value("z", 0);
                    req.lod = static_cast<uint8_t>(msg.value("lod", 0));
                    req.player_x = msg.value("player_x", 0.0f);
                    req.player_y = msg.value("player_y", 0.0f);
                    req.player_z = msg.value("player_z", 0.0f);
                    req.session_id = session->GetSessionId();
                    game_logic.OnChunkData(req);
                }
                else if (msgType == "collision") {
                    CollisionData req;
                    req.position.x = msg.value("x", 0.0f);
                    req.position.y = msg.value("y", 0.0f);
                    req.position.z = msg.value("z", 0.0f);
                    req.radius = msg.value("radius", 0.5f);
                    req.session_id = session->GetSessionId();
                    game_logic.OnCollisionCheck(req);
                }
                else if (msgType == "player_state") {
                    // TODO: fill full state data format
                    PlayerStateData posData;
                    posData.player_id = game_logic.GetPlayerIdBySession(session->GetSessionId());
                    if (posData.player_id < 1)
                        posData.player_id = game_logic.GetPlayerIdBySession(session->GetSessionId());
                    posData.position.x = msg.value("pos_x", 0.0f);
                    posData.position.y = msg.value("pos_y", 0.0f);
                    posData.position.z = msg.value("pos_z", 0.0f);
                    posData.timestamp = game_logic.GetCurrentTimestamp();
                    posData.session_id = session->GetSessionId();
                    game_logic.OnPlayerState(posData);
                }
                else if (msgType == "player_position") {
                    auto data = msg;
                    asio::post(ioContext_, [session, data, &game_logic]() mutable {
                        PlayerPositionData posData;
                        posData.player_id = data.value("player_id", 0ULL);
                        if (posData.player_id < 1)
                            posData.player_id = game_logic.GetPlayerIdBySession(session->GetSessionId());
                        posData.position.x = data.value("x", 0.0f);
                        posData.position.y = data.value("y", 0.0f);
                        posData.position.z = data.value("z", 0.0f);
                        posData.timestamp = game_logic.GetCurrentTimestamp();
                        posData.session_id = session->GetSessionId();
                        game_logic.OnPlayerPosition(posData);
                    });
                }
                else if (msgType == "npc_interaction") {
                    NpcData req;
                    req.npc_id = msg.value("npc_id", 0);
                    req.type = msg.value("npc_type", "");
                    req.session_id = session->GetSessionId();
                    game_logic.OnNPCInteraction(req);
                }
                else if (msgType == "familiar") {
                    FamiliarData req;
                    req.session_id = session->GetSessionId();
                    req.familiar_id = msg.value("familiarId", 0ULL);
                    req.target_id = msg.value("targetId", 0ULL);
                    req.command = msg.value("command", "");
                    game_logic.OnFamiliarCommand(req);
                }
                else if (msgType == "entity_spawn") {
                    EntitySpawnData req;
                    req.entity_id = msg.value("id", 0);
                    req.type = msg.value("entity_type", 0);
                    req.position.x = msg.value("x", 0.0f);
                    req.position.y = msg.value("y", 0.0f);
                    req.position.z = msg.value("z", 0.0f);
                    req.session_id = session->GetSessionId();
                    game_logic.OnEntitySpawnRequest(req);
                }
                else if (msgType == "loot_pickup") {
                    LootPickupData req;
                    req.loot_id = msg.value("loot_id", 0ULL);
                    req.quantity = msg.value("quantity", 1);
                    req.session_id = session->GetSessionId();
                    game_logic.OnLootPickup(req);
                }
                else if (msgType == "inventory") {
                    InventoryData req;
                    req.loot_id = msg.value("loot_id", 0ULL);
                    req.target_id = msg.value("target_id", 0ULL);
                    req.move_type = static_cast<InventoryMoveType>(msg.value("move_type", 0));
                    req.inv_slot_id = msg.value("inv_slot_id", -1);
                    req.use_slot_id = msg.value("use_slot_id", -1);
                    req.quantity = msg.value("quantity", 1);
                    req.session_id = session->GetSessionId();
                    game_logic.OnInventory(req);
                }
                //TODO: add all game_logic msgType
                else {
                    game_logic.HandleMessage(session->GetSessionId(), msg);
                }
            });
            session->SetBinaryMessageHandler([session, workerId, &game_logic]
            (uint16_t type, const std::vector<uint8_t>& data) mutable{
                game_logic.HandleBinaryMessage(session->GetSessionId(), type, data);
            });
            session->SetCloseHandler([session, workerId, &game_logic]() mutable{
                Logger::Trace("Worker {} session {} closing", workerId, session->GetSessionId());
                game_logic.OnPlayerDisconnected(session->GetSessionId());
                ConnectionManager::GetInstance().Stop(session);
                Logger::Trace("Worker {} session {} cleanup complete", workerId, session->GetSessionId());
            });
            return session;
        };
    }
}

void GameServer::DoAccept() {
    acceptor_.async_accept(
        [this](std::error_code ec, asio::ip::tcp::socket socket) {
            if (!ec) {
                if (groupConfig_.tcp_nodelay) {
                    asio::ip::tcp::no_delay option(true);
                    socket.set_option(option);
                }
                if (groupConfig_.send_buffer_size > 0) {
                    asio::socket_base::send_buffer_size option(groupConfig_.send_buffer_size);
                    socket.set_option(option);
                }
                if (groupConfig_.receive_buffer_size > 0) {
                    asio::socket_base::receive_buffer_size option(groupConfig_.receive_buffer_size);
                    socket.set_option(option);
                }
                if (groupConfig_.protocol == "binary") {
                    if (sessionFactory_) {
                        auto session = sessionFactory_(std::move(socket), sslContext_);
                        ConnectionManager::GetInstance().Start(session);
                        session->Start();
                    } else {
                        Logger::Error("No session factory set for binary protocol");
                    }
                } else if (groupConfig_.protocol == "websocket") {
                    if (webSocketFactory_) {
                        auto session = webSocketFactory_(std::move(socket), sslContext_);
                        ConnectionManager::GetInstance().Start(session);
                        session->Start();
                        Logger::Trace("WebSocket session {} started", session->GetSessionId());
                    } else {
                        Logger::Error("No WebSocket factory set");
                    }
                } else {
                    Logger::Error("Unknown protocol: {}", groupConfig_.protocol);
                }
            } else {
                if (ec != asio::error::operation_aborted) {
                    Logger::Error("Accept error: {}", ec.message());
                } else {
                    Logger::Debug("Accept aborted during shutdown");
                }
            }
            if (running_) {
                DoAccept();
            }
        });
}

void GameServer::StartWorkerThreads() {
    for (int i = 0; i < ioThreads_ - 1; ++i) {
        workerThreads_.emplace_back([this]() {
            ioContext_.run();
        });
    }
}

void GameServer::Shutdown() {
    if (!running_) {
        Logger::Trace("GameServer::Shutdown - already stopped, do nothing, return");
        return;
    }
    Logger::Trace("GameServer::Shutdown running ...");
    running_ = false;
    acceptor_.close();
    ConnectionManager::GetInstance().StopAll();
    work_guard_.reset();
    ioContext_.stop();
    Logger::Info("GameServer::Shutdown completed");
}

std::vector<std::shared_ptr<IConnection>> GameServer::GetSessionsInRadius(const glm::vec3& position, float radius) {
    std::vector<std::shared_ptr<IConnection>> result;
    auto& pm = PlayerManager::GetInstance();
    auto nearbyPlayers = pm.GetPlayersInRadius(position, radius);
    for (auto& player : nearbyPlayers) {
        uint64_t session_id = pm.GetSessionIdByPlayerId(player->GetId());
        if (session_id != 0) {
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session && session->IsConnected()) {
                result.push_back(session);
            }
        }
    }
    return result;
}

void GameServer::RegisterCallbacks(const std::string& protocol, GameLogic& game_logic) {
    // ATTENTION
    // In any case, the client checks the response values.
    // Which means the client himself determines success or failure.
    // Most answers don't need an overloaded, useless "success" flag.
    if (protocol == "binary") {
        game_logic.SetSendAuthenticationResponseCallback([&](uint64_t session_id, const std::string& message, uint64_t player_id) {
            BinaryProtocol::BinaryWriter writer;
            writer.WriteUInt64(game_logic.GetCurrentTimestamp());
            writer.WriteUInt64(player_id);
            writer.WriteString(message);
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) {
                session->Send(BinaryProtocol::MESSAGE_TYPE_AUTHENTICATION, writer.GetBuffer());
            }
        });
        game_logic.SetSendChunkParamsCallback([&](uint64_t session_id, const ChunkParams& data) {
            BinaryProtocol::BinaryWriter writer;
            writer.WriteUInt64(data.timestamp);
            writer.WriteUInt32(static_cast<uint32_t>(data.size));
            writer.WriteFloat(data.spacing);
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) {
                session->Send(BinaryProtocol::MESSAGE_TYPE_CHUNK_PARAMS, writer.GetBuffer());
            }
        });
        game_logic.SetSendChunkCallback([&](uint64_t session_id, const ChunkData& data) {
            BinaryProtocol::BinaryWriter writer;
            writer.WriteUInt64(data.timestamp);
            writer.WriteInt32(data.x);
            writer.WriteInt32(data.z);
            uint32_t vertexDataSize = static_cast<uint32_t>(data.vertices.size() * sizeof(float));
            writer.WriteUInt32(vertexDataSize);
            writer.WriteBytes(reinterpret_cast<const uint8_t*>(data.vertices.data()), vertexDataSize);
            uint32_t indexDataSize = static_cast<uint32_t>(data.indices.size() * sizeof(uint32_t));
            writer.WriteUInt32(indexDataSize);
            writer.WriteBytes(reinterpret_cast<const uint8_t*>(data.indices.data()), indexDataSize);
            writer.WriteUInt32(0);
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) {
                session->Send(BinaryProtocol::MESSAGE_TYPE_CHUNK_DATA, writer.GetBuffer());
            }
        });
        game_logic.SetSendCollisionResponseCallback([&](uint64_t session_id, const CollisionResult& result) {
            BinaryProtocol::BinaryWriter writer;
            writer.WriteUInt64(game_logic.GetCurrentTimestamp());
            writer.WriteUInt8(result.collided ? 1 : 0);
            writer.WriteUInt64(result.collided_id);
            writer.WriteFloat(result.penetration);
            writer.WriteVector3(result.resolution);
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) {
                session->Send(BinaryProtocol::MESSAGE_TYPE_COLLISION_CHECK, writer.GetBuffer());
            }
        });
        game_logic.SetPlayerStateCallback([&](const PlayerStateData& state) {
            BinaryProtocol::BinaryWriter writer;
            writer.WriteUInt64(state.timestamp);
            writer.WriteUInt64(state.player_id);
            writer.WriteVector3(state.position);
            writer.WriteVector3(state.velocity);
            writer.WriteVector3(state.rotation);
            writer.WriteUInt8(state.on_ground ? 1 : 0);
            auto nearbySessions = GetSessionsInRadius(state.position, config_.GetFloat("world.interest_radius", 100.0f));
            for (auto& session : nearbySessions) {
                if (session->GetSessionId() == state.session_id) continue;
                session->Send(BinaryProtocol::MESSAGE_TYPE_PLAYER_STATE, writer.GetBuffer());
            }
        });
        game_logic.SetSendPlayerSpawnCallback([&](uint64_t session_id, const PlayerSpawnData& data) {
            BinaryProtocol::BinaryWriter writer;
            writer.WriteUInt64(data.timestamp);
            writer.WriteUInt64(data.player_id);
            writer.WriteString(data.name);
            writer.WriteVector3(data.position);
            writer.WriteFloat(data.yaw);
            writer.WriteFloat(data.health);
            writer.WriteFloat(data.max_health);
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            session->Send(BinaryProtocol::MESSAGE_TYPE_PLAYER_SPAWN, writer.GetBuffer());
        });
        game_logic.SetSendPlayerDespawnCallback([&](uint64_t session_id, const PlayerDespawnData& data) {
            BinaryProtocol::BinaryWriter writer;
            writer.WriteUInt64(data.timestamp);
            writer.WriteUInt64(data.player_id);
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            session->Send(BinaryProtocol::MESSAGE_TYPE_PLAYER_DESPAWN, writer.GetBuffer());
        });
        game_logic.SetBroadcastPlayerPositionCallback([&](const PlayerPositionData& data, float radius) {
            asio::post(ioContext_, [this, data, radius]() {
                BinaryProtocol::BinaryWriter writer;
                writer.WriteUInt64(data.timestamp);
                writer.WriteUInt64(data.player_id);
                writer.WriteVector3(data.position);
                writer.WriteVector3(data.velocity);
                auto nearbySessions = GetSessionsInRadius(data.position, radius);
                for (auto& session : nearbySessions) {
                    if (session->GetSessionId() == data.session_id) continue;
                    session->Send(BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION, writer.GetBuffer());
                }
            });
        });
        game_logic.SetSendPlayerUpdateCallback([&](uint64_t session_id, const PlayerUpdateData& data) {
            BinaryProtocol::BinaryWriter writer;
            writer.WriteUInt64(data.timestamp);
            writer.WriteUInt64(data.player_id);
            writer.WriteVector3(data.position);
            writer.WriteFloat(data.yaw);
            writer.WriteFloat(data.health);
            writer.WriteFloat(data.max_health);
            writer.WriteString(data.name);
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) {
                session->Send(BinaryProtocol::MESSAGE_TYPE_PLAYER_UPDATE, writer.GetBuffer());
            }
        });
        game_logic.SetSendPlayersUpdateCallback([&](uint64_t session_id, const PlayerUpdateData& data) {
            BinaryProtocol::BinaryWriter writer;
            writer.WriteUInt64(data.timestamp);
            writer.WriteUInt64(data.player_id);
            writer.WriteVector3(data.position);
            writer.WriteFloat(data.yaw);
            writer.WriteFloat(data.health);
            writer.WriteFloat(data.max_health);
            writer.WriteString(data.name);
            auto nearbySessions = GetSessionsInRadius(data.position, config_.GetFloat("world.interest_radius", 100.0f));
            for (auto& session : nearbySessions) {// Don't send to the player who owns this update
                if (session->GetSessionId() != session_id) {
                    session->Send(BinaryProtocol::MESSAGE_TYPE_PLAYERS_UPDATE, writer.GetBuffer());
                }
            }
        });
        game_logic.SetSendNPCInteractionResponseCallback([&](uint64_t session_id, const NpcData& response) {
            BinaryProtocol::BinaryWriter writer;
            if (response.type == "combat") {
                writer.WriteUInt64(response.timestamp);
                writer.WriteUInt64(response.player_id);
                writer.WriteUInt64(response.npc_id);
                writer.WriteFloat(response.damage);
                writer.WriteFloat(response.health);
                writer.WriteUInt8(response.is_dead ? 1 : 0);
                auto session = ConnectionManager::GetInstance().GetSession(session_id);
                if (session) session->Send(BinaryProtocol::MESSAGE_TYPE_COMBAT_EVENT, writer.GetBuffer());
            } else if (response.type == "dialogue") {
                writer.WriteUInt64(response.npc_id);
                writer.WriteJson(response.quests);
                writer.WriteUInt64(response.timestamp);
                auto session = ConnectionManager::GetInstance().GetSession(session_id);
                if (session) session->Send(BinaryProtocol::MESSAGE_TYPE_NPC_INTERACTION, writer.GetBuffer());
            } else {
                auto session = ConnectionManager::GetInstance().GetSession(session_id);
                if (session)
                    session->SendError(BinaryProtocol::MESSAGE_TYPE_ERROR, "NPC interaction failed", 400);
            }
        });
        game_logic.SetSendFamiliarCommandResponseCallback([&](uint64_t session_id, const FamiliarData& response) {
            BinaryProtocol::BinaryWriter writer;
            writer.WriteUInt64(response.timestamp);
            writer.WriteUInt64(response.familiar_id);
            writer.WriteUInt64(response.target_id);
            writer.WriteString(response.command);
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) session->Send(BinaryProtocol::MESSAGE_TYPE_FAMILIAR_COMMAND, writer.GetBuffer());
        });
        game_logic.SetSendEntitySpawnResponseCallback([&](uint64_t session_id, const EntitySpawnData& response) {
            BinaryProtocol::BinaryWriter writer;
            writer.WriteUInt64(response.timestamp);
            writer.WriteUInt64(response.entity_id);
            writer.WriteInt32(response.type);
            writer.WriteVector3(response.position);
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) {
                session->Send(BinaryProtocol::MESSAGE_TYPE_ENTITY_SPAWN, writer.GetBuffer());
            }
        });
        game_logic.SetSendLootPickupResponseCallback([&](uint64_t session_id, const LootPickupData& response) {
            BinaryProtocol::BinaryWriter writer;
            writer.WriteUInt64(response.timestamp);
            writer.WriteUInt64(response.loot_id);
            writer.WriteUInt16(response.quantity);
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) {
                session->Send(BinaryProtocol::MESSAGE_TYPE_LOOT_PICKUP, writer.GetBuffer());
            }
        });
        game_logic.SetSendInventoryResponseCallback([&](uint64_t session_id, const InventoryData& response) {
            BinaryProtocol::BinaryWriter writer;
            writer.WriteUInt64(response.timestamp);
            writer.WriteUInt64(response.loot_id);
            writer.WriteUInt64(response.target_id);
            writer.WriteUInt8(static_cast<uint8_t>(response.move_type));
            writer.WriteInt32(response.inv_slot_id);
            writer.WriteInt32(response.use_slot_id);
            writer.WriteUInt16(response.quantity);
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) {
                session->Send(BinaryProtocol::MESSAGE_TYPE_INVENTORY_MOVE, writer.GetBuffer());
            }
        });
        //TODO: add all game_logic
    } else { // websocket
        game_logic.SetSendAuthenticationResponseCallback([&](uint64_t session_id, const std::string& message, uint64_t player_id) {
            nlohmann::json response = {
                {"msg", "authentication"},
                {"timestamp", game_logic.GetCurrentTimestamp()},
                {"desc", message},
                {"player_id", player_id}// if value > 0 then success, has no more duplicate success fields
            };
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) {
                session->SendJson(response);
            }
        });
        game_logic.SetSendChunkParamsCallback([&](uint64_t session_id, const ChunkParams& data) {
            nlohmann::json msg = {
                {"msg", "chunk_params"},
                {"timestamp", data.timestamp},
                {"size", data.size},
                {"spacing", data.spacing}
            };
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) {
                session->SendJson(msg);
            }
        });
        game_logic.SetSendChunkCallback([&](uint64_t session_id, const ChunkData& data) {
            nlohmann::json msg = {
                {"msg", "get_chunk"},
                {"timestamp", data.timestamp},
                {"x", data.x},
                {"z", data.z},
                {"lod", data.lod},
                {"vertices", data.vertices},
                {"indices", data.indices}
            };
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) {
                session->SendJson(msg);
            }
        });
        game_logic.SetSendCollisionResponseCallback([&](uint64_t session_id, const CollisionResult& result) {
            nlohmann::json response = {
                {"msg", "collision"},
                {"timestamp", game_logic.GetCurrentTimestamp()},
                {"collided", result.collided},
                {"collided_id", result.collided_id},
                {"penetration", result.penetration},
                {"resolution", {result.resolution.x, result.resolution.y, result.resolution.z}}
            };
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) {
                session->SendJson(response);
            }
        });
        game_logic.SetPlayerStateCallback([&](const PlayerStateData& state) {
            nlohmann::json jsonMsg = {
                {"msg", "player_state"},
                {"timestamp", state.timestamp},
                {"entity_id", state.player_id},
                {"x", state.position.x}, {"y", state.position.y}, {"z", state.position.z},
                {"rx", state.rotation.x}, {"ry", state.rotation.y}, {"rz", state.rotation.z},
                {"vx", state.velocity.x}, {"vy", state.velocity.y}, {"vz", state.velocity.z}
            };
            auto nearbySessions = GetSessionsInRadius(state.position, 100.0f);
            for (auto& session : nearbySessions) {
                if (session->GetSessionId() == state.session_id) continue;
                session->SendJson(jsonMsg);
            }
        });
        game_logic.SetBroadcastPlayerPositionCallback([&](const PlayerPositionData& data, float radius) {
            asio::post(ioContext_, [this, data, radius]() {
                nlohmann::json jsonMsg = {
                    {"msg", "player_position"},
                    {"timestamp", data.timestamp},
                    {"player_id", data.player_id},
                    {"x", data.position.x}, {"y", data.position.y}, {"z", data.position.z},
                    {"vx", data.velocity.x}, {"vy", data.velocity.y}, {"vz", data.velocity.z}
                };
                auto nearbySessions = GetSessionsInRadius(data.position, radius);
                for (auto& session : nearbySessions) {
                    if (session->GetSessionId() == data.session_id) continue;
                    session->SendJson(jsonMsg);
                }
            });
        });
        game_logic.SetSendPlayersUpdateCallback([&](uint64_t /*session_id*/, const PlayerUpdateData& data) {
            nlohmann::json jsonMsg = {
                {"msg", "players_update"},
                {"timestamp", data.timestamp},
                {"players", {{
                    {"id", data.player_id},
                    {"x", data.position.x}, {"y", data.position.y}, {"z", data.position.z},
                    {"yaw", data.yaw},
                    {"health", data.health},
                    {"max_health", data.max_health},
                    {"name", data.name}
                }}}
            };
            auto nearbySessions = GetSessionsInRadius(data.position, config_.GetFloat("world.interest_radius", 100.0f));
            for (auto& session : nearbySessions) {
                if (session->GetSessionId() == data.session_id) continue;
                session->SendJson(jsonMsg);
            }
        });
        game_logic.SetSendPlayerSpawnCallback([&](uint64_t session_id, const PlayerSpawnData& data) {
            nlohmann::json msg = {
                {"msg", "player_spawn"},
                {"timestamp", data.timestamp},
                {"player_id", data.player_id},
                {"name", data.name},
                {"position", {data.position.x, data.position.y, data.position.z}},
                {"yaw", data.yaw},
                {"health", data.health},
                {"max_health", data.max_health}
            };
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) session->SendJson(msg);
        });
        game_logic.SetSendPlayerDespawnCallback([&](uint64_t session_id, const PlayerDespawnData& data) {
            nlohmann::json msg = {
                {"msg", "player_despawn"},
                {"timestamp", data.timestamp},
                {"player_id", data.player_id}
            };
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) session->SendJson(msg);
        });
        game_logic.SetSendNPCInteractionResponseCallback([&](uint64_t session_id, const NpcData& response) {
            nlohmann::json jsonMsg;
            if (response.type == "combat") {
                jsonMsg = {
                    {"msg", "combat"},
                    {"timestamp", response.timestamp},
                    {"player_id", response.player_id},
                    {"npc_id", response.npc_id},
                    {"damage", response.damage},
                    {"health", response.health},
                    {"is_dead", response.is_dead}
                };
            } else if (response.type == "dialogue") {
                jsonMsg = {
                    {"msg", "dialogue"},
                    {"timestamp", response.timestamp},
                    {"npc_id", response.npc_id},
                    {"quests", response.quests}
                };
            } else {
                jsonMsg = {{"msg", "error"}, {"desc", "NPC interaction failed"}};
            }
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) session->SendJson(jsonMsg);
        });
        game_logic.SetSendFamiliarCommandResponseCallback([&](uint64_t session_id, const FamiliarData& response) {
            nlohmann::json jsonMsg = {
                {"msg", "familiar"},
                {"timestamp", response.timestamp},
                {"familiar_id", response.familiar_id},
                {"target_id", response.target_id},
                {"command", response.command}
            };
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) session->SendJson(jsonMsg);
        });
        game_logic.SetSendEntitySpawnResponseCallback([&](uint64_t session_id, const EntitySpawnData& response) {
            nlohmann::json jsonMsg = {
                {"msg", "entity_spawn"},
                {"timestamp", response.timestamp},
                {"id", response.entity_id},
                {"type", response.type},
                {"position", {response.position.x, response.position.y, response.position.z}}
            };
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) session->SendJson(jsonMsg);
        });
        game_logic.SetSendLootPickupResponseCallback([&](uint64_t session_id, const LootPickupData& response) {
            nlohmann::json jsonMsg = {
                {"msg", "loot_pickup"},
                {"timestamp", response.timestamp},
                {"loot_id", response.loot_id},
                {"quantity", response.quantity}
            };
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) session->SendJson(jsonMsg);
        });
        game_logic.SetSendInventoryResponseCallback([&](uint64_t session_id, const InventoryData& response) {
            nlohmann::json jsonMsg = {
                {"msg", "inventory"},
                {"timestamp", response.timestamp},
                {"loot_id", response.loot_id},
                {"target_id", response.target_id},
                {"move_type", static_cast<int>(response.move_type)},
                {"inv_slot_id", response.inv_slot_id},
                {"use_slot_id", response.use_slot_id},
                {"quantity", response.quantity}
            };
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) session->SendJson(jsonMsg);
        });
        //TODO: add all game_logic
    }
}
