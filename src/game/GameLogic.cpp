#include "game/GameLogic.hpp"

// =============== Static Members ===============
std::mutex GameLogic::instanceMutex_;
GameLogic* GameLogic::instance_ = nullptr;

// =============== Singleton Access ===============
GameLogic& GameLogic::GetInstance() {
    std::lock_guard<std::mutex> lock(instanceMutex_);
    if (!instance_) {
        instance_ = new GameLogic();
    }
    return *instance_;
}

// =============== Constructor and Destructor ===============
GameLogic::GameLogic()
//    : playerManager_(PlayerManager::GetInstance())
{
    Logger::Debug("GameLogic created");
}

GameLogic::~GameLogic() {
    if (instance_) {
        Shutdown();
    }
}

// =============== Initialization and Shutdown ===============
void GameLogic::Initialize() {
    if (instance_) {
        Logger::Warn("GameLogic already initialized");
        return;
    }

    Logger::Info("Initializing GameLogic with world system...");

    auto& config = ConfigManager::GetInstance();

    // Initialize world configuration
    WorldConfig worldConfig;
    worldConfig.seed = config.GetInt("world.seed", 12345);
    worldConfig.viewDistance = config.GetInt("world.view_distance", 4);
    worldConfig.chunkSize = config.GetFloat("world.chunk_size", 32.0f);
    worldConfig.maxActiveChunks = config.GetInt("world.max_active_chunks", 100);
    worldConfig.terrainScale = config.GetFloat("world.terrain_scale", 100.0f);
    worldConfig.maxTerrainHeight = config.GetFloat("world.max_terrain_height", 50.0f);
    worldConfig.waterLevel = config.GetFloat("world.water_level", 10.0f);
    worldConfig.chunkUnloadDistance = config.GetFloat("world.chunk_unload_distance", 200.0f);

    SetWorldConfig(worldConfig);

    // Initialize component systems
    worldLogic_.Initialize(worldConfig);
    entityLogic_.Initialize();

    // Load game data
    if (!LoadGameData()) {
        Logger::Error("Failed to load game data");
    }

    // Register handlers
    RegisterWorldHandlers();

    Logger::Info("GameLogic world system initialized successfully");
}

void GameLogic::Shutdown() {
    if (!instance_) {
        return;
    }

    Logger::Info("Shutting down GameLogic world system...");

    // Shutdown component systems
    entityLogic_.Shutdown();
    worldLogic_.Shutdown();

    Logger::Info("GameLogic world system shutdown complete");
}

// =============== World Configuration ===============
void GameLogic::SetWorldConfig(const WorldConfig& config) {
    worldLogic_.SetConfig(config);
}

const GameLogic::WorldConfig& GameLogic::GetWorldConfig() const {
    return static_cast<const WorldConfig&>(worldLogic_.GetConfig());
}

// =============== World Methods ===============
std::shared_ptr<WorldChunk> GameLogic::GetOrCreateChunk(int chunkX, int chunkZ) {
    return worldLogic_.GetOrCreateChunk(chunkX, chunkZ);
}

void GameLogic::GenerateWorldAroundPlayer(uint64_t playerId, const glm::vec3& position) {
    worldLogic_.GenerateWorldAroundPlayer(position, GetWorldConfig().viewDistance);
    
    // Sync entities to player
    SyncNearbyEntitiesToPlayer(GetSessionIdByPlayer(playerId), position);
}

void GameLogic::PreloadWorldData(float radius) {
    worldLogic_.PreloadWorldData(radius);
}

float GameLogic::GetTerrainHeight(float x, float z) const {
    return worldLogic_.GetTerrainHeight(x, z);
}

BiomeType GameLogic::GetBiomeAt(float x, float z) const {
    return worldLogic_.GetBiomeAt(x, z);
}

