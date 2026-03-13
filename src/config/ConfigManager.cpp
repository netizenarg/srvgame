#include "config/ConfigManager.hpp"

ConfigManager& ConfigManager::GetInstance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::LoadConfig(const std::string& configPath) {
    std::lock_guard<std::mutex> lock(configMutex_);
    configPath_ = configPath;

    try {
        std::ifstream configFile(configPath);
        if (!configFile.is_open()) {
            Logger::Error("Failed to open config file: {}", configPath);
            return false;
        }

        std::stringstream buffer;
        buffer << configFile.rdbuf();
        config_ = nlohmann::json::parse(buffer.str());

        Logger::Info("Configuration loaded successfully from: {}", configPath);

        // Validate configuration
        return ValidateConfig();

    } catch (const nlohmann::json::parse_error& e) {
        Logger::Critical("JSON parse error in config file: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        Logger::Critical("Failed to load config: {}", e.what());
        return false;
    }
}

bool ConfigManager::ReloadConfig() {
    if (configPath_.empty()) {
        Logger::Error("No config file path set for reload");
        return false;
    }

    Logger::Info("Reloading configuration from: {}", configPath_);
    return LoadConfig(configPath_);
}

bool ConfigManager::ValidateConfig() const {
    Logger::Info("Validate config started...");
    try {
        if (config_.contains("server"))
            Logger::Info("Validate config 'server' section...");
        else
            throw std::runtime_error("Missing 'server' section");
        const auto& server = config_["server"];
        if (!server.contains("host") || !server["host"].is_string()) {
            throw std::runtime_error("Invalid or missing 'server.host'");
        }
        if (!server.contains("port") || !server["port"].is_number_unsigned()) {
            throw std::runtime_error("Invalid or missing 'server.port'");
        }
        if (server["port"].get<uint16_t>() == 0) {
            throw std::runtime_error("Invalid server port");
        }

        if (config_.contains("database"))
            Logger::Info("Validate config 'database' section...");
        else
            throw std::runtime_error("Missing 'database' section");
        const auto& database = config_["database"];
        if (!database.contains("host") || !database["host"].is_string()) {
            Logger::Warn("database.host not set, will use default 127.0.0.1");
        }
        if (!database.contains("port") || !database["port"].is_number_unsigned()) {
            Logger::Warn("database.port not set, will use default 5432");
        }
        if (!database.contains("name") || !database["name"].is_string()) {
            throw std::runtime_error("Invalid or missing 'database.name'");
        }

        if (config_.contains("game"))
            Logger::Info("Validate config 'game' section...");
        else
            throw std::runtime_error("Missing 'game' section");
        const auto& game = config_["game"];
        if (!game.contains("max_players_per_session") ||
            !game["max_players_per_session"].is_number_unsigned()) {
            throw std::runtime_error("Invalid or missing 'game.max_players_per_session'");
            }

        if (config_.contains("logging"))
            Logger::Info("Validate config 'logging' section...");
        else
            throw std::runtime_error("Missing 'logging' section");
        const auto& logging = config_["logging"];
        if (!logging.contains("level") || !logging["level"].is_string()) {
            throw std::runtime_error("Invalid or missing 'logging.level'");
        }
        // Validate log levels
        const std::string logLevel = logging["level"];
        const std::vector<std::string> validLevels = {
            "trace", "debug", "info", "warn", "error", "critical", "off"
        };
        std::string lowerLevel = logLevel;
        std::transform(lowerLevel.begin(), lowerLevel.end(), lowerLevel.begin(), ::tolower);
        if (std::find(validLevels.begin(), validLevels.end(), lowerLevel) == validLevels.end()) {
            throw std::runtime_error("Invalid log level: " + logLevel);
        }

        Logger::Info("Configuration validation passed");
        return true;

    } catch (const std::exception& e) {
        Logger::Critical("Configuration validation failed: {}", e.what());
        return false;
    }
}

void ConfigManager::SetBool(const std::string& key, bool value) {
    std::lock_guard<std::mutex> lock(configMutex_);
    std::string keyPath = key;
    std::replace(keyPath.begin(), keyPath.end(), '.', '/');
    nlohmann::json::json_pointer ptr("/" + keyPath);
    config_[ptr] = value;
}

void ConfigManager::SetInt(const std::string& key, int value) {
    std::lock_guard<std::mutex> lock(configMutex_);
    std::string keyPath = key;
    std::replace(keyPath.begin(), keyPath.end(), '.', '/');
    nlohmann::json::json_pointer ptr("/" + keyPath);
    config_[ptr] = value;
}

void ConfigManager::SetFloat(const std::string& key, float value) {
    std::lock_guard<std::mutex> lock(configMutex_);
    std::string keyPath = key;
    std::replace(keyPath.begin(), keyPath.end(), '.', '/');
    nlohmann::json::json_pointer ptr("/" + keyPath);
    config_[ptr] = value;
}

void ConfigManager::SetString(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(configMutex_);
    std::string keyPath = key;
    std::replace(keyPath.begin(), keyPath.end(), '.', '/');
    nlohmann::json::json_pointer ptr("/" + keyPath);
    config_[ptr] = value;
}

void ConfigManager::SetJson(const std::string& key, const nlohmann::json& value) {
    std::lock_guard<std::mutex> lock(configMutex_);
    std::string keyPath = key;
    std::replace(keyPath.begin(), keyPath.end(), '.', '/');
    nlohmann::json::json_pointer ptr("/" + keyPath);
    config_[ptr] = value;
}

// Server configuration getters
std::string ConfigManager::GetServerHost() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("server").at("host").get<std::string>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get server host, using default: 0.0.0.0");
        return "0.0.0.0";
    }
}

