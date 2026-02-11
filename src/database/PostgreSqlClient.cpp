#include "database/PostgreSqlClient.hpp"

// Safe conversion helpers in anonymous namespace
namespace {
    bool SafeStringToInt64(const char* str, int64_t& result) {
        if (!str || str[0] == '\0') return false;
        
        char* endptr = nullptr;
        errno = 0;
        long long val = strtoll(str, &endptr, 10);
        
        if (errno == ERANGE || val < std::numeric_limits<int64_t>::min() || 
            val > std::numeric_limits<int64_t>::max()) {
            return false;
        }
        
        if (endptr == str || *endptr != '\0') {
            return false;
        }
        
        result = static_cast<int64_t>(val);
        return true;
    }
    
    bool SafeStringToInt(const char* str, int& result) {
        int64_t temp;
        if (!SafeStringToInt64(str, temp)) return false;
        
        if (temp < std::numeric_limits<int>::min() || 
            temp > std::numeric_limits<int>::max()) {
            return false;
        }
        
        result = static_cast<int>(temp);
        return true;
    }
}

// =============== Constructor and Destructor ===============
PostgreSqlClient::PostgreSqlClient(const nlohmann::json& config)
    : config_(config),
      poolInitialized_(false),
      poolShuttingDown_(false),
      lastInsertId_(0),
      affectedRows_(0) {
    
    // Validate shards configuration with safe bounds
    int shards = config.value("shards", 32);
    if (shards <= 0 || shards > 1024) {
        Logger::Error("Invalid shard count: {}. Using default 32.", shards);
        shards = 32;
    }
    totalShards_ = shards;
    
    stats_.startTime = std::chrono::steady_clock::now();
    connectionString_ = BuildConnectionString();

    Logger::Debug("PostgreSqlClient created with {} shards", totalShards_);
}

PostgreSqlClient::~PostgreSqlClient() {
    ReleaseConnectionPool();
    Disconnect();
    Logger::Debug("PostgreSqlClient destroyed");
}

// =============== Connection Management ===============
bool PostgreSqlClient::Connect() {
    if (poolInitialized_) {
        return true;
    }

    try {
        // Initialize connection pool if configured
        if (config_.contains("connection_pool") &&
            config_["connection_pool"].value("enabled", true)) {

            size_t minConn = config_["connection_pool"].value("min_connections", 5);
            size_t maxConn = config_["connection_pool"].value("max_connections", 20);

            if (!InitializeConnectionPool(minConn, maxConn)) {
                Logger::Error("Failed to initialize connection pool");
                return false;
            }
        } else {
            // Single connection mode
            PGconn* testConn = CreateNewConnection();
            if (!testConn || PQstatus(testConn) != CONNECTION_OK) {
                if (testConn) {
                    CloseConnection(testConn);
                }
                Logger::Error("Failed to establish database connection");
                return false;
            }
            CloseConnection(testConn);
        }

        Logger::Info("Connected to PostgreSQL database: {}",
                    config_.value("database", "unknown"));
        return true;

    } catch (const std::exception& e) {
        Logger::Error("Connection error: {}", e.what());
        return false;
    }
}

bool PostgreSqlClient::Reconnect() {
    std::lock_guard<std::mutex> lock(poolMutex_);

    Logger::Info("Reconnecting all database connections...");

    // Close all existing connections
    for (auto& conn : connections_) {
        if (conn.conn) {
            PQfinish(conn.conn);
            conn.conn = nullptr;
        }
    }
    connections_.clear();

    // Create new connections
    for (size_t i = 0; i < minConnections_; ++i) {
        PGconn* newConn = CreateNewConnection();
        if (!newConn || PQstatus(newConn) != CONNECTION_OK) {
            if (newConn) {
                PQfinish(newConn);
            }
            Logger::Error("Failed to recreate connection {}", i);
            return false;
        }
        connections_.push_back({newConn, false, std::chrono::steady_clock::now()});
    }

    Logger::Info("Reconnected {} database connections", minConnections_);
    return true;
}

void PostgreSqlClient::Disconnect() {
    std::lock_guard<std::mutex> lock(poolMutex_);

    for (auto& conn : connections_) {
        if (conn.conn) {
            PQfinish(conn.conn);
            conn.conn = nullptr;
        }
    }
    connections_.clear();

    poolInitialized_ = false;
    Logger::Info("Disconnected from PostgreSQL database");
}

bool PostgreSqlClient::IsConnected() const {
    if (!poolInitialized_) {
        return false;
    }

    std::lock_guard<std::mutex> lock(poolMutex_);

    // Test the first connection
    if (connections_.empty()) {
        return false;
    }

    // Find an available connection to test
    for (const auto& conn : connections_) {
        if (!conn.inUse) {
            return TestConnection(conn.conn);
        }
    }

    return false;
}

bool PostgreSqlClient::CheckHealth() {
    if (!poolInitialized_) {
        return false;
    }

    std::lock_guard<std::mutex> lock(poolMutex_);

    int healthyConnections = 0;
    for (const auto& conn : connections_) {
        if (TestConnection(conn.conn)) {
            healthyConnections++;
        }
    }

    bool healthy = healthyConnections >= minConnections_;
    if (!healthy) {
        Logger::Warn("Database health check failed: {}/{} connections healthy",
                    healthyConnections, connections_.size());
    }

    return healthy;
}

void PostgreSqlClient::ReconnectAll() {
    Reconnect();
}

