#include "game/LogicWorld.hpp"

std::mutex LogicWorld::instanceMutex_;
LogicWorld* LogicWorld::instance_ = nullptr;

LogicWorld& LogicWorld::GetInstance() {
    std::lock_guard<std::mutex> lock(instanceMutex_);
    if (!instance_) {
        instance_ = new LogicWorld();
    }
    return *instance_;
}

LogicWorld::LogicWorld() :running_(true) {
    Logger::Info("LogicWorld created");
}

LogicWorld::~LogicWorld() {
    Shutdown();
}

void LogicWorld::Initialize(const WorldConfig& config) {
    worldConfig_ = config;

    GenerationConfig genConfig;
    genConfig.seed = worldConfig_.seed;
    genConfig.terrainScale = worldConfig_.terrainScale;
    genConfig.terrainHeight = worldConfig_.maxTerrainHeight;
    genConfig.waterLevel = worldConfig_.waterLevel;

    worldGenerator_ = std::make_unique<WorldGenerator>(genConfig);
    Logger::Info("LogicWorld initialized with seed: {}", worldConfig_.seed);
}

void LogicWorld::Shutdown() {
    if (!running_.exchange(false)) return;
    Logger::Trace("LogicWorld::Shutdown running...");
    std::lock_guard<std::mutex> lock(chunksMutex_);
    loadedChunks_.clear();
    activeChunkCount_ = 0;
    worldGenerator_.reset();
    Logger::Trace("LogicWorld::Shutdown complete");
}

std::string LogicWorld::GetChunkKey(int chunkX, int chunkZ) const {
    return std::to_string(chunkX) + "_" + std::to_string(chunkZ);
}

std::shared_ptr<WorldChunk> LogicWorld::GetOrCreateChunk(int chunkX, int chunkZ) {
    std::lock_guard<std::mutex> lock(chunksMutex_);
    //Logger::Trace("worldGenerator_ raw pointer: {}", static_cast<void*>(worldGenerator_.get()));
    if (canary_ != 0xDEADBEEF) {
        Logger::Critical("LogicWorld memory corrupted! canary=0x{:x}", canary_);
        std::abort();
    }

    // --- Workaround: reinitialize generator if lost ---
    if (!worldGenerator_) {
        Logger::Error("World generator was null! Reinitializing...");
        GenerationConfig genConfig;
        genConfig.seed = worldConfig_.seed;
        genConfig.terrainScale = worldConfig_.terrainScale;
        genConfig.terrainHeight = worldConfig_.maxTerrainHeight;
        genConfig.waterLevel = worldConfig_.waterLevel;
        worldGenerator_ = std::make_unique<WorldGenerator>(genConfig);
    }

    std::string chunkKey = GetChunkKey(chunkX, chunkZ);
    auto it = loadedChunks_.find(chunkKey);
    if (it != loadedChunks_.end()) {
        return it->second;
    }
    if (activeChunkCount_ >= worldConfig_.maxActiveChunks) {
        if (!loadedChunks_.empty()) {
            auto first = loadedChunks_.begin();
            loadedChunks_.erase(first);
            activeChunkCount_--;
        }
    }
    if (!worldGenerator_) {
        Logger::Error("World generator is null! Cannot generate chunk ({},{})", chunkX, chunkZ);
        return nullptr;
    }
    std::unique_ptr<WorldChunk> uniqueChunk = worldGenerator_->GenerateChunk(chunkX, chunkZ);
    if (!uniqueChunk) {
        Logger::Error("Failed to generate chunk [{}, {}]", chunkX, chunkZ);
        return nullptr;
    }
    std::shared_ptr<WorldChunk> chunk = std::move(uniqueChunk);
    loadedChunks_[chunkKey] = chunk;
    activeChunkCount_++;
    //Logger::Trace("Generated chunk [{}, {}], total: {}", chunkX, chunkZ, activeChunkCount_.load());
    return chunk;
}

