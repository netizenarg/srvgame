#include "game/GameLogic.hpp"

static std::unique_ptr<ScriptHotReloader> g_hotReloader;
std::mutex GameLogic::instanceMutex_;
GameLogic* GameLogic::instance_ = nullptr;

GameLogic& GameLogic::GetInstance() {
    std::lock_guard<std::mutex> lock(instanceMutex_);
    if (!instance_) {
        instance_ = new GameLogic();
    }
   return *instance_;
}

GameLogic::GameLogic() {
    Logger::Debug("GameLogic created");
}

GameLogic::~GameLogic() {
    if (instance_) {
        Shutdown();
    }
}

void GameLogic::Initialize() {
    if (initialized_) {
        Logger::Warn("GameLogic already initialized");
        return;
    }
    initialized_ = true;
    LogicCore::Initialize();
    Logger::Info("Initializing GameLogic with world system...");
    auto& config = ConfigManager::GetInstance();
    WorldConfig worldConfig;
    worldConfig.seed = config.GetInt("world.seed", 12345);
    worldConfig.viewDistance = config.GetInt("world.view_distance", 4);
    worldConfig.chunkSize = config.GetFloat("world.chunk_size", 32.0f);
    worldConfig.maxActiveChunks = config.GetInt("world.max_active_chunks", 100);
    worldConfig.terrainScale = config.GetFloat("world.terrain_scale", 100.0f);
    worldConfig.maxTerrainHeight = config.GetFloat("world.max_terrain_height", 50.0f);
    worldConfig.waterLevel = config.GetFloat("world.water_level", 10.0f);
    worldConfig.unloadDistance = config.GetFloat("world.unload_distance", 200.0f);
    SetWorldConfig(worldConfig);
    LogicWorld::GetInstance().Initialize(worldConfig);
    int preloadRadius = 1;
    for (int x = -preloadRadius; x <= preloadRadius; ++x) {
        for (int z = -preloadRadius; z <= preloadRadius; ++z) {
            GetOrCreateChunk(x, z);
        }
    }
    LogicEntity::GetInstance().Initialize();
    if (!LoadGameData()) {
        Logger::Error("Failed to load game data");
    }
    pythonEnabled_ = config.GetBool("python.enabled", false);
    if (pythonEnabled_) {
        auto& pythonScripting = PythonScripting::GetInstance();
        if (pythonScripting.Initialize()) {
            Logger::Info("Python scripting initialized");
            RegisterPythonEventHandlers();
            bool hotReloadEnabled = config.GetBool("python.hot_reload", true);
            if (hotReloadEnabled) {
                std::string scriptDir = config.GetString("python.script_dir", "./scripts");
                g_hotReloader = std::make_unique<ScriptHotReloader>(scriptDir, 2000);
                g_hotReloader->Start();
            }
        } else {
            Logger::Warn("Failed to initialize Python scripting");
            pythonEnabled_ = false;
        }
    }
    Logger::Info("GameLogic world system initialized successfully");
}

void GameLogic::Shutdown() {
    if (!initialized_) {
        Logger::Warn("GameLogic already shutdown");
        return;
    }
    Logger::Info("Shutting down GameLogic world system...");
    if (g_hotReloader) {
        g_hotReloader->Stop();
        g_hotReloader.reset();
    }
    if (pythonEnabled_) {
        PythonScripting::GetInstance().Shutdown();
    }
    LogicEntity::GetInstance().Shutdown();
    LogicWorld::GetInstance().Shutdown();
    LogicCore::Shutdown();
    initialized_ = false;
}

void GameLogic::SetWorldConfig(const WorldConfig& config) {
    LogicWorld::GetInstance().SetConfig(config);
}

const GameLogic::WorldConfig& GameLogic::GetWorldConfig() const {
    return static_cast<const WorldConfig&>(LogicWorld::GetInstance().GetConfig());
}

std::shared_ptr<WorldChunk> GameLogic::GetOrCreateChunk(int chunk_x, int chunk_z) {
    return LogicWorld::GetInstance().GetOrCreateChunk(chunk_x, chunk_z);
}

void GameLogic::GenerateWorldAroundPlayer(uint64_t player_id, const glm::vec3& position) {
    LogicWorld::GetInstance().GenerateWorldAroundPlayer(position, GetWorldConfig().viewDistance);
    SyncNearbyEntitiesToPlayer(GetSessionIdByPlayer(player_id), position);
}

void GameLogic::PreloadWorldData(float radius) {
    LogicWorld::GetInstance().PreloadWorldData(radius);
}

float GameLogic::GetTerrainHeight(float x, float z) const {
    return LogicWorld::GetInstance().GetTerrainHeight(x, z);
}

BiomeType GameLogic::GetBiomeAt(float x, float z) const {
    return LogicWorld::GetInstance().GetBiomeAt(x, z);
}

uint64_t GameLogic::SpawnNPC(NPCType type, const glm::vec3& position, uint64_t ownerId) {
    return LogicEntity::GetInstance().SpawnNPC(type, position, ownerId);
}

void GameLogic::DespawnNPC(uint64_t npc_id) {
    LogicEntity::GetInstance().DespawnNPC(npc_id);
}

NPCEntity* GameLogic::GetNPCEntity(uint64_t npc_id) {
    return LogicEntity::GetInstance().GetNPCEntity(npc_id);
}

GameEntity* GameLogic::GetEntity(uint64_t entityId) {
    return LogicEntity::GetInstance().GetEntity(entityId);
}

std::shared_ptr<Player> GameLogic::GetPlayer(uint64_t player_id) {
    return PlayerManager::GetInstance().GetPlayer(player_id);
}

CollisionResult GameLogic::CheckCollision(const glm::vec3& position, float radius, uint64_t excludeEntityId) {
    return LogicEntity::GetInstance().CheckCollision(position, radius, excludeEntityId);
}

