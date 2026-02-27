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

bool DbManager::EnsureDatabaseExists(const std::string& configPath) {
    if (!Initialize(configPath)) {
        Logger::Error("Failed to initialize DbManager");
        return false;
    }
    std::string targetDb = config_["name"].get<std::string>();
    if (!backend_->ConnectToDatabase("postgres")) {
        Logger::Error("Failed to switch to admin database 'postgres'");
        return false;
    }
    if (!backend_->Connect()) {
        Logger::Error("Failed to connect to admin database 'postgres'");
        return false;
    }
    std::string checkQuery = "SELECT 1 FROM pg_database WHERE datname = '" + targetDb + "'";
    auto result = backend_->Query(checkQuery);
    bool exists = (!result.empty() && !result[0].empty());
    if (!exists) {
        Logger::Info("Database '{}' does not exist. Creating it...", targetDb);
        std::string createSql = "CREATE DATABASE " + targetDb;
        if (config_.contains("user") && config_["user"].is_string() && !config_["user"].get<std::string>().empty()) {
            createSql += " OWNER " + config_["user"].get<std::string>();
        }
        if (!backend_->Execute(createSql)) {
            result = backend_->Query(checkQuery);
            exists = (!result.empty() && !result[0].empty());
            if (!exists) {
                Logger::Error("Failed to create database '{}'. Check permissions.", targetDb);
                backend_->Disconnect();
                return false;
            }
            Logger::Warn("Database '{}' was created by another process; proceeding.", targetDb);
        } else {
            Logger::Info("Database '{}' created successfully.", targetDb);
        }
    } else {
        Logger::Info("Database '{}' already exists.", targetDb);
    }
    if (!backend_->ConnectToDatabase(targetDb)) {
        Logger::Error("Failed to switch to target database '{}'", targetDb);
        return false;
    }
    if (!backend_->Connect()) {
        Logger::Error("Failed to connect to target database '{}'", targetDb);
        return false;
    }
    if (!CreateDefaultTablesIfNotExist()) {
        Logger::Critical("Failed to create default tables.");
        return false;
    }
    Logger::Info("Default SQL tables verified/created successfully.");
    return true;
}

bool DbManager::Initialize(const std::string& configPath) {
    if (initialized_) {
        Logger::Warn("DbManager already initialized");
        return true;
    }
    Logger::Info("Initializing DbManager...");
    if (!LoadConfiguration(configPath)) {
        Logger::Error("Failed to load database configuration");
        return false;
    }
    if (!ValidateConfiguration(config_)) {
        Logger::Error("Invalid database configuration");
        return false;
    }
    std::string typeStr = config_.value("type", "postgresql");
    currentType_ = ParseDatabaseType(typeStr);
    if (currentType_ == INVALID) {
        Logger::Error("Unknown database type: {}", typeStr);
        return false;
    }
    switch (currentType_) {
        case POSTGRESQL:
            backend_ = std::make_unique<PostgreSqlClient>(config_);
            break;
        case CITUS:
            #ifdef USE_CITUS
            backend_ = std::make_unique<CitusClient>(config_);
            #else
            Logger::Error("Citus support not compiled in. Recompile with USE_CITUS=1");
            return false;
            #endif
            break;
        default:
            Logger::Error("Unsupported database type");
            return false;
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
        backend_->ResetStats();
    }

    initialized_ = false;
    connected_ = false;

    Logger::Info("DbManager shutdown complete");
}