// =============== Connection Pool Management ===============
bool PostgreSqlClient::InitializeConnectionPool(size_t minConnections, size_t maxConnections) {
    if (poolInitialized_) {
        Logger::Warn("Connection pool already initialized");
        return true;
    }

    if (minConnections == 0 || maxConnections == 0 || minConnections > maxConnections) {
        Logger::Error("Invalid connection pool parameters: min={}, max={}",
                     minConnections, maxConnections);
        return false;
    }

    // Set reasonable limits to prevent resource exhaustion
    const size_t MAX_ALLOWED_CONNECTIONS = 1000;
    if (maxConnections > MAX_ALLOWED_CONNECTIONS) {
        Logger::Warn("Max connections {} exceeds limit {}, capping to {}",
                    maxConnections, MAX_ALLOWED_CONNECTIONS, MAX_ALLOWED_CONNECTIONS);
        maxConnections = MAX_ALLOWED_CONNECTIONS;
    }
    
    // Ensure min doesn't exceed max after adjustment
    if (minConnections > maxConnections) {
        minConnections = maxConnections;
    }

    minConnections_ = minConnections;
    maxConnections_ = maxConnections;

    std::lock_guard<std::mutex> lock(poolMutex_);

    // Create initial connections
    for (size_t i = 0; i < minConnections_; ++i) {
        PGconn* conn = CreateNewConnection();
        if (!conn || PQstatus(conn) != CONNECTION_OK) {
            Logger::Error("Failed to create connection {} for pool", i);
            if (conn) {
                PQfinish(conn);
            }
            // Clean up any created connections
            for (auto& c : connections_) {
                if (c.conn) {
                    PQfinish(c.conn);
                }
            }
            connections_.clear();
            return false;
        }
        connections_.push_back({conn, false, std::chrono::steady_clock::now()});
    }

    poolInitialized_ = true;
    poolShuttingDown_ = false;

    // Start pool maintenance thread
    std::thread([this]() {
        while (!poolShuttingDown_) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            if (poolInitialized_ && !poolShuttingDown_) {
                MaintainPool();
            }
        }
    }).detach();

    Logger::Info("Connection pool initialized with {}-{} connections",
                minConnections_, maxConnections_);
    return true;
}

void PostgreSqlClient::ReleaseConnectionPool() {
    poolShuttingDown_ = true;

    std::lock_guard<std::mutex> lock(poolMutex_);

    for (auto& conn : connections_) {
        if (conn.conn) {
            PQfinish(conn.conn);
            conn.conn = nullptr;
        }
    }
    connections_.clear();

    poolInitialized_ = false;
    Logger::Info("Connection pool released");
}

size_t PostgreSqlClient::GetActiveConnections() const {
    std::lock_guard<std::mutex> lock(poolMutex_);

    size_t active = 0;
    for (const auto& conn : connections_) {
        if (conn.inUse) {
            active++;
        }
    }
    return active;
}

size_t PostgreSqlClient::GetIdleConnections() const {
    std::lock_guard<std::mutex> lock(poolMutex_);

    size_t idle = 0;
    for (const auto& conn : connections_) {
        if (!conn.inUse) {
            idle++;
        }
    }
    return idle;
}

PGconn* PostgreSqlClient::GetConnection() {
    std::unique_lock<std::mutex> lock(poolMutex_);

    if (!poolInitialized_) {
        Logger::Error("Connection pool not initialized");
        return nullptr;
    }

    // Try to find an idle connection
    for (auto& conn : connections_) {
        if (!conn.inUse && TestConnection(conn.conn)) {
            conn.inUse = true;
            conn.lastUsed = std::chrono::steady_clock::now();
            stats_.connectionPoolHits++;
            return conn.conn;
        }
    }

    // No idle connections available, create new one if under max limit
    if (connections_.size() < maxConnections_) {
        PGconn* newConn = CreateNewConnection();
        if (newConn && PQstatus(newConn) == CONNECTION_OK) {
            connections_.push_back({newConn, true, std::chrono::steady_clock::now()});
            stats_.connectionPoolMisses++;
            return newConn;
        }
        if (newConn) {
            PQfinish(newConn);
        }
    }

    // Wait for a connection to become available
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::seconds(10)) {
        poolCV_.wait_for(lock, std::chrono::seconds(1));

        for (auto& conn : connections_) {
            if (!conn.inUse && TestConnection(conn.conn)) {
                conn.inUse = true;
                conn.lastUsed = std::chrono::steady_clock::now();
                stats_.connectionPoolHits++;
                return conn.conn;
            }
        }
    }

    Logger::Error("Timed out waiting for database connection");
    stats_.connectionPoolMisses++;
    return nullptr;
}

void PostgreSqlClient::ReleaseConnection(PGconn* conn) {
    std::lock_guard<std::mutex> lock(poolMutex_);

    for (auto& c : connections_) {
        if (c.conn == conn) {
            c.inUse = false;
            c.lastUsed = std::chrono::steady_clock::now();
            poolCV_.notify_one();
            return;
        }
    }

    // Connection not in pool, close it
    CloseConnection(conn);
}

PGconn* PostgreSqlClient::CreateNewConnection() {
    PGconn* conn = PQconnectdb(connectionString_.c_str());

    if (PQstatus(conn) != CONNECTION_OK) {
        Logger::Error("Failed to create database connection: {}", PQerrorMessage(conn));
        return nullptr;
    }

    // Set connection options
    PQsetClientEncoding(conn, "UTF8");

    // Set statement timeout if configured
    if (config_.contains("timeout")) {
        int timeout = config_["timeout"];
        // Validate timeout value
        if (timeout < 0 || timeout > 3600) {
            Logger::Warn("Invalid timeout value: {}, using default", timeout);
            timeout = 30;
        }
        std::string timeoutCmd = "SET statement_timeout = " + std::to_string(timeout * 1000);
        PGresult* res = PQexec(conn, timeoutCmd.c_str());
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            Logger::Warn("Failed to set statement timeout: {}", PQerrorMessage(conn));
        }
        PQclear(res);
    }

    return conn;
}

