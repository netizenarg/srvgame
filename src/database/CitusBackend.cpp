#include <cstring>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <random>
#include <future>

#include "database/CitusBackend.hpp"
#include "config/ConfigManager.hpp"
#include "logging/Logger.hpp"

// =============== CitusBackend Implementation ===============

CitusBackend::CitusBackend() {
    Logger::Debug("CitusBackend created");
}

CitusBackend::~CitusBackend() {
    Logger::Debug("CitusBackend destroyed");
}

bool CitusBackend::Initialize(const std::string& coordinatorConnInfo,
                             const std::vector<std::string>& workerNodes) {
    if (initialized_) {
        Logger::Warn("CitusBackend already initialized");
        return true;
    }

    Logger::Info("Initializing Citus backend...");

    // Initialize coordinator pool
    if (!coordinatorPool_.Initialize(coordinatorConnInfo, 5)) {
        Logger::Critical("Failed to initialize coordinator connection pool");
        return false;
    }

    // Test coordinator connection
    if (!TestCoordinatorConnection()) {
        Logger::Critical("Coordinator connection test failed");
        return false;
    }

    // Initialize worker pools
    if (!InitializeWorkerPools(workerNodes)) {
        Logger::Warn("Failed to initialize some worker pools");
        // Continue anyway - some operations might still work
    }

    // Load shard information
    if (!LoadShardInformation()) {
        Logger::Error("Failed to load shard information");
    }

    // Create required distributed tables if they don't exist
    if (!CreateDefaultTables()) {
        Logger::Error("Failed to create default tables");
    }

    initialized_ = true;
    Logger::Info("Citus backend initialized successfully");
    return true;
}

bool CitusBackend::TestConnection() {
    return TestCoordinatorConnection();
}

bool CitusBackend::TestCoordinatorConnection() {
    auto result = coordinatorPool_.Query("SELECT 1");
    if (!result) {
        Logger::Error("Coordinator connection test failed");
        return false;
    }

    PQclear(result);
    Logger::Debug("Coordinator connection test successful");
    return true;
}

void CitusBackend::ReconnectAll() {
    Logger::Info("Reconnecting all Citus database connections...");

    // Reconnect coordinator
    coordinatorPool_.RecycleAllConnections();

    // Reconnect workers
    std::lock_guard<std::mutex> lock(workerPoolsMutex_);
    for (auto& [nodeKey, pool] : workerPools_) {
        pool->RecycleAllConnections();
    }

    // Reload shard information
    LoadShardInformation();

    Logger::Info("Citus reconnection completed");
}

bool CitusBackend::CheckHealth() {
    bool healthy = TestCoordinatorConnection();

    // Check workers
    std::lock_guard<std::mutex> lock(workerPoolsMutex_);
    for (const auto& [nodeKey, pool] : workerPools_) {
        if (!pool->TestConnection()) {
            Logger::Error("Worker {} is not healthy", nodeKey);
            healthy = false;
        }
    }

    return healthy;
}

bool CitusBackend::CreateTable(const std::string& tableName,
                              const std::string& schema) {
    std::string query = "CREATE TABLE IF NOT EXISTS " + tableName + " " + schema;
    return ExecuteCoordinatorCommand(query);
}