bool GameLogic::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit) {
    return LogicEntity::GetInstance().Raycast(origin, direction, maxDistance, hit);
}

void GameLogic::CreateLootEntity(const glm::vec3& position, std::shared_ptr<LootItem> item, int quantity) {
    if (!item) {
        auto& lootTables = LootTableManager::GetInstance();
        auto loot = lootTables.GenerateLoot("default_loot_table");
        if (!loot.empty()) {
            item = loot[0].first;
            quantity = loot[0].second;
        } else {
            Logger::Warn("No default loot table or empty generation");
            return;
        }
    }
    LogicEntity::GetInstance().CreateLootEntity(position, item, quantity);
}

void GameLogic::SendPositionCorrection(uint64_t session_id, const glm::vec3& position, const glm::vec3& velocity) {
    BinaryProtocol::BinaryWriter writer;
    writer.WriteVector3(position);
    writer.WriteVector3(velocity);
    writer.WriteUInt64(GetCurrentTimestamp());
    auto data = writer.GetBuffer();
    auto session = connectionManager_->GetSession(session_id);
    if (!session || !session->IsConnected()) return;
    if (session->GetProtocolMode() == ProtocolMode::Binary) {
        session->Send(BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION_CORRECTION, data);
    } else {
        nlohmann::json jsonMsg = {
            {"type", "position_correction"},
            {"x", position.x}, {"y", position.y}, {"z", position.z},
            {"vx", velocity.x}, {"vy", velocity.y}, {"vz", velocity.z},
            {"timestamp", GetCurrentTimestamp()}
        };
        session->SendJson(jsonMsg);
    }
}

void GameLogic::OnPlayerConnected(uint64_t session_id, uint64_t player_id) {
    Logger::Info("GameLogic: Player {} connected with session {}", player_id, session_id);
    FirePythonEvent("player_connected", {
        {"session_id", session_id},
        {"player_id", player_id}
    });
    LogicCore::OnPlayerConnected(session_id, player_id);
}

void GameLogic::OnPlayerDisconnected(uint64_t session_id) {
    uint64_t player_id = GetPlayerIdBySession(session_id);
    {
        std::lock_guard<std::mutex> lock(predictionMutex_);
        playerPrediction_.erase(player_id);
    }
    Logger::Info("GameLogic: Player {} disconnected from session {}", player_id, session_id);
    FirePythonEvent("player_disconnected", {
        {"session_id", session_id},
        {"player_id", player_id}
    });
    LogicCore::OnPlayerDisconnected(session_id);
}

void GameLogic::SetSendCollisionResponseCallback(std::function<void(uint64_t, const CollisionResult&)> cb) {
    sendCollisionResponseCb_ = std::move(cb);
}

void GameLogic::OnCollisionCheck(const CollisionData& data) {
    CollisionResult result = CheckCollision(data.position, data.radius);
    if (sendCollisionResponseCb_) {
        sendCollisionResponseCb_(data.session_id, result);
    } else {
        Logger::Error("No sendCollisionResponseCb_ set in GameLogic");
    }
}

void GameLogic::SyncNearbyEntitiesToPlayer(uint64_t session_id, const glm::vec3& position) {
    auto nearbyEntities = EntityManager::GetInstance().GetEntitiesInRadius(position, 100.0f);
    nlohmann::json entityList = nlohmann::json::array();
    for (uint64_t entityId : nearbyEntities) {
        auto entity = GetEntity(entityId);
        if (entity) {
            entityList.push_back(entity->Serialize());
        }
    }
    nlohmann::json message = {
        {"msg", "entity_sync"},
        {"entities", entityList},
        {"timestamp", GetCurrentTimestamp()}
    };
    SendToSessionJson(session_id, message);
}

void GameLogic::SendAuthentication(uint64_t session_id, const std::string& message, uint64_t player_id) {
    nlohmann::json response = {
        {"msg", "authentication"},
        {"player_id", player_id},
        {"desc", message},
        {"timestamp", GetCurrentTimestamp()}
    };
    SendToSessionJson(session_id, response);
}

void GameLogic::SendAuthenticationFailure(uint64_t session_id, const std::string& message) {
    SendAuthentication(session_id, message, 0);
}

void GameLogic::BroadcastToAllPlayers(const nlohmann::json& message) {
    if (!connectionManager_) {
        Logger::Warn("Cannot broadcast: ConnectionManager not available");
        return;
    }
    try {
        std::string serialized = message.dump();
        auto sessions = connectionManager_->GetAllSessions();
        if (sessions.empty()) return;
        Logger::Debug("Broadcasting to {} player(s): {}", sessions.size(), message.dump());
        for (auto& session : sessions) {
            if (session && session->IsConnected()) {
                try {
                    session->SendRaw(serialized);
                } catch (const std::exception& e) {
                    Logger::Error("Failed to send broadcast to session {}: {}",
                                  session->GetSessionId(), e.what());
                }
            }
        }
        Logger::Info("Successfully broadcasted to {} player(s)", sessions.size());
    } catch (const std::exception& e) {
        Logger::Error("Error broadcasting to all players: {}", e.what());
    }
}

void GameLogic::PerformMaintenance() {
    CleanupOldData();
    SaveChunkData();
    if (databaseBackend_) {
        bool healthy = databaseBackend_->CheckHealth();
        if (!healthy) {
            Logger::Warn("Database health check failed, attempting to reconnect");
            databaseBackend_->ReconnectAll();
        }
    }
}

bool GameLogic::LoadGameData() {
    Logger::Debug("Loading game data");
    return true;
}

void GameLogic::SaveChunkData() {
    LogicWorld::GetInstance().SaveChunkData();
}

nlohmann::json GameLogic::PlayerUpdateToJson(uint64_t player_id, const glm::vec3& pos, float yaw, float health, float maxHealth, const std::string& name) {
    return {
        {"msg", "player_update"},
        {"player_id", player_id},
        {"position", {pos.x, pos.y, pos.z}},
        {"yaw", yaw},
        {"health", health},
        {"max_health", maxHealth},
        {"name", name},
        {"timestamp", GetCurrentTimestamp()}
    };
}

