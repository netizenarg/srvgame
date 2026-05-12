#pragma once

#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <glm/glm.hpp>

#include "config/ConfigManager.hpp"
#include "logging/Logger.hpp"
#include "database/DbManager.hpp"
#include "game/WorldGenerator.hpp"
#include "game/GameEntity.hpp"

struct WorldConfig {
    int save_interval = 30;
    int seed = 12345;
    int view_distance = 4;
    float chunk_size = 32.0f;
    int max_active_chunks = 100;
    float terrain_scale = 100.0f;
    float max_terrain_height = 50.0f;
    float water_level = 10.0f;
    float unload_distance = 200.0f;
};

class LogicWorld {
public:

    static LogicWorld& GetInstance();

    // Initialization
    void Initialize(const WorldConfig& config);
    void Shutdown();

    // Chunk management
    std::shared_ptr<WorldChunk> GetOrCreateChunk(int chunkX, int chunkZ);
    void UnloadDistantChunks(const glm::vec3& centerPosition, float keepRadius = 200.0f);
    void GenerateWorldAroundPlayer(const glm::vec3& position, int view_distance);
    void PreloadWorldData(float radius);

    // Terrain queries
    float GetTerrainHeight(float x, float z) const;
    BiomeType GetBiomeAt(float x, float z) const;

    // Entity management
    void AddEntity(std::shared_ptr<GameEntity> entity);
    void RemoveEntity(uint64_t entityId);
    std::shared_ptr<GameEntity> GetEntity(uint64_t entityId) const;
    void UpdateEntities(float deltaTime);

    // Configuration
    void SetConfig(const WorldConfig& config) { worldConfig_ = config; }
    const WorldConfig& GetConfig() const { return worldConfig_; }

    // Statistics
    int GetActiveChunkCount() const { return activeChunkCount_; }
    void SaveChunkData();

    void SetTimeOfDay(float time); // 0.0 to 1.0
    float GetTimeOfDay() const;

private:
    LogicWorld();
    ~LogicWorld();

    std::atomic<bool> running_;
    static std::mutex instanceMutex_;
    static LogicWorld* instance_;

    WorldConfig worldConfig_;
    std::unique_ptr<WorldGenerator> worldGenerator_;
    std::unordered_map<std::string, std::shared_ptr<WorldChunk>> loadedChunks_;
    std::mutex chunksMutex_;
    std::atomic<int> activeChunkCount_{0};

    std::string GetChunkKey(int chunkX, int chunkZ) const;

    // Entity storage
    std::unordered_map<uint64_t, std::shared_ptr<GameEntity>> entities_;
    mutable std::mutex entitiesMutex_;
    std::atomic<float> currentTimeOfDay_{0.0f};  // 0.0 to 1.0
    uint32_t canary_ = 0xDEADBEEF;
};
