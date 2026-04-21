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
    RegisterWorldHandlers();
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

void GameLogic::DespawnNPC(uint64_t npcId) {
    LogicEntity::GetInstance().DespawnNPC(npcId);
}

NPCEntity* GameLogic::GetNPCEntity(uint64_t npcId) {
    return LogicEntity::GetInstance().GetNPCEntity(npcId);
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

void GameLogic::HandleLootPickup(uint64_t session_id, const nlohmann::json& data) {
    try {
        uint64_t player_id = GetPlayerIdBySession(session_id);
        uint64_t lootId = data.value("lootId", 0ULL);
        int quantity = data.value("quantity", 1);
        if (lootId == 0) {
            SendError(session_id, "Invalid loot entity ID");
            return;
        }
        GameEntity* lootEntity = GetEntity(lootId);
        if (!lootEntity || lootEntity->GetType() != EntityType::ITEM) {
            SendError(session_id, "Invalid loot entity");
            return;
        }
        std::shared_ptr<Player> player = PlayerManager::GetInstance().GetPlayer(player_id);
        if (!player) {
            SendError(session_id, "Player not found");
            return;
        }
        float distance = glm::distance(player->GetPosition(), lootEntity->GetPosition());
        if (distance > 5.0f) {
            SendError(session_id, "Too far to loot");
            return;
        }
        auto& inv = InventorySystem::GetInstance();
        if (inv.AddItem(player_id, LootItem(lootId, lootEntity->GetName()), quantity)) {
            EntityManager::GetInstance().DestroyEntity(lootId);
            SendSuccess(session_id, "Loot collected");
            FirePythonEvent("loot_pickup", {
                {"player_id", player_id},
                {"itemId", lootId},
                {"quantity", quantity}
            });
        } else {
            SendError(session_id, "Inventory full");
        }
    } catch (const std::exception& e) {
        Logger::Error("Error in HandleLootPickup: {}", e.what());
        SendError(session_id, "Internal server error");
    }
}

void GameLogic::HandleInventoryMove(uint64_t session_id, const nlohmann::json& data) {
    try {
        uint64_t player_id = GetPlayerIdBySession(session_id);
        int fromSlot = data.value("fromSlot", -1);
        int toSlot = data.value("toSlot", -1);
        if (fromSlot < 0 || toSlot < 0) {
            SendError(session_id, "Invalid slot indices");
            return;
        }
        auto& inv = InventorySystem::GetInstance();
        if (inv.MoveItem(player_id, fromSlot, toSlot)) {
            nlohmann::json response = {
                {"type", "inventory_move_response"},
                {"success", true},
                {"fromSlot", fromSlot},
                {"toSlot", toSlot},
                {"timestamp", GetCurrentTimestamp()}
            };
            SendToSession(session_id, response);
        } else {
            SendError(session_id, "Failed to move item");
        }
    } catch (const std::exception& e) {
        Logger::Error("Error in HandleInventoryMove: {}", e.what());
        SendError(session_id, "Internal server error");
    }
}

void GameLogic::HandleItemUse(uint64_t session_id, const nlohmann::json& data) {
    try {
        uint64_t player_id = GetPlayerIdBySession(session_id);
        int slot = data.value("slot", -1);
        uint64_t itemId = data.value("itemId", 0);
        auto& inv = InventorySystem::GetInstance();
        std::shared_ptr<LootItem> item;
        if (slot >= 0) {
            item = inv.GetItem(player_id, slot);
        } else if (itemId) {
            auto invData = inv.GetInventory(player_id);
            for (const auto& s : invData) {
                if (s.item && s.item->GetId() == itemId) {
                    item = s.item;
                    slot = s.position;
                    break;
                }
            }
        }
        if (!item) {
            SendError(session_id, "Item not found");
            return;
        }
        if (item->IsConsumable()) {
            if (inv.RemoveItem(player_id, itemId, 1)) {
                SendSuccess(session_id, "Item used");
                FirePythonEvent("item_used", {
                    {"player_id", player_id},
                    {"itemId", itemId},
                    {"slot", slot}
                });
            } else {
                SendError(session_id, "Failed to use item");
            }
        } else {
            SendError(session_id, "Item cannot be used this way");
        }
    } catch (const std::exception& e) {
        Logger::Error("Error in HandleItemUse: {}", e.what());
        SendError(session_id, "Internal server error");
    }
}

void GameLogic::HandleItemDrop(uint64_t session_id, const nlohmann::json& data) {
    try {
        uint64_t player_id = GetPlayerIdBySession(session_id);
        int slot = data.value("slot", -1);
        int quantity = data.value("quantity", 1);
        if (slot < 0) {
            SendError(session_id, "Invalid slot");
            return;
        }
        auto& inv = InventorySystem::GetInstance();
        auto item = inv.GetItem(player_id, slot);
        if (!item) {
            SendError(session_id, "No item in that slot");
            return;
        }
        if (inv.RemoveItem(player_id, item->GetId(), quantity)) {
            auto player = GetPlayer(player_id);
            if (player) {
                CreateLootEntity(player->GetPosition(), item, quantity);
            }
            SendSuccess(session_id, "Item dropped");
        } else {
            SendError(session_id, "Failed to drop item");
        }
    } catch (const std::exception& e) {
        Logger::Error("Error in HandleItemDrop: {}", e.what());
        SendError(session_id, "Internal server error");
    }
}

void GameLogic::HandleTradeRequest(uint64_t session_id, const nlohmann::json& data) {
    try {
        uint64_t player_id = GetPlayerIdBySession(session_id);
        uint64_t targetPlayerId = data.value("targetPlayerId", 0ULL);
        std::string action = data.value("action", "request");
        if (targetPlayerId == 0) {
            SendError(session_id, "Invalid target player");
            return;
        }
        if (action == "request") {
            uint64_t targetSession = GetSessionIdByPlayer(targetPlayerId);
            if (targetSession == 0) {
                SendError(session_id, "Target player not online");
                return;
            }
            nlohmann::json request = {
                {"type", "trade_request"},
                {"fromPlayerId", player_id},
                {"fromPlayerName", GetPlayer(player_id)->GetName()},
                {"timestamp", GetCurrentTimestamp()}
            };
            SendToSession(targetSession, request);
            SendSuccess(session_id, "Trade request sent");
        } else if (action == "accept") {
            uint64_t targetSession = GetSessionIdByPlayer(targetPlayerId);
            nlohmann::json acceptMsg = {
                {"type", "trade_start"},
                {"player1", player_id},
                {"player2", targetPlayerId},
                {"timestamp", GetCurrentTimestamp()}
            };
            SendToSession(session_id, acceptMsg);
            SendToSession(targetSession, acceptMsg);
        } else {
            SendError(session_id, "Unsupported trade action");
        }
    } catch (const std::exception& e) {
        Logger::Error("Error in HandleTradeRequest: {}", e.what());
        SendError(session_id, "Internal server error");
    }
}

void GameLogic::HandleGoldTransaction(uint64_t session_id, const nlohmann::json& data) {
    try {
        uint64_t player_id = GetPlayerIdBySession(session_id);
        uint64_t targetPlayerId = data.value("targetPlayerId", 0ULL);
        int64_t amount = data.value("amount", 0);
        std::string type = data.value("type", "transfer");
        if (targetPlayerId == 0 || amount <= 0) {
            SendError(session_id, "Invalid target or amount");
            return;
        }
        auto& inv = InventorySystem::GetInstance();
        if (type == "transfer") {
            if (inv.GetGold(player_id) < amount) {
                SendError(session_id, "Insufficient gold");
                return;
            }
            if (inv.RemoveGold(player_id, amount) && inv.AddGold(targetPlayerId, amount)) {
                SendSuccess(session_id, "Gold transferred");
                uint64_t targetSession = GetSessionIdByPlayer(targetPlayerId);
                if (targetSession) {
                    nlohmann::json notify = {
                        {"type", "gold_received"},
                        {"fromPlayerId", player_id},
                        {"amount", amount},
                        {"timestamp", GetCurrentTimestamp()}
                    };
                    SendToSession(targetSession, notify);
                }
            } else {
                SendError(session_id, "Transaction failed");
            }
        } else {
            SendError(session_id, "Unsupported transaction type");
        }
    } catch (const std::exception& e) {
        Logger::Error("Error in HandleGoldTransaction: {}", e.what());
        SendError(session_id, "Internal server error");
    }
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
        session->SendBinary(BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION_CORRECTION, data);
    } else {
        nlohmann::json jsonMsg = {
            {"type", "position_correction"},
            {"x", position.x}, {"y", position.y}, {"z", position.z},
            {"vx", velocity.x}, {"vy", velocity.y}, {"vz", velocity.z},
            {"timestamp", GetCurrentTimestamp()}
        };
        session->Send(jsonMsg);
    }
}