nlohmann::json GameLogic::PlayerPositionToJson(const std::vector<uint8_t>& data) {
    BinaryProtocol::BinaryReader reader(data.data(), data.size());
    uint64_t player_id = reader.ReadUInt64();
    glm::vec3 pos = reader.ReadVector3();
    glm::vec3 vel = reader.ReadVector3();
    uint64_t timestamp = reader.ReadUInt64();
    return {
        {"msg", "player_position"},
        {"player_id", player_id},
        {"x", pos.x}, {"y", pos.y}, {"z", pos.z},
        {"vx", vel.x}, {"vy", vel.y}, {"vz", vel.z},
        {"timestamp", timestamp}
    };
}

nlohmann::json GameLogic::PlayerUpdateToJson(const std::vector<uint8_t>& data) {
    BinaryProtocol::BinaryReader reader(data.data(), data.size());
    uint32_t count = reader.ReadUInt32();
    nlohmann::json playersArray = nlohmann::json::array();
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t pid = reader.ReadUInt32();
        float x = reader.ReadFloat(), y = reader.ReadFloat(), z = reader.ReadFloat();
        float yaw = reader.ReadFloat();
        float health = reader.ReadFloat(), maxHealth = reader.ReadFloat();
        std::string name = reader.ReadString();
        playersArray.push_back({
            {"id", pid},
            {"x", x}, {"y", y}, {"z", z},
            {"yaw", yaw},
            {"health", health},
            {"max_health", maxHealth},
            {"name", name}
        });
    }
    return {
        {"msg", "player_update"},
        {"players", playersArray},
        {"timestamp", GetCurrentTimestamp()}
    };
}

nlohmann::json GameLogic::EntitySpawnToJson(const std::vector<uint8_t>& data) {
    BinaryProtocol::BinaryReader reader(data.data(), data.size());
    uint64_t entityId = reader.ReadUInt64();
    uint8_t type = reader.ReadUInt8();
    std::string name = reader.ReadString();
    glm::vec3 pos = reader.ReadVector3();
    float yaw = reader.ReadFloat();
    uint64_t timestamp = reader.ReadUInt64();
    return {
        {"msg", "entity_spawn"},
        {"entity_id", entityId},
        {"type", type},
        {"name", name},
        {"x", pos.x}, {"y", pos.y}, {"z", pos.z},
        {"yaw", yaw},
        {"timestamp", timestamp}
    };
}

nlohmann::json GameLogic::EntityUpdateToJson(const std::vector<uint8_t>& data) {
    BinaryProtocol::BinaryReader reader(data.data(), data.size());
    uint64_t entityId = reader.ReadUInt64();
    glm::vec3 pos = reader.ReadVector3();
    glm::vec3 rot = reader.ReadVector3();
    glm::vec3 vel = reader.ReadVector3();
    uint64_t timestamp = reader.ReadUInt64();
    return {
        {"msg", "entity_update"},
        {"entity_id", entityId},
        {"x", pos.x}, {"y", pos.y}, {"z", pos.z},
        {"rx", rot.x}, {"ry", rot.y}, {"rz", rot.z},
        {"vx", vel.x}, {"vy", vel.y}, {"vz", vel.z},
        {"timestamp", timestamp}
    };
}

nlohmann::json GameLogic::EntityDespawnToJson(const std::vector<uint8_t>& data) {
    BinaryProtocol::BinaryReader reader(data.data(), data.size());
    uint64_t entityId = reader.ReadUInt64();
    uint64_t timestamp = reader.ReadUInt64();
    return {
        {"msg", "entity_despawn"},
        {"entity_id", entityId},
        {"timestamp", timestamp}
    };
}

nlohmann::json GameLogic::ChunkDataToJson(const std::vector<uint8_t>& data) {
    BinaryProtocol::BinaryReader reader(data.data(), data.size());
    int chunk_x = reader.ReadInt32();
    int chunk_z = reader.ReadInt32();
    uint8_t lod = reader.ReadUInt8();
    nlohmann::json chunk_json = reader.ReadJson();
    uint64_t timestamp = reader.ReadUInt64();
    return {
        {"msg", "get_chunk"},
        {"x", chunk_x},
        {"z", chunk_z},
        {"lod", lod},
        {"data", chunk_json},
        {"timestamp", timestamp}
    };
}

void GameLogic::SetSendAuthenticationResponseCallback(std::function<void(uint64_t, const std::string&, uint64_t)> cb) {
    sendAuthResponseCb_ = std::move(cb);
}

void GameLogic::SetSendChunkCallback(std::function<void(uint64_t, const ChunkData&)> cb) {
    sendChunkCb_ = std::move(cb);
}

void GameLogic::SetPlayerStateCallback(std::function<void(const PlayerStateData&)> cb) {
    playerStateCb_ = std::move(cb);
}

void GameLogic::SetBroadcastPlayerPositionCallback(std::function<void(const PlayerPositionData&, float)> cb) {
    broadcastPlayerPositionCb_ = std::move(cb);
}

void GameLogic::SetSendPlayerUpdateCallback(std::function<void(uint64_t, const PlayerUpdateData&)> cb) {
    sendPlayerUpdateCb_ = std::move(cb);
}

void GameLogic::SetSendPlayersUpdateCallback(std::function<void(uint64_t, const PlayerUpdateData&)> cb) {
    sendPlayersUpdateCb_ = std::move(cb);
}

void GameLogic::SetSendNPCInteractionResponseCallback(std::function<void(uint64_t, const NpcData&)> cb) {
    sendNPCInteractionResponseCb_ = std::move(cb);
}

void GameLogic::SetSendFamiliarCommandResponseCallback(std::function<void(uint64_t, const FamiliarData&)> cb) {
    sendFamiliarCommandResponseCb_ = std::move(cb);
}

