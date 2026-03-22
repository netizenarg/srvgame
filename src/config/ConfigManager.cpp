#include "config/ConfigManager.hpp"

ConfigManager& ConfigManager::GetInstance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::LoadConfig(const std::string& configPath) {
    nlohmann::json loaded;
    try {
        std::ifstream configFile(configPath);
        if (!configFile.is_open()) {
            Logger::Error("Failed to open config file: {}", configPath);
            return false;
        }

        std::stringstream buffer;
        buffer << configFile.rdbuf();
        loaded = nlohmann::json::parse(buffer.str());

    } catch (const nlohmann::json::parse_error& e) {
        Logger::Critical("JSON parse error in config file: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        Logger::Critical("Failed to load config: {}", e.what());
        return false;
    }

    if (!ValidateConfig(loaded)) {
        Logger::Critical("Configuration validation failed.");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(configMutex_);
        config_ = std::move(loaded);
        configPath_ = configPath;
    }

    Logger::Info("Configuration loaded successfully from: {}", configPath);
    return true;
}

bool ConfigManager::ReloadConfig() {
    if (configPath_.empty()) {
        Logger::Error("No config file path set for reload");
        return false;
    }
    Logger::Info("Reloading configuration from: {}", configPath_);
    return LoadConfig(configPath_);
}

bool ConfigManager::HasProcessConfig() const {
    //ATTENTION: RECURSIVELY CALL MUTEX LOCK, DO NOT USE LINE BELOW
    //std::lock_guard<std::mutex> lock(configMutex_);
    return config_.contains("process") && config_["process"].contains("workers") &&
    config_["process"]["workers"].is_array() && !config_["process"]["workers"].empty();
}

std::vector<WorkerGroupConfig> ConfigManager::GetWorkerGroups() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    std::vector<WorkerGroupConfig> groups;

    if (!HasProcessConfig())//ATTENTION: RECURSIVELY CALL MUTEX LOCK
        return groups;

    for (const auto& w : config_["process"]["workers"]) {
        WorkerGroupConfig g;
        g.protocol = w.value("protocol", "binary");
        g.host = w.value("host", "0.0.0.0");
        g.port = static_cast<uint16_t>(w.value("port", 8080));
        g.max_connections = w.value("max_connections", 1000);
        g.reuse = w.value("reuse", true);
        g.threads = w.value("threads", 1);
        g.count = w.value("count", 1);
        g.cpu_affinity = w.value("cpu_affinity", std::vector<int>());
        g.tcp_nodelay = w.value("tcp_nodelay", true);
        g.send_buffer_size = w.value("send_buffer_size", 0);
        g.receive_buffer_size = w.value("receive_buffer_size", 0);
        g.path = w.value("path", "/");
        g.subprotocols = w.value("subprotocols", std::vector<std::string>());
        g.max_frame_size = w.value("max_frame_size", 16384);
        if (w.contains("ssl")) {
            SSLConfig ssl;
            ssl.certificate = w["ssl"].value("certificate", "");
            ssl.private_key = w["ssl"].value("private_key", "");
            ssl.dh_params = w["ssl"].value("dh_params", "");
            ssl.verify_peer = w["ssl"].value("verify_peer", false);
            ssl.ciphers = w["ssl"].value("ciphers", std::vector<std::string>());
            ssl.ca_cert = w["ssl"].value("ca_cert", "");
            g.ssl = ssl;
        }
        groups.push_back(g);
    }
    return groups;
}

int ConfigManager::GetTotalWorkerCount() const {
    int total = 0;
    for (const auto& g : GetWorkerGroups())
        total += g.count;
    return total;
}

int ConfigManager::GetTotalThreadCount() const {
    int total = 0;
    for (const auto& g : GetWorkerGroups())
        total += g.threads * g.count; // each worker in group has its own threads
    return total;
}

