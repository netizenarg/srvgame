#include <cstring>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <random>
#include <algorithm>

#include "database/PostgreSQLBackend.hpp"
#include "config/ConfigManager.hpp"
#include "logging/Logger.hpp"

// =============== PostgreSQLBackend Implementation ===============

PostgreSQLBackend::PostgreSQLBackend() {
    Logger::Debug("PostgreSQLBackend created");
}

PostgreSQLBackend::~PostgreSQLBackend() {
    Logger::Debug("PostgreSQLBackend destroyed");
}

bool PostgreSQLBackend::Initialize(const std::string& connInfo,
                                  const std::vector<std::string>& workerNodes) {
    if (initialized_) {
        Logger::Warn("PostgreSQLBackend already initialized");
        return true;
    }

    Logger::Info("Initializing PostgreSQL backend...");
    
    connectionInfo_ = connInfo;
    connectionPool_ = std::make_unique<DatabasePool>();
    
    // Initialize connection pool
    if (!connectionPool_->Initialize(connInfo, 10)) {
        Logger::Critical("Failed to initialize PostgreSQL connection pool");
        return false;
    }
    
    // Test connection
    if (!TestConnection()) {
        Logger::Critical("PostgreSQL connection test failed");
        return false;
    }
    
    // Initialize virtual shards for emulation
    InitializeVirtualShards();
    
    // Create required tables
    if (!CreateDefaultTables()) {
        Logger::Error("Failed to create default tables");
        return false;
    }
    
    initialized_ = true;
    Logger::Info("PostgreSQL backend initialized successfully");
    return true;
}

bool PostgreSQLBackend::TestConnection() {
    auto result = connectionPool_->Query("SELECT 1");
    if (!result) {
        Logger::Error("PostgreSQL connection test failed");
        return false;
    }
    
    PQclear(result);
    Logger::Debug("PostgreSQL connection test successful");
    return true;
}

void PostgreSQLBackend::ReconnectAll() {
    Logger::Info("Reconnecting PostgreSQL connections...");
    if (connectionPool_) {
        connectionPool_->RecycleAllConnections();
    }
    Logger::Info("PostgreSQL reconnection completed");
}

bool PostgreSQLBackend::CheckHealth() {
    return TestConnection();
}

bool PostgreSQLBackend::CreateTable(const std::string& tableName,
                                   const std::string& schema) {
    std::string query = "CREATE TABLE IF NOT EXISTS " + tableName + " " + schema;
    return ExecuteCommand(query);
}

bool PostgreSQLBackend::CreateDistributedTable(const std::string& tableName,
                                              const std::string& distributionColumn,
                                              const std::string& distributionType) {
    Logger::Info("Creating distributed table (emulated): {} (distribution column: {})",
                tableName, distributionColumn);
    
    // For PostgreSQL, we just create a regular table with the distribution column as an index
    std::string schema;
    
    if (tableName == "players") {
        schema = R"(
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
        )";
    } else if (tableName == "player_items") {
        schema = R"(
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
            metadata JSONB DEFAULT '{}',
            FOREIGN KEY (player_id) REFERENCES players(player_id)
        )";
    } else if (tableName == "game_events") {
        schema = R"(
            event_id BIGSERIAL PRIMARY KEY,
            game_id BIGINT NOT NULL,
            player_id BIGINT,
            event_type VARCHAR(50) NOT NULL,
            event_data JSONB NOT NULL,
            severity INTEGER DEFAULT 0,
            created_at TIMESTAMP DEFAULT NOW(),
            processed BOOLEAN DEFAULT FALSE,
            metadata JSONB DEFAULT '{}'
        )";
    } else if (tableName == "game_states") {
        schema = R"(
            game_id BIGINT PRIMARY KEY,
            state_data JSONB NOT NULL,
            updated_at TIMESTAMP DEFAULT NOW()
        )";
    } else {
        schema = "(id BIGSERIAL PRIMARY KEY)";
    }
    
    if (!CreateTable(tableName, schema)) {
        Logger::Error("Failed to create table: {}", tableName);
        return false;
    }
    
    // Create index on distribution column for emulation
    std::string indexQuery = "CREATE INDEX IF NOT EXISTS idx_" + tableName + "_" + 
                            distributionColumn + " ON " + tableName + "(" + distributionColumn + ")";
    ExecuteCommand(indexQuery);
    
    Logger::Info("Distributed table created (emulated): {}", tableName);
    return true;
}

