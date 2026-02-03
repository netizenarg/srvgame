#include "GameClient.hpp"
#include <chrono>
#include <android/log.h>
#include "BinaryProtocol.hpp"
#include "WebSocketProtocol.hpp"
#include "EntityType.hpp"
#include "NPCType.hpp"

#define LOG_TAG "GameClient"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

GameClient::GameClient() {
    gameState_.worldData = std::make_unique<WorldData>();
    gameState_.entityManager = std::make_unique<ClientEntityManager>();
    gameState_.playerPosition = glm::vec3(0.0f, 0.0f, 0.0f);
}

GameClient::~GameClient() {
    Shutdown();
}

bool GameClient::Initialize(ANativeWindow* window, int width, int height) {
    LOGI("Initializing GameClient...");
    
    // Initialize subsystems
    renderer_ = std::make_unique<Renderer>();
    if (!renderer_->Initialize(window, width, height)) {
        LOGE("Failed to initialize renderer");
        return false;
    }
    
    inputHandler_ = std::make_unique<InputHandler>();
    uiManager_ = std::make_unique<UIManager>();
    
    // Initialize network
    networkClient_ = std::make_unique<NetworkClient>();
    networkClient_->SetProtocol(NetworkProtocol::BINARY); // Default to binary
    
    // Start network thread
    running_ = true;
    networkThread_ = std::thread(&GameClient::NetworkThread, this);
    
    LOGI("GameClient initialized successfully");
    return true;
}

void GameClient::Shutdown() {
    LOGI("Shutting down GameClient...");
    
    running_ = false;
    if (networkThread_.joinable()) {
        networkThread_.join();
    }
    
    Disconnect();
    
    if (renderer_) {
        renderer_->Shutdown();
    }
    
    LOGI("GameClient shutdown complete");
}

void GameClient::Update() {
    auto currentTime = std::chrono::steady_clock::now();
    static auto lastTime = currentTime;
    deltaTime_ = std::chrono::duration<float>(currentTime - lastTime).count();
    lastTime = currentTime;
    
    // Process input
    ProcessInput();
    
    // Process network messages
    ProcessReceivedMessages();
    
    // Update game state
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        gameState_.Update(deltaTime_);
    }
    
    // Update camera
    UpdateCamera();
    
    // Update UI
    uiManager_->Update(deltaTime_);
}

void GameClient::Render() {
    if (!renderer_) return;
    
    renderer_->BeginFrame();
    
    // Render 3D world
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        renderer_->RenderWorld(gameState_);
        renderer_->RenderEntities(gameState_);
    }
    
    // Render UI
    uiManager_->Render();
    
    renderer_->EndFrame();
}

void GameClient::ConnectToServer(const std::string& host, int port, ConnectionProtocol protocol) {
    serverHost_ = host;
    serverPort_ = port;
    currentProtocol_ = protocol;
    
    // Configure network client based on protocol
    switch (protocol) {
        case ConnectionProtocol::BINARY:
            networkClient_->SetProtocol(NetworkProtocol::BINARY);
            break;
        case ConnectionProtocol::WEBSOCKET:
            networkClient_->SetProtocol(NetworkProtocol::WEBSOCKET);
            break;
        case ConnectionProtocol::JSON_TEXT:
            networkClient_->SetProtocol(NetworkProtocol::JSON_TEXT);
            break;
    }
    
    if (networkClient_->Connect(host, port)) {
        connected_ = true;
        LOGI("Connected to server %s:%d with protocol %d", host.c_str(), port, static_cast<int>(protocol));
        
        // Protocol negotiation for binary
        if (protocol == ConnectionProtocol::BINARY) {
            BinaryProtocol::BinaryMessage negotiationMsg;
            negotiationMsg.header = BinaryProtocol::NetworkHeader(
                BinaryProtocol::MESSAGE_TYPE_PROTOCOL_NEGOTIATION,
                sequenceNumber_++
            );
            
            BinaryProtocol::BinaryWriter writer;
            writer.WriteUInt8(BinaryProtocol::CURRENT_PROTOCOL_VERSION);
            writer.WriteUInt32(BinaryProtocol::MAX_MESSAGE_SIZE);
            
            negotiationMsg.data = writer.GetBuffer();
            negotiationMsg.header.length = static_cast<uint32_t>(negotiationMsg.data.size());
            negotiationMsg.header.checksum = BinaryProtocol::CalculateCRC32(
                negotiationMsg.data.data(), negotiationMsg.data.size());
            
            SendBinaryMessage(negotiationMsg);
        }
    } else {
        LOGE("Failed to connect to server");
    }
}