bool CitusBackend::CreateDistributedTable(const std::string& tableName,
                                         const std::string& distributionColumn,
                                         const std::string& distributionType) {
    Logger::Info("Creating distributed table: {} (distribution column: {})",
                tableName, distributionColumn);

    // Check if table already exists
    if (TableExists(tableName)) {
        Logger::Debug("Table {} already exists", tableName);
        return true;
    }

    // Create table based on name
    std::string createTableQuery;

    if (tableName == "players") {
        createTableQuery = R"(
            CREATE TABLE players (
                player_id BIGSERIAL PRIMARY KEY,
                username VARCHAR(50) UNIQUE NOT NULL,
                email VARCHAR(100) UNIQUE NOT NULL,
                password_hash VARCHAR(255) NOT NULL,
                created_at TIMESTAMP DEFAULT NOW(),
                last_login TIMESTAMP,
                last_logout TIMESTAMP,
                total_playtime INTEGER DEFAULT 0,
                level INTEGER DEFAULT 1,
                experience BIGINT DEFAULT 0,
                score INTEGER DEFAULT 0,
                currency_gold INTEGER DEFAULT 100,
                currency_gems INTEGER DEFAULT 10,
                position_x FLOAT DEFAULT 0,
                position_y FLOAT DEFAULT 0,
                position_z FLOAT DEFAULT 0,
                health INTEGER DEFAULT 100,
                max_health INTEGER DEFAULT 100,
                mana INTEGER DEFAULT 100,
                max_mana INTEGER DEFAULT 100,
                attributes JSONB DEFAULT '{}',
                inventory JSONB DEFAULT '[]',
                equipment JSONB DEFAULT '{}',
                quests JSONB DEFAULT '{}',
                achievements JSONB DEFAULT '{}',
                settings JSONB DEFAULT '{}',
                banned BOOLEAN DEFAULT FALSE,
                ban_reason TEXT,
                ban_expires TIMESTAMP,
                online BOOLEAN DEFAULT FALSE,
                last_heartbeat TIMESTAMP,
                ip_address INET,
                session_id VARCHAR(100),
                metadata JSONB DEFAULT '{}'
            )
        )";
    } else if (tableName == "player_items") {
        createTableQuery = R"(
            CREATE TABLE player_items (
                item_id BIGSERIAL PRIMARY KEY,
                player_id BIGINT NOT NULL,
                item_def_id INTEGER NOT NULL,
                quantity INTEGER DEFAULT 1,
                durability INTEGER,
                max_durability INTEGER,
                enchant_level INTEGER DEFAULT 0,
                attributes JSONB DEFAULT '{}',
                created_at TIMESTAMP DEFAULT NOW(),
                acquired_from VARCHAR(50),
                expires_at TIMESTAMP,
                metadata JSONB DEFAULT '{}'
            )
        )";
    } else if (tableName == "game_events") {
        createTableQuery = R"(
            CREATE TABLE game_events (
                event_id BIGSERIAL PRIMARY KEY,
                game_id BIGINT NOT NULL,
                player_id BIGINT,
                event_type VARCHAR(50) NOT NULL,
                event_data JSONB NOT NULL,
                severity INTEGER DEFAULT 0,
                created_at TIMESTAMP DEFAULT NOW(),
                processed BOOLEAN DEFAULT FALSE,
                metadata JSONB DEFAULT '{}'
            )
        )";
    } else {
        // Generic table creation
        createTableQuery = "CREATE TABLE " + tableName + " (id BIGSERIAL PRIMARY KEY)";
    }

    if (!ExecuteCoordinatorCommand(createTableQuery)) {
        Logger::Error("Failed to create table: {}", tableName);
        return false;
    }

    // Distribute the table
    std::string distributeQuery;
    if (distributionType == "hash") {
        distributeQuery = "SELECT create_distributed_table('" + tableName + "', '" +
                         distributionColumn + "', 'hash')";
    } else if (distributionType == "range") {
        distributeQuery = "SELECT create_distributed_table('" + tableName + "', '" +
                         distributionColumn + "', 'range')";
    } else {
        distributeQuery = "SELECT create_distributed_table('" + tableName + "', '" +
                         distributionColumn + "')";
    }

    if (!ExecuteCoordinatorCommand(distributeQuery)) {
        Logger::Error("Failed to distribute table: {}", tableName);
        return false;
    }

    Logger::Info("Distributed table created: {}", tableName);
    return true;
}