bool PostgreSQLBackend::CreateReferenceTable(const std::string& tableName) {
    Logger::Info("Creating reference table: {}", tableName);
    
    // For PostgreSQL, reference tables are just regular tables
    std::string schema;
    
    if (tableName == "game_config") {
        schema = R"(
            config_key VARCHAR(100) PRIMARY KEY,
            config_value TEXT NOT NULL,
            config_type VARCHAR(20) DEFAULT 'string',
            description TEXT,
            updated_at TIMESTAMP DEFAULT NOW(),
            updated_by VARCHAR(50)
        )";
    } else if (tableName == "item_definitions") {
        schema = R"(
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
        )";
    } else {
        schema = "(id SERIAL PRIMARY KEY)";
    }
    
    bool result = CreateTable(tableName, schema);
    
    if (result && tableName == "game_config") {
        // Insert default config
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
            ExecuteCommand(query);
        }
    }
    
    return result;
}

bool PostgreSQLBackend::TableExists(const std::string& tableName) {
    std::string query = "SELECT EXISTS ("
                       "SELECT FROM information_schema.tables "
                       "WHERE table_schema = 'public' "
                       "AND table_name = '" + tableName + "')";
    
    auto result = ExecuteQuery(query);
    return !result.empty() && result[0]["exists"].get<bool>();
}

nlohmann::json PostgreSQLBackend::Query(const std::string& query) {
    Logger::Debug("PostgreSQL query: {}", query);
    
    auto result = connectionPool_->Query(query);
    if (!result) {
        Logger::Error("Query failed: {}", query);
        return nlohmann::json::array();
    }
    
    return PGResultToJson(result);
}

bool PostgreSQLBackend::Execute(const std::string& query) {
    Logger::Debug("PostgreSQL execute: {}", query);
    return connectionPool_->Execute(query);
}

nlohmann::json PostgreSQLBackend::QueryShard(int shardId, const std::string& query) {
    // For PostgreSQL emulation, all queries go to the same database
    // The shardId parameter is ignored
    Logger::Debug("Query shard {} (emulated): {}", shardId, query);
    return Query(query);
}

nlohmann::json PostgreSQLBackend::QueryAllShards(const std::string& query) {
    // For PostgreSQL emulation, there's only one shard
    Logger::Debug("Query all shards (emulated): {}", query);
    return Query(query);
}

// Player data management methods
bool PostgreSQLBackend::CreatePlayer(const nlohmann::json& playerData) {
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
            ) RETURNING player_id
        )";
        
        auto result = Query(query);
        return !result.empty();
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to create player: {}", e.what());
        return false;
    }
}

nlohmann::json PostgreSQLBackend::GetPlayer(int64_t playerId) {
    std::string query = "SELECT * FROM players WHERE player_id = " + std::to_string(playerId);
    auto result = Query(query);
    return result.empty() ? nlohmann::json() : result[0];
}

bool PostgreSQLBackend::UpdatePlayer(int64_t playerId, const nlohmann::json& updates) {
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
                query << it.key() << " = '" << EscapeString(it.value().dump()) << "'::jsonb";
            }
        }
        
        query << " WHERE player_id = " << playerId;
        
        return Execute(query.str());
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to update player {}: {}", playerId, e.what());
        return false;
    }
}

bool PostgreSQLBackend::DeletePlayer(int64_t playerId) {
    std::string query = "DELETE FROM players WHERE player_id = " + std::to_string(playerId);
    return Execute(query);
}

bool PostgreSQLBackend::SaveGameState(int64_t gameId, const nlohmann::json& gameState) {
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
        
        return Execute(query);
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to save game state {}: {}", gameId, e.what());
        return false;
    }
}

nlohmann::json PostgreSQLBackend::LoadGameState(int64_t gameId) {
    std::string query = "SELECT state_data FROM game_states WHERE game_id = " + std::to_string(gameId);
    auto result = Query(query);
    
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

bool PostgreSQLBackend::SetOnlineStatus(int64_t playerId, bool online,
                                       const std::string& sessionId,
                                       const std::string& ipAddress) {
    std::string query = R"(
        UPDATE players
        SET online = )" + std::string(online ? "TRUE" : "FALSE") + R"(,
            last_login = )" + (online ? "NOW()" : "last_login") + R"(,
            last_logout = )" + (!online ? "NOW()" : "last_logout") + R"(,
            session_id = ')" + EscapeString(sessionId) + R"(',
            ip_address = ')" + EscapeString(ipAddress) + R"(',
            last_heartbeat = NOW()
        WHERE player_id = )" + std::to_string(playerId);
    
    return Execute(query);
}

bool PostgreSQLBackend::UpdateHeartbeat(int64_t playerId) {
    std::string query = "UPDATE players SET last_heartbeat = NOW() WHERE player_id = " +
                       std::to_string(playerId);
    return Execute(query);
}

