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

bool DbManager::LoadSQLForBackend() {
    std::string sqlPath = "dbschema/";  // configurable base path
    switch (currentType_) {
        case SQLITE:
            sqlPath += "sqlite.sql";
            break;
        case POSTGRESQL:
            sqlPath += "postgres.sql";
            break;
        case CITUS:
            // Load base PostgreSQL first, then Citus extensions
            if (!sqlProvider_.LoadFromFile(sqlPath + "postgres.sql")) {
                return false;
            }
            sqlPath += "citus.sql";
            break;
        default:
            return false;
    }
    if (currentType_ != CITUS) {
        if (!sqlProvider_.LoadFromFile(sqlPath)) {
            Logger::Error("Could not load SQL file: {}", sqlPath);
            return false;
        }
    } else {
        // Load Citus-specific file
        if (!sqlProvider_.LoadFromFile(sqlPath)) {
            Logger::Warn("Citus SQL file not loaded, some features may be unavailable");
        }
    }
    return true;
}

bool DbManager::EnsureDatabaseExists(const std::string& configPath) {
    if (!Initialize(configPath)) {
        Logger::Error("Failed to initialize DbManager");
        return false;
    }

    std::string targetDb = config_["name"].get<std::string>(); // For SQLite this is a file path

    if (currentType_ == SQLITE) { // --- SQLite handling ---
        std::filesystem::path path(targetDb);
        std::filesystem::path dir = path.parent_path();
        if (!dir.empty() &&
            !std::filesystem::exists(dir)) { // Ensure directory for db file exists
            std::filesystem::create_directories(dir);
        }
        if (!backend_->Connect()) { // Connect – this will create the file if it doesn't exist
            Logger::Error("Failed to connect to SQLite database at '{}'", targetDb);
            return false;
        }
        if (!CreateDefaultTablesIfNotExist()) { // Create default tables
            Logger::Critical("Failed to create default tables for SQLite.");
            backend_->Disconnect();
            return false;
        }
        backend_->Disconnect();
        Logger::Info("SQLite database file ready and tables created: {}", targetDb);
        return true;
    }

    if (!backend_->ConnectToDatabase("postgres")) { // --- PostgreSQL / Citus handling ---
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
            // Re-check in case another process created it concurrently
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

    // Switch to the target database
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
    std::string backendStr = config_.value("backend", "postgresql");
    currentType_ = ParseBackendType(backendStr);
    if (currentType_ == INVALID) {
        Logger::Error("Unknown database backend: {}", backendStr);
        return false;
    }

    if (!LoadSQLForBackend()) {
        Logger::Error("Failed to load SQL queries for backend");
        return false;
    }

    switch (currentType_) {
        case SQLITE:
#ifdef USE_SQLITE
            backend_ = std::make_unique<SQLiteClient>(config_, sqlProvider_);
#else
            Logger::Error("SQLite support not compiled in. Recompile with USE_SQLITE=1");
            return false;
#endif
            break;
        case POSTGRESQL:
#ifdef USE_POSTGRESQL
            backend_ = std::make_unique<PostgreSqlClient>(config_, sqlProvider_);
#else
            Logger::Error("PostgreSql support not compiled in. Recompile with USE_POSTGRESQL=1");
#endif
            break;
        case CITUS:
#ifdef USE_CITUS
            backend_ = std::make_unique<CitusClient>(config_, sqlProvider_);
#else
            Logger::Error("Citus support not compiled in. Recompile with USE_CITUS=1");
            return false;
#endif
            break;
        default:
            Logger::Error("Unsupported database backend");
            return false;
    }
    initialized_ = true;
    Logger::Info("DbManager initialized with {} backend", BackendTypeToString(currentType_));
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
            {"backend", config_.value("backend", "postgresql")},
            {"host", config_.value("host", "127.0.0.1")},
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
        if (config.contains("host") && !config["host"].is_string()) {
            Logger::Error("Invalid 'host' in database configuration (must be string)");
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
        // Port is optional (default 5432), but if present must be valid
        if (config.contains("port")) {
            if (!config["port"].is_number()) {
                Logger::Error("Invalid 'port' in database configuration (must be number)");
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

bool DbManager::SetBackend(BackendType backendType, const nlohmann::json& config) {
    std::lock_guard<std::mutex> lock(instanceMutex_);

    if (!ValidateConfiguration(config)) {
        Logger::Error("Invalid configuration for new backend");
        return false;
    }

    // Temporarily store old backend (not needed but kept for safety)
    std::unique_ptr<DatabaseBackend> oldBackend = std::move(backend_);

    // Update current type and config
    currentType_ = backendType;
    config_ = config;

    // Reload SQL for the new backend
    if (!LoadSQLForBackend()) {
        Logger::Error("Failed to load SQL queries for new backend");
        // Restore old backend? We'll just return false and leave backend_ empty
        return false;
    }

    // Create new backend with the provider
    switch (backendType) {
        case SQLITE:
#ifdef USE_SQLITE
            backend_ = std::make_unique<SQLiteClient>(config_, sqlProvider_);
#else
            Logger::Error("SQLite support not compiled in.");
            return false;
#endif
            break;
        case POSTGRESQL:
#ifdef USE_POSTGRESQL
            backend_ = std::make_unique<PostgreSqlClient>(config_, sqlProvider_);
#else
            Logger::Error("PostgreSql support not compiled in.");
            return false;
#endif
            break;
        case CITUS:
#ifdef USE_CITUS
            backend_ = std::make_unique<CitusClient>(config_, sqlProvider_);
#else
            Logger::Error("Citus support not compiled in.");
            return false;
#endif
            break;
        default:
            Logger::Error("Unsupported database backend");
            return false;
    }

    connected_ = false; // Will need to reconnect
    Logger::Info("Database backend changed to {}", BackendTypeToString(currentType_));
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
            Logger::Info("Connected to {} database", BackendTypeToString(currentType_));
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
    stats["backend"] = BackendTypeToString(currentType_);
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
    Logger::Info("  Backend: {}", stats["backend"].get<std::string>());
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
        Logger::Error("Cannot rollback migration: not connected to database");
        return false;
    }

    // Path to migrations directory – could be configurable
    const std::string migrationsDir = "migrations/";
    std::string downFileName = "U" + std::to_string(version) + "*.sql"; // wildcard for description

    try {
        // Find the actual down file (there should be exactly one)
        std::vector<std::filesystem::path> downFiles;
        for (const auto& entry : std::filesystem::directory_iterator(migrationsDir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (filename.rfind("U" + std::to_string(version), 0) == 0 &&
                    filename.size() > 3 && filename.substr(filename.size() - 4) == ".sql") {
                    downFiles.push_back(entry.path());
                    }
            }
        }

        if (downFiles.empty()) {
            Logger::Error("No down migration found for version {}", version);
            return false;
        }
        if (downFiles.size() > 1) {
            Logger::Error("Multiple down migrations found for version {}: {}", version, downFiles.size());
            return false;
        }

        // Read the down SQL script
        std::ifstream file(downFiles[0]);
        if (!file.is_open()) {
            Logger::Error("Failed to open down migration file: {}", downFiles[0].string());
            return false;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string downSql = buffer.str();

        Logger::Info("Rolling back migration version {} using file: {}", version, downFiles[0].string());

        // Execute the down script within a transaction
        if (!backend_->BeginTransaction()) {
            Logger::Error("Failed to begin transaction for rollback");
            return false;
        }

        bool success = false;
        try {
            // Execute the down SQL (may contain multiple statements)
            // Simple approach: execute whole script. If your backend doesn't support multiple statements,
            // you may need to split by ';'. We'll assume Execute handles batches.
            if (backend_->Execute(downSql)) {
                // Remove the migration record
                std::string deleteSql = "DELETE FROM schema_migrations WHERE version = " + std::to_string(version);
                if (backend_->Execute(deleteSql)) {
                    success = true;
                } else {
                    Logger::Error("Failed to delete migration record for version {}", version);
                }
            } else {
                Logger::Error("Failed to execute down migration SQL for version {}", version);
            }

            if (success) {
                if (!backend_->CommitTransaction()) {
                    Logger::Error("Failed to commit transaction during rollback");
                    success = false;
                }
            } else {
                backend_->RollbackTransaction();
            }
        } catch (const std::exception& e) {
            Logger::Error("Exception during rollback: {}", e.what());
            backend_->RollbackTransaction();
            return false;
        }

        if (success) {
            Logger::Info("Migration version {} rolled back successfully", version);
        }
        return success;

    } catch (const std::exception& e) {
        Logger::Error("Rollback error: {}", e.what());
        return false;
    }
}

DbManager::BackendType DbManager::ParseBackendType(const std::string& backendStr) const {
    std::string lowerType = backendStr;
    std::transform(lowerType.begin(), lowerType.end(), lowerType.begin(), ::tolower);

    if (lowerType == "sqlite") {
        return SQLITE;
    } else if (lowerType == "postgresql" || lowerType == "postgres") {
        return POSTGRESQL;
    } else if (lowerType == "citus") {
        return CITUS;
    }

    return INVALID;
}

std::string DbManager::BackendTypeToString(BackendType backendType) const {
    switch (backendType) {
        case SQLITE: return "SQLite";
        case POSTGRESQL: return "PostgreSQL";
        case CITUS: return "Citus";
        default: return "Unknown";
    }
}

bool DbManager::TableExists(const std::string& tableName) {
    try {
        nlohmann::json result;
        if (currentType_ == SQLITE) { // SQLite: query sqlite_master
            std::string sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='" +
            EscapeString(tableName) + "';";
            result = backend_->Query(sql);
            return !result.empty();
        } else { // PostgreSQL / Citus: use information_schema
            std::string sql = "SELECT EXISTS ("
            "SELECT FROM information_schema.tables "
            "WHERE table_schema = 'public' AND table_name = '" +
            EscapeString(tableName) + "') AS exists;";
            result = backend_->Query(sql);
            if (result.empty() || !result[0].contains("exists")) {
                return false;
            }
            const auto& existsVal = result[0]["exists"];
            if (existsVal.is_boolean()) {
                return existsVal.get<bool>();
            } else if (existsVal.is_string()) {
                std::string str = existsVal.get<std::string>();
                return str == "t" || str == "true" || str == "1";
            } else if (existsVal.is_number()) {
                return existsVal.get<int>() != 0;
            }
            return false;
        }
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
        backend_->Execute(createSql);
        Logger::Info("Table '{}' created successfully.", tableName);
        return true;
    } catch (const std::exception& e) {
        Logger::Error("Failed to create table '{}': {}", tableName, e.what());
        return false;
    }
}


bool DbManager::CreateDefaultTablesIfNotExist() {
    if (!IsConnected() && !Connect()) return false;

    bool success = true;
    std::vector<std::string> tableQueries = {
        "create_table_players",
        "create_table_game_state",
        "create_table_world_chunks",
        "create_table_player_inventory",
        "create_table_player_quests",
        "create_table_npcs",
        "create_table_loot_tables",
        "create_table_schema_migrations"
    };

    for (const auto& key : tableQueries) {
        std::string sql = sqlProvider_.GetQuery(key);
        if (sql.empty()) {
            Logger::Error("Missing SQL for table creation: {}", key);
            success = false;
            continue;
        }
        if (!backend_->Execute(sql)) {
            Logger::Error("Failed to execute: {}", key);
            success = false;
        }
    }

    #ifdef USE_CITUS
    if (currentType_ == CITUS) {
        std::vector<std::string> distQueries = {
            "create_distributed_table_players",
            "create_distributed_table_player_inventory",
            "create_distributed_table_player_quests",
            "create_distributed_table_world_chunks",
            "create_distributed_table_npcs",
            "create_reference_table_loot_tables"
        };
        for (const auto& key : distQueries) {
            std::string sql = sqlProvider_.GetQuery(key);
            if (!sql.empty()) {
                backend_->Execute(sql);  // ignore failure if table already distributed
            }
        }
    }
    #endif

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