uint16_t ConfigManager::GetServerPort() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("server").at("port").get<uint16_t>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get server port, using default: 8080");
        return 8080;
    }
}

int ConfigManager::GetMaxConnections() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("server").at("max_connections").get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get max connections, using default: 10000");
        return 10000;
    }
}

int ConfigManager::GetIoThreads() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("server").at("io_threads").get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get IO threads, using default: 4");
        return 4;
    }
}

bool ConfigManager::GetReusePort() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("server").at("reuse_port").get<bool>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get reuse_port, using default: true");
        return true;
    }
}

int ConfigManager::GetProcessCount() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("server").at("process_count").get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get process count, using default: 4");
        return 4;
    }
}

// Database configuration getters
std::string ConfigManager::GetDatabaseHost() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("database").at("host").get<std::string>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get database host, using default: 127.0.0.1");
        return "127.0.0.1";
    }
}

uint16_t ConfigManager::GetDatabasePort() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("database").at("port").get<uint16_t>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get database port, using default: 5432");
        return 5432;
    }
}

std::string ConfigManager::GetDatabaseName() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("database").at("name").get<std::string>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get database name, using default: game_db");
        return "game_db";
    }
}

std::string ConfigManager::GetDatabaseUser() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("database").at("user").get<std::string>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get database user, using default: game_user");
        return "game_user";
    }
}

std::string ConfigManager::GetDatabasePassword() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("database").at("password").get<std::string>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get database password, using empty default");
        return "";
    }
}

std::string ConfigManager::GetDatabaseBackend() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("database").at("backend").get<std::string>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get database backend, using default: postgresql");
        return "postgresql";
    }
}

int ConfigManager::GetDatabasePoolSize() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("database").at("pool_size").get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get database pool size, using default: 10");
        return 10;
    }
}

std::vector<std::string> ConfigManager::GetCitusWorkerNodes() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    std::vector<std::string> nodes;
    try {
        auto& db = config_.at("database");
        if (db.contains("citus_worker_nodes") && db["citus_worker_nodes"].is_array()) {
            for (const auto& node : db["citus_worker_nodes"]) {
                if (node.is_string()) {
                    nodes.push_back(node.get<std::string>());
                }
            }
        }
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get Citus worker nodes, using empty list");
    }
    // Add default coordinator if present
    if (nodes.empty() && config_.contains("database") && config_["database"].contains("citus_coordinator")) {
        try {
            std::string coordinator = config_["database"]["citus_coordinator"].get<std::string>();
            nodes.push_back(coordinator + ":5432");
        } catch (...) {}
    }
    return nodes;
}

int ConfigManager::GetShardCount() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("database").at("shard_count").get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get shard count, using default: 32");
        return 32;
    }
}

// Game configuration getters
int ConfigManager::GetMaxPlayersPerSession() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("game").at("max_players_per_session").get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get max players per session, using default: 100");
        return 100;
    }
}

int ConfigManager::GetHeartbeatInterval() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("game").at("heartbeat_interval_seconds").get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get heartbeat interval, using default: 30");
        return 30;
    }
}

int ConfigManager::GetSessionTimeout() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("game").at("session_timeout_seconds").get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get session timeout, using default: 300");
        return 300;
    }
}

// 3D World configuration getters
int ConfigManager::GetWorldSeed() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("world").at("seed").get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get world seed, using default: 12345");
        return 12345;
    }
}

int ConfigManager::GetViewDistance() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("world").at("view_distance").get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get view distance, using default: 1000");
        return 1000;
    }
}

int ConfigManager::GetChunkSize() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("world").at("chunk_size").get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get chunk size, using default: 32");
        return 32;
    }
}

