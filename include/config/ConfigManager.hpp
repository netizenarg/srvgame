#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <stdexcept>
#include <vector>

#include <nlohmann/json.hpp>

#include "logging/Logger.hpp"

// SSL configuration (optional)
struct SSLConfig {
    std::string certificate;
    std::string private_key;
    std::string dh_params;              // optional DH parameters
    bool verify_peer = false;
    std::vector<std::string> ciphers;   // allowed cipher suites
    std::string ca_cert;                // CA certificate for client validation
};

// Worker group configuration (one per listener)
struct WorkerGroupConfig {
    std::string protocol;               // "binary" or "websocket"
    std::string host;                   // "0.0.0.0" or specific IP
    uint16_t port;                      // listening port
    int max_connections;                // asio::ip::tcp::acceptor.listen(max_connections)
    bool reuse;                         // asio::ip::tcp::acceptor::reuse_address
    int threads;                        // number of io_context threads for this worker
    int count;                          // number of worker processes for this group
    std::vector<int> cpu_affinity;      // CPU cores to bind to (optional)
    bool tcp_nodelay;                   // TCP_NODELAY option
    int send_buffer_size;               // SO_SNDBUF (0 = system default)
    int receive_buffer_size;            // SO_RCVBUF (0 = system default)

    // WebSocket-specific (only used when protocol == "websocket")
    std::string path;                   // WebSocket endpoint path (e.g., "/game")
    std::vector<std::string> subprotocols;
    int max_frame_size;                 // maximum WebSocket frame size in bytes

    // SSL (optional)
    std::optional<SSLConfig> ssl;
};

class ConfigManager {
public:
    static ConfigManager& GetInstance();

    bool LoadConfig(const std::string& configPath);
    bool ReloadConfig();
    const std::string& GetConfigPath() const { return configPath_; }

    // Setters (generic)
    void SetBool(const std::string& key, bool value);
    void SetInt(const std::string& key, int value);
    void SetFloat(const std::string& key, float value);
    void SetString(const std::string& key, const std::string& value);
    void SetJson(const std::string& key, const nlohmann::json& value);

    // Worker groups API
    std::vector<WorkerGroupConfig> GetWorkerGroups() const;

    // Total workers and threads (derived from groups)
    int GetTotalWorkerCount() const;
    int GetTotalThreadCount() const;

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
    std::vector<std::string> GetStringArray(const std::string& key) const;
    nlohmann::json GetJson(const std::string& key) const;
    bool HasKey(const std::string& key) const;

private:
    ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    
    bool HasProcessConfig() const;
    bool ValidateConfig(const nlohmann::json& config) const;

    mutable std::mutex configMutex_;
    nlohmann::json config_;
    std::string configPath_;
};
