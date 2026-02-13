#include "database/DbManager.hpp"

// =============== Static Members ===============
std::mutex DbManager::instanceMutex_;
DbManager* DbManager::instance_ = nullptr;

// =============== DbManager Implementation ===============
DbManager& DbManager::GetInstance() {
    std::lock_guard<std::mutex> lock(instanceMutex_);
    if (!instance_) {
        instance_ = new DbManager();
    }
    return *instance_;
}

DbManager::DbManager()
    : currentType_(INVALID),
      initialized_(false),
      connected_(false) {
    stats_.startTime = std::chrono::steady_clock::now();
    Logger::Debug("DbManager created");
}

DbManager::~DbManager() {
    Shutdown();
    Logger::Debug("DbManager destroyed");
}

bool DbManager::Initialize() {
    if (initialized_) {
        Logger::Warn("DbManager already initialized");
        return true;
    }

    Logger::Info("Initializing DbManager...");

    // Load configuration
    if (!LoadConfiguration()) {
        Logger::Error("Failed to load database configuration");
        return false;
    }

    // Validate configuration
    if (!ValidateConfiguration(config_)) {
        Logger::Error("Invalid database configuration");
        return false;
    }

    // Parse database type
    std::string typeStr = config_.value("type", "postgresql");
    currentType_ = ParseDatabaseType(typeStr);

    if (currentType_ == INVALID) {
        Logger::Error("Unknown database type: {}", typeStr);
        return false;
    }

    // Create appropriate backend
    bool backendCreated = false;
    switch (currentType_) {
        case POSTGRESQL:
            backend_ = std::make_unique<PostgreSqlClient>(config_);
            backendCreated = true;
            break;
        case CITUS:
#ifdef USE_CITUS
            backend_ = std::make_unique<CitusClient>(config_);
            backendCreated = true;
#else
            Logger::Error("Citus support not compiled in. Recompile with USE_CITUS=1");
            return false;
#endif
            break;
        default:
            Logger::Error("Unsupported database type");
            return false;
    }

    if (!backendCreated) {
        Logger::Error("Failed to create database backend");
        return false;
    }

    // Initialize connection pool if configured
    if (config_.contains("connection_pool")) {
        size_t minConnections = config_["connection_pool"].value("min_connections", 5);
        size_t maxConnections = config_["connection_pool"].value("max_connections", 20);

        if (!backend_->InitializeConnectionPool(minConnections, maxConnections)) {
            Logger::Warn("Failed to initialize connection pool");
        }
    }

    initialized_ = true;
    Logger::Info("DbManager initialized with {} backend", DatabaseTypeToString(currentType_));
    return true;
}

void DbManager::Shutdown() {
    if (!initialized_) {
        return;
    }

    Logger::Info("Shutting down DbManager...");

    if (backend_) {
        backend_->Disconnect();
        backend_->ReleaseConnectionPool();
        backend_.reset();
    }

    initialized_ = false;
    connected_ = false;

    Logger::Info("DbManager shutdown complete");
}

bool DbManager::LoadConfiguration(const std::string& configPath) {
    try {
        auto& configManager = ConfigManager::GetInstance();

        // Try to load from file first
        std::string path = configPath.empty() ?
            configManager.GetString("database.config_path", "./config/database.json") :
            configPath;

        if (!path.empty()) {
            std::ifstream configFile(path);
            if (configFile.is_open()) {
                configFile >> config_;
                configFile.close();
                Logger::Debug("Database configuration loaded from: {}", path);
                return true;
            }
        }

        // Fall back to config manager
        config_ = {
            {"type", configManager.GetString("database.type", "postgresql")},
            {"host", configManager.GetString("database.host", "localhost")},
            {"port", configManager.GetInt("database.port", 5432)},
            {"database", configManager.GetString("database.name", "game_db")},
            {"username", configManager.GetString("database.username", "postgres")},
            {"password", configManager.GetString("database.password", "")},
            {"connection_pool", {
                {"enabled", configManager.GetBool("database.pool.enabled", true)},
                {"min_connections", configManager.GetInt("database.pool.min", 5)},
                {"max_connections", configManager.GetInt("database.pool.max", 20)}
            }},
            {"shards", configManager.GetInt("database.shards", 32)},
            {"replication", configManager.GetBool("database.replication", false)},
            {"ssl", configManager.GetBool("database.ssl", false)},
            {"timeout", configManager.GetInt("database.timeout", 30)}
        };

        Logger::Debug("Database configuration loaded from ConfigManager");
        return true;

    } catch (const std::exception& e) {
        Logger::Error("Failed to load database configuration: {}", e.what());
        return false;
    }
}