int ConfigManager::GetMaxActiveChunks() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("world").at("max_active_chunks").get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get max active chunks, using default: 1000");
        return 1000;
    }
}

float ConfigManager::GetTerrainScale() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("world").at("terrain_scale").get<float>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get terrain scale, using default: 1.0");
        return 1.0f;
    }
}

float ConfigManager::GetMaxTerrainHeight() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("world").at("max_terrain_height").get<float>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get max terrain height, using default: 100.0");
        return 100.0f;
    }
}

float ConfigManager::GetWaterLevel() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("world").at("water_level").get<float>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get water level, using default: 10.0");
        return 10.0f;
    }
}

bool ConfigManager::ShouldPreloadWorld() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("world").at("preload_world").get<bool>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get preload world setting, using default: false");
        return false;
    }
}

int ConfigManager::GetWorldPreloadRadius() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("world").at("preload_radius").get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get world preload radius, using default: 500");
        return 500;
    }
}

// Logging configuration getters
std::string ConfigManager::GetLogLevel() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        std::string level = config_.at("logging").at("level").get<std::string>();
        std::transform(level.begin(), level.end(), level.begin(), ::tolower);
        return level;
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get log level, using default: info");
        return "info";
    }
}

std::string ConfigManager::GetLogFilePath() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("logging").at("file").get<std::string>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get log file path, using default: gameserver.log");
        return "gameserver.log";
    }
}

int ConfigManager::GetMaxLogFileSize() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("logging").at("max_file_size_mb").get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get max log file size, using default: 100");
        return 100;
    }
}

int ConfigManager::GetMaxLogFiles() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("logging").at("max_files").get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get max log files, using default: 10");
        return 10;
    }
}

bool ConfigManager::GetConsoleOutput() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("logging").at("console_output").get<bool>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get console output setting, using default: true");
        return true;
    }
}

// Generic config accessors
int ConfigManager::GetInt(const std::string& key, int defaultValue) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        std::string keyPath = key;
        std::replace(keyPath.begin(), keyPath.end(), '.', '/');
        return config_.at(nlohmann::json::json_pointer("/" + keyPath)).get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get int for key '{}': {}", key, e.what());
        return defaultValue;
    }
}

float ConfigManager::GetFloat(const std::string& key, float defaultValue) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        std::string keyPath = key;
        std::replace(keyPath.begin(), keyPath.end(), '.', '/');
        return config_.at(nlohmann::json::json_pointer("/" + keyPath)).get<float>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get float for key '{}': {}", key, e.what());
        return defaultValue;
    }
}

bool ConfigManager::GetBool(const std::string& key, bool defaultValue) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        std::string keyPath = key;
        std::replace(keyPath.begin(), keyPath.end(), '.', '/');
        return config_.at(nlohmann::json::json_pointer("/" + keyPath)).get<bool>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get bool for key '{}': {}", key, e.what());
        return defaultValue;
    }
}

std::string ConfigManager::GetString(const std::string& key, const std::string& defaultValue) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        std::string keyPath = key;
        std::replace(keyPath.begin(), keyPath.end(), '.', '/');
        return config_.at(nlohmann::json::json_pointer("/" + keyPath)).get<std::string>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get string for key '{}': {}", key, e.what());
        return defaultValue;
    }
}

std::vector<std::string> ConfigManager::GetStringArray(const std::string& key) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    std::vector<std::string> result;
    try {
        std::string keyPath = key;
        std::replace(keyPath.begin(), keyPath.end(), '.', '/');
        auto& arr = config_.at(nlohmann::json::json_pointer("/" + keyPath));
        if (arr.is_array()) {
            for (const auto& item : arr) {
                if (item.is_string()) {
                    result.push_back(item.get<std::string>());
                } else {
                    result.push_back(item.dump());
                }
            }
        }
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get string array for key '{}': {}", key, e.what());
    }
    return result;
}

nlohmann::json ConfigManager::GetJson(const std::string& key) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        std::string keyPath = key;
        std::replace(keyPath.begin(), keyPath.end(), '.', '/');
        return config_.at(nlohmann::json::json_pointer("/" + keyPath));
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get json for key '{}': {}", key, e.what());
        return nlohmann::json();
    }
}

bool ConfigManager::HasKey(const std::string& key) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        std::string keyPath = key;
        std::replace(keyPath.begin(), keyPath.end(), '.', '/');
        return config_.contains(nlohmann::json::json_pointer("/" + keyPath));
    } catch (const std::exception& e) {
        Logger::Warn("Failed HasKey for key '{}': {}", key, e.what());
        return false;
    }
}