bool DbManager::LoadConfiguration(const std::string& configPath) {
    try {
        auto& config_mgr = ConfigManager::GetInstance();

        // Try to load from file first
        std::string path = configPath.empty() ?
        config_mgr.GetString("database.config_path", "config/database.json") :
        configPath;

        if (!path.empty()) {
            std::ifstream configFile(path);
            if (configFile.is_open()) {
                configFile >> config_;
                configFile.close();
                Logger::Debug("Database configuration loaded from: {}", path);

                // If the loaded JSON has a "database" key, use that as the actual config
                if (config_.contains("database") && config_["database"].is_object()) {
                    config_ = config_["database"];
                }
            }
        }

        // Fallback to ConfigManager if config_ is still empty
        if (config_.empty()) {
            config_ = config_mgr.GetJson("database");
            Logger::Debug("Database configuration loaded from ConfigManager: {}", config_mgr.GetConfigPath());
        }

        // Reconstruct with defaults (ensures all required fields exist)
        nlohmann::json poolConfig = config_.value("pool", nlohmann::json::object());

        config_ = {
            {"type", config_.value("type", "postgresql")},
            {"host", config_.value("host", "localhost")},
            {"port", config_.value("port", 5432)},
            {"name", config_.value("name", "game_db")},
            {"user", config_.value("user", "postgres")},
            {"password", config_.value("password", "")},
            {"connection_pool", {
                {"enabled", poolConfig.value("enabled", true)},
                {"min_connections", poolConfig.value("min", 5)},
                {"max_connections", poolConfig.value("max", 20)}
            }},
            {"shards", config_.value("shards", 32)},
            {"replication", config_.value("replication", false)},
            {"ssl", config_.value("ssl", false)},
            {"timeout", config_.value("timeout", 30)}
        };

        Logger::Debug("Database configuration finalized");
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

        if (!config.contains("name") || !config["name"].is_string()) {
            Logger::Error("Missing or invalid 'name' in database configuration");
            return false;
        }

        if (!config.contains("user") || !config["user"].is_string()) {
            Logger::Error("Missing or invalid 'user' in database configuration");
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

std::string DbManager::EscapeString(const std::string& input) {
    // Use your database client's escaping function.
    // For PostgreSQL via libpq, you might use PQescapeLiteral.
    // For simplicity, this example doubles single quotes.
    std::string escaped;
    for (char c : input) {
        if (c == '\'') escaped += "''";
        else escaped += c;
    }
    return escaped;
}

bool DbManager::SaveGameState(const std::string& key, const nlohmann::json& state) {
    return backend_->SaveGameState(key, state);
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

    // The master process has already ensured the database exists.
    // Simply attempt to connect.
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
    stats["initialized"] = initialized_.load();
    stats["connected"] = connected_.load();

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

bool DbManager::TableExists(const std::string& tableName) {
    std::string sql = "SELECT EXISTS ("
    "SELECT FROM information_schema.tables "
    "WHERE table_schema = 'public' AND table_name = '" + tableName + "'"
    ") AS exists;";
        try {
            nlohmann::json result = backend_->Query(sql);
            if (result.empty() || !result[0].contains("exists")) {
                return false;
            }

            const auto& existsVal = result[0]["exists"];
            if (existsVal.is_boolean()) {
                return existsVal.get<bool>();
            } else if (existsVal.is_string()) {
                std::string str = existsVal.get<std::string>();
                // PostgreSQL returns 't' for true, 'f' for false
                return str == "t" || str == "true" || str == "1";
            } else if (existsVal.is_number()) {
                return existsVal.get<int>() != 0;
            }
            return false;
        } catch (const std::exception& e) {
            Logger::Error("Failed to check existence of table {}: {}", tableName, e.what());
            return false;
        }
}

bool DbManager::ExecuteCreateTable(const std::string& tableName, const std::string& createSql) {
    if (TableExists(tableName)) {
        Logger::Debug("Table '{}' already exists, skipping creation.", tableName);
        return true;
    }

    Logger::Info("Creating table '{}'...", tableName);
    try {
        backend_->Query(createSql);
        Logger::Info("Table '{}' created successfully.", tableName);
        return true;
    } catch (const std::exception& e) {
        std::string error = e.what();
        // Check for PostgreSQL "duplicate key" error on pg_type (error code 23505)
        // This can happen when two sessions create the same table concurrently.
        if (error.find("23505") != std::string::npos ||
            error.find("duplicate key value") != std::string::npos) {
            Logger::Warn("Table '{}' was created concurrently by another process; treating as success.", tableName);
        return true;
            }
            Logger::Error("Failed to create table '{}': {}", tableName, e.what());
            return false;
    }
}

bool DbManager::CreateDefaultTablesIfNotExist() {
    if (!IsConnected() && !Connect()) {
        Logger::Error("Cannot create default tables: not connected to database.");
        return false;
    }

    bool success = true;

    success &= ExecuteCreateTable("players", R"(
        CREATE TABLE IF NOT EXISTS players (
            id BIGINT PRIMARY KEY,
            data JSONB NOT NULL,
            created_at TIMESTAMPTZ DEFAULT NOW(),
            updated_at TIMESTAMPTZ DEFAULT NOW()
        );
        CREATE INDEX IF NOT EXISTS idx_players_updated ON players(updated_at);
    )");

    success &= ExecuteCreateTable("player_inventory", R"(
        CREATE TABLE IF NOT EXISTS player_inventory (
            player_id BIGINT REFERENCES players(id) ON DELETE CASCADE,
            slot INT NOT NULL,
            item_id BIGINT NOT NULL,
            quantity INT NOT NULL DEFAULT 1,
            data JSONB,  -- additional item-specific data (durability, enchantments, etc.)
            PRIMARY KEY (player_id, slot)
        );
        CREATE INDEX IF NOT EXISTS idx_inventory_player ON player_inventory(player_id);
    )");

    success &= ExecuteCreateTable("player_skills", R"(
        CREATE TABLE IF NOT EXISTS player_skills (
            player_id BIGINT REFERENCES players(id) ON DELETE CASCADE,
            skill_id VARCHAR(64) NOT NULL,
            level INT NOT NULL DEFAULT 1,
            experience FLOAT NOT NULL DEFAULT 0,
            data JSONB,
            PRIMARY KEY (player_id, skill_id)
        );
        CREATE INDEX IF NOT EXISTS idx_skills_player ON player_skills(player_id);
    )");

    success &= ExecuteCreateTable("player_quests", R"(
        CREATE TABLE IF NOT EXISTS player_quests (
            player_id BIGINT REFERENCES players(id) ON DELETE CASCADE,
            quest_id BIGINT NOT NULL,
            state INT NOT NULL,  -- QuestState enum as integer
            progress JSONB NOT NULL,
            started_at TIMESTAMPTZ,
            completed_at TIMESTAMPTZ,
            PRIMARY KEY (player_id, quest_id)
        );
        CREATE INDEX IF NOT EXISTS idx_quests_player ON player_quests(player_id);
        CREATE INDEX IF NOT EXISTS idx_quests_state ON player_quests(state);
    )");

    success &= ExecuteCreateTable("world_chunks", R"(
        CREATE TABLE IF NOT EXISTS world_chunks (
            chunk_x INT NOT NULL,
            chunk_z INT NOT NULL,
            biome INT NOT NULL,
            data JSONB NOT NULL,  -- serialized WorldChunk data
            generated_at TIMESTAMPTZ DEFAULT NOW(),
            PRIMARY KEY (chunk_x, chunk_z)
        );
        CREATE INDEX IF NOT EXISTS idx_chunks_coords ON world_chunks(chunk_x, chunk_z);
    )");

    success &= ExecuteCreateTable("npcs", R"(
        CREATE TABLE IF NOT EXISTS npcs (
            id BIGINT PRIMARY KEY,
            type INT NOT NULL,
            position JSONB NOT NULL,  -- {x, y, z}
            level INT NOT NULL DEFAULT 1,
            data JSONB NOT NULL,      -- stats, AI state, loot table, etc.
            created_at TIMESTAMPTZ DEFAULT NOW(),
            updated_at TIMESTAMPTZ DEFAULT NOW()
        );
        CREATE INDEX IF NOT EXISTS idx_npcs_type ON npcs(type);
    )");

    success &= ExecuteCreateTable("loot_tables", R"(
        CREATE TABLE IF NOT EXISTS loot_tables (
            table_id VARCHAR(64) PRIMARY KEY,
            name VARCHAR(128) NOT NULL,
            data JSONB NOT NULL,  -- entries, drop chances, etc.
            created_at TIMESTAMPTZ DEFAULT NOW()
        );
    )");

    success &= ExecuteCreateTable("game_state", R"(
        CREATE TABLE IF NOT EXISTS game_state (
            key VARCHAR(64) PRIMARY KEY,
            value JSONB NOT NULL,
            updated_at TIMESTAMPTZ DEFAULT NOW()
        );
    )");

#ifdef USE_CITUS
    if (currentType_ == DatabaseType::CITUS) {
        try {
            // Distribute tables by player_id or appropriate shard key
            backend_->Query("SELECT create_distributed_table('players', 'id');");
            backend_->Query("SELECT create_distributed_table('player_inventory', 'player_id');");
            backend_->Query("SELECT create_distributed_table('player_skills', 'player_id');");
            backend_->Query("SELECT create_distributed_table('player_quests', 'player_id');");
            // World chunks could be distributed by (chunk_x, chunk_z) – Citus supports hash distribution on multiple columns
            backend_->Query("SELECT create_distributed_table('world_chunks', 'chunk_x');");
            backend_->Query("SELECT create_distributed_table('npcs', 'id');");
            Logger::Info("Citus distribution created for main tables.");
        } catch (const std::exception& e) {
            Logger::Warn("Failed to distribute tables for Citus: {}", e.what());
        }
    }
#endif

    if (success) {
        Logger::Info("All default tables verified/created successfully.");
    } else {
        Logger::Error("Some tables could not be created. Check logs for details.");
    }

    return success;
}

bool DbManager::CheckDefaultTablesExist() {
    std::vector<std::string> requiredTables = {
        "players", "player_inventory", "player_skills", "player_quests",
        "world_chunks", "npcs", "loot_tables", "game_state"
    };
    for (const auto& table : requiredTables) {
        if (!TableExists(table)) {
            Logger::Warn("Required table '{}' does not exist.", table);
            return false;
        }
    }
    return true;
}
