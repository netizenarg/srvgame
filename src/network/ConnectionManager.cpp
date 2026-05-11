#include "network/ConnectionManager.hpp"

ConnectionManager::ConnectionManager(const WorkerGroupConfig& groupConfig, MasterSender masterSender)
    : ioContext_(groupConfig.threads)
    , acceptor_(ioContext_)
    , masterSender_(std::move(masterSender))
    , groupConfig_(groupConfig)
    , host_(groupConfig.host)
    , port_(groupConfig.port)
    , reuse_(groupConfig.reuse)
{
    if (groupConfig.ssl.has_value()) {
        sslContext_ = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_server);
        sslContext_->use_certificate_chain_file(groupConfig.ssl->certificate);
        sslContext_->use_private_key_file(groupConfig.ssl->private_key, asio::ssl::context::pem);
        if (!groupConfig.ssl->dh_params.empty()) sslContext_->use_tmp_dh_file(groupConfig.ssl->dh_params);
    }
    initSessionFactory();
}

ConnectionManager::~ConnectionManager() { Shutdown(); }

bool ConnectionManager::Start() {
    try {
        asio::ip::tcp::endpoint endpoint(asio::ip::make_address(host_), port_);
        acceptor_.open(endpoint.protocol());
        if (reuse_) {
            acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
            int optval = 1;
            setsockopt(acceptor_.native_handle(), SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
        }
        acceptor_.bind(endpoint);
        acceptor_.listen(1000);
        running_ = true;
        doAccept();
        for (int i = 0; i < groupConfig_.threads - 1; ++i)
            workerThreads_.emplace_back([this]() { ioContext_.run(); });
        Logger::Info("ConnectionManager started on {}:{}", host_, port_);
        ioContext_.run();
    } catch (const std::exception& e) {
        Logger::Error("ConnectionManager start failed: {}", e.what());
        return false;
    }
    return true;
}

void ConnectionManager::Shutdown() {
    if (!running_) return;
    running_ = false;
    acceptor_.close();
    std::vector<std::shared_ptr<IConnection>> sessions;
    {
        std::unique_lock<std::shared_mutex> lock(sessionsMutex_);
        for (auto& [id, s] : sessions_) sessions.push_back(s);
        sessions_.clear();
    }
    for (auto& s : sessions) s->Stop();
    ioContext_.stop();
    for (auto& t : workerThreads_) if (t.joinable()) t.join();
    Logger::Info("ConnectionManager shut down");
}

void ConnectionManager::initSessionFactory() {
    if (groupConfig_.protocol == "binary") {
        sessionFactory_ = [this](asio::ip::tcp::socket socket, std::shared_ptr<asio::ssl::context> sslCtx) -> std::shared_ptr<IConnection> {
            auto session = std::make_shared<BinarySession>(std::move(socket), sslCtx);
            uint64_t id = session->GetSessionId();
            {
                std::unique_lock<std::shared_mutex> lock(sessionsMutex_);
                sessions_[id] = session;
            }
            session->SetDefaultBinaryMessageHandler([this, id](uint16_t type, const std::vector<uint8_t>& data) {
                onClientMessage(id, type, data);
            });
            session->SetCloseHandler([this, id]() { onClientClose(id); });
            return session;
        };
    } else if (groupConfig_.protocol == "websocket") {
        webSocketFactory_ = [this](asio::ip::tcp::socket socket, std::shared_ptr<asio::ssl::context>) -> std::shared_ptr<IConnection> {
            auto wsConn = std::make_shared<WebSocketProtocol::WebSocketConnection>(std::move(socket));
            auto session = std::make_shared<WebSocketSession>(wsConn);
            uint64_t id = session->GetSessionId();
            {
                std::unique_lock<std::shared_mutex> lock(sessionsMutex_);
                sessions_[id] = session;
            }
            session->SetMessageHandler([this, id](const nlohmann::json& msg) {
                std::string msgStr = msg.value("msg", "");
                uint16_t type = jsonMsgType(msgStr);
                std::vector<uint8_t> body;
                switch (type) {
                    case BinaryProtocol::MESSAGE_TYPE_AUTHENTICATION: {
                        BinaryProtocol::BinaryWriter w;
                        w.WriteString(msg.value("login",""));
                        w.WriteString(msg.value("password",""));
                        body = w.GetBuffer();
                        break;
                    }
                    case BinaryProtocol::MESSAGE_TYPE_CHUNK_DATA: {
                        BinaryProtocol::BinaryWriter w;
                        w.WriteInt32(msg.value("x",0));
                        w.WriteInt32(msg.value("z",0));
                        w.WriteUInt8(msg.value("lod",0));
                        w.WriteFloat(msg.value("player_x",0.0f));
                        w.WriteFloat(msg.value("player_y",0.0f));
                        w.WriteFloat(msg.value("player_z",0.0f));
                        body = w.GetBuffer();
                        break;
                    }
                    case BinaryProtocol::MESSAGE_TYPE_COLLISION_CHECK: {
                        BinaryProtocol::BinaryWriter w;
                        w.WriteVector3({msg.value("x",0.0f), msg.value("y",0.0f), msg.value("z",0.0f)});
                        w.WriteFloat(msg.value("radius",0.5f));
                        body = w.GetBuffer();
                        break;
                    }
                    case BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION: {
                        BinaryProtocol::BinaryWriter w;
                        w.WriteUInt64(msg.value("player_id",0ULL));
                        w.WriteVector3({msg.value("x",0.0f), msg.value("y",0.0f), msg.value("z",0.0f)});
                        w.WriteVector3({msg.value("vx",0.0f), msg.value("vy",0.0f), msg.value("vz",0.0f)});
                        w.WriteUInt64(msg.value("timestamp",0ULL));
                        body = w.GetBuffer();
                        break;
                    }
                    case BinaryProtocol::MESSAGE_TYPE_NPC_INTERACTION: {
                        BinaryProtocol::BinaryWriter w;
                        w.WriteUInt64(msg.value("npc_id",0ULL));
                        w.WriteString(msg.value("npc_type",""));
                        body = w.GetBuffer();
                        break;
                    }
                    case BinaryProtocol::MESSAGE_TYPE_FAMILIAR_COMMAND: {
                        BinaryProtocol::BinaryWriter w;
                        w.WriteUInt64(msg.value("familiarId",0ULL));
                        w.WriteUInt64(msg.value("targetId",0ULL));
                        w.WriteString(msg.value("command",""));
                        body = w.GetBuffer();
                        break;
                    }
                    case BinaryProtocol::MESSAGE_TYPE_ENTITY_SPAWN: {
                        BinaryProtocol::BinaryWriter w;
                        w.WriteUInt64(msg.value("id",0ULL));
                        w.WriteInt32(msg.value("entity_type",0));
                        w.WriteVector3({msg.value("x",0.0f), msg.value("y",0.0f), msg.value("z",0.0f)});
                        body = w.GetBuffer();
                        break;
                    }
                    case BinaryProtocol::MESSAGE_TYPE_LOOT_PICKUP: {
                        BinaryProtocol::BinaryWriter w;
                        w.WriteUInt64(msg.value("loot_id",0ULL));
                        w.WriteUInt16(msg.value("quantity",1));
                        body = w.GetBuffer();
                        break;
                    }
                    case BinaryProtocol::MESSAGE_TYPE_INVENTORY_MOVE: {
                        BinaryProtocol::BinaryWriter w;
                        w.WriteUInt64(msg.value("loot_id",0ULL));
                        w.WriteUInt64(msg.value("target_id",0ULL));
                        w.WriteUInt8(static_cast<uint8_t>(msg.value("move_type",0)));
                        w.WriteInt32(msg.value("inv_slot_id",-1));
                        w.WriteInt32(msg.value("use_slot_id",-1));
                        w.WriteUInt16(msg.value("quantity",1));
                        body = w.GetBuffer();
                        break;
                    }
                    default: {
                        std::string raw = msg.dump();
                        body.assign(raw.begin(), raw.end());
                        break;
                    }
                }
                onClientMessage(id, type, body);
            });
            session->SetCloseHandler([this, id]() { onClientClose(id); });
            return session;
        };
    }
}

void ConnectionManager::doAccept() {
    acceptor_.async_accept([this](std::error_code ec, asio::ip::tcp::socket socket) {
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
            if (groupConfig_.protocol == "binary" && sessionFactory_)
                sessionFactory_(std::move(socket), sslContext_);
            else if (groupConfig_.protocol == "websocket" && webSocketFactory_)
                webSocketFactory_(std::move(socket), sslContext_);
        }
        if (running_) doAccept();
    });
}