void PostgreSqlClient::CloseConnection(PGconn* conn) {
    if (conn) {
        PQfinish(conn);
    }
}

bool PostgreSqlClient::TestConnection(PGconn* conn) {
    if (!conn) {
        return false;
    }

    ConnStatusType status = PQstatus(conn);
    if (status != CONNECTION_OK) {
        return false;
    }

    // Execute a simple query to test connection
    PGresult* res = PQexec(conn, "SELECT 1");
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
        if (res) {
            PQclear(res);
        }
        return false;
    }

    PQclear(res);
    return true;
}

void PostgreSqlClient::MaintainPool() {
    std::lock_guard<std::mutex> lock(poolMutex_);

    if (!poolInitialized_) {
        return;
    }

    // Clean up idle connections beyond minConnections_
    auto now = std::chrono::steady_clock::now();
    auto it = connections_.begin();

    while (it != connections_.end()) {
        if (!it->inUse) {
            // Close connections that have been idle for more than 5 minutes
            auto idleTime = std::chrono::duration_cast<std::chrono::seconds>(now - it->lastUsed);
            if (idleTime > std::chrono::minutes(5) && connections_.size() > minConnections_) {
                CloseConnection(it->conn);
                it = connections_.erase(it);
                continue;
            }

            // Test and fix broken connections
            if (!TestConnection(it->conn)) {
                Logger::Debug("Replacing broken connection in pool");
                CloseConnection(it->conn);
                it->conn = CreateNewConnection();
                if (!it->conn || !TestConnection(it->conn)) {
                    // Remove if can't recreate
                    it = connections_.erase(it);
                    continue;
                }
            }
        }
        ++it;
    }

    // Ensure we have at least minConnections_
    while (connections_.size() < minConnections_) {
        PGconn* newConn = CreateNewConnection();
        if (newConn && TestConnection(newConn)) {
            connections_.push_back({newConn, false, now});
        } else {
            if (newConn) {
                CloseConnection(newConn);
            }
            Logger::Warn("Failed to maintain minimum connection pool size");
            break;
        }
    }
}

std::string PostgreSqlClient::BuildConnectionString() const {
    std::ostringstream oss;

    oss << "host=" << config_.value("host", "localhost") << " ";
    oss << "port=" << config_.value("port", 5432) << " ";
    oss << "dbname=" << config_.value("database", "game_db") << " ";
    oss << "user=" << config_.value("username", "postgres") << " ";
    oss << "password=" << config_.value("password", "") << " ";

    if (config_.value("ssl", false)) {
        oss << "sslmode=require ";
    } else {
        oss << "sslmode=disable ";
    }

    oss << "connect_timeout=10 ";
    oss << "application_name=game_server";

    return oss.str();
}

// =============== Query Operations ===============
nlohmann::json PostgreSqlClient::Query(const std::string& sql) {
    PGconn* conn = GetConnection();
    if (!conn) {
        stats_.failedQueries++;
        return nlohmann::json::array();
    }

    auto result = ExecuteQuery(conn, sql);
    ReleaseConnection(conn);

    stats_.totalQueries++;
    return result;
}

nlohmann::json PostgreSqlClient::QueryWithParams(const std::string& sql, const std::vector<std::string>& params) {
    PGconn* conn = GetConnection();
    if (!conn) {
        stats_.failedQueries++;
        return nlohmann::json::array();
    }

    // Convert params to C strings
    std::vector<const char*> c_params;
    c_params.reserve(params.size());
    for (const auto& param : params) {
        c_params.push_back(param.c_str());
    }

    auto result = ExecuteQuery(conn, sql, c_params);
    ReleaseConnection(conn);

    stats_.totalQueries++;
    return result;
}

bool PostgreSqlClient::Execute(const std::string& sql) {
    PGconn* conn = GetConnection();
    if (!conn) {
        stats_.failedQueries++;
        return false;
    }

    bool success = ExecuteCommand(conn, sql);
    ReleaseConnection(conn);

    stats_.totalQueries++;
    return success;
}

bool PostgreSqlClient::ExecuteWithParams(const std::string& sql, const std::vector<std::string>& params) {
    PGconn* conn = GetConnection();
    if (!conn) {
        stats_.failedQueries++;
        return false;
    }

    // Convert params to C strings
    std::vector<const char*> c_params;
    c_params.reserve(params.size());
    for (const auto& param : params) {
        c_params.push_back(param.c_str());
    }

    bool success = ExecuteCommand(conn, sql, c_params);
    ReleaseConnection(conn);

    stats_.totalQueries++;
    return success;
}