bool ConfigManager::ValidateConfig(const nlohmann::json& config) const {
    Logger::Info("Validate config started...");
    try {
        if (!config.contains("process") || !config["process"].contains("workers") ||
            !config["process"]["workers"].is_array() || config["process"]["workers"].empty()) {
            throw std::runtime_error("Missing 'process.workers' array section");
        }

        const auto& workers = config["process"]["workers"];
        for (size_t i = 0; i < workers.size(); ++i) {
            const auto& w = workers[i];
            std::string proto = w.value("protocol", "binary");
            if (proto != "binary" && proto != "websocket") {
                throw std::runtime_error("Worker group " + std::to_string(i) +
                    ": invalid protocol '" + proto + "' (must be 'binary' or 'websocket')");
            }
            if (w.value("port", 0) == 0) {
                throw std::runtime_error("Worker group " + std::to_string(i) + ": missing or zero port");
            }
            int count = w.value("count", 1);
            if (count <= 0) {
                throw std::runtime_error("Worker group " + std::to_string(i) + ": count must be positive");
            }
            int threads = w.value("threads", 1);
            if (threads <= 0) {
                throw std::runtime_error("Worker group " + std::to_string(i) + ": threads must be positive");
            }
            if (proto == "websocket" && w.contains("ssl")) {
                const auto& ssl = w["ssl"];
                if (!ssl.contains("certificate") || !ssl["certificate"].is_string() ||
                    !ssl.contains("private_key") || !ssl["private_key"].is_string()) {
                    throw std::runtime_error("Worker group " + std::to_string(i) +
                        ": WebSocket SSL requires 'certificate' and 'private_key'");
                }
            }
        }

        // Validate database section
        if (config.contains("database")) {
            const auto& database = config["database"];
            if (!database.contains("name") || !database["name"].is_string()) {
                throw std::runtime_error("Invalid or missing 'database.name'");
            }
        } else {
            throw std::runtime_error("Missing 'database' section");
        }

        // Validate game section
        if (config.contains("game")) {
            const auto& game = config["game"];
            if (!game.contains("max_players_per_session") ||
                !game["max_players_per_session"].is_number_unsigned()) {
                throw std::runtime_error("Invalid or missing 'game.max_players_per_session'");
            }
        } else {
            throw std::runtime_error("Missing 'game' section");
        }

        // Validate logging section
        if (config.contains("logging")) {
            const auto& logging = config["logging"];
            if (!logging.contains("level") || !logging["level"].is_string()) {
                throw std::runtime_error("Invalid or missing 'logging.level'");
            }
            const std::string logLevel = logging["level"];
            const std::vector<std::string> validLevels = {
                "trace", "debug", "info", "warn", "error", "critical", "off"
            };
            std::string lowerLevel = logLevel;
            std::transform(lowerLevel.begin(), lowerLevel.end(), lowerLevel.begin(), ::tolower);
            if (std::find(validLevels.begin(), validLevels.end(), lowerLevel) == validLevels.end()) {
                throw std::runtime_error("Invalid log level: " + logLevel);
            }
        } else {
            throw std::runtime_error("Missing 'logging' section");
        }

        Logger::Info("Configuration validation passed");
        return true;

    } catch (const std::exception& e) {
        Logger::Critical("Configuration validation failed: {}", e.what());
        return false;
    }
}

// --------------------------------------------------------------------------
// Setters (unchanged)
// --------------------------------------------------------------------------
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

// --------------------------------------------------------------------------
// Database configuration getters (unchanged)
// --------------------------------------------------------------------------
std::string ConfigManager::GetDatabaseHost() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("database").at("host").get<std::string>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return "127.0.0.1";
    }
}

uint16_t ConfigManager::GetDatabasePort() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("database").at("port").get<uint16_t>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return 5432;
    }
}

std::string ConfigManager::GetDatabaseName() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("database").at("name").get<std::string>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return "game_db";
    }
}

std::string ConfigManager::GetDatabaseUser() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("database").at("user").get<std::string>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return "game_user";
    }
}

std::string ConfigManager::GetDatabasePassword() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("database").at("password").get<std::string>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return "";
    }
}

std::string ConfigManager::GetDatabaseBackend() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("database").at("backend").get<std::string>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return "postgresql";
    }
}

int ConfigManager::GetDatabasePoolSize() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("database").at("pool_size").get<int>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
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
                if (node.is_string())
                    nodes.push_back(node.get<std::string>());
            }
        }
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());}
    return nodes;
}

int ConfigManager::GetShardCount() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("database").at("shard_count").get<int>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return 32;
    }
}

// --------------------------------------------------------------------------
// Game configuration getters (unchanged)
// --------------------------------------------------------------------------
int ConfigManager::GetMaxPlayersPerSession() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("game").at("max_players_per_session").get<int>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return 100;
    }
}