void GameLogic::SetSendEntitySpawnResponseCallback(std::function<void(uint64_t, const EntitySpawnData&)> cb) {
    sendEntitySpawnResponseCb_ = std::move(cb);
}

void GameLogic::SetSendLootPickupResponseCallback(std::function<void(uint64_t, const LootPickupData&)> cb) {
    sendLootPickupResponseCb_ = std::move(cb);
}

void GameLogic::SetSendInventoryResponseCallback(std::function<void(uint64_t, const InventoryData&)> cb) {
    sendInventoryResponseCb_ = std::move(cb);
}

void GameLogic::OnAuthentication(const AuthenticationData& data) {
    auto& pm = PlayerManager::GetInstance();
    bool authenticated = false;
    std::string message;
    uint64_t player_id = 0;
    if (pm.PlayerExists(data.username)) {
        if (pm.AuthenticatePlayer(data.username, data.password)) {
            authenticated = true;
            message = "Welcome back, " + data.username;
            player_id = pm.GetPlayerByUsername(data.username)->GetId();
        } else {
            message = "Invalid password";
        }
    } else {
        auto player = pm.CreatePlayer(data.username, data.password);
        if (player) {
            authenticated = true;
            player_id = player->GetId();
            message = "Account created successfully. Welcome, " + data.username;
            if (data.password.empty()) {
                message += " (Warning: no password set)";
            }
        } else {
            message = "Failed to create player account";
        }
    }
    if (authenticated) {
        pm.PlayerConnected(data.session_id, player_id);
        auto session = connectionManager_->GetSession(data.session_id);
        if (session) {
            session->SetPlayerId(player_id);
            session->Authenticate(data.password);
        }
    }
    if (sendAuthResponseCb_) {
        sendAuthResponseCb_(data.session_id, message, player_id);
    } else {
        Logger::Error("No sendAuthResponseCb_ set in GameLogic");
    }
}

void GameLogic::OnChunkRequest(const ChunkData& req) {
    auto chunk = GetOrCreateChunk(req.x, req.z);
    if (!chunk) {
        Logger::Error("Failed to get chunk ({},{}) for session {}", req.x, req.z, req.session_id);
        return;
    }
    ChunkData resp;
    resp.x = req.x;
    resp.z = req.z;
    resp.lod = req.lod;
    resp.size = WorldChunk::CHUNK_SIZE;
    resp.spacing = WorldChunk::DEFAULT_SPACING;
    resp.timestamp = GetCurrentTimestamp();
    const auto& verts = chunk->GetVertices();
    resp.vertices.reserve(verts.size() * 6);
    for (const auto& v : verts) {
        resp.vertices.push_back(v.position.x);
        resp.vertices.push_back(v.position.y);
        resp.vertices.push_back(v.position.z);
        resp.vertices.push_back(v.normal.x);
        resp.vertices.push_back(v.normal.y);
        resp.vertices.push_back(v.normal.z);
    }
    const auto& tris = chunk->GetTriangles();
    resp.indices.reserve(tris.size() * 3);
    for (const auto& tri : tris) {
        resp.indices.push_back(tri.v0);
        resp.indices.push_back(tri.v1);
        resp.indices.push_back(tri.v2);
    }
    if (sendChunkCb_) {
        sendChunkCb_(req.session_id, resp);
    } else {
        Logger::Error("No sendChunkCb_ set in GameLogic");
    }
}

void GameLogic::OnPlayerState(const PlayerStateData& data) {
    uint64_t player_id = data.player_id;
    auto player = GetPlayer(player_id);
    if (!player) return;
    ClientInput input;
    input.input_id = data.input_id;
    input.position = data.position;
    input.velocity = data.velocity;
    input.rotation = data.rotation;
    input.on_ground = data.on_ground;
    input.jumping = data.jumping;
    input.crouching = data.crouching;
    input.sprinting = data.sprinting;
    input.timestamp = data.timestamp;
    {
        std::lock_guard<std::mutex> lock(predictionMutex_);
        playerPrediction_[player_id].StoreClientInput(input);
    }
    PredictionSystem* pred = &playerPrediction_[player_id];
    auto currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    ServerState authState;
    authState.last_processed_input = 0;
    authState.timestamp = currentTime;
    authState.position = player->GetPosition();
    authState.velocity = player->GetVelocity();
    authState.rotation = player->GetRotation();
    authState.on_ground = player->IsOnGround();
    auto unprocessed = pred->GetUnprocessedInputs(authState.last_processed_input);
    if (!unprocessed.empty()) {
        float deltaTime = 1.0f / 30.0f;
        authState = pred->SimulateMovement(authState, unprocessed, deltaTime);
    }
    player->SetPosition(authState.position);
    player->SetVelocity(authState.velocity);
    player->SetRotation(authState.rotation);
    player->SetOnGround(authState.on_ground);
    if (playerStateCb_) {
        PlayerStateData correctedState = data;
        correctedState.position = authState.position;
        correctedState.velocity = authState.velocity;
        correctedState.rotation = authState.rotation;
        correctedState.on_ground = authState.on_ground;
        correctedState.timestamp = currentTime;
        playerStateCb_(correctedState);
    } else {
        Logger::Error("No playerStateCb_ set in GameLogic");
    }

    // ATTENTION: useless and duplicate piece of code needs to be removed
    // Send self player update for rendering
    // PlayerUpdateData update;
    // update.session_id = data.session_id;
    // update.player_id = data.player_id;
    // update.position = authState.position;
    // update.yaw = authState.rotation.y;
    // update.health = player->GetHealth();
    // update.max_health = player->GetMaxHealth();
    // update.name = player->GetName();
    // OnPlayerUpdate(update);

    // correction
    static float correctionThreshold = 0.5f;
    if (glm::distance(authState.position, player->GetPosition()) > correctionThreshold) {
        SendPositionCorrection(data.session_id, authState.position, authState.velocity);
    }
}