nlohmann::json PostgreSqlClient::ExecuteQuery(PGconn* conn, const std::string& sql,
                                             const std::vector<const char*>& params) {
    if (!conn) {
        return nlohmann::json::array();
    }

    PGresult* result = nullptr;
    if (params.empty()) {
        result = PQexec(conn, sql.c_str());
    } else {
        result = PQexecParams(conn, sql.c_str(),
                             static_cast<int>(params.size()),
                             nullptr, // param types (inferred)
                             params.data(),
                             nullptr, // param lengths (text)
                             nullptr, // param formats (text)
                             0);      // result format (text)
    }

    if (!result) {
        HandleSQLError(conn, "Query execution failed");
        return nlohmann::json::array();
    }

    ExecStatusType status = PQresultStatus(result);
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        HandleSQLError(conn, "Query failed: " + std::string(PQerrorMessage(conn)));
        PQclear(result);
        return nlohmann::json::array();
    }

    // Get last insert ID if applicable using safe conversion
    if (status == PGRES_COMMAND_OK && sql.find("INSERT") != std::string::npos) {
        const char* insertId = PQoidValue(result);
        if (insertId) {
            int64_t tempId;
            if (SafeStringToInt64(insertId, tempId)) {
                lastInsertId_ = tempId;
            } else {
                lastInsertId_ = 0;
                Logger::Warn("Failed to parse last insert ID: {}", insertId);
            }
        }

        const char* affected = PQcmdTuples(result);
        if (affected) {
            int tempRows;
            if (SafeStringToInt(affected, tempRows)) {
                affectedRows_ = tempRows;
            } else {
                affectedRows_ = 0;
                Logger::Warn("Failed to parse affected rows: {}", affected);
            }
        }
    }

    // Convert result to JSON
    nlohmann::json jsonResult = ResultToJson(result);
    PQclear(result);

    return jsonResult;
}

bool PostgreSqlClient::ExecuteCommand(PGconn* conn, const std::string& sql,
                                     const std::vector<const char*>& params) {
    if (!conn) {
        return false;
    }

    PGresult* result = nullptr;
    if (params.empty()) {
        result = PQexec(conn, sql.c_str());
    } else {
        result = PQexecParams(conn, sql.c_str(),
                             static_cast<int>(params.size()),
                             nullptr, // param types (inferred)
                             params.data(),
                             nullptr, // param lengths (text)
                             nullptr, // param formats (text)
                             0);      // result format (text)
    }

    if (!result) {
        HandleSQLError(conn, "Command execution failed");
        return false;
    }

    ExecStatusType status = PQresultStatus(result);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        HandleSQLError(conn, "Command failed: " + std::string(PQerrorMessage(conn)));
        PQclear(result);
        return false;
    }

    // Get affected rows using safe conversion
    const char* affected = PQcmdTuples(result);
    if (affected) {
        int tempRows;
        if (SafeStringToInt(affected, tempRows)) {
            affectedRows_ = tempRows;
        } else {
            affectedRows_ = 0;
            Logger::Warn("Failed to parse affected rows: {}", affected);
        }
    }

    // Get last insert ID if applicable using safe conversion
    if (status == PGRES_COMMAND_OK && sql.find("INSERT") != std::string::npos) {
        const char* insertId = PQoidValue(result);
        if (insertId) {
            int64_t tempId;
            if (SafeStringToInt64(insertId, tempId)) {
                lastInsertId_ = tempId;
            } else {
                lastInsertId_ = 0;
                Logger::Warn("Failed to parse last insert ID: {}", insertId);
            }
        }
    }

    PQclear(result);
    return true;
}

nlohmann::json PostgreSqlClient::ResultToJson(PGresult* result) const {
    nlohmann::json jsonResult = nlohmann::json::array();

    if (!result) {
        return jsonResult;
    }

    int rows = PQntuples(result);
    int cols = PQnfields(result);

    // Get column names
    std::vector<std::string> columnNames;
    for (int i = 0; i < cols; ++i) {
        columnNames.push_back(PQfname(result, i));
    }

    // Convert rows to JSON objects
    for (int row = 0; row < rows; ++row) {
        nlohmann::json rowObj;
        for (int col = 0; col < cols; ++col) {
            const char* value = PQgetvalue(result, row, col);
            if (value) {
                // Try to parse as JSON if it looks like JSON
                std::string strValue = value;
                if (!strValue.empty() && (strValue[0] == '{' || strValue[0] == '[')) {
                    try {
                        rowObj[columnNames[col]] = nlohmann::json::parse(strValue);
                    } catch (...) {
                        rowObj[columnNames[col]] = strValue;
                    }
                } else {
                    rowObj[columnNames[col]] = strValue;
                }
            } else {
                rowObj[columnNames[col]] = nullptr;
            }
        }
        jsonResult.push_back(rowObj);
    }

    return jsonResult;
}

// =============== Shard Operations ===============
nlohmann::json PostgreSqlClient::QueryShard(int shardId, const std::string& sql) {
    // In standard PostgreSQL, we ignore shardId
    return Query(sql);
}

nlohmann::json PostgreSqlClient::QueryShardWithParams(int shardId, const std::string& sql,
                                                     const std::vector<std::string>& params) {
    // In standard PostgreSQL, we ignore shardId
    return QueryWithParams(sql, params);
}

bool PostgreSqlClient::ExecuteShard(int shardId, const std::string& sql) {
    // In standard PostgreSQL, we ignore shardId
    return Execute(sql);
}

bool PostgreSqlClient::ExecuteShardWithParams(int shardId, const std::string& sql,
                                             const std::vector<std::string>& params) {
    // In standard PostgreSQL, we ignore shardId
    return ExecuteWithParams(sql, params);
}

// =============== Utility Methods ===============
std::string PostgreSqlClient::EscapeString(const std::string& str) {
    PGconn* conn = GetConnection();
    if (!conn) {
        return "";
    }

    char* escaped = PQescapeLiteral(conn, str.c_str(), str.length());
    std::string result;

    if (escaped) {
        result = escaped;
        PQfreemem(escaped);
    } else {
        // Fallback: basic escaping
        std::ostringstream oss;
        for (char c : str) {
            if (c == '\'') {
                oss << "''";
            } else {
                oss << c;
            }
        }
        result = oss.str();
    }

    ReleaseConnection(conn);
    return result;
}

