#include "network/GameServer.hpp"

GameServer::GameServer(const WorkerGroupConfig& groupConfig, const ConfigManager& globalConfig)
    : ioContext_(groupConfig.threads),
      acceptor_(ioContext_),
      groupConfig_(groupConfig),
      globalConfig_(globalConfig),
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
    } catch (const std::exception& e) {
        Logger::Critical("Failed to initialize server for protocol '{}': {}",
                         groupConfig_.protocol, e.what());
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

void GameServer::InitSessionFactory(int workerId, ProcessPool* processPool, GameLogic& game_logic) {
    if (groupConfig_.protocol == "binary") {
        sessionFactory_ = [workerId, processPool, &game_logic, this]
        (asio::ip::tcp::socket socket, std::shared_ptr<asio::ssl::context> sslCtx) mutable{
            auto session = std::make_shared<BinarySession>(std::move(socket), sslCtx);
            Logger::Trace("Worker {} created new game session {}; protocol: binary", workerId, session->GetSessionId());
            session->SetMessageHandler([session, workerId, processPool, &game_logic]
            (const nlohmann::json& msg) mutable{
                try {
                    std::string msgType = msg.value("type", "");
                    Logger::Trace("Worker {} processing message type: {}", workerId, msgType);
                    if (msgType == "ipc_message" && processPool) {
                        if (msg.contains("target_worker") && msg.contains("payload")) {
                            int targetWorker = msg["target_worker"];
                            std::string payload = msg["payload"].dump();
                            if (processPool->SendToWorker(targetWorker, payload)) {
                                Logger::Trace("Worker {} sent IPC message to worker {}", workerId, targetWorker);
                            } else {
                                Logger::Error("Worker {} failed to send IPC message to worker {}", workerId, targetWorker);
                            }
                        }
                    } else {
                        game_logic.HandleMessage(session->GetSessionId(), msg);
                    }
                } catch (const std::exception& e) {
                    Logger::Error("Worker {} error processing message: {}", workerId, e.what());
                    session->SendError("Internal server error", 500);
                }
            });
            session->SetDefaultBinaryMessageHandler([session, workerId, &game_logic]
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
                    case BinaryProtocol::MESSAGE_TYPE_CHUNK_REQUEST: {
                        BinaryProtocol::BinaryReader reader(data.data(), data.size());
                        ChunkRequestData req;
                        req.chunk_x = reader.ReadInt32();
                        req.chunk_z = reader.ReadInt32();
                        req.lod = reader.ReadUInt8();
                        req.session_id = session->GetSessionId();
                        game_logic.OnChunkRequest(req);
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
                        PlayerPositionData posData;
                        posData.player_id = reader.ReadUInt64();
                        posData.position = reader.ReadVector3();
                        posData.velocity = reader.ReadVector3();
                        posData.timestamp = reader.ReadUInt64();
                        posData.session_id = session->GetSessionId();
                        game_logic.OnPlayerPosition(posData);
                        break;
                    }
                    default:
                        game_logic.HandleBinaryMessage(session->GetSessionId(), type, data);
                        break;
                }
            });
            session->SetCloseHandler([session, workerId, &game_logic]() mutable{
                Logger::Trace("Worker {} session {} closing", workerId, session->GetSessionId());
                game_logic.OnPlayerDisconnected(session->GetSessionId());
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
            session->SetMessageHandler([session, workerId, processPool, &game_logic]
            (const nlohmann::json& msg) mutable{
                std::string msgType = msg.value("type", "");
                if (msgType == "authentication") {
                    AuthenticationData authData;
                    authData.username = msg.value("login", "");
                    authData.password = msg.value("password", "");
                    authData.session_id = session->GetSessionId();
                    game_logic.OnAuthentication(authData);
                }
                else if (msgType == "get_chunk") {
                    ChunkRequestData req;
                    req.chunk_x = msg.value("x", 0);
                    req.chunk_z = msg.value("z", 0);
                    req.lod = static_cast<uint8_t>(msg.value("lod", 0));
                    req.session_id = session->GetSessionId();
                    game_logic.OnChunkRequest(req);
                }
                else if (msgType == "player_state") {
                    // TODO: fill full state data format
                    PlayerStateData posData;
                    posData.player_id = game_logic.GetPlayerIdBySession(session->GetSessionId());
                    posData.position.x = msg.value("pos_x", 0.0f);
                    posData.position.y = msg.value("pos_y", 0.0f);
                    posData.position.z = msg.value("pos_z", 0.0f);
                    posData.timestamp = game_logic.GetCurrentTimestamp();
                    posData.session_id = session->GetSessionId();
                    game_logic.OnPlayerState(posData);
                }
                else if (msgType == "player_position_update") {
                    PlayerPositionData posData;
                    posData.player_id = game_logic.GetPlayerIdBySession(session->GetSessionId());
                    posData.position.x = msg.value("x", 0.0f);
                    posData.position.y = msg.value("y", 0.0f);
                    posData.position.z = msg.value("z", 0.0f);
                    posData.timestamp = game_logic.GetCurrentTimestamp();
                    posData.session_id = session->GetSessionId();
                    game_logic.OnPlayerPosition(posData);
                }
                else {
                    game_logic.HandleMessage(session->GetSessionId(), msg);
                }
            });
            // session->SetMessageHandler([session, workerId, processPool](const nlohmann::json& msg) {
            //     try {
            //         std::string msgType = msg.value("type", "");
            //         Logger::Trace("Worker {} processing message type: {}", workerId, msgType);
            //         if (msgType == "ipc_message" && processPool) {
            //             if (msg.contains("target_worker") && msg.contains("payload")) {
            //                 int targetWorker = msg["target_worker"];
            //                 std::string payload = msg["payload"].dump();
            //                 if (processPool->SendToWorker(targetWorker, payload)) {
            //                     Logger::Trace("Worker {} sent IPC message to worker {}", workerId, targetWorker);
            //                 } else {
            //                     Logger::Error("Worker {} failed to send IPC message to worker {}", workerId, targetWorker);
            //                 }
            //             }
            //         } else {
            //             game_logic.HandleMessage(session->GetSessionId(), msg);
            //         }
            //     } catch (const std::exception& e) {
            //         Logger::Error("Worker {} error processing message: {}", workerId, e.what());
            //         session->SendError("Internal server error", 500);
            //     }
            // });
            session->SetBinaryMessageHandler([session, workerId, &game_logic]
            (uint16_t type, const std::vector<uint8_t>& data) mutable{
                game_logic.HandleBinaryMessage(session->GetSessionId(), type, data);
            });
            session->SetCloseHandler([session, workerId, &game_logic]() mutable{
                Logger::Trace("Worker {} session {} closing", workerId, session->GetSessionId());
                game_logic.OnPlayerDisconnected(session->GetSessionId());
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
    Logger::Info("GameServer shutdown completed");
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
    if (protocol == "binary") {
        game_logic.SetSendAuthenticationResponseCallback([&](uint64_t session_id, bool success, const std::string& message, uint64_t player_id) {
            BinaryProtocol::BinaryWriter writer;
            writer.WriteUInt8(success ? 1 : 0);
            writer.WriteUInt64(player_id);
            writer.WriteString(message);
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) {
                session->SendBinary(BinaryProtocol::MESSAGE_TYPE_AUTHENTICATION, writer.GetBuffer());
            }
        });
        game_logic.SetSendChunkCallback([&](uint64_t session_id, const ChunkData& data) {
            BinaryProtocol::BinaryWriter writer;
            writer.WriteInt32(data.chunk_x);
            writer.WriteInt32(data.chunk_z);
            writer.WriteUInt8(data.lod);
            writer.WriteJson(data.chunk_json);
            writer.WriteUInt64(data.timestamp);
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) {
                session->SendBinary(BinaryProtocol::MESSAGE_TYPE_CHUNK_DATA, writer.GetBuffer());
            }
        });
        game_logic.SetPlayerStateCallback([&](const PlayerStateData& state) {
            BinaryProtocol::BinaryWriter writer;
            writer.WriteUInt64(state.player_id);
            writer.WriteVector3(state.position);
            writer.WriteVector3(state.velocity);
            writer.WriteVector3(state.rotation);
            writer.WriteUInt8(state.on_ground ? 1 : 0);
            writer.WriteUInt64(state.timestamp);
            auto nearbySessions = GetSessionsInRadius(state.position, 100.0f);
            for (auto& session : nearbySessions) {
                session->SendBinary(BinaryProtocol::MESSAGE_TYPE_ENTITY_UPDATE, writer.GetBuffer());
            }
        });
        game_logic.SetBroadcastPlayerPositionCallback([&](const PlayerPositionData& data, float radius) {
            BinaryProtocol::BinaryWriter writer;
            writer.WriteUInt64(data.player_id);
            writer.WriteVector3(data.position);
            writer.WriteVector3(data.velocity);
            writer.WriteUInt64(data.timestamp);
            auto nearbySessions = GetSessionsInRadius(data.position, radius);
            for (auto& session : nearbySessions) {
                session->SendBinary(BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION, writer.GetBuffer());
            }
        });
    } else { // websocket
        game_logic.SetSendAuthenticationResponseCallback([&](uint64_t session_id, bool success, const std::string& message, uint64_t player_id) {
            nlohmann::json response = {
                {"type", "authentication_response"},
                {"success", success},
                {"message", message},
                {"player_id", player_id},
                {"timestamp", game_logic.GetCurrentTimestamp()}
            };
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) {
                session->Send(response);
            }
        });
        game_logic.SetSendChunkCallback([&](uint64_t session_id, const ChunkData& data) {
            nlohmann::json msg = {
                {"type", "world_chunk"},
                {"chunk_x", data.chunk_x},
                {"chunk_z", data.chunk_z},
                {"lod", data.lod},
                {"data", data.chunk_json},
                {"timestamp", data.timestamp}
            };
            auto session = ConnectionManager::GetInstance().GetSession(session_id);
            if (session) {
                session->Send(msg);
            }
        });
        game_logic.SetPlayerStateCallback([&](const PlayerStateData& state) {
            nlohmann::json jsonMsg = {
                {"type", "entity_update"},
                {"entity_id", state.player_id},
                {"x", state.position.x}, {"y", state.position.y}, {"z", state.position.z},
                {"rx", state.rotation.x}, {"ry", state.rotation.y}, {"rz", state.rotation.z},
                {"vx", state.velocity.x}, {"vy", state.velocity.y}, {"vz", state.velocity.z},
                {"timestamp", state.timestamp}
            };
            auto nearbySessions = GetSessionsInRadius(state.position, 100.0f);
            for (auto& session : nearbySessions) {
                session->Send(jsonMsg);
            }
        });
        game_logic.SetBroadcastPlayerPositionCallback([&](const PlayerPositionData& data, float radius) {
            nlohmann::json jsonMsg = {
                {"type", "player_position"},
                {"player_id", data.player_id},
                {"x", data.position.x}, {"y", data.position.y}, {"z", data.position.z},
                {"vx", data.velocity.x}, {"vy", data.velocity.y}, {"vz", data.velocity.z},
                {"timestamp", data.timestamp}
            };
            auto nearbySessions = GetSessionsInRadius(data.position, radius);
            for (auto& session : nearbySessions) {
                session->Send(jsonMsg);
            }
        });
    }
}
