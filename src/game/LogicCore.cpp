#include "game/LogicCore.hpp"

// =============== Static Members ===============
std::mutex LogicCore::instanceMutex_;
LogicCore* LogicCore::instance_ = nullptr;

// =============== Constructor and Destructor ===============
LogicCore::LogicCore()
    : running_(false),
    playerManager_(PlayerManager::GetInstance()),
    dbManager_(DbManager::GetInstance()),
    pythonScripting_(PythonScripting::GetInstance()),
    scriptHotReloader_(nullptr),
    pythonEnabled_(false)
{

    rng_.seed(std::random_device()());
    Logger::Debug("LogicCore initialized");
}

LogicCore::~LogicCore() {
    if (running_) {
        Shutdown();
    }
}

// =============== Singleton Access ===============
LogicCore& LogicCore::GetInstance() {
    std::lock_guard<std::mutex> lock(instanceMutex_);
    if (!instance_) {
        instance_ = new LogicCore();
    }
    return *instance_;
}

// =============== Initialization and Shutdown ===============
void LogicCore::Initialize() {
    if (running_) {
        Logger::Warn("LogicCore already initialized");
        return;
    }

    Logger::Info("Initializing LogicCore...");

    auto& config = ConfigManager::GetInstance();
    
    // Register default handlers
    RegisterDefaultHandlers();

    // Initialize Python scripting if enabled
    pythonEnabled_ = config.GetBool("python.enabled", false);
    if (pythonEnabled_) {
        if (pythonScripting_.Initialize()) {
            Logger::Info("Python scripting initialized");
            RegisterPythonEventHandlers();

            bool hotReloadEnabled = config.GetBool("python.hot_reload", true);
            if (hotReloadEnabled) {
                std::string scriptDir = config.GetString("python.script_dir", "./scripts");
                scriptHotReloader_ = std::make_unique<ScriptHotReloader>(scriptDir, 2000);
                scriptHotReloader_->Start();
            }
        } else {
            Logger::Warn("Failed to initialize Python scripting");
            pythonEnabled_ = false;
        }
    }

    // Start threads
    running_ = true;
    gameLoopThread_ = RAIIThread([this]() { GameLoop(); });
    spawnerThread_ = RAIIThread([this]() { SpawnerLoop(); });
    saveThread_ = RAIIThread([this]() { SaveLoop(); });

    Logger::Info("LogicCore initialized successfully");
}

void LogicCore::Shutdown() {
    if (!running_) {
        return;
    }

    Logger::Info("Shutting down LogicCore...");

    if (scriptHotReloader_) {
        scriptHotReloader_->Stop();
        scriptHotReloader_.reset();
    }

    if (pythonEnabled_) {
        pythonScripting_.Shutdown();
    }

    running_ = false;
    
    // Notify all condition variables
    gameLoopCV_.notify_all();
    spawnerCV_.notify_all();
    saveCV_.notify_all();

    // Stop threads
    gameLoopThread_.Stop();
    spawnerThread_.Stop();
    saveThread_.Stop();

    // Cleanup
    {
        std::lock_guard<std::mutex> lock(handlersMutex_);
        messageHandlers_.clear();
        binaryHandlers_.clear();
    }
    
    {
        std::lock_guard<std::mutex> lock(rateLimitMutex_);
        rateLimits_.clear();
    }
    
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        sessionToPlayerMap_.clear();
        playerToSessionMap_.clear();
    }

    Logger::Info("LogicCore shutdown complete");
}

// =============== Message Handling ===============
void LogicCore::HandleMessage(uint64_t sessionId, const nlohmann::json& message) {
    if (!message.contains("type") || !message["type"].is_string()) {
        SendError(sessionId, "Invalid message format");
        return;
    }

    std::string messageType = message["type"];
    Logger::Debug("Handling message type '{}' from session {}", messageType, sessionId);

    try {
        if (!CheckRateLimit(sessionId)) {
            SendError(sessionId, "Rate limit exceeded", 429);
            return;
        }

        std::lock_guard<std::mutex> lock(handlersMutex_);
        auto it = messageHandlers_.find(messageType);
        if (it != messageHandlers_.end()) {
            it->second(sessionId, message);
        } else {
            SendError(sessionId, "Unknown message type: " + messageType);
        }
    } catch (const std::exception& e) {
        Logger::Error("Error handling message: {}", e.what());
        SendError(sessionId, "Internal server error", 500);
    }
}

