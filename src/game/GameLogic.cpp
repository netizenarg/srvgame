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
    LogicWorld::GetInstance().Initialize(worldConfig);
    LogicEntity::GetInstance().Initialize();

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
    LogicEntity::GetInstance().Shutdown();
    LogicWorld::GetInstance().Shutdown();

    Logger::Info("GameLogic world system shutdown complete");
}

// =============== World Configuration ===============
void GameLogic::SetWorldConfig(const WorldConfig& config) {
    LogicWorld::GetInstance().SetConfig(config);
}

const GameLogic::WorldConfig& GameLogic::GetWorldConfig() const {
    return static_cast<const WorldConfig&>(LogicWorld::GetInstance().GetConfig());
}

// =============== World Methods ===============
std::shared_ptr<WorldChunk> GameLogic::GetOrCreateChunk(int chunkX, int chunkZ) {
    return LogicWorld::GetInstance().GetOrCreateChunk(chunkX, chunkZ);
}

void GameLogic::GenerateWorldAroundPlayer(uint64_t playerId, const glm::vec3& position) {
    LogicWorld::GetInstance().GenerateWorldAroundPlayer(position, GetWorldConfig().viewDistance);

    // Sync entities to player
    SyncNearbyEntitiesToPlayer(GetSessionIdByPlayer(playerId), position);
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

// =============== Entity Methods ===============
uint64_t GameLogic::SpawnNPC(NPCType type, const glm::vec3& position, uint64_t ownerId) {
    return LogicEntity::GetInstance().SpawnNPC(type, position, ownerId);
}

void GameLogic::DespawnNPC(uint64_t npcId) {
    LogicEntity::GetInstance().DespawnNPC(npcId);
}

NPCEntity* GameLogic::GetNPCEntity(uint64_t npcId) {
    return LogicEntity::GetInstance().GetNPCEntity(npcId);
}

GameEntity* GameLogic::GetEntity(uint64_t entityId) {
    return LogicEntity::GetInstance().GetEntity(entityId);
}

std::shared_ptr<Player> GameLogic::GetPlayer(uint64_t playerId) {
    return PlayerManager::GetInstance().GetPlayer(playerId);
}

// =============== Collision Methods ===============
CollisionResult GameLogic::CheckCollision(const glm::vec3& position, float radius, uint64_t excludeEntityId) {
    return LogicEntity::GetInstance().CheckCollision(position, radius, excludeEntityId);
}

bool GameLogic::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit) {
    return LogicEntity::GetInstance().Raycast(origin, direction, maxDistance, hit);
}