void GameLogic::OnPlayerPosition(const PlayerPositionData& data) {
    auto player = GetPlayer(data.player_id);
    if (!player) return;
    float collisionRadius = 0.5f;
    CollisionResult collision = CheckCollision(data.position, collisionRadius, data.player_id);
    glm::vec3 finalPos = data.position;
    if (collision.collided) {
        finalPos += collision.resolution;
    }
    player->SetPosition(finalPos);
    GenerateWorldAroundPlayer(data.player_id, finalPos);
    if (broadcastPlayerPositionCb_) {
        PlayerPositionData broadcastData = data;
        broadcastData.position = finalPos;
        broadcastPlayerPositionCb_(broadcastData, 100.0f);
    }

    // ATTENTION: useless and duplicate piece of code needs to be removed
    // PlayerUpdateData update;
    // update.session_id = data.session_id;
    // update.player_id = data.player_id;
    // update.position = finalPos;
    // update.yaw = player->GetRotation().y;
    // update.health = player->GetHealth();
    // update.max_health = player->GetMaxHealth();
    // update.name = player->GetName();
    // OnPlayerUpdate(update);

    FirePythonEvent("player_move_3d", {
        {"player_id", data.player_id},
        {"x", finalPos.x}, {"y", finalPos.y}, {"z", finalPos.z},
        {"session_id", data.session_id}
    });
}

void GameLogic::OnPlayerUpdate(const PlayerUpdateData& data) {
    auto player = GetPlayer(data.player_id);
    if (!player) return;
    if (sendPlayerUpdateCb_) {
        PlayerUpdateData update;
        update.timestamp = GetCurrentTimestamp();
        update.session_id = data.session_id;
        update.player_id = data.player_id;
        update.position = player->GetPosition();
        update.yaw = player->GetRotation().y;
        update.health = player->GetHealth();
        update.max_health = player->GetMaxHealth();
        update.name = player->GetName();
        sendPlayerUpdateCb_(data.session_id, update);
    }
}

void GameLogic::OnPlayersUpdate(const PlayerUpdateData& data) {
    auto player = GetPlayer(data.player_id);
    if (!player) return;
    if (sendPlayersUpdateCb_) {
        PlayerUpdateData update;
        update.timestamp = GetCurrentTimestamp();
        update.session_id = data.session_id;
        update.player_id = data.player_id;
        update.position = player->GetPosition();
        update.yaw = player->GetRotation().y;
        update.health = player->GetHealth();
        update.max_health = player->GetMaxHealth();
        update.name = player->GetName();
        sendPlayersUpdateCb_(data.session_id, update);
    }
}

void GameLogic::OnNPCInteraction(const NpcData& data) {
    NPCEntity* npc = GetNPCEntity(data.npc_id);
    if (!npc) {
        NpcData error;
        error.type = "error";
        error.session_id = data.session_id;
        if (sendNPCInteractionResponseCb_) sendNPCInteractionResponseCb_(data.session_id, error);
        return;
    }
    uint64_t player_id = GetPlayerIdBySession(data.session_id);
    std::shared_ptr<Player> player = GetPlayer(player_id);
    if (!player) {
        NpcData error;
        error.type = "error";
        error.session_id = data.session_id;
        if (sendNPCInteractionResponseCb_) sendNPCInteractionResponseCb_(data.session_id, error);
        return;
    }
    float distance = glm::distance(player->GetPosition(), npc->GetPosition());
    if (distance > 15.0f) {
        NpcData error;
        error.type = "error";
        error.session_id = data.session_id;
        if (sendNPCInteractionResponseCb_) sendNPCInteractionResponseCb_(data.session_id, error);
        return;
    }
    NpcData response;
    response.session_id = data.session_id;
    response.npc_id = data.npc_id;
    response.player_id = player_id;
    response.timestamp = GetCurrentTimestamp();
    if (data.type == "attack") {
        float damage = 10.0f;
        npc->TakeDamage(damage, player_id);
        response.type = "combat";
        response.damage = damage;
        response.health = npc->GetStats().health;
        response.is_dead = npc->IsDead();
        if (response.is_dead) MobSystem::GetInstance().OnMobDeath(data.npc_id, player_id);
    } else if (data.type == "talk") {
        auto& questMgr = QuestManager::GetInstance();
        response.type = "dialogue";
        response.quests = questMgr.GetQuestsFromNPC(player_id, npc->GetId());
    } else {
        response.type = "error";
    }
    if (sendNPCInteractionResponseCb_) sendNPCInteractionResponseCb_(data.session_id, response);
}

void GameLogic::OnFamiliarCommand(const FamiliarData& data) {
    NPCEntity* familiar = GetNPCEntity(data.familiar_id);
    if (!familiar) {
        FamiliarData error;
        error.session_id = data.session_id;
        if (sendFamiliarCommandResponseCb_) sendFamiliarCommandResponseCb_(data.session_id, error);
        return;
    }
    uint64_t player_id = GetPlayerIdBySession(data.session_id);
    if (familiar->GetOwnerId() != player_id) {
        FamiliarData error;
        error.session_id = data.session_id;
        if (sendFamiliarCommandResponseCb_) sendFamiliarCommandResponseCb_(data.session_id, error);
        return;
    }
    if (data.command == "follow") {
        familiar->SetBehaviorState(NPCAIState::FOLLOW);
        familiar->SetTarget(player_id);
    } else if (data.command == "attack") {
        familiar->SetBehaviorState(NPCAIState::CHASE);
        familiar->SetTarget(data.target_id);
    } else if (data.command == "stay") {
        familiar->SetBehaviorState(NPCAIState::IDLE);
        familiar->SetTarget(0);
    } else {
        FamiliarData error;
        error.session_id = data.session_id;
        if (sendFamiliarCommandResponseCb_) sendFamiliarCommandResponseCb_(data.session_id, error);
        return;
    }
    FamiliarData response;
    response.session_id = data.session_id;
    response.familiar_id = data.familiar_id;
    response.target_id = data.target_id;
    response.command = data.command;
    response.timestamp = GetCurrentTimestamp();
    if (sendFamiliarCommandResponseCb_) sendFamiliarCommandResponseCb_(data.session_id, response);
}