bool PostgreSQLBackend::UpdatePlayerPosition(int64_t playerId, float x, float y, float z) {
    std::string query = R"(
        UPDATE players
        SET position_x = )" + std::to_string(x) + R"(,
            position_y = )" + std::to_string(y) + R"(,
            position_z = )" + std::to_string(z) + R"(
        WHERE player_id = )" + std::to_string(playerId);
    
    return Execute(query);
}

nlohmann::json PostgreSQLBackend::GetOnlinePlayers() {
    std::string query = R"(
        SELECT
            player_id,
            username,
            level,
            position_x,
            position_y,
            position_z,
            EXTRACT(EPOCH FROM (NOW() - last_heartbeat)) as seconds_since_heartbeat
        FROM players
        WHERE online = TRUE
        AND last_heartbeat > NOW() - INTERVAL '5 minutes'
        ORDER BY player_id
    )";
    
    return Query(query);
}

nlohmann::json PostgreSQLBackend::GetNearbyPlayers(int64_t playerId, float radius) {
    // First get the player's position
    std::string positionQuery = "SELECT position_x, position_y, position_z FROM players WHERE player_id = " +
                               std::to_string(playerId);
    
    auto positionResult = Query(positionQuery);
    if (positionResult.empty()) {
        return nlohmann::json::array();
    }
    
    float playerX = positionResult[0]["position_x"].get<float>();
    float playerY = positionResult[0]["position_y"].get<float>();
    float playerZ = positionResult[0]["position_z"].get<float>();
    
    // Find nearby players
    std::string query = R"(
        SELECT
            player_id,
            username,
            level,
            position_x,
            position_y,
            position_z,
            SQRT(
                POWER(position_x - )" + std::to_string(playerX) + R"(, 2) +
                POWER(position_y - )" + std::to_string(playerY) + R"(, 2) +
                POWER(position_z - )" + std::to_string(playerZ) + R"(, 2)
            ) as distance
        FROM players
        WHERE online = TRUE
        AND player_id != )" + std::to_string(playerId) + R"(
        AND SQRT(
            POWER(position_x - )" + std::to_string(playerX) + R"(, 2) +
            POWER(position_y - )" + std::to_string(playerY) + R"(, 2) +
            POWER(position_z - )" + std::to_string(playerZ) + R"(, 2)
        ) <= )" + std::to_string(radius) + R"(
        ORDER BY distance
        LIMIT 50
    )";
    
    return Query(query);
}

bool PostgreSQLBackend::AddPlayerItem(int64_t playerId, int itemDefId,
                                     int quantity, const nlohmann::json& attributes) {
    try {
        std::string attrsJson = attributes.dump();
        
        std::string query = R"(
            INSERT INTO player_items (
                player_id, item_def_id, quantity, attributes
            ) VALUES ()" + std::to_string(playerId) + R"(,
                    )" + std::to_string(itemDefId) + R"(,
                    )" + std::to_string(quantity) + R"(,
                    ')" + EscapeString(attrsJson) + R"('::jsonb
            )
        )";
        
        return Execute(query);
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to add item to player {}: {}", playerId, e.what());
        return false;
    }
}

nlohmann::json PostgreSQLBackend::GetPlayerItems(int64_t playerId) {
    std::string query = R"(
        SELECT
            pi.*,
            id.item_name,
            id.item_type,
            id.item_rarity
        FROM player_items pi
        JOIN item_definitions id ON pi.item_def_id = id.item_def_id
        WHERE pi.player_id = )" + std::to_string(playerId) + R"(
        ORDER BY pi.created_at DESC
    )";
    
    return Query(query);
}

bool PostgreSQLBackend::LogGameEvent(int64_t playerId, int64_t gameId,
                                    const std::string& eventType,
                                    const nlohmann::json& eventData) {
    try {
        std::string dataJson = eventData.dump();
        
        std::string query = R"(
            INSERT INTO game_events (
                game_id, player_id, event_type, event_data
            ) VALUES ()" + std::to_string(gameId) + R"(,
                    )" + std::to_string(playerId) + R"(,
                    ')" + EscapeString(eventType) + R"(',
                    ')" + EscapeString(dataJson) + R"('::jsonb
            )
        )";
        
        return Execute(query);
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to log game event: {}", e.what());
        return false;
    }
}

