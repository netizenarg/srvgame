#ifdef USE_SQLITE

#include "database/SQLiteClient.hpp"

SQLiteClient::SQLiteClient(const nlohmann::json& config, const SQLProvider& sqlProvider)
    : db_(nullptr),
      sqlProvider_(sqlProvider),
      config_(config),
      lastInsertId_(0),
      affectedRows_(0)
{
    if (config.contains("file") && config["file"].is_string()) {
        dbPath_ = config["file"].get<std::string>();
    } else if (config.contains("name") && config["name"].is_string()) {
        dbPath_ = config["name"].get<std::string>();
    } else {
        dbPath_ = "game.db";
    }
    int shards = config.value("citus.shard_count", 1);
    totalShards_ = (shards > 0) ? shards : 1;
    stats_.startTime = std::chrono::steady_clock::now();
    Logger::Debug("SQLiteClient created with database file: {}", dbPath_);
}

SQLiteClient::~SQLiteClient() {
    Disconnect();
    Logger::Debug("SQLiteClient destroyed");
}

bool SQLiteClient::Connect() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (db_) return true;

    std::filesystem::path path(dbPath_);
    std::filesystem::path dir = path.parent_path();
    if (!dir.empty() && !std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }

    int rc = sqlite3_open(dbPath_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        Logger::Error("Failed to open SQLite database '{}': {}", dbPath_, sqlite3_errmsg(db_));
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    sqlite3_busy_timeout(db_, 5000);

    char* errMsg = nullptr;

    rc = sqlite3_exec(db_, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        Logger::Warn("Failed to enable synchronous NORMAL: {}", errMsg ? errMsg : "unknown error");
        sqlite3_free(errMsg);
    }

    rc = sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        Logger::Warn("Failed to enable WAL mode: {}", errMsg ? errMsg : "unknown error");
        sqlite3_free(errMsg);
    }

    rc = sqlite3_exec(db_, "PRAGMA foreign_keys = ON;", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        Logger::Warn("Failed to enable foreign keys: {}", errMsg ? errMsg : "unknown error");
        sqlite3_free(errMsg);
    }

    rc = sqlite3_exec(db_, "SELECT json('{}');", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        Logger::Warn("JSON1 extension not available: {}", errMsg ? errMsg : "unknown error");
        sqlite3_free(errMsg);
    }

    Logger::Info("Connected to SQLite database: {}", dbPath_);
    return true;
}

bool SQLiteClient::ConnectToDatabase(const std::string& dbname) {
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
    const char* sql = "SELECT 1;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW;
}

void SQLiteClient::ReconnectAll() {
    Reconnect();
}

bool SQLiteClient::InitializeConnectionPool(size_t /*minConnections*/, size_t /*maxConnections*/) {
    Logger::Debug("SQLiteClient: connection pool not implemented");
    return true;
}

void SQLiteClient::ReleaseConnectionPool() {}

size_t SQLiteClient::GetActiveConnections() const {
    return db_ ? 1 : 0;
}

size_t SQLiteClient::GetIdleConnections() const {
    return 0;
}

bool SQLiteClient::ExecuteSql(const std::string& sql, std::vector<std::vector<std::string>>* results) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    //Logger::Trace("SQLiteClient::ExecuteSql start: {}", sql.substr(0, 100));
    if (!db_) {
        Logger::Error("SQLiteClient::ExecuteSql: database not connected");
        stats_.failedQueries++;
        stats_.connectionErrors++;
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::Error("SQL prepare error: {} (SQL: {})", sqlite3_errmsg(db_), sql);
        stats_.failedQueries++;
        return false;
    }

    bool success = true;
    int stepResult = sqlite3_step(stmt);
    if (stepResult == SQLITE_ROW) {
        if (results) {
            int colCount = sqlite3_column_count(stmt);
            do {
                std::vector<std::string> row;
                for (int i = 0; i < colCount; ++i) {
                    const unsigned char* text = sqlite3_column_text(stmt, i);
                    row.emplace_back(text ? reinterpret_cast<const char*>(text) : "");
                }
                results->push_back(std::move(row));
            } while ((stepResult = sqlite3_step(stmt)) == SQLITE_ROW);
        } else {
            while ((stepResult = sqlite3_step(stmt)) == SQLITE_ROW) {}
        }
    }

    if (stepResult != SQLITE_DONE) {
        Logger::Error("SQLiteClient::ExecuteSql step error: {} (SQL: {})", sqlite3_errmsg(db_), sql);
        success = false;
        stats_.failedQueries++;
    } else {
        if (sql.find("INSERT") != std::string::npos || sql.find("UPDATE") != std::string::npos ||
            sql.find("DELETE") != std::string::npos) {
            lastInsertId_ = static_cast<int64_t>(sqlite3_last_insert_rowid(db_));
            affectedRows_ = sqlite3_changes(db_);
        }
        stats_.totalQueries++;
    }

    sqlite3_finalize(stmt);
    //Logger::Trace("SQLiteClient::ExecuteSql end");
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
    return ExecuteSql(sql, &results) && !results.empty();
}