void GameLogic::OnEntitySpawnRequest(const EntitySpawnData& data) {
    EntitySpawnData response = data;
    response.timestamp = GetCurrentTimestamp();
    if (sendEntitySpawnResponseCb_) {
        sendEntitySpawnResponseCb_(data.session_id, response);
    }
}

void GameLogic::OnLootPickup(const LootPickupData& data) {
    LootPickupData response = data;
    response.timestamp = GetCurrentTimestamp();
    uint64_t player_id = GetPlayerIdBySession(data.session_id);
    GameEntity* lootEntity = GetEntity(data.loot_id);
    if (!lootEntity || lootEntity->GetType() != EntityType::ITEM) {
        if (sendLootPickupResponseCb_) sendLootPickupResponseCb_(data.session_id, response);
        return;
    }
    std::shared_ptr<Player> player = GetPlayer(player_id);
    if (!player) {
        if (sendLootPickupResponseCb_) sendLootPickupResponseCb_(data.session_id, response);
        return;
    }
    float distance = glm::distance(player->GetPosition(), lootEntity->GetPosition());
    if (distance > 5.0f) {
        if (sendLootPickupResponseCb_) sendLootPickupResponseCb_(data.session_id, response);
        return;
    }
    auto& inv = InventorySystem::GetInstance();
    if (inv.AddItem(player_id, LootItem(data.loot_id, lootEntity->GetName()), data.quantity)) {
        EntityManager::GetInstance().DestroyEntity(data.loot_id);
        FirePythonEvent("loot_pickup", {
            {"player_id", player_id},
            {"itemId", data.loot_id},
            {"quantity", data.quantity}
        });
    }
    if (sendLootPickupResponseCb_) sendLootPickupResponseCb_(data.session_id, response);
}

void GameLogic::OnInventory(const InventoryData& data) {
    InventoryData response = data;
    response.timestamp = GetCurrentTimestamp();
    uint64_t player_id = GetPlayerIdBySession(data.session_id);
    auto& inv = InventorySystem::GetInstance();
    switch (data.move_type) {
        case InventoryMoveType::REMOVE: {
            if (data.inv_slot_id >= 0) {
                auto item = inv.GetItem(player_id, data.inv_slot_id);
                if (item && inv.RemoveItem(player_id, item->GetId(), data.quantity)) {
                    Logger::Trace("Player {} Inventory RemoveItem {}({}) count {}",
                                  player_id, data.inv_slot_id, item->GetId(), data.quantity);
                }
            }
            break;
        }
        case InventoryMoveType::USE: {
            if (data.inv_slot_id >= 0) {
                auto item = inv.GetItem(player_id, data.inv_slot_id);
                if (item) {
                    if (item->IsEquippable()) {
                        inv.EquipItem(player_id, data.inv_slot_id, data.quantity);
                    } else if (item->IsConsumable()) {
                        inv.UseItem(player_id, item->GetId(), data.quantity);
                    }
                }
            }
            break;
        }
        case InventoryMoveType::TRADE: {
            if (data.inv_slot_id >= 0 && data.target_id != 0) {
                auto item = inv.GetItem(player_id, data.inv_slot_id);
                if (item && inv.CanTradeItem(player_id, item->GetId())) {
                    inv.TransferItem(player_id, data.target_id, item->GetId(), data.quantity);
                }
            }
            break;
        }
        default:
            break;
    }
    if (sendInventoryResponseCb_) {
        sendInventoryResponseCb_(data.session_id, response);
    } else {
        SendError(data.session_id, "No inventory callback set", 500);
    }
}

void GameLogic::SetDatabaseService(DatabaseService* dbService) {
    dbService_ = dbService;
}

void GameLogic::SetConnectionManager(std::shared_ptr<ConnectionManager> connMgr) {
    connectionManager_ = std::move(connMgr);
    Logger::Trace("ConnectionManager set for GameLogic");
}

void GameLogic::SetDatabaseBackend(std::unique_ptr<DatabaseBackend> backend) {
    databaseBackend_ = std::move(backend);
    Logger::Trace("Database backend set for GameLogic");
}

DatabaseBackend* GameLogic::GetDatabaseBackend() const {
    return databaseBackend_.get();
}

void GameLogic::FirePythonEvent(const std::string& eventName, const nlohmann::json& data) {
    if (pythonEnabled_) {
        PythonScripting::GetInstance().FireEvent(eventName, data);
    }
}

nlohmann::json GameLogic::CallPythonFunction(const std::string& moduleName,
                                             const std::string& functionName,
                                             const nlohmann::json& args) {
    if (!pythonEnabled_) return nlohmann::json();
    return PythonScripting::GetInstance().CallFunctionWithResult(moduleName, functionName, args);
}

void GameLogic::RegisterPythonEventHandlers() {
    if (!pythonEnabled_) return;
    auto& scripting = PythonScripting::GetInstance();
    scripting.RegisterEventHandler("player_login", "game_events", "on_player_login");
    scripting.RegisterEventHandler("player_move", "game_events", "on_player_move");
    scripting.RegisterEventHandler("player_attack", "game_events", "on_player_attack");
    scripting.RegisterEventHandler("player_level_up", "game_events", "on_player_level_up");
    scripting.RegisterEventHandler("player_death", "game_events", "on_player_death");
    scripting.RegisterEventHandler("player_respawn", "game_events", "on_player_respawn");
    scripting.RegisterEventHandler("custom_event", "game_events", "on_custom_event");
    Logger::Info("Python event handlers registered");
}