// =============== Entity Methods ===============
uint64_t GameLogic::SpawnNPC(NPCType type, const glm::vec3& position, uint64_t ownerId) {
    return entityLogic_.SpawnNPC(type, position, ownerId);
}

void GameLogic::DespawnNPC(uint64_t npcId) {
    entityLogic_.DespawnNPC(npcId);
}

NPCEntity* GameLogic::GetNPCEntity(uint64_t npcId) {
    return entityLogic_.GetNPCEntity(npcId);
}

GameEntity* GameLogic::GetEntity(uint64_t entityId) {
    return entityLogic_.GetEntity(entityId);
}

PlayerEntity* GameLogic::GetPlayerEntity(uint64_t playerId) {
    return entityLogic_.GetPlayerEntity(playerId);
}

// =============== Collision Methods ===============
CollisionResult GameLogic::CheckCollision(const glm::vec3& position, float radius, uint64_t excludeEntityId) {
    return entityLogic_.CheckCollision(position, radius, excludeEntityId);
}

bool GameLogic::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit) {
    return entityLogic_.Raycast(origin, direction, maxDistance, hit);
}

// =============== Loot Methods ===============
void GameLogic::CreateLootEntity(const glm::vec3& position, std::shared_ptr<LootItem> item, int quantity) {
    entityLogic_.CreateLootEntity(position, item, quantity);
}

void GameLogic::HandleLootPickup(uint64_t sessionId, const nlohmann::json& data) {
    try {
        uint64_t playerId = GetPlayerIdBySession(sessionId);
        uint64_t lootEntityId = data.value("lootEntityId", 0ULL);

        if (lootEntityId == 0) {
            SendError(sessionId, "Invalid loot entity ID");
            return;
        }

        GameEntity* lootEntity = GetEntity(lootEntityId);
        if (!lootEntity || lootEntity->GetType() != EntityType::ITEM) {
            SendError(sessionId, "Invalid loot entity");
            return;
        }

        PlayerEntity* player = GetPlayerEntity(playerId);
        if (!player) {
            SendError(sessionId, "Player not found");
            return;
        }

        float distance = glm::distance(player->GetPosition(), lootEntity->GetPosition());
        if (distance > 5.0f) {
            SendError(sessionId, "Too far to loot");
            return;
        }

        // TODO: Implement actual loot pickup logic
        SendSuccess(sessionId, "Loot collected (stub)");

        FirePythonEvent("loot_pickup", {
            {"playerId", playerId},
            {"itemId", "dummy_item"},
            {"quantity", 1}
        });

    } catch (const std::exception& e) {
        Logger::Error("Error in HandleLootPickup: {}", e.what());
        SendError(sessionId, "Internal server error");
    }
}

void GameLogic::HandleInventoryMove(uint64_t sessionId, const nlohmann::json& data) {
    SendError(sessionId, "Inventory system not implemented yet", 501);
}

void GameLogic::HandleItemUse(uint64_t sessionId, const nlohmann::json& data) {
    SendError(sessionId, "Item use not implemented yet", 501);
}

void GameLogic::HandleItemDrop(uint64_t sessionId, const nlohmann::json& data) {
    SendError(sessionId, "Item drop not implemented yet", 501);
}

void GameLogic::HandleTradeRequest(uint64_t sessionId, const nlohmann::json& data) {
    SendError(sessionId, "Trading not implemented yet", 501);
}

void GameLogic::HandleGoldTransaction(uint64_t sessionId, const nlohmann::json& data) {
    SendError(sessionId, "Gold transactions not implemented yet", 501);
}

