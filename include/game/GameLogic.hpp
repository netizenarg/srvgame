#pragma once

#include <nlohmann/json.hpp>
#include <functional>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <cmath>
#include <random>
#include <condition_variable>
#include <queue>
#include <glm/glm.hpp>

#include "../../include/game/LootItem.hpp"
#include "../../include/game/PlayerManager.hpp"
#include "../../include/game/WorldChunk.hpp"
#include "../../include/game/NPCSystem.hpp"
#include "../../include/game/MobSystem.hpp"
#include "../../include/game/CollisionSystem.hpp"
#include "../../include/game/EntityManager.hpp"
#include "../../include/scripting/PythonScripting.hpp"
#include "../../include/network/ConnectionManager.hpp"
#include "../../include/network/BinaryProtocol.hpp"
#include "../../include/network/PredictionSystem.hpp"
#include "../../include/game/RAIIThread.hpp"

class GameLogic {
public:
    using MessageHandler = std::function<void(uint64_t sessionId, const nlohmann::json&)>;
    using BinaryMessageHandler = std::function<void(uint64_t sessionId, uint16_t messageType, const std::vector<uint8_t>&)>;

    // World configuration structure
    struct WorldConfig {
        int seed = 12345;
        int viewDistance = 4;
        float chunkSize = 32.0f;
        int maxActiveChunks = 100;
        float terrainScale = 100.0f;
        float maxTerrainHeight = 50.0f;
        float waterLevel = 10.0f;
        float chunkUnloadDistance = 200.0f;
    };

    // Rate limiting structure
    struct RateLimitInfo {
        std::chrono::steady_clock::time_point lastMessageTime;
        int messageCount = 0;
        std::deque<std::chrono::steady_clock::time_point> messageTimes;
    };

    static GameLogic& GetInstance();

    // Core lifecycle methods
    void Initialize();
    void Shutdown();

    // Message handling system
    void HandleMessage(uint64_t sessionId, const nlohmann::json& msg);
    void HandleBinaryMessage(uint64_t sessionId, uint16_t messageType, const std::vector<uint8_t>& data);
    void RegisterHandler(const std::string& messageType, MessageHandler handler);
    void RegisterBinaryHandler(uint16_t messageType, BinaryMessageHandler handler);

    // Player session management
    void OnPlayerConnected(uint64_t sessionId, uint64_t playerId);
    void OnPlayerDisconnected(uint64_t sessionId);
    uint64_t GetPlayerIdBySession(uint64_t sessionId) const;
    uint64_t GetSessionIdByPlayer(uint64_t playerId) const;

    // 3D World management methods
    std::shared_ptr<WorldChunk> GetOrCreateChunk(int chunkX, int chunkZ);
    void UnloadDistantChunks(const glm::vec3& centerPosition, float keepRadius = 200.0f);
    void GenerateWorldAroundPlayer(uint64_t playerId, const glm::vec3& position);
    void PreloadWorldData(float radius);
    float GetTerrainHeight(float x, float z) const;
    BiomeType GetBiomeAt(float x, float z) const;

    // NPC and Entity management
    uint64_t SpawnNPC(NPCType type, const glm::vec3& position, uint64_t ownerId = 0);
    void DespawnNPC(uint64_t npcId);
    void UpdateNPCs(float deltaTime);
    NPCEntity* GetNPCEntity(uint64_t npcId);
    GameEntity* GetEntity(uint64_t entityId);
    PlayerEntity* GetPlayerEntity(uint64_t playerId);

    // Collision management
    CollisionResult CheckCollision(const glm::vec3& position, float radius, uint64_t excludeEntityId = 0);
    bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit);
    void UpdateCollisions(float deltaTime);

    // Loot and Inventory system
    void CreateLootEntity(const glm::vec3& position, std::shared_ptr<LootItem> item, int quantity);
    void HandleLootPickup(uint64_t sessionId, const nlohmann::json& data);
    void HandleInventoryMove(uint64_t sessionId, const nlohmann::json& data);
    void HandleItemUse(uint64_t sessionId, const nlohmann::json& data);
    void HandleItemDrop(uint64_t sessionId, const nlohmann::json& data);
    void HandleTradeRequest(uint64_t sessionId, const nlohmann::json& data);
    void HandleGoldTransaction(uint64_t sessionId, const nlohmann::json& data);

    // Binary protocol support
    void SendBinaryToSession(uint64_t sessionId, uint16_t messageType, const std::vector<uint8_t>& data);
    void BroadcastBinaryToNearbyPlayers(const glm::vec3& position, uint16_t messageType, 
                                        const std::vector<uint8_t>& data, float radius = 50.0f);

    // Core game message handlers (for backward compatibility)
    void HandleLogin(uint64_t sessionId, const nlohmann::json& data);
    void HandleMovement(uint64_t sessionId, const nlohmann::json& data);
    void HandleChat(uint64_t sessionId, const nlohmann::json& data);
    void HandleCombat(uint64_t sessionId, const nlohmann::json& data);
    void HandleQuest(uint64_t sessionId, const nlohmann::json& data);

    // 3D World specific handlers
    void HandleWorldChunkRequest(uint64_t sessionId, const nlohmann::json& data);
    void HandlePlayerPositionUpdate(uint64_t sessionId, const nlohmann::json& data);
    void HandleNPCInteraction(uint64_t sessionId, const nlohmann::json& data);
    void HandleCollisionCheck(uint64_t sessionId, const nlohmann::json& data);
    void HandleEntitySpawnRequest(uint64_t sessionId, const nlohmann::json& data);
    void HandleFamiliarCommand(uint64_t sessionId, const nlohmann::json& data);

    // Game loop and events
    void ProcessGameTick(float deltaTime);
    void UpdateWorld(float deltaTime);
    void SpawnEnemies();
    void ProcessCombat();
    void ProcessEvents();

    // Response methods
    void SendError(uint64_t sessionId, const std::string& message, int code = 0);
    void SendSuccess(uint64_t sessionId, const std::string& message, const nlohmann::json& data = {});
    void SendToSession(uint64_t sessionId, const nlohmann::json& message);
    void BroadcastToNearbyPlayers(const glm::vec3& position, const nlohmann::json& message, float radius = 50.0f);
    void SyncNearbyEntitiesToPlayer(uint64_t sessionId, const glm::vec3& position);

    // Python scripting integration
    void FirePythonEvent(const std::string& eventName, const nlohmann::json& data);
    nlohmann::json CallPythonFunction(const std::string& moduleName, const std::string& functionName, 
                                      const nlohmann::json& args);
    void RegisterPythonEventHandlers();

    // Utility methods
    bool IsRunning() const { return running_; }
    void SetWorldConfig(const WorldConfig& config) { worldConfig_ = config; }
    const WorldConfig& GetWorldConfig() const { return worldConfig_; }
    int64_t GetCurrentTimestamp();