nlohmann::json PostgreSQLBackend::GetPlayerStats(int64_t playerId) {
    std::string query = R"(
        SELECT
            p.player_id,
            p.username,
            p.level,
            p.experience,
            p.score,
            p.total_playtime,
            COUNT(DISTINCT pi.item_id) as total_items,
            SUM(pi.quantity) as total_item_count,
            COUNT(DISTINCT ge.event_id) as total_events,
            MAX(ge.created_at) as last_event_time
        FROM players p
        LEFT JOIN player_items pi ON p.player_id = pi.player_id
        LEFT JOIN game_events ge ON p.player_id = ge.player_id
        WHERE p.player_id = )" + std::to_string(playerId) + R"(
        GROUP BY p.player_id, p.username, p.level, p.experience, p.score, p.total_playtime
    )";
    
    auto result = Query(query);
    return result.empty() ? nlohmann::json() : result[0];
}

nlohmann::json PostgreSQLBackend::GetGameAnalytics(int64_t gameId) {
    std::string query = R"(
        SELECT
            game_id,
            COUNT(*) as total_events,
            COUNT(DISTINCT player_id) as unique_players,
            MIN(created_at) as first_event,
            MAX(created_at) as last_event,
            COUNT(*) FILTER (WHERE event_type = 'login') as logins,
            COUNT(*) FILTER (WHERE event_type = 'logout') as logouts,
            COUNT(*) FILTER (WHERE event_type = 'combat') as combats,
            COUNT(*) FILTER (WHERE event_type = 'chat') as chats,
            COUNT(*) FILTER (WHERE event_type = 'trade') as trades,
            COUNT(*) FILTER (WHERE event_type = 'achievement') as achievements
        FROM game_events
        WHERE game_id = )" + std::to_string(gameId) + R"(
        GROUP BY game_id
    )";
    
    auto result = Query(query);
    return result.empty() ? nlohmann::json() : result[0];
}

bool PostgreSQLBackend::VacuumTables() {
    Logger::Info("Starting table vacuum (PostgreSQL)...");
    
    bool success = true;
    std::vector<std::string> tables = {
        "players", "player_items", "game_events", "game_states",
        "game_config", "item_definitions"
    };
    
    for (const auto& table : tables) {
        std::string query = "VACUUM ANALYZE " + table;
        if (!Execute(query)) {
            Logger::Warn("Failed to vacuum table: {}", table);
            success = false;
        } else {
            Logger::Debug("Vacuumed table: {}", table);
        }
    }
    
    if (success) {
        Logger::Info("Table vacuum completed successfully");
    } else {
        Logger::Warn("Table vacuum completed with errors");
    }
    
    return success;
}

bool PostgreSQLBackend::RebalanceShards() {
    // No-op for PostgreSQL
    Logger::Info("Rebalance shards: No-op for PostgreSQL backend");
    return true;
}

nlohmann::json PostgreSQLBackend::GetClusterStatus() {
    // For PostgreSQL, we only have one node
    nlohmann::json status;
    status["node_type"] = "postgresql";
    status["shard_count"] = 1;
    status["virtual_shards"] = virtualShardCount_;
    status["connection_pool"] = {
        {"available", 0},  // Would need to get from pool
        {"in_use", 0}
    };
    
    return status;
}

nlohmann::json PostgreSQLBackend::GetPerformanceMetrics() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm = *std::localtime(&now_c);
    
    std::stringstream timeStr;
    timeStr << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S");
    
    nlohmann::json metrics;
    metrics["timestamp"] = timeStr.str();
    metrics["backend_type"] = "postgresql";
    
    // Get database statistics
    std::vector<std::string> queries = {
        "SELECT COUNT(*) as total_players FROM players",
        "SELECT COUNT(*) as online_players FROM players WHERE online = TRUE",
        "SELECT COUNT(*) as total_items FROM player_items",
        "SELECT COUNT(*) as total_events FROM game_events WHERE created_at > NOW() - INTERVAL '1 hour'",
        "SELECT pg_database_size(current_database()) as db_size_bytes"
    };
    
    for (const auto& query : queries) {
        auto result = Query(query);
        if (!result.empty()) {
            for (auto it = result[0].begin(); it != result[0].end(); ++it) {
                metrics[it.key()] = it.value();
            }
        }
    }
    
    return metrics;
}

