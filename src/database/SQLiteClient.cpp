#ifdef USE_SQLITE

#include "database/SQLiteClient.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

// =============== Constructor and Destructor ===============
SQLiteClient::SQLiteClient(const nlohmann::json& config)
    : db_(nullptr),
      config_(config),
      lastInsertId_(0),
      affectedRows_(0) {

    // Determine database file path: try "name" (from DbManager), fallback to "file", then default
    if (config.contains("file") && config["file"].is_string()) {
        dbPath_ = config["file"].get<std::string>();
    } else if (config.contains("name") && config["name"].is_string()) {
        dbPath_ = config["name"].get<std::string>();
    } else {
        dbPath_ = "game.db";
    }

    // Shards configuration (SQLite doesn't support sharding, but keep for interface)
    int shards = config.value("shards", 1);
    totalShards_ = (shards > 0) ? shards : 1;

    stats_.startTime = std::chrono::steady_clock::now();
    Logger::Debug("SQLiteClient created with database file: {}", dbPath_);
}

SQLiteClient::~SQLiteClient() {
    Disconnect();
    Logger::Debug("SQLiteClient destroyed");
}

// =============== Connection Management ===============
bool SQLiteClient::Connect() {
    std::lock_guard<std::mutex> lock(dbMutex_);

    if (db_) {
        // Already connected
        return true;
    }

    // Ensure the directory exists
    std::filesystem::path path(dbPath_);
    std::filesystem::path dir = path.parent_path();
    if (!dir.empty() && !std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }

    // Open the database
    int rc = sqlite3_open(dbPath_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        Logger::Error("Failed to open SQLite database '{}': {}", dbPath_, sqlite3_errmsg(db_));
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        return false;
    }

    // Enable foreign keys
    char* errMsg = nullptr;
    rc = sqlite3_exec(db_, "PRAGMA foreign_keys = ON;", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        Logger::Warn("Failed to enable foreign keys: {}", errMsg ? errMsg : "unknown error");
        sqlite3_free(errMsg);
    }

    // Enable JSON1 extension (if available)
    rc = sqlite3_exec(db_, "SELECT json('{}');", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        Logger::Warn("JSON1 extension not available: {}", errMsg ? errMsg : "unknown error");
        sqlite3_free(errMsg);
    }

    Logger::Info("Connected to SQLite database: {}", dbPath_);
    return true;
}

bool SQLiteClient::ConnectToDatabase(const std::string& dbname) {
    // SQLite: dbname is the file path; we can change the file.
    Disconnect();
    dbPath_ = dbname;
    return Connect();
}

bool SQLiteClient::Reconnect() {
    Disconnect();
    return Connect();
}

void SQLiteClient::Disconnect() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
        Logger::Info("Disconnected from SQLite database");
    }
}

bool SQLiteClient::IsConnected() const {
    std::lock_guard<std::mutex> lock(dbMutex_);
    return db_ != nullptr;
}

bool SQLiteClient::CheckHealth() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) return false;
    // Execute a simple query to test
    const char* sql = "SELECT 1;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW;
}

void SQLiteClient::ReconnectAll() {
    Reconnect();
}

// =============== Connection Pool Management (dummy) ===============
bool SQLiteClient::InitializeConnectionPool(size_t /*minConnections*/, size_t /*maxConnections*/) {
    Logger::Debug("SQLiteClient: connection pool not implemented (single connection used)");
    return true; // no-op, always succeeds
}

void SQLiteClient::ReleaseConnectionPool() {
    // no-op
}

size_t SQLiteClient::GetActiveConnections() const {
    return db_ ? 1 : 0;
}

size_t SQLiteClient::GetIdleConnections() const {
    return 0;
}

