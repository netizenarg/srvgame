#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <memory>
#include <vector>
#include <cstdint>
#include <mutex>

class ConfigManager {
public:
    static ConfigManager& GetInstance();

    bool LoadConfig(const std::string& configPath);
    bool ReloadConfig();

    // Server configuration
    std::string GetServerHost() const;
    uint16_t GetServerPort() const;
    int GetMaxConnections() const;
    int GetIoThreads() const;
    bool GetReusePort() const;
    int GetProcessCount() const;

    // Database configuration
    std::string GetDatabaseHost() const;
    uint16_t GetDatabasePort() const;
    std::string GetDatabaseName() const;
    std::string GetDatabaseUser() const;
    std::string GetDatabasePassword() const;
    std::string GetDatabaseBackend() const;
    int GetDatabasePoolSize() const;
    std::vector<std::string> GetCitusWorkerNodes() const;
    int GetShardCount() const;

    // Game configuration
    int GetMaxPlayersPerSession() const;
    int GetHeartbeatInterval() const;
    int GetSessionTimeout() const;
    
    // 3D World configuration
    int GetWorldSeed() const;
    int GetViewDistance() const;
    int GetChunkSize() const;
    int GetMaxActiveChunks() const;
    float GetTerrainScale() const;
    float GetMaxTerrainHeight() const;
    float GetWaterLevel() const;
    bool ShouldPreloadWorld() const;
    int GetWorldPreloadRadius() const;

    // Logging configuration
    std::string GetLogLevel() const;
    std::string GetLogFilePath() const;
    int GetMaxLogFileSize() const;
    int GetMaxLogFiles() const;
    bool GetConsoleOutput() const;

    // Generic config accessors
    int GetInt(const std::string& key, int defaultValue = 0) const;
    float GetFloat(const std::string& key, float defaultValue = 0.0f) const;
    bool GetBool(const std::string& key, bool defaultValue = false) const;
    std::string GetString(const std::string& key, const std::string& defaultValue = "") const;
    nlohmann::json GetJson(const std::string& key) const;
    bool HasKey(const std::string& key) const;

private:
    ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    
    bool ValidateConfig() const;
    
    mutable std::mutex configMutex_;  // For thread safety
    nlohmann::json config_;
    std::string configPath_;
};