void GameLogic::RegisterWorldHandlers() {
    RegisterHandler("npc_interaction", [this](uint64_t session_id, const nlohmann::json& data) {
        HandleNPCInteraction(session_id, data);
    });
    RegisterHandler("collision_check", [this](uint64_t session_id, const nlohmann::json& data) {
        HandleCollisionCheck(session_id, data);
    });
    RegisterHandler("familiar_command", [this](uint64_t session_id, const nlohmann::json& data) {
        HandleFamiliarCommand(session_id, data);
    });
    RegisterHandler("entity_spawn_request", [this](uint64_t session_id, const nlohmann::json& data) {
        HandleEntitySpawnRequest(session_id, data);
    });
    RegisterHandler("loot_pickup", [this](uint64_t session_id, const nlohmann::json& data) {
        HandleLootPickup(session_id, data);
    });
    RegisterHandler("inventory_move", [this](uint64_t session_id, const nlohmann::json& data) {
        HandleInventoryMove(session_id, data);
    });
    RegisterHandler("item_use", [this](uint64_t session_id, const nlohmann::json& data) {
        HandleItemUse(session_id, data);
    });
    RegisterHandler("item_drop", [this](uint64_t session_id, const nlohmann::json& data) {
        HandleItemDrop(session_id, data);
    });
    RegisterHandler("trade_request", [this](uint64_t session_id, const nlohmann::json& data) {
        HandleTradeRequest(session_id, data);
    });
    RegisterHandler("gold_transaction", [this](uint64_t session_id, const nlohmann::json& data) {
        HandleGoldTransaction(session_id, data);
    });
    Logger::Info("Registered world message handlers");
}