void GameLogic::SaveGameState() {
    try {
        nlohmann::json gameState = {
            {"server_time", GetCurrentTimestamp()},
            {"world_seed", GetWorldConfig().seed},
            {"active_chunks", LogicWorld::GetInstance().GetActiveChunkCount()},
            {"active_npcs", 0},
            {"world_config", {
                {"view_distance", GetWorldConfig().viewDistance},
                {"chunk_size", GetWorldConfig().chunkSize},
                {"terrain_scale", GetWorldConfig().terrainScale}
            }}
        };
        if (!DbManager::GetInstance().SaveGameState("current_game", gameState)) {
            Logger::Error("Failed to save game state: DbManager returned false");
        } else {
            Logger::Debug("Game state saved");
        }
    } catch (const std::exception& e) {
        Logger::Error("Failed to save game state: {}", e.what());
    }
}

void GameLogic::CleanupOldData() {
}

void GameLogic::ProcessGameTick(float deltaTime) {
    auto& world = LogicWorld::GetInstance();
    world.UpdateEntities(deltaTime);
    ProcessEvents();
    FirePythonEvent("game_tick", {{"delta_time", deltaTime}});
}

void GameLogic::SpawnEnemies() {
}

void GameLogic::RespawnNPCs() {
}

void GameLogic::SpawnResources() {
}

void GameLogic::SaveLoop() {
    Logger::Info("Save loop started");
    while (running_) {
        std::unique_lock<std::mutex> lock(saveMutex_);
        saveCV_.wait_for(lock, std::chrono::minutes(5), [this] { return !running_; });
        if (!running_) break;
        PlayerManager::GetInstance().SaveAllPlayers();
        SaveGameState();
        CleanupOldData();
    }
    Logger::Info("Save loop stopped");
}

void GameLogic::HandleLogin(uint64_t session_id, const nlohmann::json& data) {
    if (!data.contains("username") || !data["username"].is_string() ||
        !data.contains("password") || !data["password"].is_string()) {
        SendError(session_id, "Missing or invalid username/password", 400);
        return;
    }
    std::string username = data["username"];
    std::string password = data["password"];
    auto& pm = PlayerManager::GetInstance();
    bool authenticated = pm.AuthenticatePlayer(username, password);
    if (!authenticated) {
        SendError(session_id, "Invalid credentials", 401);
        return;
    }
    auto player = pm.GetPlayerByUsername(username);
    if (!player) {
        SendError(session_id, "Player data unavailable", 500);
        return;
    }
    uint64_t player_id = player->GetId();
    OnPlayerConnected(session_id, player_id);
    auto& world = LogicWorld::GetInstance();
    world.AddEntity(player);
    FirePythonEvent("player_login", {{"player_id", player_id}, {"username", username}});
    SendSuccess(session_id, "Login successful", nlohmann::json{
        {"player_id", player_id},
        {"position", player->JsonGetPosition()},
        {"health", player->GetHealth()},
        {"max_health", player->GetMaxHealth()},
        {"level", player->GetLevel()},
        {"experience", player->GetExperience()},
        {"inventory", player->JsonGetInventory()}
    });
}

void GameLogic::HandleChat(uint64_t session_id, const nlohmann::json& data) {
    uint64_t player_id = GetPlayerIdBySession(session_id);
    if (player_id == 0) {
        SendError(session_id, "Player not authenticated", 401);
        return;
    }
    if (!data.contains("message") || !data["message"].is_string()) {
        SendError(session_id, "Missing or invalid message", 400);
        return;
    }
    std::string chatMessage = data["message"];
    if (chatMessage.empty()) {
        SendError(session_id, "Message cannot be empty", 400);
        return;
    }
    auto player = PlayerManager::GetInstance().GetPlayer(player_id);
    if (!player) {
        SendError(session_id, "Player not found", 500);
        return;
    }
    Logger::Info("Chat from player {} ({}): {}", player_id, player->GetName(), chatMessage);
    nlohmann::json chatJson = {
        {"type", "chat"},
        {"player_id", player_id},
        {"name", player->GetName()},
        {"message", chatMessage},
        {"timestamp", GetCurrentTimestamp()}
    };
    std::string channel = data.value("channel", "local");
    if (channel == "global") {
        auto& connMgr = ConnectionManager::GetInstance();
        auto sessions = connMgr.GetAllSessions();
        for (auto& session : sessions) {
            if (session && session->IsConnected())
                session->SendJson(chatJson);
        }
    } else {
        PlayerManager::GetInstance().BroadcastToNearbyPlayers(player_id, chatJson);
    }
    FirePythonEvent("player_chat", {{"player_id", player_id}, {"message", chatMessage}, {"channel", channel}});
}

void GameLogic::HandleCombat(uint64_t session_id, const nlohmann::json& data) {
    uint64_t player_id = GetPlayerIdBySession(session_id);
    if (player_id == 0) {
        SendError(session_id, "Player not authenticated", 401);
        return;
    }
    if (!data.contains("target_id") || !data["target_id"].is_number_integer()) {
        SendError(session_id, "Missing or invalid target_id", 400);
        return;
    }
    uint64_t target_id = data["target_id"];
    std::string attack_type = data.value("attack_type", "melee");
    auto player = PlayerManager::GetInstance().GetPlayer(player_id);
    if (!player) {
        SendError(session_id, "Player not found", 500);
        return;
    }
    auto& world = LogicWorld::GetInstance();
    auto target = world.GetEntity(target_id);
    if (!target) {
        SendError(session_id, "Target not found", 404);
        return;
    }
    float distance = glm::distance(player->GetPosition(), target->GetPosition());
    if (distance > player->GetAttackRange()) {
        SendError(session_id, "Target out of range", 400);
        return;
    }
    int damage = player->CalculateDamage(attack_type);
    target->TakeDamage(damage, player_id);
    FirePythonEvent("player_attack", {{"player_id", player_id}, {"target_id", target_id}, {"damage", damage}, {"attack_type", attack_type}});
    SendSuccess(session_id, "Combat resolved", nlohmann::json{
        {"damage", damage},
        {"target_health", target->GetHealth()},
        {"target_defeated", target->IsDead()}
    });
}