void GameClient::Disconnect() {
    if (networkClient_) {
        networkClient_->Disconnect();
    }
    connected_ = false;
    authenticated_ = false;
}

void GameClient::SendMessage(const nlohmann::json& msg) {
    if (!connected_ || !networkClient_) return;
    
    switch (currentProtocol_) {
        case ConnectionProtocol::JSON_TEXT:
            networkClient_->SendJson(msg);
            break;
        case ConnectionProtocol::BINARY: {
            // Convert JSON to binary message
            uint16_t messageType = MESSAGE_TYPE_CUSTOM_EVENT; // Default
            if (msg.contains("type")) {
                std::string typeStr = msg["type"];
                // Map JSON message types to binary types
                if (typeStr == "login") messageType = BinaryProtocol::MESSAGE_TYPE_AUTHENTICATION;
                else if (typeStr == "movement") messageType = BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION;
                // Add more mappings as needed
            }
            
            BinaryProtocol::BinaryMessage binaryMsg = JsonToBinary(msg, messageType);
            SendBinaryMessage(binaryMsg);
            break;
        }
        case ConnectionProtocol::WEBSOCKET:
            networkClient_->SendWebSocket(msg);
            break;
    }
}

void GameClient::SendBinaryMessage(const BinaryProtocol::BinaryMessage& msg) {
    if (connected_ && networkClient_) {
        networkClient_->SendBinary(msg);
    }
}

void GameClient::Login(const std::string& username, const std::string& password) {
    if (currentProtocol_ == ConnectionProtocol::BINARY) {
        // Send binary authentication message
        BinaryProtocol::BinaryMessage authMsg;
        authMsg.header = BinaryProtocol::NetworkHeader(
            BinaryProtocol::MESSAGE_TYPE_AUTHENTICATION,
            sequenceNumber_++
        );
        
        BinaryProtocol::BinaryWriter writer;
        writer.WriteString(username);
        writer.WriteString(password);
        
        authMsg.data = writer.GetBuffer();
        authMsg.header.length = static_cast<uint32_t>(authMsg.data.size());
        authMsg.header.checksum = BinaryProtocol::CalculateCRC32(
            authMsg.data.data(), authMsg.data.size());
        
        SendBinaryMessage(authMsg);
    } else {
        // JSON login
        nlohmann::json msg = {
            {"type", "login"},
            {"data", {
                {"username", username},
                {"password", password}
            }}
        };
        SendMessage(msg);
    }
}

void GameClient::MovePlayer(const glm::vec3& direction) {
    if (!authenticated_) return;
    
    if (currentProtocol_ == ConnectionProtocol::BINARY) {
        BinaryProtocol::BinaryMessage moveMsg;
        moveMsg.header = BinaryProtocol::NetworkHeader(
            BinaryProtocol::MESSAGE_TYPE_PLAYER_POSITION,
            sequenceNumber_++
        );
        
        BinaryProtocol::BinaryWriter writer;
        writer.WriteUInt64(playerId_);
        writer.WriteVector3(direction);
        writer.WriteUInt32(static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()));
        
        moveMsg.data = writer.GetBuffer();
        moveMsg.header.length = static_cast<uint32_t>(moveMsg.data.size());
        moveMsg.header.checksum = BinaryProtocol::CalculateCRC32(
            moveMsg.data.data(), moveMsg.data.size());
        
        SendBinaryMessage(moveMsg);
    } else {
        nlohmann::json msg = {
            {"type", "movement"},
            {"data", {
                {"playerId", playerId_},
                {"direction", {direction.x, direction.y, direction.z}},
                {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()}
            }}
        };
        SendMessage(msg);
    }
}