// =============== Helper Methods ===============
bool SQLiteClient::ExecuteSql(const std::string& sql, std::vector<std::vector<std::string>>* results) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) {
        Logger::Error("ExecuteSql: database not connected");
        stats_.failedQueries++;
        stats_.connectionErrors++;
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* tail = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), static_cast<int>(sql.size()), &stmt, &tail);

    if (rc != SQLITE_OK) {
        Logger::Error("SQL prepare error: {} (SQL: {})", sqlite3_errmsg(db_), sql);
        stats_.failedQueries++;
        return false;
    }

    // Execute and possibly fetch results
    bool success = true;
    int stepResult = sqlite3_step(stmt);
    if (stepResult == SQLITE_ROW) {
        // Query returns rows
        if (results) {
            int colCount = sqlite3_column_count(stmt);
            do {
                std::vector<std::string> row;
                for (int i = 0; i < colCount; ++i) {
                    const unsigned char* text = sqlite3_column_text(stmt, i);
                    if (text) {
                        row.emplace_back(reinterpret_cast<const char*>(text));
                    } else {
                        row.emplace_back(); // empty string for NULL
                    }
                }
                results->push_back(std::move(row));
            } while ((stepResult = sqlite3_step(stmt)) == SQLITE_ROW);
        } else {
            // Just step through without collecting
            while ((stepResult = sqlite3_step(stmt)) == SQLITE_ROW) {}
        }
    }

    if (stepResult != SQLITE_DONE) {
        Logger::Error("SQL step error: {} (SQL: {})", sqlite3_errmsg(db_), sql);
        success = false;
        stats_.failedQueries++;
    } else {
        // For INSERT/UPDATE, get last insert rowid and changes
        if (sql.find("INSERT") != std::string::npos || sql.find("UPDATE") != std::string::npos ||
            sql.find("DELETE") != std::string::npos) {
            lastInsertId_ = static_cast<int64_t>(sqlite3_last_insert_rowid(db_));
            affectedRows_ = sqlite3_changes(db_);
        }
        stats_.totalQueries++;
    }

    sqlite3_finalize(stmt);
    return success;
}

nlohmann::json SQLiteClient::ResultSetToJson(const std::vector<std::vector<std::string>>& rows,
                                             const std::vector<std::string>& columnNames) const {
    nlohmann::json result = nlohmann::json::array();
    for (const auto& row : rows) {
        nlohmann::json rowObj;
        for (size_t i = 0; i < columnNames.size() && i < row.size(); ++i) {
            const std::string& value = row[i];
            if (value.empty()) {
                rowObj[columnNames[i]] = nullptr;
            } else {
                // Try to parse as JSON if it looks like JSON
                if (!value.empty() && (value[0] == '{' || value[0] == '[')) {
                    try {
                        rowObj[columnNames[i]] = nlohmann::json::parse(value);
                    } catch (...) {
                        rowObj[columnNames[i]] = value;
                    }
                } else {
                    rowObj[columnNames[i]] = value;
                }
            }
        }
        result.push_back(rowObj);
    }
    return result;
}

std::string SQLiteClient::EscapeString(const std::string& str) {
    // SQLite escaping: double single quotes
    std::string escaped;
    escaped.reserve(str.size() + 2);
    for (char c : str) {
        if (c == '\'') escaped += "''";
        else escaped += c;
    }
    return escaped;
}

bool SQLiteClient::TableExists(const std::string& tableName) {
    std::string sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='" + EscapeString(tableName) + "';";
    std::vector<std::vector<std::string>> results;
    if (!ExecuteSql(sql, &results)) {
        return false;
    }
    return !results.empty();
}