private:
    GameLogic();
    ~GameLogic();

    // Singleton instance
    static std::mutex instanceMutex_;
    static GameLogic* instance_;

    // Threading and synchronization
    std::atomic<bool> running_{false};
    RAIIThread gameLoopThread_;  // Changed from std::thread to RAIIThread
    RAIIThread spawnerThread_;   // Changed from std::thread to RAIIThread
    RAIIThread saveThread_;      // Changed from std::thread to RAIIThread
    std::condition_variable gameLoopCV_;
    std::condition_variable spawnerCV_;
    std::condition_variable saveCV_;
    std::mutex gameLoopMutex_;
    std::mutex spawnerMutex_;
    std::mutex saveMutex_;
    std::chrono::milliseconds gameLoopInterval_{16}; // ~60 FPS

    // Message handling
    std::unordered_map<std::string, MessageHandler> messageHandlers_;
    std::unordered_map<uint16_t, BinaryMessageHandler> binaryHandlers_;
    std::mutex handlersMutex_;

    // Rate limiting
    std::unordered_map<uint64_t, RateLimitInfo> rateLimits_;
    std::mutex rateLimitMutex_;
    const int MAX_MESSAGES_PER_SECOND = 100;
    const int MAX_BURST_SIZE = 20;

    // Session management
    std::unordered_map<uint64_t, uint64_t> sessionToPlayerMap_; // sessionId -> playerId
    std::unordered_map<uint64_t, uint64_t> playerToSessionMap_; // playerId -> sessionId
    std::mutex sessionMutex_;

    // 3D World systems
    WorldConfig worldConfig_;
    std::unique_ptr<WorldGenerator> worldGenerator_;
    std::unordered_map<std::string, std::shared_ptr<WorldChunk>> loadedChunks_;
    std::mutex chunksMutex_;
    std::atomic<int> activeChunkCount_{0};

    // NPC and Entity systems
    std::unique_ptr<NPCManager> npcManager_;
    std::unordered_map<uint64_t, std::unique_ptr<NPCEntity>> npcEntities_;
    std::mutex npcMutex_;
    std::atomic<int> activeNPCCount_{0};
    MobSystem& mobSystem_;
    EntityManager& entityManager_;

    // Collision system
    std::unique_ptr<CollisionSystem> collisionSystem_;

    // Loot and Inventory systems
    std::unique_ptr<InventorySystem> inventorySystem_;
    std::unique_ptr<LootTableManager> lootTableManager_;

    // Player management
    PlayerManager& playerManager_;

    // Database client
    CitusClient& dbClient_;

    // Python scripting
    PythonScripting::PythonScripting& pythonScripting_;
    std::unique_ptr<PythonScripting::ScriptHotReloader> scriptHotReloader_;
    bool pythonEnabled_{false};

    // Random number generator
    std::mt19937 rng_;

    // Event queue for async processing
    std::queue<std::function<void()>> eventQueue_;
    std::mutex eventQueueMutex_;
    std::condition_variable eventQueueCV_;

    // Private initialization methods
    void InitializeWorldSystem();
    void InitializeNPCSystem();
    void InitializeMobSystem();
    void InitializeCollisionSystem();
    void LoadGameData();
    void SaveGameState();
    void SaveChunkData();
    void CleanupOldData();

    // Thread functions
    void GameLoop();
    void SpawnerLoop();
    void SaveLoop();

    // Helper methods
    void RegisterDefaultHandlers();
    void RegisterWorldHandlers();
    bool CheckRateLimit(uint64_t sessionId);
    void DisconnectAllPlayers();
    void SyncEntityStateToPlayer(uint64_t sessionId, uint64_t entityId);
    void SendChunkDataToPlayer(uint64_t sessionId, WorldChunk* chunk);
    void RespawnNPCs();
    void SpawnResources();
    void CheckQuestCompletions();
};