void LogicCore::HandleBinaryMessage(uint64_t sessionId, uint16_t messageType, const std::vector<uint8_t>& data) {
    Logger::Debug("Handling binary message type {} from session {}", messageType, sessionId);

    try {
        if (!CheckRateLimit(sessionId)) {
            BinaryProtocol::BinaryWriter writer;
            writer.WriteUInt8(1);
            writer.WriteString("Rate limit exceeded");
            writer.WriteUInt32(429);
            SendBinaryToSession(sessionId, BinaryProtocol::MESSAGE_TYPE_ERROR, writer.GetBuffer());
            return;
        }

        std::lock_guard<std::mutex> lock(handlersMutex_);
        auto it = binaryHandlers_.find(messageType);
        if (it != binaryHandlers_.end()) {
            it->second(sessionId, messageType, data);
        } else {
            try {
                BinaryProtocol::BinaryReader reader(data.data(), data.size());
                nlohmann::json jsonData = reader.ReadJson();
                HandleMessage(sessionId, jsonData);
            } catch (...) {
                BinaryProtocol::BinaryWriter writer;
                writer.WriteUInt8(1);
                writer.WriteString("Unknown binary message type");
                writer.WriteUInt32(400);
                SendBinaryToSession(sessionId, BinaryProtocol::MESSAGE_TYPE_ERROR, writer.GetBuffer());
            }
        }
    } catch (const std::exception& e) {
        Logger::Error("Error handling binary message: {}", e.what());
        SendError(sessionId, "Internal server error", 500);
    }
}

void LogicCore::RegisterHandler(const std::string& messageType, MessageHandler handler) {
    std::lock_guard<std::mutex> lock(handlersMutex_);
    messageHandlers_[messageType] = handler;
    Logger::Debug("Registered handler for: {}", messageType);
}

void LogicCore::RegisterBinaryHandler(uint16_t messageType, BinaryMessageHandler handler) {
    std::lock_guard<std::mutex> lock(handlersMutex_);
    binaryHandlers_[messageType] = handler;
    Logger::Debug("Registered binary handler for: {}", messageType);
}

void LogicCore::RegisterDefaultHandlers() {
    RegisterHandler("login", [this](uint64_t sessionId, const nlohmann::json& data) {
        HandleLogin(sessionId, data);
    });
    RegisterHandler("chat", [this](uint64_t sessionId, const nlohmann::json& data) {
        HandleChat(sessionId, data);
    });
    RegisterHandler("combat", [this](uint64_t sessionId, const nlohmann::json& data) {
        HandleCombat(sessionId, data);
    });
    RegisterHandler("quest", [this](uint64_t sessionId, const nlohmann::json& data) {
        HandleQuest(sessionId, data);
    });
    
    Logger::Info("Registered default message handlers");
}

// =============== Session Management ===============
void LogicCore::OnPlayerConnected(uint64_t sessionId, uint64_t playerId) {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    sessionToPlayerMap_[sessionId] = playerId;
    playerToSessionMap_[playerId] = sessionId;
    Logger::Info("Player {} connected with session {}", playerId, sessionId);
}

void LogicCore::OnPlayerDisconnected(uint64_t sessionId) {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    auto it = sessionToPlayerMap_.find(sessionId);
    if (it != sessionToPlayerMap_.end()) {
        uint64_t playerId = it->second;
        rateLimits_.erase(sessionId);
        sessionToPlayerMap_.erase(it);
        playerToSessionMap_.erase(playerId);
        Logger::Info("Player {} disconnected", playerId);
    }
}

uint64_t LogicCore::GetPlayerIdBySession(uint64_t sessionId) const {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    auto it = sessionToPlayerMap_.find(sessionId);
    return it != sessionToPlayerMap_.end() ? it->second : 0;
}

uint64_t LogicCore::GetSessionIdByPlayer(uint64_t playerId) const {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    auto it = playerToSessionMap_.find(playerId);
    return it != playerToSessionMap_.end() ? it->second : 0;
}