// =============== Loot Methods ===============
void GameLogic::CreateLootEntity(const glm::vec3& position, std::shared_ptr<LootItem> item, int quantity) {
    // If no specific item is given, generate a random one from a default loot table
    if (!item) {
        auto& lootTables = LootTableManager::GetInstance();
        auto loot = lootTables.GenerateLoot("default_loot_table"); // configurable
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

void GameLogic::HandleLootPickup(uint64_t sessionId, const nlohmann::json& data) {
    try {
        uint64_t playerId = GetPlayerIdBySession(sessionId);
        uint64_t lootId = data.value("lootId", 0ULL);
        int quantity = data.value("quantity", 1);
        if (lootId == 0) {
            SendError(sessionId, "Invalid loot entity ID");
            return;
        }
        GameEntity* lootEntity = GetEntity(lootId);
        if (!lootEntity || lootEntity->GetType() != EntityType::ITEM) {
            SendError(sessionId, "Invalid loot entity");
            return;
        }
        std::shared_ptr<Player> player = PlayerManager::GetInstance().GetPlayer(playerId);
        if (!player) {
            SendError(sessionId, "Player not found");
            return;
        }
        float distance = glm::distance(player->GetPosition(), lootEntity->GetPosition());
        if (distance > 5.0f) {
            SendError(sessionId, "Too far to loot");
            return;
        }
        // Add to player's inventory
        auto& inv = InventorySystem::GetInstance();
        if (inv.AddItem(playerId, LootItem(lootId, lootEntity->GetName()), quantity)) {
            // Destroy the loot entity
            EntityManager::GetInstance().DestroyEntity(lootId);
            SendSuccess(sessionId, "Loot collected");
            FirePythonEvent("loot_pickup", {
                {"playerId", playerId},
                {"itemId", lootId},
                {"quantity", quantity}
            });
        } else {
            SendError(sessionId, "Inventory full");
        }

    } catch (const std::exception& e) {
        Logger::Error("Error in HandleLootPickup: {}", e.what());
        SendError(sessionId, "Internal server error");
    }
}

void GameLogic::HandleInventoryMove(uint64_t sessionId, const nlohmann::json& data) {
    try {
        uint64_t playerId = GetPlayerIdBySession(sessionId);
        int fromSlot = data.value("fromSlot", -1);
        int toSlot = data.value("toSlot", -1);

        if (fromSlot < 0 || toSlot < 0) {
            SendError(sessionId, "Invalid slot indices");
            return;
        }

        auto& inv = InventorySystem::GetInstance();
        if (inv.MoveItem(playerId, fromSlot, toSlot)) {
            nlohmann::json response = {
                {"type", "inventory_move_response"},
                {"success", true},
                {"fromSlot", fromSlot},
                {"toSlot", toSlot},
                {"timestamp", GetCurrentTimestamp()}
            };
            SendToSession(sessionId, response);
        } else {
            SendError(sessionId, "Failed to move item");
        }
    } catch (const std::exception& e) {
        Logger::Error("Error in HandleInventoryMove: {}", e.what());
        SendError(sessionId, "Internal server error");
    }
}

void GameLogic::HandleItemUse(uint64_t sessionId, const nlohmann::json& data) {
    try {
        uint64_t playerId = GetPlayerIdBySession(sessionId);
        int slot = data.value("slot", -1);
        uint64_t itemId = data.value("itemId", 0);

        auto& inv = InventorySystem::GetInstance();
        std::shared_ptr<LootItem> item;
        if (slot >= 0) {
            item = inv.GetItem(playerId, slot);
        } else if (itemId) {
            // Find item by ID (could be in any slot)
            auto invData = inv.GetInventory(playerId);
            for (const auto& s : invData) {
                if (s.item && s.item->GetId() == itemId) {
                    item = s.item;
                    slot = s.position;
                    break;
                }
            }
        }

        if (!item) {
            SendError(sessionId, "Item not found");
            return;
        }

        // Use the item (e.g., consume, apply effect)
        // For now, assume consumable and remove one
        if (item->IsConsumable()) {
            if (inv.RemoveItem(playerId, itemId, 1)) {
                // Trigger any game effect (skill, buff, etc.)
                // Could integrate with SkillSystem if item grants a skill
                SendSuccess(sessionId, "Item used");
                FirePythonEvent("item_used", {
                    {"playerId", playerId},
                    {"itemId", itemId},
                    {"slot", slot}
                });
            } else {
                SendError(sessionId, "Failed to use item");
            }
        } else {
            // Non-consumable items might be equipped or activated
            SendError(sessionId, "Item cannot be used this way");
        }
    } catch (const std::exception& e) {
        Logger::Error("Error in HandleItemUse: {}", e.what());
        SendError(sessionId, "Internal server error");
    }
}

void GameLogic::HandleItemDrop(uint64_t sessionId, const nlohmann::json& data) {
    try {
        uint64_t playerId = GetPlayerIdBySession(sessionId);
        int slot = data.value("slot", -1);
        int quantity = data.value("quantity", 1);

        if (slot < 0) {
            SendError(sessionId, "Invalid slot");
            return;
        }

        auto& inv = InventorySystem::GetInstance();
        auto item = inv.GetItem(playerId, slot);
        if (!item) {
            SendError(sessionId, "No item in that slot");
            return;
        }

        // Remove from inventory
        if (inv.RemoveItem(playerId, item->GetId(), quantity)) {
            // Create loot entity at player's feet
            auto player = GetPlayer(playerId);
            if (player) {
                CreateLootEntity(player->GetPosition(), item, quantity);
            }
            SendSuccess(sessionId, "Item dropped");
        } else {
            SendError(sessionId, "Failed to drop item");
        }
    } catch (const std::exception& e) {
        Logger::Error("Error in HandleItemDrop: {}", e.what());
        SendError(sessionId, "Internal server error");
    }
}

void GameLogic::HandleTradeRequest(uint64_t sessionId, const nlohmann::json& data) {
    try {
        uint64_t playerId = GetPlayerIdBySession(sessionId);
        uint64_t targetPlayerId = data.value("targetPlayerId", 0ULL);
        std::string action = data.value("action", "request"); // request, accept, decline, cancel

        if (targetPlayerId == 0) {
            SendError(sessionId, "Invalid target player");
            return;
        }

        // Simple trade state machine (simplified)
        if (action == "request") {
            // Send trade request to target player
            uint64_t targetSession = GetSessionIdByPlayer(targetPlayerId);
            if (targetSession == 0) {
                SendError(sessionId, "Target player not online");
                return;
            }
            nlohmann::json request = {
                {"type", "trade_request"},
                {"fromPlayerId", playerId},
                {"fromPlayerName", GetPlayer(playerId)->GetName()},
                {"timestamp", GetCurrentTimestamp()}
            };
            SendToSession(targetSession, request);
            SendSuccess(sessionId, "Trade request sent");
        } else if (action == "accept") {
            // Start trade session
            uint64_t targetSession = GetSessionIdByPlayer(targetPlayerId);
            nlohmann::json acceptMsg = {
                {"type", "trade_start"},
                {"player1", playerId},
                {"player2", targetPlayerId},
                {"timestamp", GetCurrentTimestamp()}
            };
            SendToSession(sessionId, acceptMsg);
            SendToSession(targetSession, acceptMsg);
        } else {
            SendError(sessionId, "Unsupported trade action");
        }
    } catch (const std::exception& e) {
        Logger::Error("Error in HandleTradeRequest: {}", e.what());
        SendError(sessionId, "Internal server error");
    }
}

void GameLogic::HandleGoldTransaction(uint64_t sessionId, const nlohmann::json& data) {
    try {
        uint64_t playerId = GetPlayerIdBySession(sessionId);
        uint64_t targetPlayerId = data.value("targetPlayerId", 0ULL);
        int64_t amount = data.value("amount", 0);
        std::string type = data.value("type", "transfer"); // transfer, gift, etc.

        if (targetPlayerId == 0 || amount <= 0) {
            SendError(sessionId, "Invalid target or amount");
            return;
        }

        auto& inv = InventorySystem::GetInstance();
        if (type == "transfer") {
            if (inv.GetGold(playerId) < amount) {
                SendError(sessionId, "Insufficient gold");
                return;
            }
            if (inv.RemoveGold(playerId, amount) && inv.AddGold(targetPlayerId, amount)) {
                SendSuccess(sessionId, "Gold transferred");
                uint64_t targetSession = GetSessionIdByPlayer(targetPlayerId);
                if (targetSession) {
                    nlohmann::json notify = {
                        {"type", "gold_received"},
                        {"fromPlayerId", playerId},
                        {"amount", amount},
                        {"timestamp", GetCurrentTimestamp()}
                    };
                    SendToSession(targetSession, notify);
                }
            } else {
                SendError(sessionId, "Transaction failed");
            }
        } else {
            SendError(sessionId, "Unsupported transaction type");
        }
    } catch (const std::exception& e) {
        Logger::Error("Error in HandleGoldTransaction: {}", e.what());
        SendError(sessionId, "Internal server error");
    }
}

void GameLogic::SendPositionCorrection(uint64_t sessionId, const glm::vec3& position, const glm::vec3& velocity) {
    BinaryProtocol::BinaryWriter writer;
    writer.WriteVector3(position);
    writer.WriteVector3(velocity);
    writer.WriteUInt64(GetCurrentTimestamp());
    SendBinaryToSession(sessionId, BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION_CORRECTION, writer.GetBuffer());
}

// =============== World Message Handlers ===============
void GameLogic::RegisterWorldHandlers() {
    RegisterBinaryHandler(BinaryProtocol::MESSAGE_TYPE_PLAYER_STATE,
        [this](uint64_t sessionId, uint16_t /*messageType*/, const std::vector<uint8_t>& data) {
            HandlePlayerState(sessionId, data);
    });

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
        [this](uint64_t sessionId, uint16_t /*messageType*/, const std::vector<uint8_t>& data) {
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
        [this](uint64_t sessionId, uint16_t /*messageType*/, const std::vector<uint8_t>& data) {
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

void GameLogic::HandleMessage(uint64_t sessionId, const nlohmann::json& message) {
    Logger::Debug("GameLogic handling message from session {}", sessionId);
    FirePythonEvent("game_message", {
        {"sessionId", sessionId},
        {"message", message}
    });
    LogicCore::HandleMessage(sessionId, message);
}

void GameLogic::OnPlayerConnected(uint64_t sessionId, uint64_t playerId) {
    Logger::Info("GameLogic: Player {} connected with session {}", playerId, sessionId);
    FirePythonEvent("player_connected", {
        {"sessionId", sessionId},
        {"playerId", playerId}
    });
    LogicCore::OnPlayerConnected(sessionId, playerId);
}

void GameLogic::OnPlayerDisconnected(uint64_t sessionId) {
    // Capture player ID before base class removes the mapping
    uint64_t playerId = GetPlayerIdBySession(sessionId);
    {
        std::lock_guard<std::mutex> lock(predictionMutex_);
        playerPrediction_.erase(playerId);
    }
    Logger::Info("GameLogic: Player {} disconnected from session {}", playerId, sessionId);
    FirePythonEvent("player_disconnected", {
        {"sessionId", sessionId},
        {"playerId", playerId}
    });
    LogicCore::OnPlayerDisconnected(sessionId);
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

        std::shared_ptr<Player> player = GetPlayer(playerId);
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

            BroadcastToNearbyPlayers(position, BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION,
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

void GameLogic::HandlePlayerState(uint64_t sessionId, const std::vector<uint8_t>& data) {
    try {
        // Deserialize client input
        ClientInput input = ClientInput::Deserialize(data.data(), data.size());
        if (!input.IsValid()) {
            Logger::Warn("Invalid client input from session {}", sessionId);
            return;
        }

        uint64_t playerId = GetPlayerIdBySession(sessionId);
        if (playerId == 0) {
            Logger::Warn("No player for session {}", sessionId);
            return;
        }

        // Store input in the player's prediction system
        {
            std::lock_guard<std::mutex> lock(predictionMutex_);
            playerPrediction_[playerId].StoreClientInput(input);
        }

        // Get authoritative player state
        auto player = GetPlayer(playerId);
        if (!player) return;

        // Simulate movement using the prediction system
        // (We'll compute the authoritative state from the last confirmed state plus unprocessed inputs)
        PredictionSystem* pred = &playerPrediction_[playerId];
        auto currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

            // Get the last confirmed state from prediction system (or from player)
            ServerState authState;
            authState.last_processed_input = 0; // TODO: track last processed input
            authState.timestamp = currentTime;
            authState.position = player->GetPosition();
            authState.velocity = player->GetVelocity(); // assuming Player has velocity
            authState.rotation = player->GetRotation(); // assuming Player has rotation
            authState.on_ground = player->IsOnGround(); // assuming Player has onGround

            // Simulate with unprocessed inputs
            auto unprocessed = pred->GetUnprocessedInputs(authState.last_processed_input);
            if (!unprocessed.empty()) {
                float deltaTime = 1.0f / 30.0f; // or compute based on time difference
                authState = pred->SimulateMovement(authState, unprocessed, deltaTime);
            }

            // Update authoritative player state
            player->SetPosition(authState.position);
            player->SetVelocity(authState.velocity);
            player->SetRotation(authState.rotation);
            player->SetOnGround(authState.on_ground);

            // Broadcast authoritative state to nearby players (optional)
            BroadcastPlayerState(playerId, authState);

            // Check if client needs a correction (compare with client's last reported position)
            // We need to store the client's last reported position; for simplicity we can compare
            // with the position that the client sent (which is not directly available here, but we
            // could pass it in the input). For now, we'll just send a correction if the simulation
            // moved significantly from the last confirmed state.
            static float correctionThreshold = 0.5f; // 0.5 meters
            if (glm::distance(authState.position, player->GetPosition()) > correctionThreshold) {
                SendPositionCorrection(sessionId, authState.position, authState.velocity);
            }

    } catch (const std::exception& e) {
        Logger::Error("HandlePlayerState error: {}", e.what());
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
        std::shared_ptr<Player> player = GetPlayer(playerId);
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
                // Handle mob death via MobSystem
                MobSystem::GetInstance().OnMobDeath(npcId, playerId);
            }

            BinaryProtocol::BinaryWriter writer;
            writer.WriteUInt64(playerId);
            writer.WriteUInt64(npcId);
            writer.WriteFloat(damage);
            writer.WriteFloat(npc->GetStats().health);
            writer.WriteUInt8(isDead ? 1 : 0);
            writer.WriteUInt64(GetCurrentTimestamp());

            SendBinaryToSession(sessionId, BinaryProtocol::MESSAGE_TYPE_COMBAT_EVENT, writer.GetBuffer());
        } else if (interactionType == "talk") {
            // Could trigger quest dialogue via QuestManager
            auto& questMgr = QuestManager::GetInstance();
            auto quests = questMgr.GetQuestsFromNPC(playerId, npc->GetId());
            nlohmann::json response = {
                {"type", "npc_dialogue"},
                {"npcId", npcId},
                {"quests", quests},
                {"timestamp", GetCurrentTimestamp()}
            };
            SendToSession(sessionId, response);
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
            familiar->SetBehaviorState(NPCAIState::FOLLOW);
            familiar->SetTarget(playerId);
        } else if (command == "attack") {
            familiar->SetBehaviorState(NPCAIState::CHASE);
            familiar->SetTarget(targetId);
        } else if (command == "stay") {
            familiar->SetBehaviorState(NPCAIState::IDLE);
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
void GameLogic::BroadcastToNearbyPlayers(const glm::vec3& position, uint16_t messageType,
                                         const std::vector<uint8_t>& data, float radius) {
    if (!connectionManager_) return;
    auto& pm = PlayerManager::GetInstance();
    auto nearby = pm.GetPlayersInRadius(position, radius);
    for (auto& player : nearby) {
        uint64_t sessionId = pm.GetSessionIdByPlayerId(player->GetId());
        if (sessionId != 0) {
            auto session = connectionManager_->GetSession(sessionId);
            if (session && session->IsConnected()) {
                session->SendBinary(messageType, data);
            }
        }
    }
}

void GameLogic::BroadcastToNearbyOnlinePlayers(const glm::vec3& position, uint16_t messageType,
                                              const std::vector<uint8_t>& data, float radius) {
    if (!connectionManager_) return;
    auto& pm = PlayerManager::GetInstance();
    auto onlinePlayers = pm.GetOnlinePlayers();
    for (const auto& player : onlinePlayers) {
        if (glm::distance(player->GetPosition(), position) <= radius) {
            uint64_t sessionId = GetSessionIdByPlayer(player->GetId());
            if (sessionId != 0) {
                auto session = connectionManager_->GetSession(sessionId);
                if (session && session->IsConnected()) {
                    session->SendBinary(messageType, data);
                }
            }
        }
    }
}

void GameLogic::BroadcastEntitySpawn(uint64_t entityId, EntityType type, const glm::vec3& position,
                                     float yaw, const std::string& name) {
    BinaryProtocol::BinaryWriter writer;
    writer.WriteUInt64(entityId);
    writer.WriteUInt8(static_cast<uint8_t>(type));
    writer.WriteString(name);
    writer.WriteVector3(position);
    writer.WriteFloat(yaw);
    writer.WriteUInt64(GetCurrentTimestamp());
    BroadcastToNearbyPlayers(position, BinaryProtocol::MESSAGE_TYPE_ENTITY_SPAWN, writer.GetBuffer(), 100.0f);
}

void GameLogic::SyncNearbyEntitiesToPlayer(uint64_t sessionId, const glm::vec3& position) {
    // Send a batch of entity data (positions, types, etc.) to the player
    auto nearbyEntities = EntityManager::GetInstance().GetEntitiesInRadius(position, 100.0f);
    nlohmann::json entityList = nlohmann::json::array();
    for (uint64_t entityId : nearbyEntities) {
        auto entity = GetEntity(entityId);
        if (entity) {
            entityList.push_back(entity->Serialize()); // assume Serialize()
        }
    }
    nlohmann::json message = {
        {"type", "entity_sync"},
        {"entities", entityList},
        {"timestamp", GetCurrentTimestamp()}
    };
    SendToSession(sessionId, message);
}

// =============== Thread Functions ===============
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

            // Update systems
            UpdateWorld(deltaTime);
            LogicEntity::GetInstance().UpdateNPCs(deltaTime);
            LogicEntity::GetInstance().UpdateCollisions(deltaTime);

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

    instanceMutex_.unlock();

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
    // 1. Update world time (e.g., day/night cycle)
    static float worldTime = 0.0f;
    worldTime += deltaTime;
    // Example: 24-hour cycle every 30 minutes real time (1800 seconds)
    const float dayLength = 1800.0f; // in seconds
    float timeOfDay = fmod(worldTime, dayLength) / dayLength; // 0.0 to 1.0
    // Notify world systems or update lighting via some manager
    LogicWorld::GetInstance().SetTimeOfDay(timeOfDay);

    // 2. Throttled chunk unloading – only run every N seconds
    static float timeSinceLastUnload = 0.0f;
    timeSinceLastUnload += deltaTime;
    const float unloadInterval = 1.0f; // unload check every second
    if (timeSinceLastUnload >= unloadInterval) {
        timeSinceLastUnload = 0.0f;

        // Collect player positions safely
        std::vector<glm::vec3> playerPositions;
        {
            std::lock_guard<std::mutex> lock(sessionMutex_);
            for (const auto& [playerId, sessionId] : playerToSessionMap_) {
                if (auto player = GetPlayer(playerId)) {
                    playerPositions.push_back(player->GetPosition());
                }
            }
        }

        // Unload distant chunks for each player
        float unloadDistance = GetWorldConfig().chunkUnloadDistance;
        for (const auto& pos : playerPositions) {
            LogicWorld::GetInstance().UnloadDistantChunks(pos, unloadDistance);
        }
    }

    // 3. Update dynamic world objects (if any)
    // e.g., moving platforms, floating items, environmental animations
    // LogicWorld::GetInstance().UpdateDynamicObjects(deltaTime);

    // 4. Update weather system
    // WeatherSystem::GetInstance().Update(deltaTime);

    // (Optional) Log or debug info using deltaTime
    Logger::Trace("UpdateWorld processed with deltaTime = {} ms", deltaTime * 1000.0f);
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
            {"active_chunks", LogicWorld::GetInstance().GetActiveChunkCount()},
            {"active_npcs", 0}, // TODO: expose from LogicEntity if needed
            {"world_config", {
                {"view_distance", GetWorldConfig().viewDistance},
                {"chunk_size", GetWorldConfig().chunkSize},
                {"terrain_scale", GetWorldConfig().terrainScale}
            }}
        };

        // Use DbManager instead of direct CitusClient
        if (!DbManager::GetInstance().SaveGameState("current_game", gameState)) {
            Logger::Error("Failed to save game state: DbManager returned false");
        } else {
            Logger::Debug("Game state saved");
        }
    } catch (const std::exception& e) {
        Logger::Error("Failed to save game state: {}", e.what());
    }
}

void GameLogic::SaveChunkData() {
    LogicWorld::GetInstance().SaveChunkData();
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
        auto it = WebSocketProtocol::IPCMessageTypes.find(msgType);
        if (it == WebSocketProtocol::IPCMessageTypes.end()) {
            Logger::Warn("Unknown IPC message type: {}", msgType);
            return;
        }
        int typeCode = it->second;
        switch (typeCode) {
            case 1: // welcome
                //Logger::Info("Received welcome message from master: {}", message.value("message", ""));
                break;
            case 2: // heartbeat
                //int count = message.value("count", 0);
                //Logger::Debug("Received heartbeat #{} from master", count);
                break;
            case 3: // broadcast
                if (message.contains("data")) {
                    BroadcastToAllPlayers(message["data"]);
                }
                break;
            case 4: // shutdown
                Logger::Info("Received shutdown command from master");
                Shutdown();
                break;
            case 5: // reload_config
                Logger::Info("Received config reload command from master");
                // TODO: Reload configuration if needed
                break;
            default:
                Logger::Warn("Unhandled IPC message code: {}", typeCode);
                break;
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
            //Logger::Debug("No active sessions to broadcast to");
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
            //Logger::Debug("No active sessions to broadcast binary to");
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

void GameLogic::BroadcastPlayerState(uint64_t playerId, const ServerState& state) {
    // Create a binary update message (ENTITY_UPDATE or PLAYER_STATE) for other players
    BinaryProtocol::BinaryWriter writer;
    writer.WriteUInt64(playerId);
    writer.WriteVector3(state.position);
    writer.WriteVector3(state.rotation);
    writer.WriteVector3(state.velocity);
    writer.WriteUInt64(state.timestamp);
    BroadcastToNearbyPlayers(state.position, BinaryProtocol::MESSAGE_TYPE_ENTITY_UPDATE, writer.GetBuffer(), 100.0f);
}

void GameLogic::BroadcastEntityDespawn(uint64_t entityId, const glm::vec3& position) {
    BinaryProtocol::BinaryWriter writer;
    writer.WriteUInt64(entityId);
    writer.WriteUInt64(GetCurrentTimestamp());

    BroadcastToNearbyPlayers(position, BinaryProtocol::MESSAGE_TYPE_ENTITY_DESPAWN, writer.GetBuffer(), 100.0f);
}