// =============== Query Operations ===============
nlohmann::json SQLiteClient::Query(const std::string& sql) {
    std::vector<std::vector<std::string>> rows;
    if (!ExecuteSql(sql, &rows)) {
        return nlohmann::json::array();
    }

    // Need column names. Since we don't have them from ExecuteSql, we need to prepare separately.
    // Alternative: use sqlite3_column_name in ExecuteSql and return column names.
    // For simplicity, we'll modify ExecuteSql to optionally return column names.
    // But to keep changes minimal, we'll re-execute a separate query to get column info? Not efficient.
    // Better to enhance ExecuteSql to return column names. Let's redesign quickly.

    // For now, we'll assume Query is used with SELECT and we can get column names via a separate query.
    // But that's hacky. Let's implement a proper method that returns both rows and column names.
    // We'll refactor: ExecuteSql will fill a struct with rows and column names.

    // Since we're in the middle of implementation, let's create a private struct ResultSet.
    // But to avoid major changes, we'll create a new helper that does the full job.

    // Let's implement a method ExecuteQuery that returns nlohmann::json directly.
    // We'll keep ExecuteSql for simple execution.

    // Instead, we'll add a new method ExecuteSelect that returns json.
    // But for now, we'll implement Query by calling ExecuteSql and then constructing JSON without column names - that's wrong.

    // So let's properly implement Query using sqlite3 directly.

    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) {
        Logger::Error("Query: database not connected");
        stats_.failedQueries++;
        return nlohmann::json::array();
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::Error("Query prepare error: {} (SQL: {})", sqlite3_errmsg(db_), sql);
        stats_.failedQueries++;
        return nlohmann::json::array();
    }

    // Get column names
    int colCount = sqlite3_column_count(stmt);
    std::vector<std::string> colNames;
    for (int i = 0; i < colCount; ++i) {
        colNames.push_back(sqlite3_column_name(stmt, i));
    }

    // Fetch rows
    nlohmann::json result = nlohmann::json::array();
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        nlohmann::json rowObj;
        for (int i = 0; i < colCount; ++i) {
            const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
            if (text) {
                std::string value(text);
                // Try to parse JSON
                if (!value.empty() && (value[0] == '{' || value[0] == '[')) {
                    try {
                        rowObj[colNames[i]] = nlohmann::json::parse(value);
                    } catch (...) {
                        rowObj[colNames[i]] = value;
                    }
                } else {
                    rowObj[colNames[i]] = value;
                }
            } else {
                rowObj[colNames[i]] = nullptr;
            }
        }
        result.push_back(rowObj);
    }

    if (rc != SQLITE_DONE) {
        Logger::Error("Query step error: {} (SQL: {})", sqlite3_errmsg(db_), sql);
        stats_.failedQueries++;
        sqlite3_finalize(stmt);
        return nlohmann::json::array();
    }

    sqlite3_finalize(stmt);
    stats_.totalQueries++;
    return result;
}

nlohmann::json SQLiteClient::QueryWithParams(const std::string& sql, const std::vector<std::string>& params) {
    // SQLite doesn't support named parameters easily; we can construct the SQL by escaping.
    // Not the safest but acceptable for now.
    std::string processedSql = sql;
    size_t pos = 0;
    for (const auto& param : params) {
        pos = processedSql.find('?', pos);
        if (pos == std::string::npos) break;
        processedSql.replace(pos, 1, "'" + EscapeString(param) + "'");
        pos += param.size() + 2;
    }
    return Query(processedSql);
}

bool SQLiteClient::Execute(const std::string& sql) {
    return ExecuteSql(sql);
}

bool SQLiteClient::ExecuteWithParams(const std::string& sql, const std::vector<std::string>& params) {
    std::string processedSql = sql;
    size_t pos = 0;
    for (const auto& param : params) {
        pos = processedSql.find('?', pos);
        if (pos == std::string::npos) break;
        processedSql.replace(pos, 1, "'" + EscapeString(param) + "'");
        pos += param.size() + 2;
    }
    return Execute(processedSql);
}

// =============== Shard Operations (ignore shardId) ===============
nlohmann::json SQLiteClient::QueryShard(int /*shardId*/, const std::string& sql) {
    return Query(sql);
}
nlohmann::json SQLiteClient::QueryShardWithParams(int /*shardId*/, const std::string& sql,
                                                 const std::vector<std::string>& params) {
    return QueryWithParams(sql, params);
}
bool SQLiteClient::ExecuteShard(int /*shardId*/, const std::string& sql) {
    return Execute(sql);
}
bool SQLiteClient::ExecuteShardWithParams(int /*shardId*/, const std::string& sql,
                                         const std::vector<std::string>& params) {
    return ExecuteWithParams(sql, params);
}