int ConfigManager::GetHeartbeatInterval() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("game").at("heartbeat_interval_seconds").get<int>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return 30;
    }
}

int ConfigManager::GetSessionTimeout() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("game").at("session_timeout_seconds").get<int>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return 300;
    }
}

// --------------------------------------------------------------------------
// World configuration getters (unchanged)
// --------------------------------------------------------------------------
int ConfigManager::GetWorldSeed() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("world").at("seed").get<int>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return 12345;
    }
}

int ConfigManager::GetViewDistance() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("world").at("view_distance").get<int>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return 1000;
    }
}

int ConfigManager::GetChunkSize() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("world").at("chunk_size").get<int>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return 32;
    }
}

int ConfigManager::GetMaxActiveChunks() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("world").at("max_active_chunks").get<int>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return 1000;
    }
}

float ConfigManager::GetTerrainScale() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("world").at("terrain_scale").get<float>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return 1.0f;
    }
}

float ConfigManager::GetMaxTerrainHeight() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("world").at("max_terrain_height").get<float>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return 100.0f;
    }
}

float ConfigManager::GetWaterLevel() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("world").at("water_level").get<float>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return 10.0f;
    }
}

bool ConfigManager::ShouldPreloadWorld() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("world").at("preload_world").get<bool>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return false;
    }
}

int ConfigManager::GetWorldPreloadRadius() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("world").at("preload_radius").get<int>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return 500;
    }
}

// --------------------------------------------------------------------------
// Logging configuration getters (unchanged)
// --------------------------------------------------------------------------
std::string ConfigManager::GetLogLevel() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        std::string level = config_.at("logging").at("level").get<std::string>();
        std::transform(level.begin(), level.end(), level.begin(), ::tolower);
        return level;
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return "info";
    }
}

std::string ConfigManager::GetLogFilePath() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("logging").at("file").get<std::string>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return "gameserver.log";
    }
}

int ConfigManager::GetMaxLogFileSize() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("logging").at("max_file_size_mb").get<int>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return 100;
    }
}

int ConfigManager::GetMaxLogFiles() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("logging").at("max_files").get<int>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return 10;
    }
}

bool ConfigManager::GetConsoleOutput() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        return config_.at("logging").at("console_output").get<bool>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return true;
    }
}

// --------------------------------------------------------------------------
// Generic config accessors (unchanged)
// --------------------------------------------------------------------------
int ConfigManager::GetInt(const std::string& key, int defaultValue) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        std::string keyPath = key;
        std::replace(keyPath.begin(), keyPath.end(), '.', '/');
        return config_.at(nlohmann::json::json_pointer("/" + keyPath)).get<int>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return defaultValue;
    }
}

float ConfigManager::GetFloat(const std::string& key, float defaultValue) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        std::string keyPath = key;
        std::replace(keyPath.begin(), keyPath.end(), '.', '/');
        return config_.at(nlohmann::json::json_pointer("/" + keyPath)).get<float>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return defaultValue;
    }
}

bool ConfigManager::GetBool(const std::string& key, bool defaultValue) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        std::string keyPath = key;
        std::replace(keyPath.begin(), keyPath.end(), '.', '/');
        return config_.at(nlohmann::json::json_pointer("/" + keyPath)).get<bool>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return defaultValue;
    }
}

std::string ConfigManager::GetString(const std::string& key, const std::string& defaultValue) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        std::string keyPath = key;
        std::replace(keyPath.begin(), keyPath.end(), '.', '/');
        return config_.at(nlohmann::json::json_pointer("/" + keyPath)).get<std::string>();
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
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
                if (item.is_string())
                    result.push_back(item.get<std::string>());
                else
                    result.push_back(item.dump());
            }
        }
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());}
    return result;
}

nlohmann::json ConfigManager::GetJson(const std::string& key) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        std::string keyPath = key;
        std::replace(keyPath.begin(), keyPath.end(), '.', '/');
        return config_.at(nlohmann::json::json_pointer("/" + keyPath));
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return nlohmann::json();
    }
}

bool ConfigManager::HasKey(const std::string& key) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    try {
        std::string keyPath = key;
        std::replace(keyPath.begin(), keyPath.end(), '.', '/');
        return config_.contains(nlohmann::json::json_pointer("/" + keyPath));
    } catch (const std::exception& err) {Logger::Warn("failed: {}", err.what());
        return false;
    }
}