int PostgreSqlClient::GetShardId(uint64_t entityId) const {
    // Safe shard calculation with bounds checking
    if (totalShards_ <= 0) {
        Logger::Error("Invalid totalShards: {}", totalShards_);
        return 0;  // Default to shard 0
    }
    
    // Prevent division by zero and handle large entityId
    uint64_t shard = entityId % static_cast<uint64_t>(totalShards_);
    
    // Ensure shard fits in int without overflow
    if (shard > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        Logger::Warn("Shard calculation overflow for entityId: {}", entityId);
        shard = shard % static_cast<uint64_t>(totalShards_);
    }
    
    return static_cast<int>(shard);
}

int PostgreSqlClient::GetTotalShards() const {
    return totalShards_;
}

std::string PostgreSqlClient::GetConnectionInfo() const {
    std::ostringstream oss;
    oss << "PostgreSQL: " << config_.value("host", "localhost")
        << ":" << config_.value("port", 5432)
        << "/" << config_.value("database", "game_db")
        << " (Pool: " << GetActiveConnections() << "/"
        << connections_.size() << " active)";
    return oss.str();
}

int64_t PostgreSqlClient::GetLastInsertId() {
    return lastInsertId_;
}

int PostgreSqlClient::GetAffectedRows() {
    return affectedRows_;
}

// =============== Player Data Operations ===============
bool PostgreSqlClient::SavePlayerData(uint64_t playerId, const nlohmann::json& data) {
    try {
        std::string dataJson = data.dump();
        std::string escapedJson = EscapeString(dataJson);

        std::string sql =
            "INSERT INTO players (player_id, player_data, last_updated) "
            "VALUES (" + std::to_string(playerId) + ", " + escapedJson + ", NOW()) "
            "ON CONFLICT (player_id) DO UPDATE SET "
            "player_data = EXCLUDED.player_data, "
            "last_updated = NOW()";

        return Execute(sql);

    } catch (const std::exception& e) {
        Logger::Error("Failed to save player data for {}: {}", playerId, e.what());
        return false;
    }
}

nlohmann::json PostgreSqlClient::LoadPlayerData(uint64_t playerId) {
    try {
        std::string sql =
            "SELECT player_data FROM players WHERE player_id = " + std::to_string(playerId);

        auto result = Query(sql);

        if (!result.empty() && result[0].contains("player_data")) {
            std::string jsonStr = result[0]["player_data"].get<std::string>();
            return nlohmann::json::parse(jsonStr);
        }

        return nlohmann::json();

    } catch (const std::exception& e) {
        Logger::Error("Failed to load player data for {}: {}", playerId, e.what());
        return nlohmann::json();
    }
}

bool PostgreSqlClient::UpdatePlayer(uint64_t playerId, const nlohmann::json& updates) {
    try {
        if (updates.empty()) {
            return true;
        }

        std::ostringstream sql;
        sql << "UPDATE players SET ";

        bool first = true;
        for (const auto& [key, value] : updates.items()) {
            if (!first) {
                sql << ", ";
            }
            first = false;

            if (value.is_string()) {
                sql << key << " = '" << EscapeString(value.get<std::string>()) << "'";
            } else if (value.is_number()) {
                sql << key << " = " << value.dump();
            } else if (value.is_boolean()) {
                sql << key << " = " << (value.get<bool>() ? "TRUE" : "FALSE");
            } else if (value.is_null()) {
                sql << key << " = NULL";
            } else {
                // For objects/arrays, store as JSON
                sql << key << " = '" << EscapeString(value.dump()) << "'";
            }
        }

        sql << ", last_updated = NOW() WHERE player_id = " << playerId;

        return Execute(sql.str());

    } catch (const std::exception& e) {
        Logger::Error("Failed to update player {}: {}", playerId, e.what());
        return false;
    }
}

bool PostgreSqlClient::DeletePlayer(uint64_t playerId) {
    std::string sql = "DELETE FROM players WHERE player_id = " + std::to_string(playerId);
    return Execute(sql);
}

bool PostgreSqlClient::UpdatePlayerPosition(uint64_t playerId, float x, float y, float z) {
    std::string sql =
        "UPDATE players SET position_x = " + std::to_string(x) +
        ", position_y = " + std::to_string(y) +
        ", position_z = " + std::to_string(z) +
        ", last_position_update = NOW() "
        "WHERE player_id = " + std::to_string(playerId);

    return Execute(sql);
}

bool PostgreSqlClient::PlayerExists(uint64_t playerId) {
    std::string sql =
        "SELECT EXISTS(SELECT 1 FROM players WHERE player_id = " +
        std::to_string(playerId) + ")";

    auto result = Query(sql);

    if (!result.empty() && result[0].contains("exists")) {
        return result[0]["exists"].get<bool>();
    }

    return false;
}

nlohmann::json PostgreSqlClient::GetPlayerStats(uint64_t playerId) {
    std::string sql =
        "SELECT level, experience, health, max_health, mana, max_mana, "
        "currency_gold, currency_gems, total_playtime "
        "FROM players WHERE player_id = " + std::to_string(playerId);

    auto result = Query(sql);

    if (!result.empty()) {
        return result[0];
    }

    return nlohmann::json();
}

bool PostgreSqlClient::UpdatePlayerStats(uint64_t playerId, const nlohmann::json& stats) {
    return UpdatePlayer(playerId, stats);
}

nlohmann::json PostgreSqlClient::GetPlayer(uint64_t playerId) {
    std::string sql = "SELECT * FROM players WHERE player_id = " + std::to_string(playerId);
    auto result = Query(sql);
    return result.empty() ? nlohmann::json() : result[0];
}