void ConnectionManager::onClientMessage(uint64_t sessionId, uint16_t type, const std::vector<uint8_t>& data) {
    uint32_t corrId = nextCorrelationId_++;
    pendingReplies_[corrId] = {sessionId, type};
    masterSender_(corrId, sessionId, type, data);
}

void ConnectionManager::onClientClose(uint64_t sessionId) {
    std::unique_lock<std::shared_mutex> lock(sessionsMutex_);
    auto it = sessions_.find(sessionId);
    if (it != sessions_.end()) {
        it->second->Stop();
        sessions_.erase(it);
    }
}

void ConnectionManager::OnMasterReply(uint32_t correlationId, const std::vector<uint8_t>& reply) {
    auto it = pendingReplies_.find(correlationId);
    if (it == pendingReplies_.end()) return;
    PendingEntry entry = it->second;
    pendingReplies_.erase(it);
    std::shared_ptr<IConnection> session;
    {
        std::shared_lock<std::shared_mutex> lock(sessionsMutex_);
        auto sIt = sessions_.find(entry.sessionId);
        if (sIt != sessions_.end()) session = sIt->second;
    }
    if (!session) return;
    if (groupConfig_.protocol == "binary") {
        session->SendRaw(std::string(reply.begin(), reply.end()));
        if (entry.messageType == BinaryProtocol::MESSAGE_TYPE_AUTHENTICATION && reply.size() >= 16) {
            BinaryProtocol::BinaryReader r(reply.data(), reply.size());
            r.ReadUInt64();
            uint64_t playerId = r.ReadUInt64();
            if (playerId > 0) {
                session->SetPlayerId(playerId);
                session->Authenticate("");
            }
        }
    } else {
        if (reply.size() >= 2) {
            BinaryProtocol::BinaryReader r(reply.data(), reply.size());
            uint16_t type = r.ReadUInt16();
            std::vector<uint8_t> body(reply.begin() + 2, reply.end());
            session->SendJson(binaryToJson(type, body));
            if (type == BinaryProtocol::MESSAGE_TYPE_AUTHENTICATION) {
                nlohmann::json j = binaryToJson(type, body);
                uint64_t playerId = j.value("player_id", 0ULL);
                if (playerId > 0) {
                    session->SetPlayerId(playerId);
                    session->Authenticate("");
                }
            }
        }
    }
}

