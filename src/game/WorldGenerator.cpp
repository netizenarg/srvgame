#include "game/WorldGenerator.hpp"

WorldGenerator::WorldGenerator(const GenerationConfig& config)
    : config_(config), rng_(config.seed), dist_(-1.0f, 1.0f) {
}

std::unique_ptr<WorldChunk> WorldGenerator::GenerateChunk(int chunkX, int chunkZ) {
    auto chunk = std::make_unique<WorldChunk>(chunkX, chunkZ);
    const int chunkSize = WorldChunk::DEFAULT_SIZE;
    const float spacing = WorldChunk::DEFAULT_SPACING;
    const float physWidth = (chunkSize - 1) * spacing;
    const float physHeight = (chunkSize - 1) * spacing;
    const float worldOriginX = chunkX * physWidth;
    const float worldOriginZ = chunkZ * physHeight;
    BiomeType biome = GetBiomeAt(worldOriginX + physWidth / 2.0f, worldOriginZ + physHeight / 2.0f);
    chunk->SetBiome(biome);
    chunk->vertices_.clear();
    chunk->triangles_.clear();
    chunk->heightmap_.resize(chunkSize * chunkSize);
    for (int z = 0; z < chunkSize; ++z) {
        for (int x = 0; x < chunkSize; ++x) {
            float wx = worldOriginX + x * spacing;
            float wz = worldOriginZ + z * spacing;
            float wy = GetTerrainHeight(wx, wz);
            chunk->heightmap_[z * chunkSize + x] = wy;
            glm::vec3 normal(0.0f, 1.0f, 0.0f);
            glm::vec3 color = chunk->GetBiomeColor(biome, wy / config_.terrainHeight);
            chunk->vertices_.emplace_back(glm::vec3(wx, wy, wz), normal, color, glm::vec2(0.0f, 0.0f));
        }
    }
    for (size_t i = 0; i < chunk->vertices_.size(); ++i) {
        int x = i % chunkSize;
        int z = i / chunkSize;
        if (x > 0 && x < chunkSize - 1 && z > 0 && z < chunkSize - 1) {
            float hx1 = chunk->vertices_[(z) * chunkSize + (x + 1)].position.y;
            float hx2 = chunk->vertices_[(z) * chunkSize + (x - 1)].position.y;
            float hz1 = chunk->vertices_[(z + 1) * chunkSize + x].position.y;
            float hz2 = chunk->vertices_[(z - 1) * chunkSize + x].position.y;
            float dx = hx1 - hx2;
            float dz = hz1 - hz2;
            glm::vec3 n(-dx, 2.0f * spacing, -dz);
            float len = glm::length(n);
            if (len > 0.0f) {
                n /= len;
            }
            chunk->vertices_[i].normal = n;
        }
    }
    for (int z = 0; z < chunkSize - 1; ++z) {
        for (int x = 0; x < chunkSize - 1; ++x) {
            uint32_t i = z * chunkSize + x;
            chunk->triangles_.emplace_back(i, i + 1, i + chunkSize);
            chunk->triangles_.emplace_back(i + 1, i + chunkSize + 1, i + chunkSize);
        }
    }
    int seed = (chunkX * 1000003) ^ (chunkZ * 1000033);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    int numItems = 5 + (rng() % 6);
    for (int i = 0; i < numItems; ++i) {
        float x = worldOriginX + 1.5f + dist(rng) * (physWidth - 3.0f);
        float z = worldOriginZ + 1.5f + dist(rng) * (physHeight - 3.0f);
        float y = GetTerrainHeight(x, z);
        float trunkHeight = 1.8f + dist(rng) * 0.4f;
        float foliageRadius = 1.0f + dist(rng) * 0.4f;
        float rotationY = dist(rng) * 2.0f * 3.14159f;
        chunk->stones_.push_back({x, y, z, trunkHeight, foliageRadius, rotationY});
        chunk->trees_.push_back({x + 0.5f, y, z + 0.5f, trunkHeight, foliageRadius, rotationY});
    }
    if (dist(rng) < 0.1f) {
        float margin = 2.0f;
        float px = worldOriginX + margin + dist(rng) * (physWidth - 2.0f * margin);
        float pz = worldOriginZ + margin + dist(rng) * (physHeight - 2.0f * margin);
        float py = GetTerrainHeight(px, pz);
        float rotationY = dist(rng) * 2.0f * 3.14159f;
        float scale = 0.9f + dist(rng) * 0.2f;
        chunk->portal_ = {px, py, pz, rotationY, scale, true};
    }
    return chunk;
}