// =============== Game State Operations ===============
bool PostgreSqlClient::SaveGameState(const std::string& key, const nlohmann::json& state) {
    try {
        std::string stateJson = state.dump();
        std::string escapedJson = EscapeString(stateJson);

        std::string sql =
            "INSERT INTO game_states (state_key, state_data, last_updated) "
            "VALUES ('" + EscapeString(key) + "', " + escapedJson + ", NOW()) "
            "ON CONFLICT (state_key) DO UPDATE SET "
            "state_data = EXCLUDED.state_data, "
            "last_updated = NOW()";

        return Execute(sql);

    } catch (const std::exception& e) {
        Logger::Error("Failed to save game state '{}': {}", key, e.what());
        return false;
    }
}

nlohmann::json PostgreSqlClient::LoadGameState(const std::string& key) {
    try {
        std::string sql =
            "SELECT state_data FROM game_states WHERE state_key = '" +
            EscapeString(key) + "'";

        auto result = Query(sql);

        if (!result.empty() && result[0].contains("state_data")) {
            std::string jsonStr = result[0]["state_data"].get<std::string>();
            return nlohmann::json::parse(jsonStr);
        }

        return nlohmann::json();

    } catch (const std::exception& e) {
        Logger::Error("Failed to load game state '{}': {}", key, e.what());
        return nlohmann::json();
    }
}

bool PostgreSqlClient::DeleteGameState(const std::string& key) {
    std::string sql =
        "DELETE FROM game_states WHERE state_key = '" + EscapeString(key) + "'";
    return Execute(sql);
}

std::vector<std::string> PostgreSqlClient::ListGameStates() {
    std::string sql = "SELECT state_key FROM game_states ORDER BY state_key";

    auto result = Query(sql);
    std::vector<std::string> states;

    for (const auto& row : result) {
        if (row.contains("state_key")) {
            states.push_back(row["state_key"].get<std::string>());
        }
    }

    return states;
}

// =============== World Data Operations ===============
bool PostgreSqlClient::SaveChunkData(int chunkX, int chunkZ, const nlohmann::json& chunkData) {
    try {
        std::string dataJson = chunkData.dump();
        std::string escapedJson = EscapeString(dataJson);

        std::string sql =
            "INSERT INTO world_chunks (chunk_x, chunk_z, chunk_data, last_updated) "
            "VALUES (" + std::to_string(chunkX) + ", " + std::to_string(chunkZ) +
            ", " + escapedJson + ", NOW()) "
            "ON CONFLICT (chunk_x, chunk_z) DO UPDATE SET "
            "chunk_data = EXCLUDED.chunk_data, "
            "last_updated = NOW()";

        return Execute(sql);

    } catch (const std::exception& e) {
        Logger::Error("Failed to save chunk [{}, {}]: {}", chunkX, chunkZ, e.what());
        return false;
    }
}

nlohmann::json PostgreSqlClient::LoadChunkData(int chunkX, int chunkZ) {
    try {
        std::string sql =
            "SELECT chunk_data FROM world_chunks "
            "WHERE chunk_x = " + std::to_string(chunkX) +
            " AND chunk_z = " + std::to_string(chunkZ);

        auto result = Query(sql);

        if (!result.empty() && result[0].contains("chunk_data")) {
            std::string jsonStr = result[0]["chunk_data"].get<std::string>();
            return nlohmann::json::parse(jsonStr);
        }

        return nlohmann::json();

    } catch (const std::exception& e) {
        Logger::Error("Failed to load chunk [{}, {}]: {}", chunkX, chunkZ, e.what());
        return nlohmann::json();
    }
}

bool PostgreSqlClient::DeleteChunkData(int chunkX, int chunkZ) {
    std::string sql =
        "DELETE FROM world_chunks WHERE chunk_x = " + std::to_string(chunkX) +
        " AND chunk_z = " + std::to_string(chunkZ);
    return Execute(sql);
}

std::vector<std::pair<int, int>> PostgreSqlClient::ListChunksInRange(int centerX, int centerZ, int radius) {
    // Validate inputs to prevent integer overflow
    if (radius < 0) {
        Logger::Error("Invalid radius: {}", radius);
        return {};
    }
    
    // Prevent integer overflow in calculations
    if (radius > 10000) {  // Reasonable limit for chunk operations
        Logger::Warn("Radius too large: {}, limiting to 10000", radius);
        radius = 10000;
    }
    
    // Use safe subtraction with bounds checking
    int64_t minX = static_cast<int64_t>(centerX) - radius;
    int64_t maxX = static_cast<int64_t>(centerX) + radius;
    int64_t minZ = static_cast<int64_t>(centerZ) - radius;
    int64_t maxZ = static_cast<int64_t>(centerZ) + radius;
    
    // Check for overflow
    if (minX < std::numeric_limits<int>::min() || maxX > std::numeric_limits<int>::max() ||
        minZ < std::numeric_limits<int>::min() || maxZ > std::numeric_limits<int>::max()) {
        Logger::Error("Chunk range calculation overflow");
        return {};
    }

    std::string sql =
        "SELECT chunk_x, chunk_z FROM world_chunks "
        "WHERE chunk_x BETWEEN " + std::to_string(static_cast<int>(minX)) + 
        " AND " + std::to_string(static_cast<int>(maxX)) +
        " AND chunk_z BETWEEN " + std::to_string(static_cast<int>(minZ)) + 
        " AND " + std::to_string(static_cast<int>(maxZ));

    auto result = Query(sql);
    std::vector<std::pair<int, int>> chunks;

    for (const auto& row : result) {
        if (row.contains("chunk_x") && row.contains("chunk_z")) {
            chunks.emplace_back(
                row["chunk_x"].get<int>(),
                row["chunk_z"].get<int>()
            );
        }
    }

    return chunks;
}