std::string PostgreSQLBackend::EscapeString(const std::string& str) {
    // Simple escape for single quotes
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

nlohmann::json PostgreSQLBackend::PGResultToJson(PGresult* res) {
    nlohmann::json jsonArray = nlohmann::json::array();
    
    if (!res) {
        return jsonArray;
    }
    
    int rowCount = PQntuples(res);
    int colCount = PQnfields(res);
    
    // Get column names
    std::vector<std::string> columnNames;
    for (int i = 0; i < colCount; ++i) {
        columnNames.push_back(PQfname(res, i));
    }
    
    // Convert each row to JSON
    for (int row = 0; row < rowCount; ++row) {
        nlohmann::json jsonRow;
        
        for (int col = 0; col < colCount; ++col) {
            const char* value = PQgetvalue(res, row, col);
            
            if (PQgetisnull(res, row, col)) {
                jsonRow[columnNames[col]] = nullptr;
            } else {
                // Try to determine type and parse appropriately
                Oid type = PQftype(res, col);
                
                switch (type) {
                    case 16: // bool
                        jsonRow[columnNames[col]] = (strcmp(value, "t") == 0);
                        break;
                    case 20: // int8 (bigint)
                    case 21: // int2 (smallint)
                    case 23: // int4 (integer)
                        jsonRow[columnNames[col]] = std::stoll(value);
                        break;
                    case 700: // float4
                    case 701: // float8
                        jsonRow[columnNames[col]] = std::stod(value);
                        break;
                    case 114: // json
                    case 3802: // jsonb
                        try {
                            jsonRow[columnNames[col]] = nlohmann::json::parse(value);
                        } catch (...) {
                            jsonRow[columnNames[col]] = value;
                        }
                        break;
                    case 25: // text
                    case 1043: // varchar
                    default:
                        jsonRow[columnNames[col]] = value;
                        break;
                }
            }
        }
        
        jsonArray.push_back(jsonRow);
    }
    
    PQclear(res);
    return jsonArray;
}

// =============== Private Methods ===============

bool PostgreSQLBackend::CreateDefaultTables() {
    Logger::Info("Creating default PostgreSQL tables...");
    
    bool success = true;
    
    // Create reference tables first
    success &= CreateReferenceTable("game_config");
    success &= CreateReferenceTable("item_definitions");
    
    // Create main tables
    success &= CreateDistributedTable("players", "player_id");
    success &= CreateDistributedTable("player_items", "player_id");
    success &= CreateDistributedTable("game_events", "game_id");
    success &= CreateTable("game_states", R"(
        game_id BIGINT PRIMARY KEY,
        state_data JSONB NOT NULL,
        updated_at TIMESTAMP DEFAULT NOW()
    )");
    
    // Create indexes
    if (success) {
        std::vector<std::string> indexQueries = {
            "CREATE INDEX IF NOT EXISTS idx_players_online ON players(online)",
            "CREATE INDEX IF NOT EXISTS idx_players_last_heartbeat ON players(last_heartbeat)",
            "CREATE INDEX IF NOT EXISTS idx_player_items_player_id ON player_items(player_id)",
            "CREATE INDEX IF NOT EXISTS idx_game_events_game_id ON game_events(game_id)",
            "CREATE INDEX IF NOT EXISTS idx_game_events_player_id ON game_events(player_id)",
            "CREATE INDEX IF NOT EXISTS idx_game_events_created_at ON game_events(created_at)"
        };
        
        for (const auto& query : indexQueries) {
            Execute(query);
        }
    }
    
    if (success) {
        Logger::Info("Default PostgreSQL tables created successfully");
    } else {
        Logger::Warn("Some tables failed to create");
    }
    
    return success;
}

void PostgreSQLBackend::InitializeVirtualShards() {
    std::lock_guard<std::mutex> lock(shardsMutex_);
    
    virtualShards_.clear();
    
    // Create a single virtual shard for PostgreSQL
    VirtualShard shard;
    shard.shard_id = 0;
    shard.node_name = "localhost";
    shard.node_port = 5432;
    shard.table_name = "all_tables";
    
    virtualShards_.push_back(shard);
    virtualShardCount_ = 1;
    
    Logger::Debug("Initialized 1 virtual shard for PostgreSQL emulation");
}

int PostgreSQLBackend::GetVirtualShardId(int64_t entityId) const {
    // For PostgreSQL emulation, all entities go to shard 0
    return 0;
}

std::string PostgreSQLBackend::BuildVirtualShardQuery(const std::string& query, int shardId) const {
    // For PostgreSQL emulation, queries are unchanged
    return query;
}

nlohmann::json PostgreSQLBackend::ExecuteQuery(const std::string& query) {
    auto result = connectionPool_->Query(query);
    return PGResultToJson(result);
}

bool PostgreSQLBackend::ExecuteCommand(const std::string& command) {
    return connectionPool_->Execute(command);
}

bool PostgreSQLBackend::BeginTransaction() {
    return ExecuteCommand("BEGIN");
}

bool PostgreSQLBackend::CommitTransaction() {
    return ExecuteCommand("COMMIT");
}

bool PostgreSQLBackend::RollbackTransaction() {
    return ExecuteCommand("ROLLBACK");
}