// =============== World Message Handlers ===============
void GameLogic::RegisterWorldHandlers() {
    RegisterHandler("world_chunk_request", [this](uint64_t sessionId, const nlohmann::json& data) {
        HandleWorldChunkRequest(sessionId, data);
    });

    RegisterHandler("player_position_update", [this](uint64_t sessionId, const nlohmann::json& data) {
        HandlePlayerPositionUpdate(sessionId, data);
    });

    RegisterHandler("npc_interaction", [this](uint64_t sessionId, const nlohmann::json& data) {
        HandleNPCInteraction(sessionId, data);
    });

    RegisterHandler("collision_check", [this](uint64_t sessionId, const nlohmann::json& data) {
        HandleCollisionCheck(sessionId, data);
    });

    RegisterHandler("familiar_command", [this](uint64_t sessionId, const nlohmann::json& data) {
        HandleFamiliarCommand(sessionId, data);
    });

    RegisterHandler("entity_spawn_request", [this](uint64_t sessionId, const nlohmann::json& data) {
        HandleEntitySpawnRequest(sessionId, data);
    });

    // Loot handlers
    RegisterHandler("loot_pickup", [this](uint64_t sessionId, const nlohmann::json& data) {
        HandleLootPickup(sessionId, data);
    });
    RegisterHandler("inventory_move", [this](uint64_t sessionId, const nlohmann::json& data) {
        HandleInventoryMove(sessionId, data);
    });
    RegisterHandler("item_use", [this](uint64_t sessionId, const nlohmann::json& data) {
        HandleItemUse(sessionId, data);
    });
    RegisterHandler("item_drop", [this](uint64_t sessionId, const nlohmann::json& data) {
        HandleItemDrop(sessionId, data);
    });
    RegisterHandler("trade_request", [this](uint64_t sessionId, const nlohmann::json& data) {
        HandleTradeRequest(sessionId, data);
    });
    RegisterHandler("gold_transaction", [this](uint64_t sessionId, const nlohmann::json& data) {
        HandleGoldTransaction(sessionId, data);
    });

    // Binary handlers
    RegisterBinaryHandler(BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION, 
        [this](uint64_t sessionId, uint16_t messageType, const std::vector<uint8_t>& data) {
            BinaryProtocol::BinaryReader reader(data.data(), data.size());
            glm::vec3 position = reader.ReadVector3();
            nlohmann::json jsonData = {
                {"x", position.x},
                {"y", position.y},
                {"z", position.z}
            };
            HandlePlayerPositionUpdate(sessionId, jsonData);
        });

    RegisterBinaryHandler(BinaryProtocol::MESSAGE_TYPE_CHUNK_REQUEST,
        [this](uint64_t sessionId, uint16_t messageType, const std::vector<uint8_t>& data) {
            BinaryProtocol::BinaryReader reader(data.data(), data.size());
            int chunkX = reader.ReadInt32();
            int chunkZ = reader.ReadInt32();
            int lod = reader.ReadUInt8();
            nlohmann::json jsonData = {
                {"chunkX", chunkX},
                {"chunkZ", chunkZ},
                {"lod", lod}
            };
            HandleWorldChunkRequest(sessionId, jsonData);
        });

    Logger::Info("Registered world message handlers");
}

void GameLogic::HandleWorldChunkRequest(uint64_t sessionId, const nlohmann::json& data) {
    try {
        int chunkX = data.value("chunkX", 0);
        int chunkZ = data.value("chunkZ", 0);
        int lod = data.value("lod", 0);

        Logger::Debug("World chunk request: [{}, {}] LOD: {}", chunkX, chunkZ, lod);

        auto chunk = GetOrCreateChunk(chunkX, chunkZ);
        if (!chunk) {
            SendError(sessionId, "Failed to generate chunk", 404);
            return;
        }

        BinaryProtocol::BinaryWriter writer;
        writer.WriteInt32(chunkX);
        writer.WriteInt32(chunkZ);
        writer.WriteUInt8(static_cast<uint8_t>(lod));
        writer.WriteJson(chunk->Serialize());
        writer.WriteUInt64(GetCurrentTimestamp());

        SendBinaryToSession(sessionId, BinaryProtocol::MESSAGE_TYPE_CHUNK_DATA, writer.GetBuffer());

    } catch (const std::exception& e) {
        Logger::Error("Error handling world chunk request: {}", e.what());
        SendError(sessionId, "Failed to process chunk request", 500);
    }
}