bool DbManager::ValidateConfiguration(const nlohmann::json& config) const {
    try {
        // Check required fields
        if (!config.contains("host") || !config["host"].is_string()) {
            Logger::Error("Missing or invalid 'host' in database configuration");
            return false;
        }

        if (!config.contains("database") || !config["database"].is_string()) {
            Logger::Error("Missing or invalid 'database' in database configuration");
            return false;
        }

        if (!config.contains("username") || !config["username"].is_string()) {
            Logger::Error("Missing or invalid 'username' in database configuration");
            return false;
        }

        // Validate port
        if (config.contains("port")) {
            if (!config["port"].is_number()) {
                Logger::Error("Invalid 'port' in database configuration");
                return false;
            }
            int port = config["port"];
            if (port < 1 || port > 65535) {
                Logger::Error("Port out of range: {}", port);
                return false;
            }
        }

        // Validate connection pool settings if present
        if (config.contains("connection_pool")) {
            const auto& pool = config["connection_pool"];
            if (pool.contains("min_connections") && pool["min_connections"] <= 0) {
                Logger::Error("Invalid min_connections in connection pool");
                return false;
            }
            if (pool.contains("max_connections")) {
                if (pool["max_connections"] <= 0) {
                    Logger::Error("Invalid max_connections in connection pool");
                    return false;
                }
                if (pool.contains("min_connections") &&
                    pool["min_connections"] > pool["max_connections"]) {
                    Logger::Error("min_connections cannot be greater than max_connections");
                    return false;
                }
            }
        }

        return true;

    } catch (const std::exception& e) {
        Logger::Error("Configuration validation error: {}", e.what());
        return false;
    }
}

bool DbManager::SetBackend(DatabaseType type, const nlohmann::json& config) {
    std::lock_guard<std::mutex> lock(instanceMutex_);

    if (!ValidateConfiguration(config)) {
        Logger::Error("Invalid configuration for new backend");
        return false;
    }

    // Store old backend
    std::unique_ptr<DatabaseBackend> oldBackend = std::move(backend_);

    // Create new backend
    switch (type) {
        case POSTGRESQL:
            backend_ = std::make_unique<PostgreSqlClient>(config);
            break;
        case CITUS:
#ifdef USE_CITUS
            backend_ = std::make_unique<CitusClient>(config);
#else
            Logger::Error("Citus support not compiled in");
            return false;
#endif
            break;
        default:
            Logger::Error("Unsupported database type");
            backend_ = std::move(oldBackend);
            return false;
    }

    currentType_ = type;
    config_ = config;
    connected_ = false;

    Logger::Info("Database backend changed to {}", DatabaseTypeToString(type));
    return true;
}

bool DbManager::SaveGameState(const std::string& key, const nlohmann::json& state) {
    return backend_.SaveGameState(key, state);
}

bool DbManager::Connect() {
    if (!initialized_) {
        Logger::Error("DbManager not initialized");
        return false;
    }

    if (!backend_) {
        Logger::Error("No database backend available");
        return false;
    }

    if (connected_) {
        Logger::Debug("Already connected to database");
        return true;
    }

    try {
        if (backend_->Connect()) {
            connected_ = true;
            Logger::Info("Connected to {} database", DatabaseTypeToString(currentType_));
            return true;
        } else {
            Logger::Error("Failed to connect to database");
            return false;
        }
    } catch (const std::exception& e) {
        Logger::Error("Connection error: {}", e.what());
        return false;
    }
}

bool DbManager::Reconnect() {
    if (!initialized_ || !backend_) {
        return false;
    }

    Logger::Info("Attempting to reconnect to database...");

    if (connected_) {
        backend_->Disconnect();
        connected_ = false;
    }

    if (backend_->Reconnect()) {
        connected_ = true;
        Logger::Info("Reconnected to database");
        return true;
    }

    Logger::Error("Failed to reconnect to database");
    return false;
}

void DbManager::Disconnect() {
    if (backend_ && connected_) {
        backend_->Disconnect();
        connected_ = false;
        Logger::Info("Disconnected from database");
    }
}

bool DbManager::IsConnected() const {
    return connected_ && backend_ && backend_->IsConnected();
}

int DbManager::GetShardId(uint64_t entityId) const {
    if (!backend_) {
        return 0;
    }
    return backend_->GetShardId(entityId);
}

int DbManager::GetTotalShards() const {
    if (!backend_) {
        return 1;
    }
    return backend_->GetTotalShards();
}

nlohmann::json DbManager::GetStatistics() const {
    nlohmann::json stats;

    // Basic info
    stats["type"] = DatabaseTypeToString(currentType_);
    stats["initialized"] = initialized_;
    stats["connected"] = connected_;

    if (backend_) {
        stats["connection_info"] = backend_->GetConnectionInfo();
        stats["database_stats"] = backend_->GetDatabaseStats();
        stats["active_connections"] = backend_->GetActiveConnections();
        stats["idle_connections"] = backend_->GetIdleConnections();
    }

    // Manager statistics
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - stats_.startTime).count();

    stats["uptime_seconds"] = uptime;
    stats["queries_executed"] = stats_.queriesExecuted.load();
    stats["queries_failed"] = stats_.queriesFailed.load();
    stats["transactions_committed"] = stats_.transactionsCommitted.load();
    stats["transactions_rolled_back"] = stats_.transactionsRolledBack.load();
    stats["bytes_transferred"] = stats_.bytesTransferred.load();

    if (stats_.queriesExecuted > 0) {
        double successRate = 100.0 * (1.0 - (double)stats_.queriesFailed / stats_.queriesExecuted);
        stats["success_rate_percent"] = successRate;
    }

    return stats;
}