// =============== Response Methods ===============
void LogicCore::SendError(uint64_t sessionId, const std::string& message, int code) {
    nlohmann::json errorMsg = {
        {"type", "error"},
        {"message", message},
        {"code", code},
        {"timestamp", GetCurrentTimestamp()}
    };
    SendToSession(sessionId, errorMsg);
}

void LogicCore::SendSuccess(uint64_t sessionId, const std::string& message, const nlohmann::json& data) {
    nlohmann::json successMsg = {
        {"type", "success"},
        {"message", message},
        {"data", data},
        {"timestamp", GetCurrentTimestamp()}
    };
    SendToSession(sessionId, successMsg);
}

void LogicCore::SendToSession(uint64_t sessionId, const nlohmann::json& message) {
    auto& connMgr = ConnectionManager::GetInstance();
    auto session = connMgr.GetSession(sessionId);
    if (session) {
        session->Send(message);
    }
}

void LogicCore::SendBinaryToSession(uint64_t sessionId, uint16_t messageType, const std::vector<uint8_t>& data) {
    auto& connMgr = ConnectionManager::GetInstance();
    auto session = connMgr.GetSession(sessionId);
    if (session) {
        session->SendBinary(messageType, data);
    }
}

// =============== Python Scripting ===============
void LogicCore::FirePythonEvent(const std::string& eventName, const nlohmann::json& data) {
    if (pythonEnabled_) {
        Logger::Debug("LogicCore::FirePythonEvent: {}", eventName);
        pythonScripting_.FireEvent(eventName, data);
    }
}

nlohmann::json LogicCore::CallPythonFunction(const std::string& moduleName,
                                             const std::string& functionName,
                                             const nlohmann::json& args) {
    if (!pythonEnabled_) {
        return nlohmann::json();
    }
    return pythonScripting_.CallFunctionWithResult(moduleName, functionName, args);
}

void LogicCore::RegisterPythonEventHandlers() {
    if (!pythonEnabled_) return;

    pythonScripting_.RegisterEventHandler("player_login", "game_events", "on_player_login");
    pythonScripting_.RegisterEventHandler("player_move", "game_events", "on_player_move");
    pythonScripting_.RegisterEventHandler("player_attack", "game_events", "on_player_attack");
    pythonScripting_.RegisterEventHandler("player_level_up", "game_events", "on_player_level_up");
    pythonScripting_.RegisterEventHandler("player_death", "game_events", "on_player_death");
    pythonScripting_.RegisterEventHandler("player_respawn", "game_events", "on_player_respawn");
    pythonScripting_.RegisterEventHandler("custom_event", "game_events", "on_custom_event");

    Logger::Info("Python event handlers registered");
}

// =============== Event Queue ===============
void LogicCore::QueueEvent(EventCallback event) {
    std::lock_guard<std::mutex> lock(eventQueueMutex_);
    eventQueue_.push(event);
    eventQueueCV_.notify_one();
}

void LogicCore::ProcessEvents() {
    std::lock_guard<std::mutex> lock(eventQueueMutex_);
    while (!eventQueue_.empty()) {
        auto event = eventQueue_.front();
        eventQueue_.pop();
        try {
            event();
        } catch (const std::exception& e) {
            Logger::Error("Error processing event: {}", e.what());
        }
    }
}

// =============== Thread Functions ===============
void LogicCore::GameLoop() {
    Logger::Info("Game loop started");
    
    auto lastUpdate = std::chrono::steady_clock::now();
    
    while (running_) {
        try {
            auto startTime = std::chrono::steady_clock::now();
            
            auto now = std::chrono::steady_clock::now();
            auto deltaTimeMillis = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate);
            float deltaTime = deltaTimeMillis.count() / 1000.0f;
            lastUpdate = now;
            
            // Process game logic
            ProcessGameTick(deltaTime);
            ProcessEvents();
            
            auto endTime = std::chrono::steady_clock::now();
            auto processingTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
            
            if (processingTime < gameLoopInterval_.count()) {
                std::unique_lock<std::mutex> lock(gameLoopMutex_);
                gameLoopCV_.wait_for(lock, 
                    gameLoopInterval_ - std::chrono::milliseconds(processingTime),
                    [this] { return !running_; });
            } else {
                Logger::Warn("Game loop lagging: {}ms", processingTime);
            }
        } catch (const std::exception& e) {
            Logger::Error("Error in game loop: {}", e.what());
        }
    }
    
    Logger::Info("Game loop stopped");
}