void GameLogic::HandlePlayerPositionUpdate(uint64_t sessionId, const nlohmann::json& data) {
    try {
        float x = data.value("x", 0.0f);
        float y = data.value("y", 0.0f);
        float z = data.value("z", 0.0f);
        glm::vec3 position(x, y, z);

        uint64_t playerId = GetPlayerIdBySession(sessionId);
        if (playerId == 0) {
            return;
        }

        PlayerEntity* player = GetPlayerEntity(playerId);
        if (player) {
            float collisionRadius = 0.5f;
            CollisionResult collision = CheckCollision(position, collisionRadius, playerId);

            if (collision.collided) {
                position += collision.resolution;
            }

            player->SetPosition(position);
            GenerateWorldAroundPlayer(playerId, position);

            // Broadcast position
            BinaryProtocol::BinaryWriter positionWriter;
            positionWriter.WriteUInt64(playerId);
            positionWriter.WriteVector3(position);
            positionWriter.WriteVector3(glm::vec3(0, 0, 0));
            positionWriter.WriteUInt64(GetCurrentTimestamp());

            BroadcastBinaryToNearbyPlayers(position, BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION, 
                                          positionWriter.GetBuffer(), 100.0f);

            FirePythonEvent("player_move_3d", {
                {"player_id", playerId},
                {"x", x},
                {"y", y},
                {"z", z},
                {"session_id", sessionId}
            });
        }

    } catch (const std::exception& e) {
        Logger::Error("Error handling player position update: {}", e.what());
    }
}

void GameLogic::HandleNPCInteraction(uint64_t sessionId, const nlohmann::json& data) {
    try {
        uint64_t npcId = data.value("npcId", 0ULL);
        std::string interactionType = data.value("interaction", "");

        if (npcId == 0 || interactionType.empty()) {
            SendError(sessionId, "Invalid NPC interaction", 400);
            return;
        }

        NPCEntity* npc = GetNPCEntity(npcId);
        if (!npc) {
            SendError(sessionId, "NPC not found", 404);
            return;
        }

        uint64_t playerId = GetPlayerIdBySession(sessionId);
        PlayerEntity* player = GetPlayerEntity(playerId);

        if (!player) {
            SendError(sessionId, "Player not found", 404);
            return;
        }

        float distance = glm::distance(player->GetPosition(), npc->GetPosition());
        if (distance > 15.0f) {
            SendError(sessionId, "Too far from NPC", 400);
            return;
        }

        if (interactionType == "attack") {
            float damage = 10.0f;
            npc->TakeDamage(damage, playerId);
            
            bool isDead = npc->IsDead();
            if (isDead) {
                // Handle mob death
            }

            BinaryProtocol::BinaryWriter writer;
            writer.WriteUInt64(playerId);
            writer.WriteUInt64(npcId);
            writer.WriteFloat(damage);
            writer.WriteFloat(npc->GetStats().health);
            writer.WriteUInt8(isDead ? 1 : 0);
            writer.WriteUInt64(GetCurrentTimestamp());

            SendBinaryToSession(sessionId, BinaryProtocol::MESSAGE_TYPE_COMBAT_EVENT, writer.GetBuffer());
        }

    } catch (const std::exception& e) {
        Logger::Error("Error handling NPC interaction: {}", e.what());
        SendError(sessionId, "Failed to process NPC interaction", 500);
    }
}