void DbManager::PrintStatistics() const {
    auto stats = GetStatistics();

    Logger::Info("=== Database Statistics ===");
    Logger::Info("  Type: {}", stats["type"].get<std::string>());
    Logger::Info("  Status: {}", stats["connected"].get<bool>() ? "Connected" : "Disconnected");
    Logger::Info("  Uptime: {} seconds", stats["uptime_seconds"].get<int64_t>());
    Logger::Info("  ");
    Logger::Info("  Query Statistics:");
    Logger::Info("    Executed: {}", stats["queries_executed"].get<int64_t>());
    Logger::Info("    Failed: {}", stats["queries_failed"].get<int64_t>());
    if (stats.contains("success_rate_percent")) {
        Logger::Info("    Success Rate: {:.1f}%", stats["success_rate_percent"].get<double>());
    }
    Logger::Info("    Transactions Committed: {}", stats["transactions_committed"].get<int64_t>());
    Logger::Info("    Transactions Rolled Back: {}", stats["transactions_rolled_back"].get<int64_t>());
    Logger::Info("    Bytes Transferred: {}", stats["bytes_transferred"].get<int64_t>());
    Logger::Info("  ");

    if (stats.contains("database_stats")) {
        Logger::Info("  Database Statistics:");
        for (const auto& [key, value] : stats["database_stats"].items()) {
            Logger::Info("    {}: {}", key, value.dump());
        }
    }
    Logger::Info("===========================");
}

bool DbManager::RunMigrations() {
    if (!IsConnected()) {
        Logger::Error("Cannot run migrations: not connected to database");
        return false;
    }

    try {
        Logger::Info("Running database migrations...");

        // Check if migrations table exists
        nlohmann::json result = backend_->Query(
            "SELECT EXISTS (SELECT FROM information_schema.tables "
            "WHERE table_name = 'schema_migrations')");

        bool migrationsTableExists = false;
        if (!result.empty() && result[0].contains("exists")) {
            migrationsTableExists = result[0]["exists"].get<bool>();
        }

        // Create migrations table if it doesn't exist
        if (!migrationsTableExists) {
            Logger::Info("Creating migrations table...");
            backend_->Execute(
                "CREATE TABLE schema_migrations ("
                "  version INTEGER PRIMARY KEY,"
                "  name VARCHAR(255) NOT NULL,"
                "  applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
                "  checksum VARCHAR(64)"
                ")");
        }

        // Get current migration version
        int currentVersion = 0;
        result = backend_->Query("SELECT MAX(version) as current_version FROM schema_migrations");
        if (!result.empty() && result[0].contains("current_version") &&
            !result[0]["current_version"].is_null()) {
            currentVersion = result[0]["current_version"].get<int>();
        }

        Logger::Info("Current migration version: {}", currentVersion);

        // TODO: Load migration files from disk and apply them
        // This would be implemented based on your migration system

        Logger::Info("Migrations completed up to version {}", currentVersion);
        return true;

    } catch (const std::exception& e) {
        Logger::Error("Migration error: {}", e.what());
        return false;
    }
}

bool DbManager::CheckMigrationStatus() {
    if (!IsConnected()) {
        return false;
    }

    try {
        nlohmann::json result = backend_->Query(
            "SELECT version, name, applied_at FROM schema_migrations "
            "ORDER BY version DESC LIMIT 10");

        if (result.empty()) {
            Logger::Info("No migrations have been applied");
        } else {
            Logger::Info("=== Migration Status ===");
            for (const auto& row : result) {
                Logger::Info("  Version {}: {} (applied at {})",
                           row["version"].get<int>(),
                           row["name"].get<std::string>(),
                           row["applied_at"].get<std::string>());
            }
            Logger::Info("=======================");
        }

        return true;

    } catch (const std::exception& e) {
        Logger::Error("Failed to check migration status: {}", e.what());
        return false;
    }
}

bool DbManager::RollbackMigration(int version) {
    if (!IsConnected()) {
        return false;
    }

    try {
        Logger::Info("Rolling back migration version {}...", version);

        // TODO: Implement rollback logic based on your migration system
        // This would involve finding and executing the down.sql for the given version

        backend_->Execute("DELETE FROM schema_migrations WHERE version = " + std::to_string(version));

        Logger::Info("Migration version {} rolled back", version);
        return true;

    } catch (const std::exception& e) {
        Logger::Error("Rollback error: {}", e.what());
        return false;
    }
}

DbManager::DatabaseType DbManager::ParseDatabaseType(const std::string& typeStr) const {
    std::string lowerType = typeStr;
    std::transform(lowerType.begin(), lowerType.end(), lowerType.begin(), ::tolower);

    if (lowerType == "postgresql" || lowerType == "postgres") {
        return POSTGRESQL;
    } else if (lowerType == "citus") {
        return CITUS;
    }

    return INVALID;
}

std::string DbManager::DatabaseTypeToString(DatabaseType type) const {
    switch (type) {
        case POSTGRESQL: return "PostgreSQL";
        case CITUS: return "Citus";
        default: return "Unknown";
    }
}