void LogicCore::SpawnerLoop() {
    Logger::Info("Spawner loop started");
    
    while (running_) {
        try {
            SpawnEnemies();
            RespawnNPCs();
            SpawnResources();
            
            std::unique_lock<std::mutex> lock(spawnerMutex_);
            spawnerCV_.wait_for(lock, std::chrono::seconds(30),
                                [this] { return !running_; });
        } catch (const std::exception& e) {
            Logger::Error("Error in spawner loop: {}", e.what());
        }
    }
    
    Logger::Info("Spawner loop stopped");
}

void LogicCore::SaveLoop() {
    Logger::Info("Save loop started");
    
    while (running_) {
        try {
            playerManager_.SaveAllPlayers();
            SaveGameState();
            CleanupOldData();
            
            std::unique_lock<std::mutex> lock(saveMutex_);
            saveCV_.wait_for(lock, std::chrono::minutes(5),
                            [this] { return !running_; });
        } catch (const std::exception& e) {
            Logger::Error("Error in save loop: {}", e.what());
        }
    }
    
    Logger::Info("Save loop stopped");
}

// =============== Utility Methods ===============
bool LogicCore::CheckRateLimit(uint64_t sessionId) {
    std::lock_guard<std::mutex> lock(rateLimitMutex_);
    auto now = std::chrono::steady_clock::now();
    
    auto& limitInfo = rateLimits_[sessionId];
    auto cutoff = now - std::chrono::seconds(1);
    
    while (!limitInfo.messageTimes.empty() && limitInfo.messageTimes.front() < cutoff) {
        limitInfo.messageTimes.pop_front();
        limitInfo.messageCount--;
    }
    
    if (limitInfo.messageCount >= MAX_MESSAGES_PER_SECOND) {
        return false;
    }
    
    limitInfo.messageTimes.push_back(now);
    limitInfo.messageCount++;
    limitInfo.lastMessageTime = now;
    
    return true;
}

int64_t LogicCore::GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

// =============== Stub Methods ===============
void LogicCore::HandleLogin(uint64_t sessionId, const nlohmann::json& data) {
    if (!data.contains("username") || !data["username"].is_string() ||
        !data.contains("password") || !data["password"].is_string()) {
        SendError(sessionId, "Missing or invalid username/password", 400);
    return;
        }

        std::string username = data["username"];
        std::string password = data["password"];

        uint64_t playerId = playerManager_.AuthenticatePlayer(username, password);
        if (playerId == 0) {
            SendError(sessionId, "Invalid credentials", 401);
            return;
        }

        auto player = playerManager_.GetPlayer(playerId);
        if (!player) {
            SendError(sessionId, "Player data unavailable", 500);
            return;
        }

        OnPlayerConnected(sessionId, playerId);

        auto& world = LogicWorld::GetInstance();
        world.AddEntity(player);

        FirePythonEvent("player_login", {
            {"playerId", playerId},
            {"username", username}
        });

        SendSuccess(sessionId, "Login successful", nlohmann::json{
            {"playerId", playerId},
            {"position", player->JsonGetPosition()},
            {"health", player->GetHealth()},
            {"maxHealth", player->GetMaxHealth()},
            {"level", player->GetLevel()},
            {"experience", player->GetExperience()},
            {"inventory", player->JsonGetInventory()}
        });
}