bool CitusBackend::CreateReferenceTable(const std::string& tableName) {
    Logger::Info("Creating reference table: {}", tableName);

    // Check if table already exists
    if (TableExists(tableName)) {
        Logger::Debug("Table {} already exists", tableName);
        return true;
    }

    // Create table based on name
    std::string createTableQuery;

    if (tableName == "game_config") {
        createTableQuery = R"(
            CREATE TABLE game_config (
                config_key VARCHAR(100) PRIMARY KEY,
                config_value TEXT NOT NULL,
                config_type VARCHAR(20) DEFAULT 'string',
                description TEXT,
                updated_at TIMESTAMP DEFAULT NOW(),
                updated_by VARCHAR(50)
            )
        )";
    } else if (tableName == "item_definitions") {
        createTableQuery = R"(
            CREATE TABLE item_definitions (
                item_def_id SERIAL PRIMARY KEY,
                item_name VARCHAR(100) NOT NULL,
                item_type VARCHAR(50) NOT NULL,
                item_rarity VARCHAR(20) DEFAULT 'common',
                base_value INTEGER DEFAULT 0,
                weight FLOAT DEFAULT 0,
                stackable BOOLEAN DEFAULT TRUE,
                max_stack INTEGER DEFAULT 99,
                usable BOOLEAN DEFAULT FALSE,
                consumable BOOLEAN DEFAULT FALSE,
                equippable BOOLEAN DEFAULT FALSE,
                equipment_slot VARCHAR(50),
                attributes JSONB DEFAULT '{}',
                requirements JSONB DEFAULT '{}',
                effects JSONB DEFAULT '{}',
                icon_url VARCHAR(255),
                model_url VARCHAR(255),
                created_at TIMESTAMP DEFAULT NOW(),
                updated_at TIMESTAMP DEFAULT NOW(),
                active BOOLEAN DEFAULT TRUE
            )
        )";
    } else {
        // Generic reference table
        createTableQuery = "CREATE TABLE " + tableName + " (id SERIAL PRIMARY KEY)";
    }

    if (!ExecuteCoordinatorCommand(createTableQuery)) {
        Logger::Error("Failed to create reference table: {}", tableName);
        return false;
    }

    // Make it a reference table
    std::string referenceQuery = "SELECT create_reference_table('" + tableName + "')";

    if (!ExecuteCoordinatorCommand(referenceQuery)) {
        Logger::Error("Failed to create reference table: {}", tableName);
        return false;
    }

    // Insert default data for game_config
    if (tableName == "game_config") {
        InsertDefaultConfig();
    }

    Logger::Info("Reference table created: {}", tableName);
    return true;
}

bool CitusBackend::TableExists(const std::string& tableName) {
    std::string query = "SELECT EXISTS ("
                       "SELECT FROM pg_tables "
                       "WHERE tablename = '" + tableName + "')";
    
    auto result = ExecuteCoordinatorQuery(query);
    return !result.empty() && result[0]["exists"].get<bool>();
}

nlohmann::json CitusBackend::Query(const std::string& query) {
    return ExecuteCoordinatorQuery(query);
}

bool CitusBackend::Execute(const std::string& query) {
    return ExecuteCoordinatorCommand(query);
}

nlohmann::json CitusBackend::QueryShard(int shardId, const std::string& query) {
    auto pool = GetShardPool(shardId);
    if (!pool) {
        // Fall back to coordinator
        return ExecuteCoordinatorQuery(query);
    }

    auto result = pool->Query(query);
    return PGResultToJson(result);
}

nlohmann::json CitusBackend::QueryAllShards(const std::string& query) {
    nlohmann::json allResults = nlohmann::json::array();

    // Get all unique worker nodes
    std::unordered_set<std::string> workerNodes;
    {
        std::lock_guard<std::mutex> lock(shardsMutex_);
        for (const auto& shard : shards_) {
            std::string nodeKey = shard.node_name + ":" + std::to_string(shard.node_port);
            workerNodes.insert(nodeKey);
        }
    }

    // Execute query on each worker in parallel
    std::vector<std::future<nlohmann::json>> futures;

    for (const auto& nodeKey : workerNodes) {
        futures.push_back(std::async(std::launch::async, [this, nodeKey, query]() {
            std::lock_guard<std::mutex> poolLock(workerPoolsMutex_);
            auto it = workerPools_.find(nodeKey);
            if (it != workerPools_.end()) {
                auto result = it->second->Query(query);
                return PGResultToJson(result);
            }
            return nlohmann::json::array();
        }));
    }

    // Collect results
    for (auto& future : futures) {
        try {
            auto results = future.get();
            if (results.is_array()) {
                for (const auto& result : results) {
                    allResults.push_back(result);
                }
            }
        } catch (const std::exception& e) {
            Logger::Error("Error querying worker: {}", e.what());
        }
    }

    return allResults;
}