uint16_t ConnectionManager::jsonMsgType(const std::string& msg) {
    static const std::unordered_map<std::string, uint16_t> map = {
        {"authentication", BinaryProtocol::MESSAGE_TYPE_AUTHENTICATION},
        {"chunk_params", BinaryProtocol::MESSAGE_TYPE_CHUNK_PARAMS},
        {"get_chunk", BinaryProtocol::MESSAGE_TYPE_CHUNK_DATA},
        {"collision", BinaryProtocol::MESSAGE_TYPE_COLLISION_CHECK},
        {"player_state", BinaryProtocol::MESSAGE_TYPE_PLAYER_STATE},
        {"player_position", BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION},
        {"npc_interaction", BinaryProtocol::MESSAGE_TYPE_NPC_INTERACTION},
        {"familiar", BinaryProtocol::MESSAGE_TYPE_FAMILIAR_COMMAND},
        {"entity_spawn", BinaryProtocol::MESSAGE_TYPE_ENTITY_SPAWN},
        {"loot_pickup", BinaryProtocol::MESSAGE_TYPE_LOOT_PICKUP},
        {"inventory", BinaryProtocol::MESSAGE_TYPE_INVENTORY_MOVE}
    };
    auto it = map.find(msg);
    return (it != map.end()) ? it->second : 0xFFFF;
}