void LogicCore::HandleCombat(uint64_t sessionId, const nlohmann::json& data) {
    uint64_t playerId = GetPlayerIdBySession(sessionId);
    if (playerId == 0) {
        SendError(sessionId, "Player not authenticated", 401);
        return;
    }

    if (!data.contains("targetId") || !data["targetId"].is_number_integer()) {
        SendError(sessionId, "Missing or invalid targetId", 400);
        return;
    }
    uint64_t targetId = data["targetId"];

    std::string attackType = data.value("attackType", "melee");

    auto player = playerManager_.GetPlayer(playerId);
    if (!player) {
        SendError(sessionId, "Player not found", 500);
        return;
    }

    auto& world = LogicWorld::GetInstance();
    auto target = world.GetEntity(targetId);
    if (!target) {
        SendError(sessionId, "Target not found", 404);
        return;
    }

    float distance = CalculateDistance(player->GetPosition(), target->GetPosition());
    if (distance > player->GetAttackRange()) {
        SendError(sessionId, "Target out of range", 400);
        return;
    }

    int damage = player->CalculateDamage(attackType);
    target->TakeDamage(damage, playerId);

    FirePythonEvent("player_attack", {
        {"playerId", playerId},
        {"targetId", targetId},
        {"damage", damage},
        {"attackType", attackType}
    });

    SendSuccess(sessionId, "Combat resolved", nlohmann::json{
        {"damageDealt", damage},
        {"targetHealth", target->GetHealth()},
                {"targetDefeated", target->IsDead()}
    });
}

void LogicCore::HandleQuest(uint64_t sessionId, const nlohmann::json& data) {
    uint64_t playerId = GetPlayerIdBySession(sessionId);
    if (playerId == 0) {
        SendError(sessionId, "Player not authenticated", 401);
        return;
    }

    if (!data.contains("questId") || !data["questId"].is_number_integer()) {
        SendError(sessionId, "Missing or invalid questId", 400);
        return;
    }
    uint64_t questId = data["questId"];

    if (!data.contains("action") || !data["action"].is_string()) {
        SendError(sessionId, "Missing or invalid action", 400);
        return;
    }
    std::string action = data["action"];

    auto player = playerManager_.GetPlayer(playerId);
    if (!player) {
        SendError(sessionId, "Player not found", 500);
        return;
    }

    auto& questManager = QuestManager::GetInstance();
    nlohmann::json result;

    if (action == "start") {
        if (questManager.CanStartQuest(playerId, questId)) {
            questManager.StartQuest(playerId, questId);
            result["status"] = "started";
        } else {
            SendError(sessionId, "Cannot start quest", 400);
            return;
        }
    } else if (action == "update") {
        std::string objective = data.value("objective", "");
        int progress = data.value("progress", 1);
        questManager.UpdateObjective(playerId, questId, objective, progress);
        result["status"] = "updated";
    } else if (action == "complete") {
        if (questManager.CanCompleteQuest(playerId, questId)) {
            auto rewards = questManager.CompleteQuest(playerId, questId);
            result["status"] = "completed";
            result["rewards"] = rewards;
        } else {
            SendError(sessionId, "Quest cannot be completed yet", 400);
            return;
        }
    } else {
        SendError(sessionId, "Unknown action: " + action, 400);
        return;
    }

    FirePythonEvent("player_quest", {
        {"playerId", playerId},
        {"questId", questId},
        {"action", action}
    });

    SendSuccess(sessionId, "Quest action processed", result);
}

void LogicCore::SpawnEnemies() {
    Logger::Debug("Spawning enemies");
}

void LogicCore::RespawnNPCs() {
    Logger::Debug("Respawning NPCs");
}

void LogicCore::SpawnResources() {
    Logger::Debug("Spawning resources");
}

void LogicCore::ProcessGameTick(float deltaTime) {
    auto& world = LogicWorld::GetInstance();
    world.UpdateEntities(deltaTime);
    ProcessEvents();
    FirePythonEvent("game_tick", {{"deltaTime", deltaTime}});
}

void LogicCore::SaveGameState() {
    try {
        nlohmann::json gameState = {
            {"server_time", GetCurrentTimestamp()},
            {"online_players", playerManager_.GetOnlinePlayerCount()}
        };
        dbManager_.SaveGameState("current_game", gameState);
        Logger::Debug("Game state saved");
    } catch (const std::exception& e) {
        Logger::Error("Failed to save game state: {}", e.what());
    }
}

void LogicCore::CleanupOldData() {
    Logger::Debug("Cleaning up old data");
}