void LogicWorld::UnloadDistantChunks(const glm::vec3& centerPosition, float keepRadius) {
    std::lock_guard<std::mutex> lock(chunksMutex_);

    auto it = loadedChunks_.begin();
    while (it != loadedChunks_.end()) {
        WorldChunk* chunk = it->second.get();
        glm::vec3 chunkCenter = chunk->GetCenter();
        float distance = glm::distance(centerPosition, chunkCenter);

        if (distance > keepRadius) {
            it = loadedChunks_.erase(it);
            activeChunkCount_--;
        } else {
            ++it;
        }
    }
}

void LogicWorld::GenerateWorldAroundPlayer(const glm::vec3& position, int viewDistance) {
    int playerChunkX = static_cast<int>(std::floor(position.x / worldConfig_.chunkSize));
    int playerChunkZ = static_cast<int>(std::floor(position.z / worldConfig_.chunkSize));

    for (int dx = -viewDistance; dx <= viewDistance; ++dx) {
        for (int dz = -viewDistance; dz <= viewDistance; ++dz) {
            int chunkX = playerChunkX + dx;
            int chunkZ = playerChunkZ + dz;
            GetOrCreateChunk(chunkX, chunkZ);
        }
    }
}

void LogicWorld::PreloadWorldData(float radius) {
    Logger::Info("Preloading world data within radius {}...", radius);

    int chunksToLoad = static_cast<int>((radius / worldConfig_.chunkSize) * 2) + 1;

    for (int x = -chunksToLoad; x <= chunksToLoad; ++x) {
        for (int z = -chunksToLoad; z <= chunksToLoad; ++z) {
            GetOrCreateChunk(x, z);
        }
    }

    Logger::Info("Preloaded {} chunks", (chunksToLoad * 2 + 1) * (chunksToLoad * 2 + 1));
}

float LogicWorld::GetTerrainHeight(float x, float z) const {
    if (!worldGenerator_) {
        return worldConfig_.waterLevel;
    }
    return worldGenerator_->GetTerrainHeight(x, z);
}

BiomeType LogicWorld::GetBiomeAt(float x, float z) const {
    if (!worldGenerator_) {
        return BiomeType::PLAINS;
    }
    return worldGenerator_->GetBiomeAt(x, z);
}

void LogicWorld::SaveChunkData() {
    std::lock_guard<std::mutex> lock(chunksMutex_);
    DatabaseBackend* backend = DbManager::GetInstance().GetBackend();
    if (!backend) {
        Logger::Error("No database backend configured");
        return;
    }
    for (const auto& [key, chunk] : loadedChunks_) {
        try {
            nlohmann::json chunkData = chunk->SerializeJson();
            backend->SaveChunkData(chunk->GetChunkX(), chunk->GetChunkZ(), chunkData);
        } catch (const std::exception& e) {
            Logger::Error("Failed to save chunk [{}, {}]: {}",
                         chunk->GetChunkX(), chunk->GetChunkZ(), e.what());
        }
    }
}

void LogicWorld::AddEntity(std::shared_ptr<GameEntity> entity) {
    std::lock_guard<std::mutex> lock(entitiesMutex_);
    if (entity) {
        entities_[entity->GetId()] = entity;
    }
}

void LogicWorld::RemoveEntity(uint64_t entityId) {
    std::lock_guard<std::mutex> lock(entitiesMutex_);
    entities_.erase(entityId);
}

std::shared_ptr<GameEntity> LogicWorld::GetEntity(uint64_t entityId) const {
    std::lock_guard<std::mutex> lock(entitiesMutex_);
    auto it = entities_.find(entityId);
    return (it != entities_.end()) ? it->second : nullptr;
}

void LogicWorld::UpdateEntities(float deltaTime) {
    std::lock_guard<std::mutex> lock(entitiesMutex_);
    for (auto& [id, entity] : entities_) {
        entity->Update(deltaTime);
    }
}

void LogicWorld::SetTimeOfDay(float time) {
    if (time < 0.0f) time = 0.0f;
    if (time > 1.0f) time = 1.0f;
    currentTimeOfDay_.store(time, std::memory_order_relaxed);
}

float LogicWorld::GetTimeOfDay() const {
    return currentTimeOfDay_.load(std::memory_order_relaxed);
}