void GameClient::NetworkThread() {
    while (running_) {
        if (connected_ && networkClient_) {
            // Handle different protocol types
            switch (currentProtocol_) {
                case ConnectionProtocol::JSON_TEXT: {
                    auto messages = networkClient_->ReceiveJson();
                    for (const auto& msg : messages) {
                        std::lock_guard<std::mutex> lock(jsonQueueMutex_);
                        jsonMessageQueue_.push(msg);
                    }
                    break;
                }
                case ConnectionProtocol::BINARY: {
                    auto messages = networkClient_->ReceiveBinary();
                    for (const auto& msg : messages) {
                        std::lock_guard<std::mutex> lock(binaryQueueMutex_);
                        binaryMessageQueue_.push(msg);
                    }
                    break;
                }
                case ConnectionProtocol::WEBSOCKET: {
                    auto messages = networkClient_->ReceiveWebSocket();
                    for (const auto& msg : messages) {
                        std::lock_guard<std::mutex> lock(jsonQueueMutex_);
                        jsonMessageQueue_.push(msg);
                    }
                    break;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void GameClient::ProcessReceivedMessages() {
    // Process JSON messages
    std::queue<nlohmann::json> jsonMessages;
    {
        std::lock_guard<std::mutex> lock(jsonQueueMutex_);
        jsonMessages.swap(jsonMessageQueue_);
    }
    
    while (!jsonMessages.empty()) {
        auto msg = jsonMessages.front();
        jsonMessages.pop();
        HandleServerMessage(msg);
    }
    
    // Process binary messages
    std::queue<BinaryProtocol::BinaryMessage> binaryMessages;
    {
        std::lock_guard<std::mutex> lock(binaryQueueMutex_);
        binaryMessages.swap(binaryMessageQueue_);
    }
    
    while (!binaryMessages.empty()) {
        auto msg = binaryMessages.front();
        binaryMessages.pop();
        HandleBinaryMessage(msg);
    }
}

void GameClient::HandleServerMessage(const nlohmann::json& msg) {
    try {
        std::string msgType = msg["type"];
        
        if (msgType == "login_response") {
            HandleLoginResponse(msg["data"]);
        }
        else if (msgType == "world_update") {
            HandleWorldUpdate(msg["data"]);
        }
        else if (msgType == "entity_spawn") {
            HandleEntitySpawn(msg["data"]);
        }
        else if (msgType == "entity_update") {
            HandleEntityUpdate(msg["data"]);
        }
        else if (msgType == "entity_despawn") {
            HandleEntityDespawn(msg["data"]);
        }
        else if (msgType == "inventory_update") {
            HandleInventoryUpdate(msg["data"]);
        }
        else if (msgType == "quest_update") {
            HandleQuestUpdate(msg["data"]);
        }
        else if (msgType == "combat_update") {
            HandleCombatUpdate(msg["data"]);
        }
        else if (msgType == "chat_message") {
            HandleChatMessage(msg["data"]);
        }
        else if (msgType == "error") {
            HandleError(msg["data"]);
        }
        else if (msgType == "protocol_negotiation") {
            // Handle protocol negotiation response
            if (msg["data"]["success"] == true) {
                LOGI("Protocol negotiation successful");
            }
        }
    }
    catch (const std::exception& e) {
        LOGE("Error handling server message: %s", e.what());
    }
}

void GameClient::HandleBinaryMessage(const BinaryProtocol::BinaryMessage& msg) {
    try {
        // Verify checksum
        uint32_t calculatedChecksum = BinaryProtocol::CalculateCRC32(
            msg.data.data(), msg.data.size());
        
        if (msg.header.checksum != 0 && msg.header.checksum != calculatedChecksum) {
            LOGE("Checksum mismatch for message type %d", msg.header.message_type);
            return;
        }
        
        // Handle based on message type
        switch (msg.header.message_type) {
            case BinaryProtocol::MESSAGE_TYPE_SUCCESS:
                HandleBinaryLoginResponse(msg.data);
                break;
            case BinaryProtocol::MESSAGE_TYPE_CHUNK_DATA:
                HandleBinaryWorldUpdate(msg.data);
                break;
            case BinaryProtocol::MESSAGE_TYPE_ENTITY_UPDATE:
            case BinaryProtocol::MESSAGE_TYPE_ENTITY_BATCH_UPDATE:
            case BinaryProtocol::MESSAGE_TYPE_ENTITY_SPAWN:
                HandleBinaryEntityUpdate(msg.data);
                break;
            case BinaryProtocol::MESSAGE_TYPE_COMBAT_EVENT:
            case BinaryProtocol::MESSAGE_TYPE_DAMAGE_EVENT:
                HandleBinaryCombatEvent(msg.data);
                break;
            case BinaryProtocol::MESSAGE_TYPE_HEALTH_UPDATE:
                // Update entity health
                break;
            case BinaryProtocol::MESSAGE_TYPE_CHAT_MESSAGE:
                // Handle chat
                break;
            case BinaryProtocol::MESSAGE_TYPE_PROTOCOL_NEGOTIATION:
                // Store server capabilities
                serverCapabilities_ = BinaryProtocol::ProtocolCapabilities::Deserialize(
                    msg.data.data(), msg.data.size());
                LOGI("Server protocol version: %d", serverCapabilities_.version);
                break;
            default:
                LOGI("Received binary message type: %d", msg.header.message_type);
                break;
        }
    }
    catch (const std::exception& e) {
        LOGE("Error handling binary message: %s", e.what());
    }
}

void GameClient::HandleLoginResponse(const nlohmann::json& data) {
    if (data["success"] == true) {
        authenticated_ = true;
        playerId_ = data["playerId"];
        LOGI("Login successful, playerId: %llu", playerId_);
        
        // Request initial world data
        nlohmann::json request = {
            {"type", "world_request"},
            {"data", {
                {"playerId", playerId_},
                {"position", {0.0f, 0.0f, 0.0f}}
            }}
        };
        SendMessage(request);
    }
}

void GameClient::HandleBinaryLoginResponse(const std::vector<uint8_t>& data) {
    BinaryProtocol::BinaryReader reader(data.data(), data.size());
    
    bool success = reader.ReadUInt8() != 0;
    if (success) {
        authenticated_ = true;
        playerId_ = reader.ReadUInt64();
        LOGI("Binary login successful, playerId: %llu", playerId_);
        
        // Request world data using binary protocol
        BinaryProtocol::BinaryMessage worldRequest;
        worldRequest.header = BinaryProtocol::NetworkHeader(
            BinaryProtocol::MESSAGE_TYPE_CHUNK_REQUEST,
            sequenceNumber_++
        );
        
        BinaryProtocol::BinaryWriter writer;
        writer.WriteUInt64(playerId_);
        writer.WriteVector3(glm::vec3(0.0f, 0.0f, 0.0f));
        writer.WriteUInt32(5); // Request radius
        
        worldRequest.data = writer.GetBuffer();
        worldRequest.header.length = static_cast<uint32_t>(worldRequest.data.size());
        worldRequest.header.checksum = BinaryProtocol::CalculateCRC32(
            worldRequest.data.data(), worldRequest.data.size());
        
        SendBinaryMessage(worldRequest);
    }
}

void GameClient::HandleWorldUpdate(const nlohmann::json& data) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    // Update chunks
    for (const auto& chunkData : data["chunks"]) {
        int chunkX = chunkData["chunkX"];
        int chunkZ = chunkData["chunkZ"];
        
        auto chunk = std::make_unique<WorldChunk>(chunkX, chunkZ);
        chunk->Deserialize(chunkData);
        
        gameState_.worldData->AddChunk(std::move(chunk));
    }
    
    // Update player position
    if (data.contains("playerPosition")) {
        auto pos = data["playerPosition"];
        gameState_.playerPosition = glm::vec3(pos[0], pos[1], pos[2]);
    }
}

void GameClient::HandleBinaryWorldUpdate(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    BinaryProtocol::BinaryReader reader(data.data(), data.size());
    
    // Read number of chunks
    uint32_t chunkCount = reader.ReadUInt32();
    
    for (uint32_t i = 0; i < chunkCount; i++) {
        int32_t chunkX = reader.ReadInt32();
        int32_t chunkZ = reader.ReadInt32();
        
        auto chunk = std::make_unique<WorldChunk>(chunkX, chunkZ);
        
        // Read chunk data
        uint32_t chunkSize = reader.ReadUInt32();
        std::vector<uint8_t> chunkData = reader.ReadBytes(chunkSize);
        
        // Deserialize chunk from binary
        // Note: This requires WorldChunk to have a binary deserialization method
        // chunk->DeserializeBinary(chunkData);
        
        gameState_.worldData->AddChunk(std::move(chunk));
    }
    
    // Read player position if present
    if (reader.Remaining() >= 12) { // 3 floats
        float posX = reader.ReadFloat();
        float posY = reader.ReadFloat();
        float posZ = reader.ReadFloat();
        gameState_.playerPosition = glm::vec3(posX, posY, posZ);
    }
}

void GameClient::HandleEntitySpawn(const nlohmann::json& data) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    EntityState entity;
    entity.id = data["entityId"];
    entity.type = static_cast<EntityType>(data["entityType"]);
    entity.position = glm::vec3(
        data["position"][0],
        data["position"][1],
        data["position"][2]
    );
    entity.rotation = glm::vec3(
        data["rotation"][0],
        data["rotation"][1],
        data["rotation"][2]
    );
    
    if (data.contains("npcType")) {
        entity.npcType = static_cast<NPCType>(data["npcType"]);
    }
    
    gameState_.entityManager->AddEntity(entity);
}

void GameClient::HandleBinaryEntityUpdate(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    BinaryProtocol::BinaryReader reader(data.data(), data.size());
    
    // Read entity count
    uint32_t entityCount = reader.ReadUInt32();
    
    for (uint32_t i = 0; i < entityCount; i++) {
        EntityState entity;
        entity.id = reader.ReadUInt64();
        entity.type = static_cast<EntityType>(reader.ReadUInt8());
        entity.position = reader.ReadVector3();
        entity.rotation = reader.ReadVector3();
        
        // Read additional data based on entity type
        if (entity.type == EntityType::NPC) {
            entity.npcType = static_cast<NPCType>(reader.ReadUInt8());
        }
        
        // Update or add entity
        if (gameState_.entityManager->HasEntity(entity.id)) {
            gameState_.entityManager->UpdateEntity(entity);
        } else {
            gameState_.entityManager->AddEntity(entity);
        }
    }
}

void GameClient::ProcessInput() {
    if (!inputHandler_) return;
    
    auto inputState = inputHandler_->GetState();
    
    // Handle movement
    glm::vec3 moveDir(0.0f);
    if (inputState.moveForward) moveDir.z -= 1.0f;
    if (inputState.moveBackward) moveDir.z += 1.0f;
    if (inputState.moveLeft) moveDir.x -= 1.0f;
    if (inputState.moveRight) moveDir.x += 1.0f;
    
    if (glm::length(moveDir) > 0.0f) {
        moveDir = glm::normalize(moveDir);
        MovePlayer(moveDir);
    }
    
    // Handle camera with touch
    if (inputState.touching) {
        glm::vec2 delta = inputState.touchDelta * touchSensitivity_;
        cameraTarget = glm::rotate(cameraTarget, delta.x, glm::vec3(0.0f, 1.0f, 0.0f));
        
        // Update UI with touch input
        uiManager_->HandleTouch(inputState.touchPos, inputState.touchDelta, 
                               inputState.touchStarted, inputState.touchEnded);
    }
}

void GameClient::UpdateCamera() {
    // Update camera based on player position and input
    if (authenticated_) {
        cameraPosition_ = gameState_.playerPosition + glm::vec3(0.0f, 5.0f, 5.0f);
        cameraTarget_ = gameState_.playerPosition;
    }
}

// Additional missing implementations
void GameClient::HandleEntityUpdate(const nlohmann::json& data) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    uint64_t entityId = data["entityId"];
    if (gameState_.entityManager->HasEntity(entityId)) {
        EntityState entity = gameState_.entityManager->GetEntity(entityId);
        
        if (data.contains("position")) {
            auto pos = data["position"];
            entity.position = glm::vec3(pos[0], pos[1], pos[2]);
        }
        if (data.contains("rotation")) {
            auto rot = data["rotation"];
            entity.rotation = glm::vec3(rot[0], rot[1], rot[2]);
        }
        if (data.contains("health")) {
            entity.health = data["health"];
        }
        
        gameState_.entityManager->UpdateEntity(entity);
    }
}

void GameClient::HandleEntityDespawn(const nlohmann::json& data) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    uint64_t entityId = data["entityId"];
    gameState_.entityManager->RemoveEntity(entityId);
}

void GameClient::HandleInventoryUpdate(const nlohmann::json& data) {
    // Update player inventory
    if (data.contains("inventory")) {
        gameState_.playerInventory = data["inventory"];
    }
}

void GameClient::HandleQuestUpdate(const nlohmann::json& data) {
    // Update quest log
    if (data.contains("quests")) {
        gameState_.quests = data["quests"];
    }
}

void GameClient::HandleCombatUpdate(const nlohmann::json& data) {
    // Handle combat events
    LOGI("Combat update received");
}

void GameClient::HandleChatMessage(const nlohmann::json& data) {
    // Add chat message to UI
    std::string sender = data["sender"];
    std::string message = data["message"];
    uiManager_->AddChatMessage(sender, message);
}

void GameClient::HandleError(const nlohmann::json& data) {
    std::string errorMsg = data["message"];
    LOGE("Server error: %s", errorMsg.c_str());
    uiManager_->ShowError(errorMsg);
}

void GameClient::HandleBinaryCombatEvent(const std::vector<uint8_t>& data) {
    BinaryProtocol::BinaryReader reader(data.data(), data.size());
    
    uint64_t attackerId = reader.ReadUInt64();
    uint64_t targetId = reader.ReadUInt64();
    uint32_t damage = reader.ReadUInt32();
    uint8_t attackType = reader.ReadUInt8();
    
    LOGI("Combat: %llu attacks %llu for %d damage", attackerId, targetId, damage);
    
    // Update entity health if target is visible
    if (gameState_.entityManager->HasEntity(targetId)) {
        EntityState target = gameState_.entityManager->GetEntity(targetId);
        target.health -= damage;
        gameState_.entityManager->UpdateEntity(target);
    }
}

nlohmann::json GameClient::BinaryToJson(const BinaryProtocol::BinaryMessage& msg) const {
    nlohmann::json json;
    
    // Convert binary message to JSON based on message type
    switch (msg.header.message_type) {
        case BinaryProtocol::MESSAGE_TYPE_AUTHENTICATION:
            json["type"] = "login_response";
            break;
        // Add more conversions as needed
        default:
            json["type"] = "binary_message";
            json["message_type"] = msg.header.message_type;
            json["data"] = nlohmann::json::binary(msg.data);
            break;
    }
    
    return json;
}

BinaryProtocol::BinaryMessage GameClient::JsonToBinary(const nlohmann::json& json, uint16_t messageType) const {
    BinaryProtocol::BinaryMessage msg;
    msg.header = BinaryProtocol::NetworkHeader(messageType, sequenceNumber_++);
    
    BinaryProtocol::BinaryWriter writer;
    
    // Convert JSON to binary based on message type
    switch (messageType) {
        case BinaryProtocol::MESSAGE_TYPE_AUTHENTICATION:
            writer.WriteString(json["data"]["username"]);
            writer.WriteString(json["data"]["password"]);
            break;
        // Add more conversions as needed
        default:
            // Default: write JSON as string
            writer.WriteString(json.dump());
            break;
    }
    
    msg.data = writer.GetBuffer();
    msg.header.length = static_cast<uint32_t>(msg.data.size());
    msg.header.checksum = BinaryProtocol::CalculateCRC32(
        msg.data.data(), msg.data.size());
    
    return msg;
}

// Implement other missing methods
PlayerState GameClient::GetPlayerState() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    PlayerState state;
    state.position = gameState_.playerPosition;
    state.health = 100; // Default
    // Add more player state fields
    return state;
}

