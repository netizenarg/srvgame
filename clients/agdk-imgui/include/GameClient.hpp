#pragma once

#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "NetworkClient.hpp"
#include "Renderer.hpp"
#include "InputHandler.hpp"
#include "UIManager.hpp"
#include "GameState.hpp"
#include "WorldData.hpp"
#include "ClientEntityManager.hpp"
#include "EntityState.hpp"
#include "WorldChunk.hpp"
#include "BinaryProtocol.hpp"
#include "WebSocketProtocol.hpp"

enum class ConnectionProtocol {
    JSON_TEXT,      // Newline-delimited JSON (current)
    BINARY,         // Binary protocol
    WEBSOCKET       // WebSocket protocol
};

class GameClient {
public:
    GameClient();
    ~GameClient();
    
    bool Initialize(ANativeWindow* window, int width, int height);
    void Shutdown();
    
    void Update();
    void Render();
    
    // Server communication
    void ConnectToServer(const std::string& host, int port, ConnectionProtocol protocol = ConnectionProtocol::BINARY);
    void Disconnect();
    void SendMessage(const nlohmann::json& msg);
    void SendBinaryMessage(const BinaryProtocol::BinaryMessage& msg);
    
    // Game actions
    void Login(const std::string& username, const std::string& password);
    void MovePlayer(const glm::vec3& direction);
    void InteractWithEntity(uint64_t entityId);
    void UseItem(int slot);
    void SendChatMessage(const std::string& message);
    void AttackTarget(uint64_t targetId);
    void CastSpell(int spellId, const glm::vec3& target);
    
    // Game state access
    const GameState& GetGameState() const { return gameState_; }
    PlayerState GetPlayerState() const;
    std::vector<EntityState> GetVisibleEntities() const;
    WorldChunk* GetCurrentChunk() const;
    
    // UI callbacks
    void OnInventoryItemClicked(int slot);
    void OnQuestSelected(int questId);
    void OnNPCInteraction(uint64_t npcId);
    void OnTradeRequest(uint64_t playerId);
    
    // Protocol selection
    void SetProtocol(ConnectionProtocol protocol) { currentProtocol_ = protocol; }
    ConnectionProtocol GetProtocol() const { return currentProtocol_; }
    
private:
    void NetworkThread();
    void ProcessReceivedMessages();
    void HandleServerMessage(const nlohmann::json& msg);
    void HandleBinaryMessage(const BinaryProtocol::BinaryMessage& msg);
    void UpdateCamera();
    void ProcessInput();
    
    // JSON Message handlers
    void HandleLoginResponse(const nlohmann::json& data);
    void HandleWorldUpdate(const nlohmann::json& data);
    void HandleEntitySpawn(const nlohmann::json& data);
    void HandleEntityUpdate(const nlohmann::json& data);
    void HandleEntityDespawn(const nlohmann::json& data);
    void HandleInventoryUpdate(const nlohmann::json& data);
    void HandleQuestUpdate(const nlohmann::json& data);
    void HandleCombatUpdate(const nlohmann::json& data);
    void HandleChatMessage(const nlohmann::json& data);
    void HandleError(const nlohmann::json& data);
    
    // Binary Message handlers
    void HandleBinaryLoginResponse(const std::vector<uint8_t>& data);
    void HandleBinaryWorldUpdate(const std::vector<uint8_t>& data);
    void HandleBinaryEntityUpdate(const std::vector<uint8_t>& data);
    void HandleBinaryCombatEvent(const std::vector<uint8_t>& data);
    
    // Protocol conversion
    nlohmann::json BinaryToJson(const BinaryProtocol::BinaryMessage& msg) const;
    BinaryProtocol::BinaryMessage JsonToBinary(const nlohmann::json& json, uint16_t messageType) const;
    
    // Client state
    GameState gameState_;
    std::unique_ptr<NetworkClient> networkClient_;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<InputHandler> inputHandler_;
    std::unique_ptr<UIManager> uiManager_;
    
    // Threading
    std::thread networkThread_;
    std::atomic<bool> running_{false};
    std::mutex stateMutex_;
    std::queue<nlohmann::json> jsonMessageQueue_;
    std::queue<BinaryProtocol::BinaryMessage> binaryMessageQueue_;
    mutable std::mutex jsonQueueMutex_;
    mutable std::mutex binaryQueueMutex_;
    
    // Connection
    ConnectionProtocol currentProtocol_{ConnectionProtocol::BINARY};
    std::string serverHost_;
    int serverPort_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> authenticated_{false};
    uint64_t playerId_{0};
    
    // Protocol state
    BinaryProtocol::ProtocolCapabilities serverCapabilities_;
    uint32_t sequenceNumber_{0};
    std::unordered_map<uint32_t, std::function<void(const BinaryProtocol::BinaryMessage&)>> pendingResponses_;
    
    // Performance
    float deltaTime_{0.016f};
    glm::vec3 cameraPosition_{0.0f, 10.0f, 0.0f};
    glm::vec3 cameraTarget_{0.0f, 0.0f, 1.0f};
    
    // Touch controls
    glm::vec2 touchStartPos_{0.0f};
    bool isTouching_{false};
    float touchSensitivity_{0.01f};
    
    // Debug
    bool showDebugInfo_{false};
    bool showWireframe_{false};
    bool showCollision_{false};
};