nlohmann::json ConnectionManager::binaryToJson(uint16_t type, const std::vector<uint8_t>& body) {
    BinaryProtocol::BinaryReader r(body.data(), body.size());
    switch (type) {
        case BinaryProtocol::MESSAGE_TYPE_AUTHENTICATION: {
            uint64_t timestamp = r.ReadUInt64();
            uint64_t player_id = r.ReadUInt64();
            std::string message = r.ReadString();
            return {{"msg","authentication"},{"timestamp",timestamp},{"desc",message},{"player_id",player_id}};
        }
        case BinaryProtocol::MESSAGE_TYPE_CHUNK_PARAMS: {
            uint64_t timestamp = r.ReadUInt64();
            uint32_t size = r.ReadUInt32();
            float spacing = r.ReadFloat();
            return {{"msg","chunk_params"},{"timestamp",timestamp},{"size",size},{"spacing",spacing}};
        }
        case BinaryProtocol::MESSAGE_TYPE_CHUNK_DATA: {
            uint64_t timestamp = r.ReadUInt64();
            int32_t x = r.ReadInt32();
            int32_t z = r.ReadInt32();
            uint32_t vertSize = r.ReadUInt32();
            std::vector<float> verts(vertSize / sizeof(float));
            std::memcpy(verts.data(), r.ReadBytes(vertSize).data(), vertSize);
            uint32_t idxSize = r.ReadUInt32();
            std::vector<uint32_t> indices(idxSize / sizeof(uint32_t));
            std::memcpy(indices.data(), r.ReadBytes(idxSize).data(), idxSize);
            uint32_t lod = r.ReadUInt32();
            return {{"msg","get_chunk"},{"timestamp",timestamp},{"x",x},{"z",z},{"lod",lod},{"vertices",verts},{"indices",indices}};
        }
        case BinaryProtocol::MESSAGE_TYPE_COLLISION_CHECK: {
            uint64_t timestamp = r.ReadUInt64();
            bool collided = r.ReadUInt8() != 0;
            uint64_t collided_id = r.ReadUInt64();
            float penetration = r.ReadFloat();
            glm::vec3 resolution = r.ReadVector3();
            return {{"msg","collision"},{"timestamp",timestamp},{"collided",collided},{"collided_id",collided_id},{"penetration",penetration},{"resolution",{resolution.x,resolution.y,resolution.z}}};
        }
        case BinaryProtocol::MESSAGE_TYPE_PLAYER_STATE: {
            uint64_t timestamp = r.ReadUInt64();
            uint64_t player_id = r.ReadUInt64();
            glm::vec3 pos = r.ReadVector3();
            glm::vec3 vel = r.ReadVector3();
            glm::vec3 rot = r.ReadVector3();
            bool on_ground = r.ReadUInt8() != 0;
            return {{"msg","player_state"},{"timestamp",timestamp},{"entity_id",player_id},{"x",pos.x},{"y",pos.y},{"z",pos.z},{"vx",vel.x},{"vy",vel.y},{"vz",vel.z},{"rx",rot.x},{"ry",rot.y},{"rz",rot.z},{"on_ground",on_ground}};
        }
        case BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION: {
            uint64_t timestamp = r.ReadUInt64();
            uint64_t player_id = r.ReadUInt64();
            glm::vec3 pos = r.ReadVector3();
            glm::vec3 vel = r.ReadVector3();
            return {{"msg","player_position"},{"timestamp",timestamp},{"player_id",player_id},{"x",pos.x},{"y",pos.y},{"z",pos.z},{"vx",vel.x},{"vy",vel.y},{"vz",vel.z}};
        }
        case BinaryProtocol::MESSAGE_TYPE_PLAYER_SPAWN: {
            uint64_t timestamp = r.ReadUInt64();
            uint64_t player_id = r.ReadUInt64();
            std::string name = r.ReadString();
            glm::vec3 pos = r.ReadVector3();
            float yaw = r.ReadFloat();
            float health = r.ReadFloat();
            float max_health = r.ReadFloat();
            return {{"msg","player_spawn"},{"timestamp",timestamp},{"player_id",player_id},{"name",name},{"position",{pos.x,pos.y,pos.z}},{"yaw",yaw},{"health",health},{"max_health",max_health}};
        }
        case BinaryProtocol::MESSAGE_TYPE_PLAYER_DESPAWN: {
            uint64_t timestamp = r.ReadUInt64();
            uint64_t player_id = r.ReadUInt64();
            return {{"msg","player_despawn"},{"timestamp",timestamp},{"player_id",player_id}};
        }
        case BinaryProtocol::MESSAGE_TYPE_COMBAT_EVENT: {
            uint64_t timestamp = r.ReadUInt64();
            uint64_t player_id = r.ReadUInt64();
            uint64_t npc_id = r.ReadUInt64();
            float damage = r.ReadFloat();
            float health = r.ReadFloat();
            bool is_dead = r.ReadUInt8() != 0;
            return {{"msg","combat"},{"timestamp",timestamp},{"player_id",player_id},{"npc_id",npc_id},{"damage",damage},{"health",health},{"is_dead",is_dead}};
        }
        case BinaryProtocol::MESSAGE_TYPE_NPC_INTERACTION: {
            uint64_t npc_id = r.ReadUInt64();
            nlohmann::json quests = r.ReadJson();
            uint64_t timestamp = r.ReadUInt64();
            return {{"msg","npc_interaction"},{"npc_id",npc_id},{"quests",quests},{"timestamp",timestamp}};
        }
        case BinaryProtocol::MESSAGE_TYPE_FAMILIAR_COMMAND: {
            uint64_t timestamp = r.ReadUInt64();
            uint64_t familiar_id = r.ReadUInt64();
            uint64_t target_id = r.ReadUInt64();
            std::string command = r.ReadString();
            return {{"msg","familiar"},{"timestamp",timestamp},{"familiar_id",familiar_id},{"target_id",target_id},{"command",command}};
        }
        case BinaryProtocol::MESSAGE_TYPE_ENTITY_SPAWN: {
            uint64_t timestamp = r.ReadUInt64();
            uint64_t entity_id = r.ReadUInt64();
            int32_t type = r.ReadInt32();
            glm::vec3 pos = r.ReadVector3();
            return {{"msg","entity_spawn"},{"timestamp",timestamp},{"id",entity_id},{"type",type},{"position",{pos.x,pos.y,pos.z}}};
        }
        case BinaryProtocol::MESSAGE_TYPE_LOOT_PICKUP: {
            uint64_t timestamp = r.ReadUInt64();
            uint64_t loot_id = r.ReadUInt64();
            uint16_t quantity = r.ReadUInt16();
            return {{"msg","loot_pickup"},{"timestamp",timestamp},{"loot_id",loot_id},{"quantity",quantity}};
        }
        case BinaryProtocol::MESSAGE_TYPE_INVENTORY_MOVE: {
            uint64_t timestamp = r.ReadUInt64();
            uint64_t loot_id = r.ReadUInt64();
            uint64_t target_id = r.ReadUInt64();
            uint8_t move_type = r.ReadUInt8();
            int32_t inv_slot = r.ReadInt32();
            int32_t use_slot = r.ReadInt32();
            uint16_t quantity = r.ReadUInt16();
            return {{"msg","inventory"},{"timestamp",timestamp},{"loot_id",loot_id},{"target_id",target_id},{"move_type",move_type},{"inv_slot_id",inv_slot},{"use_slot_id",use_slot},{"quantity",quantity}};
        }
        default:
            return {{"msg","error"},{"desc","Unknown binary message type"}};
    }
}