std::vector<EntityState> GameClient::GetVisibleEntities() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return gameState_.entityManager->GetAllEntities();
}

WorldChunk* GameClient::GetCurrentChunk() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    // Calculate current chunk from player position
    int chunkX = static_cast<int>(gameState_.playerPosition.x) / 16;
    int chunkZ = static_cast<int>(gameState_.playerPosition.z) / 16;
    return gameState_.worldData->GetChunk(chunkX, chunkZ);
}

void GameClient::InteractWithEntity(uint64_t entityId) {
    if (!authenticated_) return;
    
    nlohmann::json msg = {
        {"type", "interact"},
        {"data", {
            {"playerId", playerId_},
            {"entityId", entityId}
        }}
    };
    SendMessage(msg);
}

void GameClient::UseItem(int slot) {
    if (!authenticated_) return;
    
    nlohmann::json msg = {
        {"type", "use_item"},
        {"data", {
            {"playerId", playerId_},
            {"slot", slot}
        }}
    };
    SendMessage(msg);
}

void GameClient::SendChatMessage(const std::string& message) {
    if (!authenticated_) return;
    
    nlohmann::json msg = {
        {"type", "chat"},
        {"data", {
            {"playerId", playerId_},
            {"message", message}
        }}
    };
    SendMessage(msg);
}