// =============== Inventory Operations ===============
bool PostgreSqlClient::SaveInventory(uint64_t playerId, const nlohmann::json& inventory) {
    try {
        std::string invJson = inventory.dump();
        std::string escapedJson = EscapeString(invJson);

        std::string sql =
            "INSERT INTO player_inventory (player_id, inventory_data, last_updated) "
            "VALUES (" + std::to_string(playerId) + ", " + escapedJson + ", NOW()) "
            "ON CONFLICT (player_id) DO UPDATE SET "
            "inventory_data = EXCLUDED.inventory_data, "
            "last_updated = NOW()";

        return Execute(sql);

    } catch (const std::exception& e) {
        Logger::Error("Failed to save inventory for {}: {}", playerId, e.what());
        return false;
    }
}

nlohmann::json PostgreSqlClient::LoadInventory(uint64_t playerId) {
    try {
        std::string sql =
            "SELECT inventory_data FROM player_inventory WHERE player_id = " +
            std::to_string(playerId);

        auto result = Query(sql);

        if (!result.empty() && result[0].contains("inventory_data")) {
            std::string jsonStr = result[0]["inventory_data"].get<std::string>();
            return nlohmann::json::parse(jsonStr);
        }

        return nlohmann::json();

    } catch (const std::exception& e) {
        Logger::Error("Failed to load inventory for {}: {}", playerId, e.what());
        return nlohmann::json();
    }
}

// =============== Quest Operations ===============
bool PostgreSqlClient::SaveQuestProgress(uint64_t playerId, const std::string& questId,
                                        const nlohmann::json& progress) {
    try {
        std::string progressJson = progress.dump();
        std::string escapedJson = EscapeString(progressJson);

        std::string sql =
            "INSERT INTO player_quests (player_id, quest_id, quest_progress, last_updated) "
            "VALUES (" + std::to_string(playerId) + ", '" + EscapeString(questId) +
            "', " + escapedJson + ", NOW()) "
            "ON CONFLICT (player_id, quest_id) DO UPDATE SET "
            "quest_progress = EXCLUDED.quest_progress, "
            "last_updated = NOW()";

        return Execute(sql);

    } catch (const std::exception& e) {
        Logger::Error("Failed to save quest progress for player {}, quest {}: {}",
                     playerId, questId, e.what());
        return false;
    }
}

nlohmann::json PostgreSqlClient::LoadQuestProgress(uint64_t playerId, const std::string& questId) {
    try {
        std::string sql =
            "SELECT quest_progress FROM player_quests "
            "WHERE player_id = " + std::to_string(playerId) +
            " AND quest_id = '" + EscapeString(questId) + "'";

        auto result = Query(sql);

        if (!result.empty() && result[0].contains("quest_progress")) {
            std::string jsonStr = result[0]["quest_progress"].get<std::string>();
            return nlohmann::json::parse(jsonStr);
        }

        return nlohmann::json();

    } catch (const std::exception& e) {
        Logger::Error("Failed to load quest progress for player {}, quest {}: {}",
                     playerId, questId, e.what());
        return nlohmann::json();
    }
}

std::vector<std::string> PostgreSqlClient::ListActiveQuests(uint64_t playerId) {
    std::string sql =
        "SELECT quest_id FROM player_quests "
        "WHERE player_id = " + std::to_string(playerId) +
        " ORDER BY quest_id";

    auto result = Query(sql);
    std::vector<std::string> quests;

    for (const auto& row : result) {
        if (row.contains("quest_id")) {
            quests.push_back(row["quest_id"].get<std::string>());
        }
    }

    return quests;
}

// =============== Transaction Operations ===============
bool PostgreSqlClient::BeginTransaction() {
    PGconn* conn = GetConnection();
    if (!conn) {
        return false;
    }

    std::lock_guard<std::mutex> lock(transactionMutex_);

    // Check if already in transaction
    if (transactionStates_[conn].inTransaction) {
        ReleaseConnection(conn);
        return false;
    }

    bool success = ExecuteCommand(conn, "BEGIN");
    if (success) {
        transactionStates_[conn] = {conn, true};
        stats_.totalTransactions++;
    }

    ReleaseConnection(conn);
    return success;
}

bool PostgreSqlClient::CommitTransaction() {
    PGconn* conn = GetConnection();
    if (!conn) {
        return false;
    }

    std::lock_guard<std::mutex> lock(transactionMutex_);

    // Check if in transaction
    auto it = transactionStates_.find(conn);
    if (it == transactionStates_.end() || !it->second.inTransaction) {
        ReleaseConnection(conn);
        return false;
    }

    bool success = ExecuteCommand(conn, "COMMIT");
    if (success) {
        transactionStates_.erase(conn);
    }

    ReleaseConnection(conn);
    return success;
}

bool PostgreSqlClient::RollbackTransaction() {
    PGconn* conn = GetConnection();
    if (!conn) {
        return false;
    }

    std::lock_guard<std::mutex> lock(transactionMutex_);

    // Check if in transaction
    auto it = transactionStates_.find(conn);
    if (it == transactionStates_.end() || !it->second.inTransaction) {
        ReleaseConnection(conn);
        return false;
    }

    bool success = ExecuteCommand(conn, "ROLLBACK");
    if (success) {
        transactionStates_.erase(conn);
    }

    ReleaseConnection(conn);
    return success;
}

bool PostgreSqlClient::ExecuteTransaction(const std::function<bool()>& operation) {
    if (!BeginTransaction()) {
        return false;
    }

    bool success = false;
    try {
        success = operation();
    } catch (const std::exception& e) {
        Logger::Error("Transaction operation failed: {}", e.what());
        success = false;
    }

    if (success) {
        if (!CommitTransaction()) {
            Logger::Error("Failed to commit transaction");
            RollbackTransaction();
            return false;
        }
    } else {
        RollbackTransaction();
    }

    return success;
}