// Player data management methods (implementation similar to PostgreSQLBackend but using coordinator)
bool CitusBackend::CreatePlayer(const nlohmann::json& playerData) {
    // Similar to PostgreSQLBackend::CreatePlayer but using coordinator
    try {
        std::string username = playerData["username"].get<std::string>();
        std::string email = playerData["email"].get<std::string>();
        std::string passwordHash = playerData["password_hash"].get<std::string>();
        
        std::string query = R"(
            INSERT INTO players (
                username, email, password_hash,
                created_at, position_x, position_y, position_z,
                attributes, inventory, equipment
            ) VALUES (
                ')" + EscapeString(username) + R"(',
                ')" + EscapeString(email) + R"(',
                ')" + EscapeString(passwordHash) + R"(',
                NOW(), 0, 0, 0,
                '{}', '[]', '{}'
            )
        )";
        
        return ExecuteCoordinatorCommand(query);
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to create player: {}", e.what());
        return false;
    }
}

nlohmann::json CitusBackend::GetPlayer(int64_t playerId) {
    std::string query = "SELECT * FROM players WHERE player_id = " + std::to_string(playerId);
    auto result = ExecuteCoordinatorQuery(query);
    return result.empty() ? nlohmann::json() : result[0];
}

bool CitusBackend::UpdatePlayer(int64_t playerId, const nlohmann::json& updates) {
    if (updates.empty()) {
        return true;
    }
    
    try {
        std::stringstream query;
        query << "UPDATE players SET ";
        
        bool first = true;
        for (auto it = updates.begin(); it != updates.end(); ++it) {
            if (!first) query << ", ";
            first = false;
            
            if (it.value().is_string()) {
                query << it.key() << " = '" << EscapeString(it.value().get<std::string>()) << "'";
            } else if (it.value().is_number_integer()) {
                query << it.key() << " = " << it.value().get<int64_t>();
            } else if (it.value().is_number_float()) {
                query << it.key() << " = " << it.value().get<double>();
            } else if (it.value().is_boolean()) {
                query << it.key() << " = " << (it.value().get<bool>() ? "TRUE" : "FALSE");
            } else if (it.value().is_null()) {
                query << it.key() << " = NULL";
            } else {
                query << it.key() << " = '" << EscapeString(it.value().dump()) << "'";
            }
        }
        
        query << " WHERE player_id = " << playerId;
        
        return ExecuteCoordinatorCommand(query.str());
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to update player {}: {}", playerId, e.what());
        return false;
    }
}

bool CitusBackend::DeletePlayer(int64_t playerId) {
    std::string query = "DELETE FROM players WHERE player_id = " + std::to_string(playerId);
    return ExecuteCoordinatorCommand(query);
}

bool CitusBackend::SaveGameState(int64_t gameId, const nlohmann::json& gameState) {
    try {
        std::string stateJson = gameState.dump();
        
        std::string query = R"(
            INSERT INTO game_states (game_id, state_data, updated_at)
            VALUES ()" + std::to_string(gameId) + R"(,
                    ')" + EscapeString(stateJson) + R"(',
                    NOW())
            ON CONFLICT (game_id)
            DO UPDATE SET
                state_data = EXCLUDED.state_data,
                updated_at = NOW()
        )";
        
        return ExecuteCoordinatorCommand(query);
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to save game state {}: {}", gameId, e.what());
        return false;
    }
}

nlohmann::json CitusBackend::LoadGameState(int64_t gameId) {
    std::string query = "SELECT state_data FROM game_states WHERE game_id = " + std::to_string(gameId);
    auto result = ExecuteCoordinatorQuery(query);
    
    if (result.empty()) {
        return nlohmann::json();
    }
    
    try {
        std::string stateJson = result[0]["state_data"].get<std::string>();
        return nlohmann::json::parse(stateJson);
    } catch (const std::exception& e) {
        Logger::Error("Failed to parse game state: {}", e.what());
        return nlohmann::json();
    }
}