void GameClient::AttackTarget(uint64_t targetId) {
    if (!authenticated_) return;
    
    nlohmann::json msg = {
        {"type", "attack"},
        {"data", {
            {"attackerId", playerId_},
            {"targetId", targetId}
        }}
    };
    SendMessage(msg);
}

void GameClient::CastSpell(int spellId, const glm::vec3& target) {
    if (!authenticated_) return;
    
    nlohmann::json msg = {
        {"type", "cast_spell"},
        {"data", {
            {"casterId", playerId_},
            {"spellId", spellId},
            {"target", {target.x, target.y, target.z}}
        }}
    };
    SendMessage(msg);
}

void GameClient::OnInventoryItemClicked(int slot) {
    // Handle UI inventory click
    uiManager_->OnInventoryClick(slot);
}

void GameClient::OnQuestSelected(int questId) {
    // Handle quest selection
    uiManager_->OnQuestSelect(questId);
}

void GameClient::OnNPCInteraction(uint64_t npcId) {
    // Handle NPC interaction
    InteractWithEntity(npcId);
}

void GameClient::OnTradeRequest(uint64_t playerId) {
    // Handle trade request
    nlohmann::json msg = {
        {"type", "trade_request"},
        {"data", {
            {"fromPlayerId", playerId_},
            {"toPlayerId", playerId}
        }}
    };
    SendMessage(msg);
}