void GameLogic::HandleMessage(uint64_t session_id, const nlohmann::json& message) {
    Logger::Debug("GameLogic::HandleMessage called for session {}", session_id);
    Logger::Debug("Message content: {}", message.dump());
    std::string type = message.value("type", "");
    Logger::Debug("Message type: '{}'", type);
    if (type == "collision_check") {
        HandleCollisionCheck(session_id, message);
    } else if (type == "npc_interaction") {
        HandleNPCInteraction(session_id, message);
    } else if (type == "familiar_command") {
        HandleFamiliarCommand(session_id, message);
    } else if (type == "entity_spawn_request") {
        HandleEntitySpawnRequest(session_id, message);
    } else if (type == "loot_pickup") {
        HandleLootPickup(session_id, message);
    } else if (type == "inventory_move") {
        HandleInventoryMove(session_id, message);
    } else if (type == "item_use") {
        HandleItemUse(session_id, message);
    } else if (type == "item_drop") {
        HandleItemDrop(session_id, message);
    } else if (type == "trade_request") {
        HandleTradeRequest(session_id, message);
    } else if (type == "gold_transaction") {
        HandleGoldTransaction(session_id, message);
    } else {
        Logger::Warn("Unknown message type '{}' from session {}", type, session_id);
        LogicCore::HandleMessage(session_id, message);
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

void GameLogic::HandleNPCInteraction(uint64_t session_id, const nlohmann::json& data) {
    try {
        uint64_t npcId = data.value("npcId", 0ULL);
        std::string interactionType = data.value("interaction", "");
        if (npcId == 0 || interactionType.empty()) {
            SendError(session_id, "Invalid NPC interaction", 400);
            return;
        }
        NPCEntity* npc = GetNPCEntity(npcId);
        if (!npc) {
            SendError(session_id, "NPC not found", 404);
            return;
        }
        uint64_t player_id = GetPlayerIdBySession(session_id);
        std::shared_ptr<Player> player = GetPlayer(player_id);
        if (!player) {
            SendError(session_id, "Player not found", 404);
            return;
        }
        float distance = glm::distance(player->GetPosition(), npc->GetPosition());
        if (distance > 15.0f) {
            SendError(session_id, "Too far from NPC", 400);
            return;
        }
        if (interactionType == "attack") {
            float damage = 10.0f;
            npc->TakeDamage(damage, player_id);
            bool isDead = npc->IsDead();
            if (isDead) {
                MobSystem::GetInstance().OnMobDeath(npcId, player_id);
            }
            BinaryProtocol::BinaryWriter writer;
            writer.WriteUInt64(player_id);
            writer.WriteUInt64(npcId);
            writer.WriteFloat(damage);
            writer.WriteFloat(npc->GetStats().health);
            writer.WriteUInt8(isDead ? 1 : 0);
            writer.WriteUInt64(GetCurrentTimestamp());
            SendBinaryToSession(session_id, BinaryProtocol::MESSAGE_TYPE_COMBAT_EVENT, writer.GetBuffer());
        } else if (interactionType == "talk") {
            auto& questMgr = QuestManager::GetInstance();
            auto quests = questMgr.GetQuestsFromNPC(player_id, npc->GetId());
            nlohmann::json response = {
                {"type", "npc_dialogue"},
                {"npcId", npcId},
                {"quests", quests},
                {"timestamp", GetCurrentTimestamp()}
            };
            SendToSession(session_id, response);
        }
    } catch (const std::exception& e) {
        Logger::Error("Error handling NPC interaction: {}", e.what());
        SendError(session_id, "Failed to process NPC interaction", 500);
    }
}

void GameLogic::HandleFamiliarCommand(uint64_t session_id, const nlohmann::json& data) {
    try {
        uint64_t familiarId = data.value("familiarId", 0ULL);
        std::string command = data.value("command", "");
        uint64_t targetId = data.value("targetId", 0ULL);
        if (familiarId == 0 || command.empty()) {
            SendError(session_id, "Invalid familiar command", 400);
            return;
        }
        NPCEntity* familiar = GetNPCEntity(familiarId);
        if (!familiar) {
            SendError(session_id, "Familiar not found", 404);
            return;
        }
        uint64_t player_id = GetPlayerIdBySession(session_id);
        if (familiar->GetOwnerId() != player_id) {
            SendError(session_id, "Not your familiar", 403);
            return;
        }
        if (command == "follow") {
            familiar->SetBehaviorState(NPCAIState::FOLLOW);
            familiar->SetTarget(player_id);
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
        SendToSession(session_id, response);
    } catch (const std::exception& e) {
        Logger::Error("Error handling familiar command: {}", e.what());
        SendError(session_id, "Failed to process familiar command", 500);
    }
}

void GameLogic::HandleCollisionCheck(uint64_t session_id, const nlohmann::json& data) {
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
        SendToSession(session_id, response);
    } catch (const std::exception& e) {
        Logger::Error("Error handling collision check: {}", e.what());
        SendError(session_id, "Failed to check collision", 500);
    }
}

void GameLogic::HandleEntitySpawnRequest(uint64_t session_id, const nlohmann::json& data) {
    try {
        int entityType = data.value("entityType", 0);
        float x = data.value("x", 0.0f);
        float y = data.value("y", 0.0f);
        float z = data.value("z", 0.0f);
        glm::vec3 position(x, y, z);
        if (entityType >= static_cast<int>(NPCType::WOLF_FAMILIAR) &&
            entityType <= static_cast<int>(NPCType::CAT_FAMILIAR)) {
            uint64_t player_id = GetPlayerIdBySession(session_id);
            NPCType type = static_cast<NPCType>(entityType);
            uint64_t npcId = SpawnNPC(type, position, player_id);
            if (npcId > 0) {
                nlohmann::json response = {
                    {"type", "entity_spawn_response"},
                    {"entityId", npcId},
                    {"entityType", entityType},
                    {"position", {x, y, z}},
                    {"success", true},
                    {"timestamp", GetCurrentTimestamp()}
                };
                SendToSession(session_id, response);
            } else {
                SendError(session_id, "Failed to spawn entity", 500);
            }
        } else {
            SendError(session_id, "Invalid entity type", 400);
        }
    } catch (const std::exception& e) {
        Logger::Error("Error handling entity spawn request: {}", e.what());
        SendError(session_id, "Failed to spawn entity", 500);
    }
}

void GameLogic::BroadcastToNearbyPlayers(const glm::vec3& position, uint16_t messageType,
                                         const std::vector<uint8_t>& data, float radius) {
    if (!connectionManager_) return;
    auto& pm = PlayerManager::GetInstance();
    auto nearby = pm.GetPlayersInRadius(position, radius);
    for (auto& player : nearby) {
        uint64_t session_id = pm.GetSessionIdByPlayerId(player->GetId());
        if (session_id == 0) continue;
        auto session = connectionManager_->GetSession(session_id);
        if (!session || !session->IsConnected()) continue;
        if (session->GetProtocolMode() == ProtocolMode::Binary) {
            session->SendBinary(messageType, data);
            continue;
        }
        nlohmann::json jsonMsg;
        try {
            BinaryProtocol::BinaryReader reader(data.data(), data.size());
            switch (messageType) {
                case BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION:
                    jsonMsg = PlayerPositionToJson(data);
                    break;
                case BinaryProtocol::MESSAGE_TYPE_PLAYER_UPDATE:
                    jsonMsg = PlayerUpdateToJson(data);
                    break;
                case BinaryProtocol::MESSAGE_TYPE_ENTITY_SPAWN:
                    jsonMsg = EntitySpawnToJson(data);
                    break;
                case BinaryProtocol::MESSAGE_TYPE_ENTITY_UPDATE:
                    jsonMsg = EntityUpdateToJson(data);
                    break;
                case BinaryProtocol::MESSAGE_TYPE_ENTITY_DESPAWN:
                    jsonMsg = EntityDespawnToJson(data);
                    break;
                case BinaryProtocol::MESSAGE_TYPE_CHUNK_DATA:
                    jsonMsg = ChunkDataToJson(data);
                    break;
                default:
                    Logger::Warn("No JSON conversion for message type {}", messageType);
                    continue;
            }
        } catch (const std::exception& e) {
            Logger::Error("BroadcastToNearbyPlayers: Failed to convert binary to JSON: {}", e.what());
            continue;
        }
        session->Send(jsonMsg);
    }
}

void GameLogic::BroadcastToNearbyOnlinePlayers(const glm::vec3& position, uint16_t messageType,
                                              const std::vector<uint8_t>& data, float radius) {
    if (!connectionManager_) return;
    auto& pm = PlayerManager::GetInstance();
    auto onlinePlayers = pm.GetOnlinePlayers();
    for (const auto& player : onlinePlayers) {
        if (glm::distance(player->GetPosition(), position) <= radius) {
            uint64_t session_id = GetSessionIdByPlayer(player->GetId());
            if (session_id != 0) {
                auto session = connectionManager_->GetSession(session_id);
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
        {"type", "entity_sync"},
        {"entities", entityList},
        {"timestamp", GetCurrentTimestamp()}
    };
    SendToSession(session_id, message);
}

void GameLogic::BroadcastPlayerSpawn(uint64_t player_id) {
    auto player = GetPlayer(player_id);
    if (!player) return;
    BinaryProtocol::BinaryWriter writer;
    writer.WriteUInt64(player_id);
    writer.WriteString(player->GetName());
    writer.WriteVector3(player->GetPosition());
    writer.WriteFloat(player->GetRotation().y);
    writer.WriteFloat(player->GetHealth());
    writer.WriteFloat(player->GetMaxHealth());
    BroadcastToNearbyPlayers(player->GetPosition(),
                             BinaryProtocol::MESSAGE_TYPE_PLAYER_SPAWN,
                             writer.GetBuffer(),
                             ConfigManager::GetInstance().GetFloat("world.interest_radius", 100.0f));
}

void GameLogic::BroadcastPlayerDespawn(uint64_t player_id, const glm::vec3& lastPosition) {
    BinaryProtocol::BinaryWriter writer;
    writer.WriteUInt64(player_id);
    BroadcastToNearbyPlayers(lastPosition,
                             BinaryProtocol::MESSAGE_TYPE_PLAYER_DESPAWN,
                             writer.GetBuffer(),
                             ConfigManager::GetInstance().GetFloat("world.interest_radius", 100.0f));
}

void GameLogic::BroadcastToNearbyPlayersJson(const glm::vec3& position, const nlohmann::json& message, float radius) {
    if (!connectionManager_) return;
    auto& pm = PlayerManager::GetInstance();
    auto nearby = pm.GetPlayersInRadius(position, radius);
    for (auto& player : nearby) {
        uint64_t session_id = pm.GetSessionIdByPlayerId(player->GetId());
        if (session_id != 0) {
            auto session = connectionManager_->GetSession(session_id);
            if (session && session->IsConnected()) {
                session->Send(message);
            }
        }
    }
}

void GameLogic::BroadcastPlayerSpawnJson(uint64_t player_id) {
    auto player = GetPlayer(player_id);
    if (!player) return;
    nlohmann::json msg = {
        {"type", "player_spawn"},
        {"player_id", player_id},
        {"name", player->GetName()},
        {"position", {player->GetPosition().x, player->GetPosition().y, player->GetPosition().z}},
        {"yaw", player->GetRotation().y},
        {"health", player->GetHealth()},
        {"max_health", player->GetMaxHealth()},
        {"timestamp", GetCurrentTimestamp()}
    };
    BroadcastToNearbyPlayersJson(player->GetPosition(), msg, ConfigManager::GetInstance().GetFloat("world.interest_radius", 100.0f));
}

void GameLogic::BroadcastPlayerDespawnJson(uint64_t player_id, const glm::vec3& lastPosition) {
    nlohmann::json msg = {
        {"type", "player_despawn"},
        {"player_id", player_id},
        {"timestamp", GetCurrentTimestamp()}
    };
    BroadcastToNearbyPlayersJson(lastPosition, msg, ConfigManager::GetInstance().GetFloat("world.interest_radius", 100.0f));
}

void GameLogic::BroadcastEntityDespawn(uint64_t entityId, const glm::vec3& position) {
    BinaryProtocol::BinaryWriter writer;
    writer.WriteUInt64(entityId);
    writer.WriteUInt64(GetCurrentTimestamp());
    BroadcastToNearbyPlayers(position, BinaryProtocol::MESSAGE_TYPE_ENTITY_DESPAWN, writer.GetBuffer(), 100.0f);
}

void GameLogic::SendAuthenticationSuccess(uint64_t session_id, uint64_t player_id, const std::string& message) {
    nlohmann::json response = {
        {"type", "authentication_response"},
        {"success", true},
        {"player_id", player_id},
        {"message", message},
        {"timestamp", GetCurrentTimestamp()}
    };
    SendToSession(session_id, response);
}

void GameLogic::SendAuthenticationFailure(uint64_t session_id, const std::string& message) {
    nlohmann::json response = {
        {"type", "authentication_response"},
        {"success", false},
        {"message", message},
        {"timestamp", GetCurrentTimestamp()}
    };
    SendToSession(session_id, response);
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

void GameLogic::BroadcastToAllPlayersBinary(uint16_t messageType, const std::vector<uint8_t>& data) {
    if (!connectionManager_) {
        Logger::Warn("Cannot broadcast binary: ConnectionManager not available");
        return;
    }
    try {
        auto sessions = connectionManager_->GetAllSessions();
        if (sessions.empty()) return;
        Logger::Debug("Broadcasting binary message type {} to {} player(s)",
                      messageType, sessions.size());
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

void GameLogic::BroadcastToPlayers(const std::vector<uint64_t>& session_ids, const nlohmann::json& message) {
    if (!connectionManager_) {
        Logger::Warn("Cannot broadcast: ConnectionManager not available");
        return;
    }
    try {
        std::string serialized = message.dump();
        int sentCount = 0;
        for (uint64_t session_id : session_ids) {
            auto session = connectionManager_->GetSession(session_id);
            if (session && session->IsConnected()) {
                try {
                    session->SendRaw(serialized);
                    sentCount++;
                } catch (const std::exception& e) {
                    Logger::Error("Failed to send message to session {}: {}",
                                  session_id, e.what());
                }
            }
        }
        if (sentCount > 0) {
            Logger::Debug("Broadcasted to {} specific player(s)", sentCount);
        }
    } catch (const std::exception& err) {
        Logger::Error("Error broadcasting to specific players: {}", err.what());
    }
}

void GameLogic::HandleIPCMessage(const nlohmann::json& message) {
    try {
        std::string msgType = message.value("type", "");
        auto it = IPCMessageTypes.find(msgType);
        if (it == IPCMessageTypes.end()) {
            Logger::Warn("Unknown IPC message type: {}", msgType);
            return;
        }
        int typeCode = it->second;
        switch (typeCode) {
            case 1:
                break;
            case 2:
                break;
            case 3:
                if (message.contains("data")) {
                    BroadcastToAllPlayers(message["data"]);
                }
                break;
            case 4:
                Logger::Info("Received shutdown command from master");
                Shutdown();
                break;
            case 5:
                Logger::Info("Received config reload command from master");
                break;
            default:
                Logger::Warn("Unhandled IPC message code: {}", typeCode);
                break;
        }
    } catch (const std::exception& e) {
        Logger::Error("Error handling IPC message: {}", e.what());
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
        {"type", "player_update"},
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
        {"type", "player_position"},
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
        {"type", "player_update"},
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
        {"type", "entity_spawn"},
        {"entity_id", entityId},
        {"entity_type", type},
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
        {"type", "entity_update"},
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
        {"type", "entity_despawn"},
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
        {"type", "world_chunk"},
        {"chunk_x", chunk_x},
        {"chunk_z", chunk_z},
        {"lod", lod},
        {"data", chunk_json},
        {"timestamp", timestamp}
    };
}

void GameLogic::OnAuthentication(const AuthenticationData& data) {
    auto& pm = PlayerManager::GetInstance();
    bool authenticated = false;
    std::string message;
    uint64_t player_id = 0;
    if (pm.PlayerExists(data.username)) {
        if (data.password.empty() || pm.AuthenticatePlayer(data.username, data.password)) {
            authenticated = true;
            message = "Welcome back, " + data.username;
            player_id = pm.GetPlayerByUsername(data.username)->GetId();
        } else {
            message = "Invalid password";
        }
    } else {
        if (data.password.empty()) {
            auto player = pm.CreatePlayer(data.username);
            if (player) {
                authenticated = true;
                message = "Welcome, " + data.username;
                player_id = player->GetId();
            } else {
                message = "Failed to create player";
            }
        } else {
            message = "Player does not exist";
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
        sendAuthResponseCb_(data.session_id, authenticated, message, player_id);
    } else {
        Logger::Error("No sendAuthResponseCb_ set in GameLogic");
    }
}

void GameLogic::OnChunkRequest(const ChunkRequestData& req) {
    auto chunk = GetOrCreateChunk(req.chunk_x, req.chunk_z);
    if (!chunk) {
        Logger::Error("Failed to get chunk ({},{}) for session {}", req.chunk_x, req.chunk_z, req.session_id);
        return;
    }
    ChunkData resp;
    resp.chunk_x = req.chunk_x;
    resp.chunk_z = req.chunk_z;
    resp.lod = req.lod;
    resp.chunk_json = chunk->Serialize();
    resp.timestamp = GetCurrentTimestamp();
    if (sendChunkCb_) {
        sendChunkCb_(req.session_id, resp);
    } else {
        Logger::Error("No sendChunkCb_ set in GameLogic");
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
    FirePythonEvent("player_move_3d", {
        {"player_id", data.player_id},
        {"x", finalPos.x}, {"y", finalPos.y}, {"z", finalPos.z},
        {"session_id", data.session_id}
    });
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
    static float correctionThreshold = 0.5f;
    if (glm::distance(authState.position, player->GetPosition()) > correctionThreshold) {
        SendPositionCorrection(data.session_id, authState.position, authState.velocity);
    }
}

void GameLogic::SetSendAuthenticationResponseCallback(std::function<void(uint64_t, bool, const std::string&, uint64_t)> cb) {
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
                session->Send(chatJson);
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