// Other methods implementation would follow similar pattern...
// Due to space constraints, showing the pattern for a few methods

bool CitusBackend::InitializeWorkerPools(const std::vector<std::string>& workerNodes) {
    std::lock_guard<std::mutex> lock(workerPoolsMutex_);

    workerPools_.clear();

    for (size_t i = 0; i < workerNodes.size(); ++i) {
        const auto& node = workerNodes[i];

        size_t colonPos = node.find(':');
        if (colonPos == std::string::npos) {
            Logger::Error("Invalid worker node format (expected host:port): {}", node);
            continue;
        }

        std::string host = node.substr(0, colonPos);
        std::string port = node.substr(colonPos + 1);

        auto& config = ConfigManager::GetInstance();
        std::string connInfo =
            "host=" + host +
            " port=" + port +
            " dbname=" + config.GetDatabaseName() +
            " user=" + config.GetDatabaseUser() +
            " password=" + config.GetDatabasePassword() +
            " connect_timeout=5";

        auto workerPool = std::make_shared<DatabasePool>();
        if (workerPool->Initialize(connInfo, 3)) {
            workerPools_[node] = workerPool;
            Logger::Debug("Worker pool initialized for node: {}", node);
        } else {
            Logger::Error("Failed to initialize worker pool for node: {}", node);
        }
    }

    Logger::Info("Initialized {} worker pools out of {} nodes",
                workerPools_.size(), workerNodes.size());
    return !workerPools_.empty();
}

bool CitusBackend::LoadShardInformation() {
    Logger::Info("Loading shard information from coordinator...");

    const char* query = R"(
        SELECT
            shardid,
            shard_name,
            nodename,
            nodeport,
            table_name,
            distribution_column
        FROM citus_shards
        JOIN pg_dist_partition ON logicalrelid = table_name::regclass
        WHERE table_name IN ('players', 'player_items', 'game_events')
        ORDER BY shardid
    )";

    auto result = coordinatorPool_.Query(query);
    if (!result) {
        Logger::Error("Failed to query shard information");
        return false;
    }

    int rowCount = PQntuples(result);
    shards_.clear();

    for (int i = 0; i < rowCount; ++i) {
        ShardInfo shard;
        shard.shard_id = std::stoi(PQgetvalue(result, i, 0));
        shard.shard_name = PQgetvalue(result, i, 1);
        shard.node_name = PQgetvalue(result, i, 2);
        shard.node_port = std::stoi(PQgetvalue(result, i, 3));
        shard.table_name = PQgetvalue(result, i, 4);
        shard.distribution_column = PQgetvalue(result, i, 5);

        shards_.push_back(shard);
        Logger::Debug("Loaded shard {} on {}:{} for table {}",
                     shard.shard_id, shard.node_name, shard.node_port,
                     shard.table_name);
    }

    PQclear(result);
    Logger::Info("Loaded {} shards", shards_.size());
    return true;
}

bool CitusBackend::CreateDefaultTables() {
    Logger::Info("Creating default distributed tables...");

    bool success = true;
    success &= CreateDistributedTable("players", "player_id");
    success &= CreateDistributedTable("player_items", "player_id");
    success &= CreateDistributedTable("game_events", "game_id");
    success &= CreateReferenceTable("game_config");
    success &= CreateReferenceTable("item_definitions");

    if (success) {
        Logger::Info("Default Citus tables created successfully");
    } else {
        Logger::Warn("Some Citus tables failed to create");
    }

    return success;
}