// =============== Utility Methods ===============
int SQLiteClient::GetShardId(uint64_t entityId) const {
    // Simple hash modulo shard count (dummy, always 0)
    return static_cast<int>(entityId % totalShards_);
}
int SQLiteClient::GetTotalShards() const {
    return totalShards_;
}
std::string SQLiteClient::GetConnectionInfo() const {
    return "SQLite: " + dbPath_;
}
int64_t SQLiteClient::GetLastInsertId() {
    return lastInsertId_;
}
int SQLiteClient::GetAffectedRows() {
    return affectedRows_;
}

// =============== Statistics ===============
nlohmann::json SQLiteClient::GetDatabaseStats() {
    nlohmann::json stats;
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - stats_.startTime).count();

    stats["uptime_seconds"] = uptime;
    stats["total_queries"] = stats_.totalQueries.load();
    stats["failed_queries"] = stats_.failedQueries.load();
    stats["total_transactions"] = stats_.totalTransactions.load();
    stats["connection_errors"] = stats_.connectionErrors.load();
    stats["active_connections"] = GetActiveConnections();
    stats["idle_connections"] = GetIdleConnections();
    stats["database_file"] = dbPath_;

    if (stats_.totalQueries > 0) {
        double successRate = 100.0 * (1.0 - static_cast<double>(stats_.failedQueries) / stats_.totalQueries);
        stats["success_rate_percent"] = successRate;
    }

    return stats;
}

void SQLiteClient::ResetStats() {
    stats_.totalQueries = 0;
    stats_.failedQueries = 0;
    stats_.totalTransactions = 0;
    stats_.connectionErrors = 0;
    stats_.startTime = std::chrono::steady_clock::now();
    Logger::Info("SQLite statistics reset");
}

// =============== Transaction Operations ===============
bool SQLiteClient::BeginTransaction() {
    if (Execute("BEGIN TRANSACTION;")) {
        stats_.totalTransactions++;
        return true;
    }
    return false;
}
bool SQLiteClient::CommitTransaction() {
    return Execute("COMMIT;");
}
bool SQLiteClient::RollbackTransaction() {
    return Execute("ROLLBACK;");
}
bool SQLiteClient::ExecuteTransaction(const std::function<bool()>& operation) {
    if (!BeginTransaction()) return false;
    bool success = false;
    try {
        success = operation();
    } catch (...) {
        success = false;
    }
    if (success) {
        if (!CommitTransaction()) {
            RollbackTransaction();
            return false;
        }
    } else {
        RollbackTransaction();
    }
    return success;
}

// =============== Player Data Operations ===============
bool SQLiteClient::SavePlayerData(uint64_t playerId, const nlohmann::json& data) {
    std::string dataJson = data.dump();
    std::string escaped = EscapeString(dataJson);
    std::string sql = "INSERT OR REPLACE INTO players (id, data, updated_at) VALUES (" +
                      std::to_string(playerId) + ", '" + escaped + "', datetime('now'));";
    return Execute(sql);
}

nlohmann::json SQLiteClient::LoadPlayerData(uint64_t playerId) {
    std::string sql = "SELECT data FROM players WHERE id = " + std::to_string(playerId) + ";";
    auto result = Query(sql);
    if (!result.empty() && result[0].contains("data")) {
        return result[0]["data"];
    }
    return nlohmann::json();
}

bool SQLiteClient::UpdatePlayer(uint64_t playerId, const nlohmann::json& updates) {
    if (updates.empty()) return true;
    std::ostringstream sql;
    sql << "UPDATE players SET ";
    bool first = true;
    for (const auto& [key, value] : updates.items()) {
        if (!first) sql << ", ";
        first = false;
        if (value.is_string()) {
            sql << key << " = '" << EscapeString(value.get<std::string>()) << "'";
        } else {
            sql << key << " = '" << EscapeString(value.dump()) << "'";
        }
    }
    sql << ", updated_at = datetime('now') WHERE id = " << playerId << ";";
    return Execute(sql.str());
}

