#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <stdexcept>
#include <vector>

#include <nlohmann/json.hpp>

#include "logging/Logger.hpp"

class ConfigManager {
public:
    static ConfigManager& GetInstance();

    bool LoadConfig(const std::string& configPath);
    bool ReloadConfig();
    const std::string& GetConfigPath() const { return configPath_; }

    // Setters
    void SetBool(const std::string& key, bool value);
    void SetInt(const std::string& key, int value);
    void SetFloat(const std::string& key, float value);
    void SetString(const std::string& key, const std::string& value);
    void SetJson(const std::string& key, const nlohmann::json& value);

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
    
    // World configuration
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
    // nlohmann::json j = nlohmann::json::parse(R"(["root", "home", "var"])");
    // std::vector<std::string> colors = {"root", "home", "var"};
    std::vector<std::string> GetStringArray(const std::string& key) const;
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