void CitusBackend::InsertDefaultConfig() {
    std::vector<std::string> configQueries = {
        "INSERT INTO game_config (config_key, config_value, config_type, description) "
        "VALUES ('game_name', 'Fantasy Realm', 'string', 'Name of the game') "
        "ON CONFLICT (config_key) DO NOTHING",
        
        "INSERT INTO game_config (config_key, config_value, config_type, description) "
        "VALUES ('max_players', '10000', 'integer', 'Maximum concurrent players') "
        "ON CONFLICT (config_key) DO NOTHING",
        
        "INSERT INTO game_config (config_key, config_value, config_type, description) "
        "VALUES ('starting_gold', '100', 'integer', 'Starting gold for new players') "
        "ON CONFLICT (config_key) DO NOTHING",
        
        "INSERT INTO game_config (config_key, config_value, config_type, description) "
        "VALUES ('xp_multiplier', '1.0', 'float', 'Experience point multiplier') "
        "ON CONFLICT (config_key) DO NOTHING",
        
        "INSERT INTO game_config (config_key, config_value, config_type, description) "
        "VALUES ('maintenance_mode', 'false', 'boolean', 'Is game in maintenance mode?') "
        "ON CONFLICT (config_key) DO NOTHING"
    };

    for (const auto& query : configQueries) {
        ExecuteCoordinatorCommand(query);
    }
}

int CitusBackend::GetShardId(int64_t entityId) const {
    if (shardCount_ <= 0) {
        return 0;
    }

    std::hash<int64_t> hasher;
    size_t hash = hasher(entityId);
    return hash % shardCount_;
}

std::shared_ptr<DatabasePool> CitusBackend::GetShardPool(int shardId) {
    std::lock_guard<std::mutex> lock(shardsMutex_);

    for (const auto& shard : shards_) {
        if (shard.shard_id == shardId) {
            std::string nodeKey = shard.node_name + ":" + std::to_string(shard.node_port);

            std::lock_guard<std::mutex> poolLock(workerPoolsMutex_);
            auto it = workerPools_.find(nodeKey);
            if (it != workerPools_.end()) {
                return it->second;
            }
        }
    }

    Logger::Warn("Shard {} not found or worker pool not available", shardId);
    return nullptr;
}

std::string CitusBackend::EscapeString(const std::string& str) {
    std::string escaped;
    escaped.reserve(str.length() * 2);

    for (char c : str) {
        if (c == '\'') {
            escaped += '\'';
        }
        escaped += c;
    }

    return escaped;
}

nlohmann::json CitusBackend::PGResultToJson(PGresult* res) {
    nlohmann::json jsonArray = nlohmann::json::array();

    if (!res) {
        return jsonArray;
    }

    int rowCount = PQntuples(res);
    int colCount = PQnfields(res);

    std::vector<std::string> columnNames;
    for (int i = 0; i < colCount; ++i) {
        columnNames.push_back(PQfname(res, i));
    }

    for (int row = 0; row < rowCount; ++row) {
        nlohmann::json jsonRow;

        for (int col = 0; col < colCount; ++col) {
            const char* value = PQgetvalue(res, row, col);

            if (PQgetisnull(res, row, col)) {
                jsonRow[columnNames[col]] = nullptr;
            } else {
                Oid type = PQftype(res, col);

                switch (type) {
                    case 16: // bool
                        jsonRow[columnNames[col]] = (strcmp(value, "t") == 0);
                        break;
                    case 20: // int8
                    case 21: // int2
                    case 23: // int4
                        jsonRow[columnNames[col]] = std::stoll(value);
                        break;
                    case 700: // float4
                    case 701: // float8
                        jsonRow[columnNames[col]] = std::stod(value);
                        break;
                    case 25: // text
                    case 1043: // varchar
                    default:
                        if (value[0] == '{' || value[0] == '[') {
                            try {
                                jsonRow[columnNames[col]] = nlohmann::json::parse(value);
                            } catch (...) {
                                jsonRow[columnNames[col]] = value;
                            }
                        } else {
                            jsonRow[columnNames[col]] = value;
                        }
                        break;
                }
            }
        }

        jsonArray.push_back(jsonRow);
    }

    PQclear(res);
    return jsonArray;
}

nlohmann::json CitusBackend::ExecuteCoordinatorQuery(const std::string& query) {
    auto result = coordinatorPool_.Query(query);
    return PGResultToJson(result);
}

bool CitusBackend::ExecuteCoordinatorCommand(const std::string& command) {
    return coordinatorPool_->Execute(command);
}

// Due to space constraints, other methods would follow similar patterns
// as the PostgreSQLBackend implementation but using Citus-specific queries