float WorldGenerator::GetTerrainHeight(float x, float z) {
    float baseHeight = FractalNoise(x / config_.terrainScale, z / config_.terrainScale);
    float detail = Noise(x / (config_.terrainScale * 0.5f), z / (config_.terrainScale * 0.5f)) * 0.2f;
    float normalizedHeight = (baseHeight + detail + 1.0f) * 0.5f;
    normalizedHeight = std::pow(normalizedHeight, 1.5f);
    float result = normalizedHeight * config_.terrainHeight;
    if (std::abs(x) < 1.0f && std::abs(z) < 1.0f) {
        Logger::Trace("GetTerrainHeight({:.2f}, {:.2f}) = {:.4f} (base={:.4f}, norm={:.4f})",
                     x, z, result, baseHeight, normalizedHeight);
    }
    return result;
}

BiomeType WorldGenerator::GetBiomeAt(float x, float z) {
    float noiseValue = FractalNoise(x / 1000.0f, z / 1000.0f);
    if (std::abs(noiseValue) < 0.001f) noiseValue = 0.001f;
    float temperature = FractalNoise(x / noiseValue * 8.0f, z / noiseValue * 8.0f);
    float humidity = FractalNoise(x / noiseValue * 7.0f, z / noiseValue * 7.0f);
    float height = GetTerrainHeight(x, z);
    if (height < config_.waterLevel) {
        if (humidity > 0.7f) return BiomeType::RIVER;
        return BiomeType::OCEAN;
    }
    if (height > config_.mountainThreshold * config_.terrainHeight) return BiomeType::MOUNTAIN;
    if (temperature < config_.desertThreshold) return BiomeType::DESERT;
    if (humidity > config_.forestThreshold) return BiomeType::FOREST;
    return BiomeType::PLAINS;
}

void WorldGenerator::SetSeed(int seed) {
    std::lock_guard<std::recursive_mutex> lock(rngMutex_);
    config_.seed = seed;
    rng_.seed(seed);
}

float WorldGenerator::Noise(float x, float y) {
    return glm::simplex(glm::vec2(x, y));
}

float WorldGenerator::FractalNoise(float x, float y) {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    for (int i = 0; i < config_.octaves; ++i) {
        float noiseValue = Noise(x * frequency, y * frequency);
        value += noiseValue * amplitude;
        amplitude *= config_.persistence;
        frequency *= config_.lacunarity;
    }
    return value;
}

glm::vec3 WorldGenerator::CalculateNormal(float x, float z, float height) {
    (void)height;
    const float epsilon = 0.1f;
    float h1 = GetTerrainHeight(x + epsilon, z);
    float h2 = GetTerrainHeight(x - epsilon, z);
    float h3 = GetTerrainHeight(x, z + epsilon);
    float h4 = GetTerrainHeight(x, z - epsilon);
    float dx = (h1 - h2) / (2.0f * epsilon);
    float dz = (h3 - h4) / (2.0f * epsilon);
    return glm::normalize(glm::vec3(-dx, 1.0f, -dz));
}

void WorldGenerator::GenerateLowPolyTerrain(WorldChunk& chunk, int chunkX, int chunkZ) {
    (void)chunk;
    (void)chunkX;
    (void)chunkZ;
}

void WorldGenerator::AddTrees(WorldChunk& chunk, BiomeType biome) {
    (void)chunk;
    (void)biome;
}

void WorldGenerator::AddRocks(WorldChunk& chunk, BiomeType biome) {
    (void)chunk;
    (void)biome;
}

void WorldGenerator::AddWaterPlane(WorldChunk& chunk) {
    (void)chunk;
}

void WorldGenerator::GenerateForestFeatures(WorldChunk& chunk) {
    (void)chunk;
}

void WorldGenerator::GenerateMountainFeatures(WorldChunk& chunk) {
    (void)chunk;
}

void WorldGenerator::GenerateDesertFeatures(WorldChunk& chunk) {
    (void)chunk;
}

void WorldGenerator::GeneratePlainsFeatures(WorldChunk& chunk) {
    (void)chunk;
}