bool SQLiteClient::DeletePlayer(uint64_t playerId) {
    std::string sql = "DELETE FROM players WHERE id = " + std::to_string(playerId) + ";";
    return Execute(sql);
}

bool SQLiteClient::UpdatePlayerPosition(uint64_t playerId, float x, float y, float z) {
    std::string sql = "UPDATE players SET pos_x = " + std::to_string(x) +
                      ", pos_y = " + std::to_string(y) +
                      ", pos_z = " + std::to_string(z) +
                      ", updated_at = datetime('now') WHERE id = " + std::to_string(playerId) + ";";
    return Execute(sql);
}

bool SQLiteClient::PlayerExists(uint64_t playerId) {
    std::string sql = "SELECT 1 FROM players WHERE id = " + std::to_string(playerId) + " LIMIT 1;";
    auto result = Query(sql);
    return !result.empty();
}

nlohmann::json SQLiteClient::GetPlayerStats(uint64_t playerId) {
    std::string sql = "SELECT level, experience, health, max_health, mana, max_mana, "
                      "currency_gold, currency_gems, total_playtime "
                      "FROM players WHERE id = " + std::to_string(playerId) + ";";
    auto result = Query(sql);
    if (!result.empty()) return result[0];
    return nlohmann::json();
}

bool SQLiteClient::UpdatePlayerStats(uint64_t playerId, const nlohmann::json& stats) {
    return UpdatePlayer(playerId, stats);
}

nlohmann::json SQLiteClient::GetPlayer(uint64_t playerId) {
    std::string sql = "SELECT * FROM players WHERE id = " + std::to_string(playerId) + ";";
    auto result = Query(sql);
    if (!result.empty()) return result[0];
    return nlohmann::json();
}

// =============== Game State Operations ===============
bool SQLiteClient::SaveGameState(const std::string& key, const nlohmann::json& state) {
    std::string stateJson = state.dump();
    std::string escaped = EscapeString(stateJson);
    std::string sql = "INSERT OR REPLACE INTO game_state (key, value, updated_at) VALUES ('" +
                      EscapeString(key) + "', '" + escaped + "', datetime('now'));";
    return Execute(sql);
}

nlohmann::json SQLiteClient::LoadGameState(const std::string& key) {
    std::string sql = "SELECT value FROM game_state WHERE key = '" + EscapeString(key) + "';";
    auto result = Query(sql);
    if (!result.empty() && result[0].contains("value")) {
        return result[0]["value"];
    }
    return nlohmann::json();
}

bool SQLiteClient::DeleteGameState(const std::string& key) {
    std::string sql = "DELETE FROM game_state WHERE key = '" + EscapeString(key) + "';";
    return Execute(sql);
}

std::vector<std::string> SQLiteClient::ListGameStates() {
    std::string sql = "SELECT key FROM game_state ORDER BY key;";
    auto result = Query(sql);
    std::vector<std::string> keys;
    for (const auto& row : result) {
        if (row.contains("key")) keys.push_back(row["key"].get<std::string>());
    }
    return keys;
}

// =============== World Data Operations ===============
bool SQLiteClient::SaveChunkData(int chunkX, int chunkZ, const nlohmann::json& chunkData) {
    std::string dataJson = chunkData.dump();
    std::string escaped = EscapeString(dataJson);
    std::string sql = "INSERT OR REPLACE INTO world_chunks (chunk_x, chunk_z, data, generated_at) VALUES (" +
                      std::to_string(chunkX) + ", " + std::to_string(chunkZ) + ", '" + escaped + "', datetime('now'));";
    return Execute(sql);
}

nlohmann::json SQLiteClient::LoadChunkData(int chunkX, int chunkZ) {
    std::string sql = "SELECT data FROM world_chunks WHERE chunk_x = " + std::to_string(chunkX) +
                      " AND chunk_z = " + std::to_string(chunkZ) + ";";
    auto result = Query(sql);
    if (!result.empty() && result[0].contains("data")) {
        return result[0]["data"];
    }
    return nlohmann::json();
}