void GameLogic::HandleFamiliarCommand(uint64_t sessionId, const nlohmann::json& data) {
    try {
        uint64_t familiarId = data.value("familiarId", 0ULL);
        std::string command = data.value("command", "");
        uint64_t targetId = data.value("targetId", 0ULL);

        if (familiarId == 0 || command.empty()) {
            SendError(sessionId, "Invalid familiar command", 400);
            return;
        }

        NPCEntity* familiar = GetNPCEntity(familiarId);
        if (!familiar) {
            SendError(sessionId, "Familiar not found", 404);
            return;
        }

        uint64_t playerId = GetPlayerIdBySession(sessionId);
        if (familiar->GetOwnerId() != playerId) {
            SendError(sessionId, "Not your familiar", 403);
            return;
        }

        if (command == "follow") {
            familiar->SetBehaviorState(NPCBehaviorState::FOLLOW);
            familiar->SetTarget(playerId);
        } else if (command == "attack") {
            familiar->SetBehaviorState(NPCBehaviorState::CHASE);
            familiar->SetTarget(targetId);
        } else if (command == "stay") {
            familiar->SetBehaviorState(NPCBehaviorState::IDLE);
            familiar->SetTarget(0);
        }

        nlohmann::json response = {
            {"type", "familiar_command_response"},
            {"familiarId", familiarId},
            {"command", command},
            {"success", true},
            {"timestamp", GetCurrentTimestamp()}
        };
        SendToSession(sessionId, response);

    } catch (const std::exception& e) {
        Logger::Error("Error handling familiar command: {}", e.what());
        SendError(sessionId, "Failed to process familiar command", 500);
    }
}

void GameLogic::HandleCollisionCheck(uint64_t sessionId, const nlohmann::json& data) {
    try {
        float x = data.value("x", 0.0f);
        float y = data.value("y", 0.0f);
        float z = data.value("z", 0.0f);
        float radius = data.value("radius", 0.5f);

        glm::vec3 position(x, y, z);
        CollisionResult result = CheckCollision(position, radius);

        nlohmann::json response = {
            {"type", "collision_check_response"},
            {"position", {x, y, z}},
            {"collided", result.collided},
            {"collidedWith", result.collidedWith},
            {"penetration", result.penetration},
            {"timestamp", GetCurrentTimestamp()}
        };

        SendToSession(sessionId, response);

    } catch (const std::exception& e) {
        Logger::Error("Error handling collision check: {}", e.what());
        SendError(sessionId, "Failed to check collision", 500);
    }
}

void GameLogic::HandleEntitySpawnRequest(uint64_t sessionId, const nlohmann::json& data) {
    try {
        int entityType = data.value("entityType", 0);
        float x = data.value("x", 0.0f);
        float y = data.value("y", 0.0f);
        float z = data.value("z", 0.0f);
        glm::vec3 position(x, y, z);

        if (entityType >= static_cast<int>(NPCType::WOLF_FAMILIAR) &&
            entityType <= static_cast<int>(NPCType::CAT_FAMILIAR)) {

            uint64_t playerId = GetPlayerIdBySession(sessionId);
            NPCType type = static_cast<NPCType>(entityType);
            uint64_t npcId = SpawnNPC(type, position, playerId);

            if (npcId > 0) {
                nlohmann::json response = {
                    {"type", "entity_spawn_response"},
                    {"entityId", npcId},
                    {"entityType", entityType},
                    {"position", {x, y, z}},
                    {"success", true},
                    {"timestamp", GetCurrentTimestamp()}
                };
                SendToSession(sessionId, response);
            } else {
                SendError(sessionId, "Failed to spawn entity", 500);
            }
        } else {
            SendError(sessionId, "Invalid entity type", 400);
        }

    } catch (const std::exception& e) {
        Logger::Error("Error handling entity spawn request: {}", e.what());
        SendError(sessionId, "Failed to spawn entity", 500);
    }
}

// =============== Broadcasting ===============
void GameLogic::BroadcastBinaryToNearbyPlayers(const glm::vec3& position, uint16_t messageType, 
                                              const std::vector<uint8_t>& data, float radius) {
    // Implementation would query collision system for nearby players
    // Simplified for this refactor
}