void GameLogic::HandleQuest(uint64_t session_id, const nlohmann::json& data) {
    uint64_t player_id = GetPlayerIdBySession(session_id);
    if (player_id == 0) {
        SendError(session_id, "Player not authenticated", 401);
        return;
    }
    if (!data.contains("quest_id") || !data["quest_id"].is_number_integer()) {
        SendError(session_id, "Missing or invalid quest_id", 400);
        return;
    }
    uint64_t quest_id = data["quest_id"];
    if (!data.contains("action") || !data["action"].is_string()) {
        SendError(session_id, "Missing or invalid action", 400);
        return;
    }
    std::string action = data["action"];
    auto player = PlayerManager::GetInstance().GetPlayer(player_id);
    if (!player) {
        SendError(session_id, "Player not found", 500);
        return;
    }
    auto& questManager = QuestManager::GetInstance();
    nlohmann::json result;
    if (action == "start") {
        if (questManager.CanStartQuest(player_id, quest_id)) {
            questManager.StartQuest(player_id, quest_id);
            result["status"] = "started";
        } else {
            SendError(session_id, "Cannot start quest", 400);
            return;
        }
    } else if (action == "update") {
        std::string objective = data.value("objective", "");
        int progress = data.value("progress", 1);
        questManager.UpdateObjective(player_id, quest_id, objective, progress);
        result["status"] = "updated";
    } else if (action == "complete") {
        if (questManager.CanCompleteQuest(player_id, quest_id)) {
            auto rewards = questManager.CompleteQuest(player_id, quest_id);
            result["status"] = "completed";
            result["rewards"] = rewards;
        } else {
            SendError(session_id, "Quest cannot be completed yet", 400);
            return;
        }
    } else {
        SendError(session_id, "Unknown action: " + action, 400);
        return;
    }
    FirePythonEvent("player_quest", {{"player_id", player_id}, {"quest_id", quest_id}, {"action", action}});
    SendSuccess(session_id, "Quest action processed", result);
}

void GameLogic::GameLoop() {
    Logger::Info("Game loop started");
    auto lastUpdate = std::chrono::steady_clock::now();
    while (!instanceMutex_.try_lock()) {
        try {
            auto startTime = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            auto deltaTimeMillis = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate);
            float deltaTime = deltaTimeMillis.count() / 1000.0f;
            lastUpdate = now;
            UpdateWorld(deltaTime);
            auto dirtyPlayers = PlayerManager::GetInstance().GetDirtyPlayersAndClear();
            for (uint64_t playerId : dirtyPlayers) {
                auto player = GetPlayer(playerId);
                if (!player || !player->IsOnline()) continue;
                PlayerUpdateData update;
                update.timestamp = GetCurrentTimestamp();
                update.session_id = player->GetSessionId();
                update.player_id = playerId;
                update.position = player->GetPosition();
                update.yaw = player->GetRotation().y;
                update.health = player->GetHealth();
                update.max_health = player->GetMaxHealth();
                update.name = player->GetUsername();
                if (sendPlayerUpdateCb_) {// Send to self for correction (PLAYER_UPDATE)
                    sendPlayerUpdateCb_(player->GetSessionId(), update);
                }
                if (sendPlayersUpdateCb_) {// Send to others (PLAYERS_UPDATE)
                    sendPlayersUpdateCb_(player->GetSessionId(), update);
                }
            }
            LogicEntity::GetInstance().UpdateNPCs(deltaTime);
            LogicEntity::GetInstance().UpdateCollisions(deltaTime);
            ProcessGameTick(deltaTime);
            ProcessEvents();
            auto endTime = std::chrono::steady_clock::now();
            auto processingTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
            if (processingTime < gameLoopInterval_.count()) {
                std::unique_lock<std::mutex> lock(gameLoopMutex_);
                gameLoopCV_.wait_for(lock,
                    gameLoopInterval_ - std::chrono::milliseconds(processingTime),
                    [this] { return !instance_; });
            } else {
                Logger::Warn("Game loop lagging: {}ms", processingTime);
            }
        } catch (const std::exception& e) {
            Logger::Error("Error in game loop: {}", e.what());
        }
    }
    instanceMutex_.unlock();
    Logger::Info("Game loop stopped");
}

void GameLogic::SpawnerLoop() {
    Logger::Info("Spawner loop started");
    LogicCore::SpawnerLoop();
    Logger::Info("Spawner loop stopped");
}

void GameLogic::UpdateWorld(float deltaTime) {
    static float worldTime = 0.0f;
    worldTime += deltaTime;
    const float dayLength = 1800.0f;
    float timeOfDay = fmod(worldTime, dayLength) / dayLength;
    LogicWorld::GetInstance().SetTimeOfDay(timeOfDay);
    static float timeSinceLastUnload = 0.0f;
    timeSinceLastUnload += deltaTime;
    const float unloadInterval = 1.0f;
    if (timeSinceLastUnload >= unloadInterval) {
        timeSinceLastUnload = 0.0f;
        std::vector<glm::vec3> playerPositions;
        {
            std::lock_guard<std::mutex> lock(sessionMutex_);
            for (const auto& [player_id, session_id] : playerToSessionMap_) {
                if (auto player = GetPlayer(player_id)) {
                    playerPositions.push_back(player->GetPosition());
                }
            }
        }
        float unloadDistance = GetWorldConfig().unloadDistance;
        for (const auto& pos : playerPositions) {
            LogicWorld::GetInstance().UnloadDistantChunks(pos, unloadDistance);
        }
    }
    Logger::Trace("UpdateWorld processed with deltaTime = {} ms", deltaTime * 1000.0f);
}