nlohmann::json SQLiteClient::Query(const std::string& sql) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    Logger::Trace("SQLite Query start: {}", sql.substr(0, 100));
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

    int colCount = sqlite3_column_count(stmt);
    std::vector<std::string> colNames;
    for (int i = 0; i < colCount; ++i) {
        colNames.push_back(sqlite3_column_name(stmt, i));
    }

    nlohmann::json result = nlohmann::json::array();
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        nlohmann::json rowObj;
        for (int i = 0; i < colCount; ++i) {
            const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
            if (text) {
                std::string value(text);
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
    Logger::Trace("SQLite Query end");
    return result;
}

nlohmann::json SQLiteClient::QueryWithParams(const std::string& sql, const std::vector<std::string>& params) {
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

int SQLiteClient::GetShardId(uint64_t entityId) const {
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

bool SQLiteClient::BeginTransaction() {
    std::string sql = sqlProvider_.GetQuery("begin_transaction");
    if (sql.empty()) sql = "BEGIN TRANSACTION;";
    if (Execute(sql)) {
        stats_.totalTransactions++;
        return true;
    }
    return false;
}

bool SQLiteClient::CommitTransaction() {
    std::string sql = sqlProvider_.GetQuery("commit_transaction");
    if (sql.empty()) sql = "COMMIT;";
    return Execute(sql);
}

bool SQLiteClient::RollbackTransaction() {
    std::string sql = sqlProvider_.GetQuery("rollback_transaction");
    if (sql.empty()) sql = "ROLLBACK;";
    return Execute(sql);
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

bool SQLiteClient::SavePlayerData(uint64_t playerId, const nlohmann::json& data) {
    std::string sql = sqlProvider_.GetQuery("save_player_data");
    if (sql.empty()) {
        Logger::Error("Missing SQL: save_player_data");
        return false;
    }
    return ExecuteWithParams(sql, { std::to_string(playerId), data.dump() });
}

nlohmann::json SQLiteClient::LoadPlayerData(uint64_t playerId) {
    std::string sql = sqlProvider_.GetQuery("load_player_data");
    if (sql.empty()) return nlohmann::json();
    auto result = QueryWithParams(sql, { std::to_string(playerId) });
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
    std::string sql = sqlProvider_.GetQuery("delete_player");
    if (sql.empty()) {
        sql = "DELETE FROM players WHERE id = ?;";
    }
    return ExecuteWithParams(sql, { std::to_string(playerId) });
}

bool SQLiteClient::UpdatePlayerPosition(uint64_t playerId, float x, float y, float z) {
    std::string sql = sqlProvider_.GetQuery("update_player_position");
    if (sql.empty()) {
        sql = "UPDATE players SET position_x = ?, position_y = ?, position_z = ?, updated_at = datetime('now') WHERE id = ?;";
    }
    return ExecuteWithParams(sql, { std::to_string(x), std::to_string(y), std::to_string(z), std::to_string(playerId) });
}

bool SQLiteClient::PlayerExists(uint64_t playerId) {
    std::string sql = sqlProvider_.GetQuery("player_exists");
    if (sql.empty()) {
        sql = "SELECT 1 FROM players WHERE id = ? LIMIT 1;";
    }
    auto result = QueryWithParams(sql, { std::to_string(playerId) });
    return !result.empty();
}

nlohmann::json SQLiteClient::GetPlayerStats(uint64_t playerId) {
    std::string sql = sqlProvider_.GetQuery("get_player_stats");
    if (sql.empty()) {
        sql = "SELECT level, experience, health, max_health, mana, max_mana, currency_gold, currency_gems, total_playtime FROM players WHERE id = ?;";
    }
    auto result = QueryWithParams(sql, { std::to_string(playerId) });
    if (!result.empty()) return result[0];
    return nlohmann::json();
}

bool SQLiteClient::UpdatePlayerStats(uint64_t playerId, const nlohmann::json& stats) {
    return UpdatePlayer(playerId, stats);
}

nlohmann::json SQLiteClient::GetPlayer(uint64_t playerId) {
    std::string sql = sqlProvider_.GetQuery("get_player");
    if (sql.empty()) {
        sql = "SELECT * FROM players WHERE id = ?;";
    }
    auto result = QueryWithParams(sql, { std::to_string(playerId) });
    if (!result.empty()) return result[0];
    return nlohmann::json();
}

bool SQLiteClient::SaveGameState(const std::string& key, const nlohmann::json& state) {
    std::string sql = sqlProvider_.GetQuery("save_game_state");
    if (sql.empty()) {
        sql = "INSERT OR REPLACE INTO game_state (key, value, updated_at) VALUES (?, ?, datetime('now'));";
    }
    return ExecuteWithParams(sql, { key, state.dump() });
}

nlohmann::json SQLiteClient::LoadGameState(const std::string& key) {
    std::string sql = sqlProvider_.GetQuery("load_game_state");
    if (sql.empty()) {
        sql = "SELECT value FROM game_state WHERE key = ?;";
    }
    auto result = QueryWithParams(sql, { key });
    if (!result.empty() && result[0].contains("value")) {
        return result[0]["value"];
    }
    return nlohmann::json();
}

bool SQLiteClient::DeleteGameState(const std::string& key) {
    std::string sql = sqlProvider_.GetQuery("delete_game_state");
    if (sql.empty()) {
        sql = "DELETE FROM game_state WHERE key = ?;";
    }
    return ExecuteWithParams(sql, { key });
}

std::vector<std::string> SQLiteClient::ListGameStates() {
    std::string sql = sqlProvider_.GetQuery("list_game_states");
    if (sql.empty()) {
        sql = "SELECT key FROM game_state ORDER BY key;";
    }
    auto result = Query(sql);
    std::vector<std::string> keys;
    for (const auto& row : result) {
        if (row.contains("key")) keys.push_back(row["key"].get<std::string>());
    }
    return keys;
}

bool SQLiteClient::SaveChunkData(int chunkX, int chunkZ, const nlohmann::json& chunkData) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) {
        Logger::Error("SaveChunkData: database not connected");
        return false;
    }

    std::string sql = sqlProvider_.GetQuery("save_chunk_data");
    if (sql.empty()) {
        sql = "INSERT OR REPLACE INTO world_chunks (chunk_x, chunk_z, biome, data, last_updated) VALUES (?, ?, ?, ?, datetime('now'));";
    }

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        Logger::Error("Failed to begin transaction: {}", errMsg ? errMsg : "unknown");
        sqlite3_free(errMsg);
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::Error("Failed to prepare chunk save statement: {}", sqlite3_errmsg(db_));
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    std::string chunkXStr = std::to_string(chunkX);
    std::string chunkZStr = std::to_string(chunkZ);
    std::string dataStr = chunkData.dump();

    sqlite3_bind_text(stmt, 1, chunkXStr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, chunkZStr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, 0);  // biome default
    sqlite3_bind_text(stmt, 4, dataStr.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    bool success = (rc == SQLITE_DONE);
    if (!success) {
        Logger::Error("Failed to execute chunk save: {}", sqlite3_errmsg(db_));
    }

    sqlite3_finalize(stmt);

    if (success) {
        rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            Logger::Error("Failed to commit chunk save: {}", errMsg ? errMsg : "unknown");
            sqlite3_free(errMsg);
            success = false;
        }
    } else {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    }

    if (success) {
        lastInsertId_ = sqlite3_last_insert_rowid(db_);
        affectedRows_ = sqlite3_changes(db_);
        stats_.totalQueries++;
    } else {
        stats_.failedQueries++;
    }

    return success;
}

nlohmann::json SQLiteClient::LoadChunkData(int chunkX, int chunkZ) {
    std::string sql = sqlProvider_.GetQuery("load_chunk_data");
    if (sql.empty()) {
        sql = "SELECT data FROM world_chunks WHERE chunk_x = ? AND chunk_z = ?;";
    }
    auto result = QueryWithParams(sql, { std::to_string(chunkX), std::to_string(chunkZ) });
    if (!result.empty() && result[0].contains("data")) {
        return result[0]["data"];
    }
    return nlohmann::json();
}

bool SQLiteClient::DeleteChunkData(int chunkX, int chunkZ) {
    std::string sql = sqlProvider_.GetQuery("delete_chunk_data");
    if (sql.empty()) {
        sql = "DELETE FROM world_chunks WHERE chunk_x = ? AND chunk_z = ?;";
    }
    return ExecuteWithParams(sql, { std::to_string(chunkX), std::to_string(chunkZ) });
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
    std::string sql = sqlProvider_.GetQuery("list_chunks_in_range");
    if (sql.empty()) {
        sql = "SELECT chunk_x, chunk_z FROM world_chunks WHERE chunk_x BETWEEN ? AND ? AND chunk_z BETWEEN ? AND ?;";
    }
    auto result = QueryWithParams(sql, {
        std::to_string(static_cast<int>(minX)), std::to_string(static_cast<int>(maxX)),
        std::to_string(static_cast<int>(minZ)), std::to_string(static_cast<int>(maxZ))
    });
    std::vector<std::pair<int, int>> chunks;
    for (const auto& row : result) {
        if (row.contains("chunk_x") && row.contains("chunk_z")) {
            chunks.emplace_back(row["chunk_x"].get<int>(), row["chunk_z"].get<int>());
        }
    }
    return chunks;
}

bool SQLiteClient::SaveInventory(uint64_t playerId, const nlohmann::json& inventory) {
    std::string sql = sqlProvider_.GetQuery("save_inventory");
    if (sql.empty()) {
        sql = "INSERT OR REPLACE INTO player_inventory (player_id, data, last_updated) VALUES (?, ?, datetime('now'));";
    }
    return ExecuteWithParams(sql, { std::to_string(playerId), inventory.dump() });
}

nlohmann::json SQLiteClient::LoadInventory(uint64_t playerId) {
    std::string sql = sqlProvider_.GetQuery("load_inventory");
    if (sql.empty()) {
        sql = "SELECT data FROM player_inventory WHERE player_id = ?;";
    }
    auto result = QueryWithParams(sql, { std::to_string(playerId) });
    if (!result.empty() && result[0].contains("data")) {
        return result[0]["data"];
    }
    return nlohmann::json();
}

bool SQLiteClient::SaveQuestProgress(uint64_t playerId, const std::string& questId, const nlohmann::json& progress) {
    std::string sql = sqlProvider_.GetQuery("save_quest_progress");
    if (sql.empty()) {
        sql = "INSERT OR REPLACE INTO player_quests (player_id, quest_id, progress, last_updated) VALUES (?, ?, ?, datetime('now'));";
    }
    return ExecuteWithParams(sql, { std::to_string(playerId), questId, progress.dump() });
}

nlohmann::json SQLiteClient::LoadQuestProgress(uint64_t playerId, const std::string& questId) {
    std::string sql = sqlProvider_.GetQuery("load_quest_progress");
    if (sql.empty()) {
        sql = "SELECT progress FROM player_quests WHERE player_id = ? AND quest_id = ?;";
    }
    auto result = QueryWithParams(sql, { std::to_string(playerId), questId });
    if (!result.empty() && result[0].contains("progress")) {
        return result[0]["progress"];
    }
    return nlohmann::json();
}

std::vector<std::string> SQLiteClient::ListActiveQuests(uint64_t playerId) {
    std::string sql = sqlProvider_.GetQuery("list_active_quests");
    if (sql.empty()) {
        sql = "SELECT quest_id FROM player_quests WHERE player_id = ? ORDER BY quest_id;";
    }
    auto result = QueryWithParams(sql, { std::to_string(playerId) });
    std::vector<std::string> quests;
    for (const auto& row : result) {
        if (row.contains("quest_id")) quests.push_back(row["quest_id"].get<std::string>());
    }
    return quests;
}

#endif // USE_SQLITE