void GameLogic::BroadcastToNearbyPlayers(const glm::vec3& position, const nlohmann::json& message, float radius) {
    // Implementation would query collision system for nearby players
    // Simplified for this refactor
}

void GameLogic::SyncNearbyEntitiesToPlayer(uint64_t sessionId, const glm::vec3& position) {
    // Implementation would send batch entity updates
    // Simplified for this refactor
}

// =============== Thread Functions ===============
void GameLogic::GameLoop() {
    Logger::Info("Game loop started");
    
    auto lastUpdate = std::chrono::steady_clock::now();
    
    while (instanceMutex_) {
        try {
            auto startTime = std::chrono::steady_clock::now();
            
            auto now = std::chrono::steady_clock::now();
            auto deltaTimeMillis = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate);
            float deltaTime = deltaTimeMillis.count() / 1000.0f;
            lastUpdate = now;
            
            // Update systems
            UpdateWorld(deltaTime);
            entityLogic_.UpdateNPCs(deltaTime);
            entityLogic_.UpdateCollisions(deltaTime);
            
            // Process game logic
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
    
    Logger::Info("Game loop stopped");
}

void GameLogic::SpawnerLoop() {
    Logger::Info("Spawner loop started");
    LogicCore::SpawnerLoop();
    Logger::Info("Spawner loop stopped");
}

void GameLogic::SaveLoop() {
    Logger::Info("Save loop started");
    LogicCore::SaveLoop();
    SaveChunkData();
    Logger::Info("Save loop stopped");
}

// =============== Game Tick Processing ===============
void GameLogic::ProcessGameTick(float deltaTime) {
    LogicCore::ProcessGameTick(deltaTime);
    // Additional game tick processing
}

void GameLogic::UpdateWorld(float deltaTime) {
    // Unload distant chunks based on player positions
    std::vector<glm::vec3> playerPositions;
    
    std::lock_guard<std::mutex> lock(sessionMutex_);
    for (const auto& [playerId, sessionId] : playerToSessionMap_) {
        PlayerEntity* player = GetPlayerEntity(playerId);
        if (player) {
            playerPositions.push_back(player->GetPosition());
        }
    }
    
    for (const auto& position : playerPositions) {
        worldLogic_.UnloadDistantChunks(position, GetWorldConfig().chunkUnloadDistance);
    }
}

// =============== Data Management ===============
bool GameLogic::LoadGameData() {
    Logger::Debug("Loading game data");
    return true;
}

void GameLogic::SaveGameState() {
    LogicCore::SaveGameState();
    
    try {
        nlohmann::json gameState = {
            {"server_time", GetCurrentTimestamp()},
            {"world_seed", GetWorldConfig().seed},
            {"active_chunks", worldLogic_.GetActiveChunkCount()},
            {"active_npcs", 0}, // entityLogic_.GetActiveNPCCount() if exposed
            {"world_config", {
                {"view_distance", GetWorldConfig().viewDistance},
                {"chunk_size", GetWorldConfig().chunkSize},
                {"terrain_scale", GetWorldConfig().terrainScale}
            }}
        };

        auto& dbClient = CitusClient::GetInstance();
        dbClient.SaveGameState("current_game", gameState);

        Logger::Debug("game state saved");
    } catch (const std::exception& e) {
        Logger::Error("Failed to save game state: {}", e.what());
    }
}

void GameLogic::SaveChunkData() {
    worldLogic_.SaveChunkData();
}

void GameLogic::CleanupOldData() {
    LogicCore::CleanupOldData();
    Logger::Debug("Cleaning up game data");
}

// =============== World maintenance ===============
void GameLogic::PerformMaintenance() {
    Logger::Info("Performing game world maintenance");

    // Clean up old data
    CleanupOldData();

    // Save chunk data
    SaveChunkData();

    // Check database health
    if (databaseBackend_) {
        bool healthy = databaseBackend_->CheckHealth();
        if (!healthy) {
            Logger::Warn("Database health check failed, attempting to reconnect");
            databaseBackend_->ReconnectAll();
        }
    }

    Logger::Info("Game world maintenance complete");
}

// =============== IPC message handling ===============
void GameLogic::HandleIPCMessage(const nlohmann::json& message) {
    try {
        std::string msgType = message.value("type", "");
        Logger::Debug("GameLogic handling IPC message type: {}", msgType);

        if (msgType == "welcome") {
            Logger::Info("Received welcome message from master: {}", message.value("message", ""));
        } else if (msgType == "heartbeat") {
            // Handle heartbeat from master
            int count = message.value("count", 0);
            Logger::Debug("Received heartbeat #{} from master", count);
        } else if (msgType == "broadcast") {
            // Broadcast message to all players
            if (message.contains("data")) {
                BroadcastToAllPlayers(message["data"]);
            }
        } else if (msgType == "shutdown") {
            Logger::Info("Received shutdown command from master");
            Shutdown();
        } else if (msgType == "reload_config") {
            Logger::Info("Received config reload command from master");
            // Reload configuration if needed
        }
    } catch (const std::exception& e) {
        Logger::Error("Error handling IPC message: {}", e.what());
    }
}

// =============== Helper broadcast methods ===============
void GameLogic::BroadcastToAllPlayers(const nlohmann::json& message) {
    if (!connectionManager_) {
        Logger::Warn("Cannot broadcast: ConnectionManager not available");
        return;
    }

    try {
        // Serialize the message to string
        std::string serialized = message.dump();

        // Get all active sessions
        auto sessions = connectionManager_->GetAllSessions();

        if (sessions.empty()) {
            Logger::Debug("No active sessions to broadcast to");
            return;
        }

        Logger::Debug("Broadcasting to {} player(s): {}", sessions.size(), message.dump());

        // Send message to each session
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

void GameLogic::BroadcastToAllPlayersBinary(uint16_t messageType, const std::vector<uint8_t>& data) {
    if (!connectionManager_) {
        Logger::Warn("Cannot broadcast binary: ConnectionManager not available");
        return;
    }

    try {
        // Get all active sessions
        auto sessions = connectionManager_->GetAllSessions();

        if (sessions.empty()) {
            Logger::Debug("No active sessions to broadcast binary to");
            return;
        }

        Logger::Debug("Broadcasting binary message type {} to {} player(s)",
                      messageType, sessions.size());

        // Send binary message to each session
        for (auto& session : sessions) {
            if (session && session->IsConnected()) {
                try {
                    session->SendBinary(messageType, data);
                } catch (const std::exception& e) {
                    Logger::Error("Failed to send binary broadcast to session {}: {}",
                                  session->GetSessionId(), e.what());
                }
            }
        }

    } catch (const std::exception& e) {
        Logger::Error("Error broadcasting binary to all players: {}", e.what());
    }
}

void GameLogic::BroadcastToPlayers(const std::vector<uint64_t>& sessionIds, const nlohmann::json& message) {
    if (!connectionManager_) {
        Logger::Warn("Cannot broadcast: ConnectionManager not available");
        return;
    }

    try {
        std::string serialized = message.dump();
        int sentCount = 0;

        for (uint64_t sessionId : sessionIds) {
            auto session = connectionManager_->GetSession(sessionId);
            if (session && session->IsConnected()) {
                try {
                    session->SendRaw(serialized);
                    sentCount++;
                } catch (const std::exception& e) {
                    Logger::Error("Failed to send message to session {}: {}",
                                  sessionId, e.what());
                }
            }
        }

        if (sentCount > 0) {
            Logger::Debug("Broadcasted to {} specific player(s)", sentCount);
        }

    } catch (const std::exception& e) {
        Logger::Error("Error broadcasting to specific players: {}", e.what());
    }
}
