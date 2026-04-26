#pragma once

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <random>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/noise.hpp>

#include "logging/Logger.hpp"
#include "game/WorldChunk.hpp"

struct GenerationConfig {
    float terrainScale = 1.0f;
    float terrainHeight = 1.0f;
    int octaves = 4;
    float persistence = 0.5f;
    float lacunarity = 2.0f;
    float waterLevel = -0.5f;
    int seed = 12345;
    float forestThreshold = 0.6f;
    float mountainThreshold = 0.8f;
    float desertThreshold = -0.3f;
};

class WorldGenerator {
public:

    WorldGenerator(const GenerationConfig& config = GenerationConfig());

    std::unique_ptr<WorldChunk> GenerateChunk(int chunkX, int chunkZ);
    BiomeType GetBiomeAt(float x, float z);
    float GetTerrainHeight(float x, float z);

    void SetSeed(int seed);
    int GetSeed() const { return config_.seed; }

private:
    GenerationConfig config_;
    std::mt19937 rng_;
    std::uniform_real_distribution<float> dist_;
    mutable std::recursive_mutex rngMutex_;  // recursive to allow nested locks

    float Noise(float x, float y);
    float FractalNoise(float x, float y);
    glm::vec3 CalculateNormal(float x, float z, float height);
    void GenerateLowPolyTerrain(WorldChunk& chunk, int chunkX, int chunkZ);
    void AddTrees(WorldChunk& chunk, BiomeType biome);
    void AddRocks(WorldChunk& chunk, BiomeType biome);
    void AddWaterPlane(WorldChunk& chunk);

    // Biome-specific generation
    void GenerateForestFeatures(WorldChunk& chunk);
    void GenerateMountainFeatures(WorldChunk& chunk);
    void GenerateDesertFeatures(WorldChunk& chunk);
    void GeneratePlainsFeatures(WorldChunk& chunk);
};
