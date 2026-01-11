#pragma once

#include <glm/glm.hpp>
#include <unordered_map>
#include <memory>
#include <mutex>

#include "../../include/game/WorldChunk.hpp"
#include "../../include/game/WorldGenerator.hpp"

class LogicWorld {
public:
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

    LogicWorld();
    ~LogicWorld();

    // Initialization
    void Initialize(const WorldConfig& config);
    void Shutdown();

    // Chunk management
    std::shared_ptr<WorldChunk> GetOrCreateChunk(int chunkX, int chunkZ);
    void UnloadDistantChunks(const glm::vec3& centerPosition, float keepRadius = 200.0f);
    void GenerateWorldAroundPlayer(const glm::vec3& position, int viewDistance);
    void PreloadWorldData(float radius);
    
    // Terrain queries
    float GetTerrainHeight(float x, float z) const;
    BiomeType GetBiomeAt(float x, float z) const;
    
    // Configuration
    void SetConfig(const WorldConfig& config) { worldConfig_ = config; }
    const WorldConfig& GetConfig() const { return worldConfig_; }
    
    // Statistics
    int GetActiveChunkCount() const { return activeChunkCount_; }
    void SaveChunkData();

private:
    WorldConfig worldConfig_;
    std::unique_ptr<WorldGenerator> worldGenerator_;
    std::unordered_map<std::string, std::shared_ptr<WorldChunk>> loadedChunks_;
    std::mutex chunksMutex_;
    std::atomic<int> activeChunkCount_{0};
    
    std::string GetChunkKey(int chunkX, int chunkZ) const;
};