// =============== Prepared Statements ===============
bool PostgreSqlClient::PrepareStatement(const std::string& name, const std::string& sql, int paramCount) {
    std::lock_guard<std::mutex> lock(preparedStatementsMutex_);

    PGconn* conn = GetConnection();
    if (!conn) {
        return false;
    }

    std::string prepareSql = "PREPARE " + name + " AS " + sql;
    bool success = ExecuteCommand(conn, prepareSql);

    if (success) {
        preparedStatements_[name] = {name, sql, paramCount};
        Logger::Debug("Prepared statement '{}' created", name);
    }

    ReleaseConnection(conn);
    return success;
}

bool PostgreSqlClient::ExecutePrepared(const std::string& name, const std::vector<std::string>& params) {
    std::lock_guard<std::mutex> lock(preparedStatementsMutex_);

    auto it = preparedStatements_.find(name);
    if (it == preparedStatements_.end()) {
        Logger::Error("Prepared statement '{}' not found", name);
        return false;
    }

    if (static_cast<int>(params.size()) != it->second.paramCount) {
        Logger::Error("Parameter count mismatch for prepared statement '{}'", name);
        return false;
    }

    PGconn* conn = GetConnection();
    if (!conn) {
        return false;
    }

    std::ostringstream sql;
    sql << "EXECUTE " << name << "(";

    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            sql << ", ";
        }
        sql << "'" << EscapeString(params[i]) << "'";
    }
    sql << ")";

    bool success = ExecuteCommand(conn, sql.str());
    ReleaseConnection(conn);

    return success;
}

nlohmann::json PostgreSqlClient::QueryPrepared(const std::string& name, const std::vector<std::string>& params) {
    std::lock_guard<std::mutex> lock(preparedStatementsMutex_);

    auto it = preparedStatements_.find(name);
    if (it == preparedStatements_.end()) {
        Logger::Error("Prepared statement '{}' not found", name);
        return nlohmann::json::array();
    }

    if (static_cast<int>(params.size()) != it->second.paramCount) {
        Logger::Error("Parameter count mismatch for prepared statement '{}'", name);
        return nlohmann::json::array();
    }

    PGconn* conn = GetConnection();
    if (!conn) {
        return nlohmann::json::array();
    }

    std::ostringstream sql;
    sql << "EXECUTE " << name << "(";

    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            sql << ", ";
        }
        sql << "'" << EscapeString(params[i]) << "'";
    }
    sql << ")";

    auto result = ExecuteQuery(conn, sql.str());
    ReleaseConnection(conn);

    return result;
}

// =============== Statistics ===============
nlohmann::json PostgreSqlClient::GetDatabaseStats() {
    nlohmann::json stats;

    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - stats_.startTime).count();

    stats["uptime_seconds"] = uptime;
    stats["total_queries"] = stats_.totalQueries.load();
    stats["failed_queries"] = stats_.failedQueries.load();
    stats["total_transactions"] = stats_.totalTransactions.load();
    stats["connection_errors"] = stats_.connectionErrors.load();
    stats["connection_pool_hits"] = stats_.connectionPoolHits.load();
    stats["connection_pool_misses"] = stats_.connectionPoolMisses.load();
    stats["active_connections"] = GetActiveConnections();
    stats["idle_connections"] = GetIdleConnections();
    stats["total_connections"] = connections_.size();
    stats["prepared_statements"] = preparedStatements_.size();

    // Calculate success rate
    if (stats_.totalQueries > 0) {
        double successRate = 100.0 * (1.0 - (double)stats_.failedQueries / stats_.totalQueries);
        stats["success_rate_percent"] = successRate;
    }

    // Calculate pool hit rate
    int64_t totalHits = stats_.connectionPoolHits + stats_.connectionPoolMisses;
    if (totalHits > 0) {
        double hitRate = 100.0 * (double)stats_.connectionPoolHits / totalHits;
        stats["pool_hit_rate_percent"] = hitRate;
    }

    return stats;
}

void PostgreSqlClient::ResetStats() {
    stats_.totalQueries = 0;
    stats_.failedQueries = 0;
    stats_.totalTransactions = 0;
    stats_.connectionErrors = 0;
    stats_.connectionPoolHits = 0;
    stats_.connectionPoolMisses = 0;
    stats_.startTime = std::chrono::steady_clock::now();

    Logger::Info("Database statistics reset");
}

// =============== Error Handling ===============
void PostgreSqlClient::HandleSQLError(PGconn* conn, const std::string& operation) {
    if (!conn) {
        Logger::Error("{}: No connection available", operation);
        return;
    }

    const char* errorMsg = PQerrorMessage(conn);
    if (errorMsg && strlen(errorMsg) > 0) {
        Logger::Error("{}: {}", operation, errorMsg);
    } else {
        Logger::Error("{}: Unknown SQL error", operation);
    }

    stats_.connectionErrors++;
}

bool PostgreSqlClient::ShouldReconnect(PGconn* conn) const {
    if (!conn) {
        return false;
    }

    ConnStatusType status = PQstatus(conn);
    if (status == CONNECTION_BAD || status == CONNECTION_NEEDED) {
        return true;
    }

    // Check if connection is still responsive
    PGresult* result = PQexec(conn, "SELECT 1");
    if (!result) {
        return true;
    }

    ExecStatusType execStatus = PQresultStatus(result);
    PQclear(result);

    return execStatus != PGRES_TUPLES_OK;
}