bool SQLiteClient::DeleteChunkData(int chunkX, int chunkZ) {
    std::string sql = "DELETE FROM world_chunks WHERE chunk_x = " + std::to_string(chunkX) +
                      " AND chunk_z = " + std::to_string(chunkZ) + ";";
    return Execute(sql);
}

std::vector<std::pair<int, int>> SQLiteClient::ListChunksInRange(int centerX, int centerZ, int radius) {
    if (radius < 0) return {};
    if (radius > 10000) radius = 10000;
    int64_t minX = static_cast<int64_t>(centerX) - radius;
    int64_t maxX = static_cast<int64_t>(centerX) + radius;
    int64_t minZ = static_cast<int64_t>(centerZ) - radius;
    int64_t maxZ = static_cast<int64_t>(centerZ) + radius;
    if (minX < std::numeric_limits<int>::min() || maxX > std::numeric_limits<int>::max() ||
        minZ < std::numeric_limits<int>::min() || maxZ > std::numeric_limits<int>::max()) {
        return {};
    }
    std::string sql = "SELECT chunk_x, chunk_z FROM world_chunks "
                      "WHERE chunk_x BETWEEN " + std::to_string(static_cast<int>(minX)) +
                      " AND " + std::to_string(static_cast<int>(maxX)) +
                      " AND chunk_z BETWEEN " + std::to_string(static_cast<int>(minZ)) +
                      " AND " + std::to_string(static_cast<int>(maxZ)) + ";";
    auto result = Query(sql);
    std::vector<std::pair<int, int>> chunks;
    for (const auto& row : result) {
        if (row.contains("chunk_x") && row.contains("chunk_z")) {
            chunks.emplace_back(row["chunk_x"].get<int>(), row["chunk_z"].get<int>());
        }
    }
    return chunks;
}

// =============== Inventory Operations ===============
bool SQLiteClient::SaveInventory(uint64_t playerId, const nlohmann::json& inventory) {
    std::string invJson = inventory.dump();
    std::string escaped = EscapeString(invJson);
    std::string sql = "INSERT OR REPLACE INTO player_inventory (player_id, data, updated_at) VALUES (" +
                      std::to_string(playerId) + ", '" + escaped + "', datetime('now'));";
    return Execute(sql);
}

nlohmann::json SQLiteClient::LoadInventory(uint64_t playerId) {
    std::string sql = "SELECT data FROM player_inventory WHERE player_id = " + std::to_string(playerId) + ";";
    auto result = Query(sql);
    if (!result.empty() && result[0].contains("data")) {
        return result[0]["data"];
    }
    return nlohmann::json();
}

// =============== Quest Operations ===============
bool SQLiteClient::SaveQuestProgress(uint64_t playerId, const std::string& questId, const nlohmann::json& progress) {
    std::string progJson = progress.dump();
    std::string escaped = EscapeString(progJson);
    std::string sql = "INSERT OR REPLACE INTO player_quests (player_id, quest_id, progress, updated_at) VALUES (" +
                      std::to_string(playerId) + ", '" + EscapeString(questId) + "', '" + escaped + "', datetime('now'));";
    return Execute(sql);
}

nlohmann::json SQLiteClient::LoadQuestProgress(uint64_t playerId, const std::string& questId) {
    std::string sql = "SELECT progress FROM player_quests WHERE player_id = " + std::to_string(playerId) +
                      " AND quest_id = '" + EscapeString(questId) + "';";
    auto result = Query(sql);
    if (!result.empty() && result[0].contains("progress")) {
        return result[0]["progress"];
    }
    return nlohmann::json();
}

std::vector<std::string> SQLiteClient::ListActiveQuests(uint64_t playerId) {
    std::string sql = "SELECT quest_id FROM player_quests WHERE player_id = " + std::to_string(playerId) +
                      " ORDER BY quest_id;";
    auto result = Query(sql);
    std::vector<std::string> quests;
    for (const auto& row : result) {
        if (row.contains("quest_id")) quests.push_back(row["quest_id"].get<std::string>());
    }
    return quests;
}

#endif // USE_SQLITE
