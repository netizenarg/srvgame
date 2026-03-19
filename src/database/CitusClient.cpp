#ifdef USE_CITUS

#include "database/CitusClient.hpp"

// =============== Static Members ===============
std::mutex CitusClient::instanceMutex_;
CitusClient* CitusClient::instance_ = nullptr;

// =============== Singleton Access ===============
CitusClient& CitusClient::GetInstance() {
    std::lock_guard<std::mutex> lock(instanceMutex_);
    if (!instance_) {
        auto& configManager = ConfigManager::GetInstance();
        nlohmann::json config = {
            {"type", "citus"},
            {"host", configManager.GetString("database.host", "localhost")},
            {"port", configManager.GetInt("database.port", 5432)},
            {"name", configManager.GetString("database.name", "game_db")},
            {"user", configManager.GetString("database.user", "postgres")},
            {"password", configManager.GetString("database.password", "")},
            {"shard_count", configManager.GetInt("database.citus.shard_count", 32)},
            {"replication_factor", configManager.GetInt("database.citus.replication_factor", 2)}
        };

        // Create a SQLProvider for the singleton
        static SQLProvider staticSqlProvider;
        static bool loaded = false;
        if (!loaded) {
            // Attempt to load the Citus SQL files (adjust paths as needed)
            staticSqlProvider.LoadFromFile("dbschema/postgres.sql");
            staticSqlProvider.LoadFromFile("dbschema/citus.sql");
            loaded = true;
        }

        instance_ = new CitusClient(config, staticSqlProvider);

        if (!instance_->Connect()) {
            Logger::Error("Failed to connect to Citus database");
            delete instance_;
            instance_ = nullptr;
            throw std::runtime_error("Failed to connect to Citus database");
        }
    }
    return *instance_;
}

// =============== Constructor and Destructor ===============
CitusClient::CitusClient(const nlohmann::json& config, const SQLProvider& sqlProvider)
    : PostgreSqlClient(config, sqlProvider),
      sqlProvider_(sqlProvider),
      citusEnabled_(false),
      shardCount_(config.value("shard_count", 32)),
      replicationFactor_(config.value("replication_factor", 2)),
      coordinatorNode_("coordinator"),
      maxShardConnectionsPerNode_(5) {

    citusStats_.startTime = std::chrono::steady_clock::now();
    Logger::Debug("CitusClient created with {} shards, replication factor {}",
                 shardCount_, replicationFactor_);
}

CitusClient::~CitusClient() {
    CloseAllShardConnections();
    Logger::Debug("CitusClient destroyed");
}

// =============== Connection Management ===============
bool CitusClient::Connect() {
    if (!PostgreSqlClient::Connect()) {
        Logger::Error("Failed to connect to Citus coordinator");
        return false;
    }

    if (!CheckCitusExtension()) {
        Logger::Warn("Citus extension not found, attempting to enable it");
        if (!EnableCitusExtension()) {
            Logger::Error("Failed to enable Citus extension");
            return false;
        }
    }

    if (!RefreshWorkerNodes()) Logger::Warn("Failed to refresh worker nodes");
    if (!RefreshShardPlacements()) Logger::Warn("Failed to refresh shard placements");

    citusEnabled_ = true;
    Logger::Info("CitusClient connected successfully with {} worker nodes", workerNodes_.size());

    std::thread([this]() {
        while (poolInitialized_ && !poolShuttingDown_) {
            std::this_thread::sleep_for(std::chrono::minutes(1));
            if (poolInitialized_ && !poolShuttingDown_) MaintainShardConnections();
        }
    }).detach();

    return true;
}

bool CitusClient::CheckCitusExtension() {
    std::string sql = sqlProvider_.GetQuery("check_citus_extension");
    if (sql.empty()) { // Fallback
        sql = "SELECT EXISTS(SELECT 1 FROM pg_extension WHERE extname = 'citus')";
    }
    auto result = Query(sql);
    if (!result.empty() && result[0].contains("exists")) {
        return result[0]["exists"].get<bool>();
    }
    return false;
}

bool CitusClient::EnableCitusExtension() {
    std::string sql = sqlProvider_.GetQuery("enable_citus_extension");
    if (sql.empty()) sql = "CREATE EXTENSION IF NOT EXISTS citus";
    return Execute(sql);
}

// =============== Worker Node Management ===============
bool CitusClient::RefreshWorkerNodes() {
    try {
        std::lock_guard<std::mutex> lock(workerNodesMutex_);
        std::string sql = sqlProvider_.GetQuery("get_worker_nodes");
        if (sql.empty()) {
            sql = "SELECT nodeid, nodename, nodeport, noderole, isactive FROM pg_dist_node ORDER BY nodeid";
        }
        auto result = Query(sql);

        workerNodes_.clear();
        for (const auto& row : result) {
            if (row.contains("nodename") && row.contains("nodeport")) {
                std::string host = row["nodename"].get<std::string>();
                int port = row.value("nodeport", 5432);
                std::string name = host + ":" + std::to_string(port);
                WorkerNode node;
                node.host = host;
                node.port = port;
                node.name = name;
                node.enabled = row.value("isactive", true);
                node.shardCount = 0;
                workerNodes_[name] = node;
            }
        }
        Logger::Debug("Refreshed {} worker nodes", workerNodes_.size());
        return true;
    } catch (const std::exception& e) {
        Logger::Error("Failed to refresh worker nodes: {}", e.what());
        return false;
    }
}

bool CitusClient::AddWorkerNode(const std::string& host, int port) {
    std::string sql = sqlProvider_.GetQuery("add_worker_node");
    if (sql.empty()) {
        sql = "SELECT citus_add_node($1, $2)";
    }
    bool success = ExecuteWithParams(sql, { host, std::to_string(port) });
    if (success) {
        RefreshWorkerNodes();
        Logger::Info("Added worker node {}:{}", host, port);
    }
    return success;
}

bool CitusClient::RemoveWorkerNode(const std::string& host, int port) {
    // First get node ID
    std::string getNodeIdSql = "SELECT nodeid FROM pg_dist_node WHERE nodename = $1 AND nodeport = $2";
    auto result = QueryWithParams(getNodeIdSql, { host, std::to_string(port) });
    if (result.empty() || !result[0].contains("nodeid")) {
        Logger::Error("Worker node {}:{} not found", host, port);
        return false;
    }
    int nodeId = result[0]["nodeid"].get<int>();

    std::string sql = sqlProvider_.GetQuery("remove_worker_node");
    if (sql.empty()) {
        sql = "SELECT citus_remove_node($1)";
    }
    bool success = ExecuteWithParams(sql, { std::to_string(nodeId) });
    if (success) {
        RefreshWorkerNodes();
        RefreshShardPlacements();
        Logger::Info("Removed worker node {}:{} (ID: {})", host, port, nodeId);
    }
    return success;
}

bool CitusClient::DisableWorkerNode(const std::string& host, int port) {
    std::string sql = "UPDATE pg_dist_node SET isactive = false WHERE nodename = $1 AND nodeport = $2";
    bool success = ExecuteWithParams(sql, { host, std::to_string(port) });
    if (success) {
        RefreshWorkerNodes();
        Logger::Info("Disabled worker node {}:{}", host, port);
    }
    return success;
}

bool CitusClient::EnableWorkerNode(const std::string& host, int port) {
    std::string sql = "UPDATE pg_dist_node SET isactive = true WHERE nodename = $1 AND nodeport = $2";
    bool success = ExecuteWithParams(sql, { host, std::to_string(port) });
    if (success) {
        RefreshWorkerNodes();
        Logger::Info("Enabled worker node {}:{}", host, port);
    }
    return success;
}

nlohmann::json CitusClient::GetWorkerNodes() {
    std::lock_guard<std::mutex> lock(workerNodesMutex_);
    nlohmann::json nodes = nlohmann::json::array();
    for (const auto& [name, node] : workerNodes_) {
        nlohmann::json nodeJson;
        nodeJson["name"] = node.name;
        nodeJson["host"] = node.host;
        nodeJson["port"] = node.port;
        nodeJson["enabled"] = node.enabled;
        nodeJson["shard_count"] = node.shardCount;
        nodes.push_back(nodeJson);
    }
    return nodes;
}

nlohmann::json CitusClient::GetWorkerNodeStats() {
    std::string sql = sqlProvider_.GetQuery("get_worker_node_stats");
    if (sql.empty()) {
        sql = "SELECT nodename, nodeport, COUNT(DISTINCT shardid) as shard_count, SUM(shardsize) as total_size_bytes "
              "FROM pg_dist_placement p JOIN pg_dist_node n ON p.groupid = n.groupid "
              "GROUP BY nodename, nodeport ORDER BY nodename, nodeport";
    }
    return Query(sql);
}

// =============== Shard Management ===============
bool CitusClient::RefreshShardPlacements() {
    try {
        std::lock_guard<std::mutex> lock(shardPlacementsMutex_);
        std::string sql = sqlProvider_.GetQuery("get_shard_placements");
        if (sql.empty()) {
            sql = "SELECT shardid, nodename, nodeport, placementid "
                  "FROM pg_dist_placement p JOIN pg_dist_node n ON p.groupid = n.groupid "
                  "ORDER BY shardid, placementid";
        }
        auto result = Query(sql);

        shardPlacements_.clear();
        {
            std::lock_guard<std::mutex> workerLock(workerNodesMutex_);
            for (auto& [name, node] : workerNodes_) node.shardCount = 0;
        }

        for (const auto& row : result) {
            if (row.contains("shardid") && row.contains("nodename") && row.contains("nodeport")) {
                int shardId = row["shardid"].get<int>();
                std::string host = row["nodename"].get<std::string>();
                int port = row["nodeport"].get<int>();
                std::string nodeName = host + ":" + std::to_string(port);
                int placementId = row.value("placementid", 0);

                ShardPlacement placement;
                placement.shardId = shardId;
                placement.nodeName = nodeName;
                placement.host = host;
                placement.port = port;
                placement.placementId = placementId;
                shardPlacements_[shardId].push_back(placement);

                std::lock_guard<std::mutex> workerLock(workerNodesMutex_);
                auto it = workerNodes_.find(nodeName);
                if (it != workerNodes_.end()) it->second.shardCount++;
            }
        }
        Logger::Debug("Refreshed shard placements for {} shards", shardPlacements_.size());
        return true;
    } catch (const std::exception& e) {
        Logger::Error("Failed to refresh shard placements: {}", e.what());
        return false;
    }
}

int CitusClient::GetShardId(uint64_t entityId) const {
    uint32_t hash = 0;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(&entityId);
    for (size_t i = 0; i < sizeof(entityId); ++i) {
        hash = (hash ^ data[i]) * 16777619;
    }
    return static_cast<int>(hash % shardCount_) + 1;
}

int CitusClient::GetTotalShards() const {
    return shardCount_;
}

bool CitusClient::CreateDistributedTable(const std::string& tableName, const std::string& distributionColumn) {
    std::string sql = sqlProvider_.GetQuery("create_distributed_table");
    if (sql.empty()) {
        sql = "SELECT create_distributed_table($1, $2)";
    }
    return ExecuteWithParams(sql, { tableName, distributionColumn });
}

bool CitusClient::CreateReferenceTable(const std::string& tableName) {
    std::string sql = sqlProvider_.GetQuery("create_reference_table");
    if (sql.empty()) {
        sql = "SELECT create_reference_table($1)";
    }
    return ExecuteWithParams(sql, { tableName });
}

bool CitusClient::CreateDistributedFunction(const std::string& functionName,
                                          const std::string& functionDefinition) {
    // functionDefinition contains the full CREATE OR REPLACE FUNCTION body
    std::string createSql = "CREATE OR REPLACE FUNCTION " + functionName + " " + functionDefinition;
    if (!Execute(createSql)) return false;
    std::string distSql = sqlProvider_.GetQuery("create_distributed_function");
    if (distSql.empty()) {
        distSql = "SELECT create_distributed_function($1)";
    }
    return ExecuteWithParams(distSql, { functionName });
}

bool CitusClient::RebalanceShards() {
    std::string sql = sqlProvider_.GetQuery("rebalance_shards");
    if (sql.empty()) sql = "SELECT rebalance_table_shards()";
    bool success = Execute(sql);
    if (success) {
        RefreshWorkerNodes();
        RefreshShardPlacements();
        Logger::Info("Shard rebalancing completed");
    }
    return success;
}

bool CitusClient::MoveShard(int shardId, const std::string& sourceNode,
                           const std::string& targetNode) {
    // Parse sourceNode and targetNode (format "host:port")
    size_t sourceColon = sourceNode.find(':');
    size_t targetColon = targetNode.find(':');
    if (sourceColon == std::string::npos || targetColon == std::string::npos) {
        Logger::Error("Invalid node format. Expected 'host:port'");
        return false;
    }
    std::string sourceHost = sourceNode.substr(0, sourceColon);
    int sourcePort = std::stoi(sourceNode.substr(sourceColon + 1));
    std::string targetHost = targetNode.substr(0, targetColon);
    int targetPort = std::stoi(targetNode.substr(targetColon + 1));

    // Get node IDs
    std::string getNodeIdSql = "SELECT nodeid FROM pg_dist_node WHERE nodename = $1 AND nodeport = $2";
    auto result = QueryWithParams(getNodeIdSql, { sourceHost, std::to_string(sourcePort) });
    if (result.empty() || !result[0].contains("nodeid")) {
        Logger::Error("Source node {} not found", sourceNode);
        return false;
    }
    int sourceNodeId = result[0]["nodeid"].get<int>();

    result = QueryWithParams(getNodeIdSql, { targetHost, std::to_string(targetPort) });
    if (result.empty() || !result[0].contains("nodeid")) {
        Logger::Error("Target node {} not found", targetNode);
        return false;
    }
    int targetNodeId = result[0]["nodeid"].get<int>();

    std::string sql = sqlProvider_.GetQuery("move_shard");
    if (sql.empty()) {
        sql = "SELECT citus_move_shard_placement($1, $2, $3)";
    }
    bool success = ExecuteWithParams(sql, { std::to_string(shardId), std::to_string(sourceNodeId), std::to_string(targetNodeId) });
    if (success) {
        RefreshShardPlacements();
        Logger::Info("Moved shard {} from {} to {}", shardId, sourceNode, targetNode);
    }
    return success;
}

bool CitusClient::IsolateShard(int shardId) {
    std::string sql = sqlProvider_.GetQuery("isolate_shard");
    if (sql.empty()) {
        sql = "UPDATE pg_dist_placement SET shardstate = 3 WHERE shardid = $1";
    }
    bool success = ExecuteWithParams(sql, { std::to_string(shardId) });
    if (success) {
        RefreshShardPlacements();
        Logger::Warn("Isolated shard {}", shardId);
    }
    return success;
}

nlohmann::json CitusClient::GetShardPlacements() {
    std::lock_guard<std::mutex> lock(shardPlacementsMutex_);
    nlohmann::json placements = nlohmann::json::array();
    for (const auto& [shardId, placementList] : shardPlacements_) {
        for (const auto& placement : placementList) {
            nlohmann::json placementJson;
            placementJson["shard_id"] = placement.shardId;
            placementJson["node_name"] = placement.nodeName;
            placementJson["host"] = placement.host;
            placementJson["port"] = placement.port;
            placementJson["placement_id"] = placement.placementId;
            placements.push_back(placementJson);
        }
    }
    return placements;
}

nlohmann::json CitusClient::GetShardStatistics() {
    std::string sql = sqlProvider_.GetQuery("get_shard_statistics");
    if (sql.empty()) {
        sql = "SELECT shardid, COUNT(*) as replica_count, "
              "SUM(CASE WHEN shardstate = 1 THEN 1 ELSE 0 END) as active_replicas, "
              "SUM(CASE WHEN shardstate = 3 THEN 1 ELSE 0 END) as isolated_replicas "
              "FROM pg_dist_placement GROUP BY shardid ORDER BY shardid";
    }
    return Query(sql);
}

// =============== Shard Connection Management ===============
PGconn* CitusClient::GetOrCreateShardConnection(int shardId) {
    std::lock_guard<std::mutex> lock(shardConnectionsMutex_);
    auto placementsIt = shardPlacements_.find(shardId);
    if (placementsIt == shardPlacements_.end() || placementsIt->second.empty()) {
        Logger::Error("No placements found for shard {}", shardId);
        return nullptr;
    }
    const auto& placement = placementsIt->second[0];

    auto& connections = shardConnections_[shardId];
    for (auto& conn : connections) {
        if (!conn.inUse) {
            if (TestConnection(conn.conn)) {
                conn.inUse = true;
                conn.lastUsed = std::chrono::steady_clock::now();
                return conn.conn;
            } else {
                CloseConnection(conn.conn);
                conn.conn = nullptr;
            }
        }
    }

    connections.erase(std::remove_if(connections.begin(), connections.end(),
                      [](const ShardConnection& c) { return c.conn == nullptr; }),
                      connections.end());

    if (connections.size() >= maxShardConnectionsPerNode_) {
        Logger::Error("Maximum connections reached for shard {}", shardId);
        return nullptr;
    }

    std::string connString =
        "host=" + placement.host + " " +
        "port=" + std::to_string(placement.port) + " " +
        "dbname=" + config_.value("name", "game_db") + " " +
        "user=" + config_.value("user", "postgres") + " " +
        "password=" + config_.value("password", "") + " " +
        "connect_timeout=5";

    PGconn* newConn = PQconnectdb(connString.c_str());
    if (!newConn || PQstatus(newConn) != CONNECTION_OK) {
        if (newConn) PQfinish(newConn);
        Logger::Error("Failed to create connection to shard {} on {}:{}",
                     shardId, placement.host, placement.port);
        citusStats_.shardConnectionErrors++;
        return nullptr;
    }

    ShardConnection shardConn;
    shardConn.conn = newConn;
    shardConn.inUse = true;
    shardConn.lastUsed = std::chrono::steady_clock::now();
    shardConn.connectionString = connString;
    connections.push_back(shardConn);
    return newConn;
}

void CitusClient::ReleaseShardConnection(PGconn* conn) {
    if (!conn) return;
    std::lock_guard<std::mutex> lock(shardConnectionsMutex_);
    for (auto& [shardId, connections] : shardConnections_) {
        for (auto& shardConn : connections) {
            if (shardConn.conn == conn) {
                shardConn.inUse = false;
                shardConn.lastUsed = std::chrono::steady_clock::now();
                return;
            }
        }
    }
    CloseConnection(conn);
}

PGconn* CitusClient::GetShardConnection(int shardId) {
    return GetOrCreateShardConnection(shardId);
}

void CitusClient::MaintainShardConnections() {
    std::lock_guard<std::mutex> lock(shardConnectionsMutex_);
    auto now = std::chrono::steady_clock::now();
    for (auto& [shardId, connections] : shardConnections_) {
        auto it = connections.begin();
        while (it != connections.end()) {
            if (!it->inUse) {
                auto idleTime = std::chrono::duration_cast<std::chrono::seconds>(now - it->lastUsed);
                if (idleTime > std::chrono::minutes(10)) {
                    CloseConnection(it->conn);
                    it = connections.erase(it);
                    continue;
                }
                if (!TestConnection(it->conn)) {
                    CloseConnection(it->conn);
                    it->conn = PQconnectdb(it->connectionString.c_str());
                    if (!it->conn || !TestConnection(it->conn)) {
                        if (it->conn) CloseConnection(it->conn);
                        it = connections.erase(it);
                        continue;
                    }
                }
            }
            ++it;
        }
    }
}

void CitusClient::CloseAllShardConnections() {
    std::lock_guard<std::mutex> lock(shardConnectionsMutex_);
    for (auto& [shardId, connections] : shardConnections_) {
        for (auto& conn : connections) {
            if (conn.conn) CloseConnection(conn.conn);
        }
    }
    shardConnections_.clear();
}

std::string CitusClient::GetShardConnectionInfo(int shardId) const {
    std::lock_guard<std::mutex> lock(shardPlacementsMutex_);
    auto it = shardPlacements_.find(shardId);
    if (it == shardPlacements_.end() || it->second.empty()) {
        return "Shard " + std::to_string(shardId) + ": No placements";
    }
    std::ostringstream oss;
    oss << "Shard " << shardId << ": ";
    for (size_t i = 0; i < it->second.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << it->second[i].host << ":" << it->second[i].port;
    }
    return oss.str();
}

// =============== Shard Query Operations ===============
nlohmann::json CitusClient::QueryShard(int shardId, const std::string& sql) {
    citusStats_.shardQueries++;
    PGconn* conn = GetShardConnection(shardId);
    if (!conn) {
        citusStats_.shardQueryFailures++;
        return nlohmann::json::array();
    }
    auto result = ExecuteQuery(conn, sql);
    ReleaseShardConnection(conn);
    return result;
}

nlohmann::json CitusClient::QueryShardWithParams(int shardId, const std::string& sql,
                                                const std::vector<std::string>& params) {
    citusStats_.shardQueries++;
    PGconn* conn = GetShardConnection(shardId);
    if (!conn) {
        citusStats_.shardQueryFailures++;
        return nlohmann::json::array();
    }
    std::vector<const char*> c_params;
    c_params.reserve(params.size());
    for (const auto& param : params) c_params.push_back(param.c_str());
    auto result = ExecuteQuery(conn, sql, c_params);
    ReleaseShardConnection(conn);
    return result;
}

bool CitusClient::ExecuteShard(int shardId, const std::string& sql) {
    citusStats_.shardQueries++;
    PGconn* conn = GetShardConnection(shardId);
    if (!conn) {
        citusStats_.shardQueryFailures++;
        return false;
    }
    bool success = ExecuteCommand(conn, sql);
    ReleaseShardConnection(conn);
    return success;
}

bool CitusClient::ExecuteShardWithParams(int shardId, const std::string& sql,
                                        const std::vector<std::string>& params) {
    citusStats_.shardQueries++;
    PGconn* conn = GetShardConnection(shardId);
    if (!conn) {
        citusStats_.shardQueryFailures++;
        return false;
    }
    std::vector<const char*> c_params;
    c_params.reserve(params.size());
    for (const auto& param : params) c_params.push_back(param.c_str());
    bool success = ExecuteCommand(conn, sql, c_params);
    ReleaseShardConnection(conn);
    return success;
}

nlohmann::json CitusClient::QueryOnShard(int shardId, const std::string& sql,
                                        const std::vector<const char*>& params) {
    PGconn* conn = GetShardConnection(shardId);
    if (!conn) return nlohmann::json::array();
    auto result = ExecuteQuery(conn, sql, params);
    ReleaseShardConnection(conn);
    return result;
}

bool CitusClient::ExecuteOnShard(int shardId, const std::string& sql,
                                const std::vector<const char*>& params) {
    PGconn* conn = GetShardConnection(shardId);
    if (!conn) return false;
    bool success = ExecuteCommand(conn, sql, params);
    ReleaseShardConnection(conn);
    return success;
}

// =============== Distributed Transactions ===============
bool CitusClient::BeginDistributedTransaction() {
    std::string sql = sqlProvider_.GetQuery("begin_transaction");
    if (sql.empty()) sql = "BEGIN";
    return Execute(sql);
}

bool CitusClient::CommitDistributedTransaction() {
    std::string sql = sqlProvider_.GetQuery("commit_transaction");
    if (sql.empty()) sql = "COMMIT";
    return Execute(sql);
}

bool CitusClient::RollbackDistributedTransaction() {
    std::string sql = sqlProvider_.GetQuery("rollback_transaction");
    if (sql.empty()) sql = "ROLLBACK";
    return Execute(sql);
}

bool CitusClient::PrepareDistributedTransaction(const std::string& transactionId) {
    std::string sql = "PREPARE TRANSACTION '" + EscapeString(transactionId) + "'";
    return Execute(sql);
}

bool CitusClient::CommitPreparedDistributedTransaction(const std::string& transactionId) {
    std::string sql = "COMMIT PREPARED '" + EscapeString(transactionId) + "'";
    return Execute(sql);
}

bool CitusClient::RollbackPreparedDistributedTransaction(const std::string& transactionId) {
    std::string sql = "ROLLBACK PREPARED '" + EscapeString(transactionId) + "'";
    return Execute(sql);
}

// =============== Statistics and Monitoring ===============
nlohmann::json CitusClient::GetCitusStats() {
    nlohmann::json stats = GetDatabaseStats();
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - citusStats_.startTime).count();
    stats["citus_uptime_seconds"] = uptime;
    stats["distributed_queries"] = citusStats_.distributedQueries.load();
    stats["shard_queries"] = citusStats_.shardQueries.load();
    stats["shard_query_failures"] = citusStats_.shardQueryFailures.load();
    stats["shard_connection_errors"] = citusStats_.shardConnectionErrors.load();
    stats["worker_nodes"] = workerNodes_.size();
    stats["total_shards"] = shardCount_;
    stats["replication_factor"] = replicationFactor_;
    stats["shard_connections"] = shardConnections_.size();
    if (citusStats_.shardQueries > 0) {
        double successRate = 100.0 * (1.0 - (double)citusStats_.shardQueryFailures / citusStats_.shardQueries);
        stats["shard_query_success_rate_percent"] = successRate;
    }
    return stats;
}

nlohmann::json CitusClient::GetQueryStats() {
    std::string sql = sqlProvider_.GetQuery("get_query_stats");
    if (sql.empty()) {
        sql = "SELECT query, calls, total_time, mean_time, rows FROM pg_stat_statements ORDER BY total_time DESC LIMIT 20";
    }
    return Query(sql);
}

nlohmann::json CitusClient::GetClusterStats() {
    std::string sql = sqlProvider_.GetQuery("get_cluster_stats");
    if (sql.empty()) {
        sql = "SELECT "
              "(SELECT COUNT(*) FROM pg_dist_node WHERE noderole = 'primary') as primary_nodes, "
              "(SELECT COUNT(*) FROM pg_dist_node WHERE noderole = 'secondary') as secondary_nodes, "
              "(SELECT COUNT(DISTINCT shardid) FROM pg_dist_placement) as total_shards, "
              "(SELECT COUNT(*) FROM pg_dist_placement WHERE shardstate = 1) as active_placements, "
              "(SELECT COUNT(*) FROM pg_dist_placement WHERE shardstate = 3) as isolated_placements, "
              "(SELECT SUM(shardsize) FROM pg_dist_placement) as total_data_size_bytes";
    }
    return Query(sql);
}

nlohmann::json CitusClient::GetShardQueryStats(int shardId) {
    std::string sql = sqlProvider_.GetQuery("get_shard_query_stats");
    if (sql.empty()) {
        sql = "SELECT shardid, query, calls, total_time FROM citus_stat_statements ";
        if (shardId >= 0) sql += "WHERE shardid = " + std::to_string(shardId) + " ";
        sql += "ORDER BY total_time DESC LIMIT 50";
    } else {
        // Use parameterised if the SQL contains placeholders
        if (shardId >= 0) {
            return QueryWithParams(sql, { std::to_string(shardId) });
        } else {
            return Query(sql);
        }
    }
    return Query(sql);
}

// =============== Performance Tuning ===============
bool CitusClient::SetShardCount(int shardCount) {
    if (shardCount <= 0) {
        Logger::Error("Invalid shard count: {}", shardCount);
        return false;
    }
    shardCount_ = shardCount;
    Logger::Info("Shard count set to {}", shardCount_);
    return true;
}

bool CitusClient::SetReplicationFactor(int replicationFactor) {
    if (replicationFactor < 1) {
        Logger::Error("Invalid replication factor: {}", replicationFactor);
        return false;
    }
    replicationFactor_ = replicationFactor;
    Logger::Info("Replication factor set to {}", replicationFactor_);
    return true;
}

bool CitusClient::EnableQueryMetrics(bool enabled) {
    std::string sql;
    if (enabled) {
        sql = sqlProvider_.GetQuery("enable_pg_stat_statements");
        if (sql.empty()) sql = "CREATE EXTENSION IF NOT EXISTS pg_stat_statements";
    } else {
        sql = sqlProvider_.GetQuery("disable_pg_stat_statements");
        if (sql.empty()) sql = "DROP EXTENSION IF EXISTS pg_stat_statements";
    }
    return Execute(sql);
}

// =============== Maintenance Operations ===============
bool CitusClient::VacuumDistributedTables() {
    Logger::Info("Starting vacuum of distributed tables...");
    std::string listSql = "SELECT logicalrelid::regclass as table_name FROM pg_dist_partition ORDER BY logicalrelid";
    auto tables = Query(listSql);
    bool allSuccess = true;
    for (const auto& table : tables) {
        if (table.contains("table_name")) {
            std::string tableName = table["table_name"].get<std::string>();
            Logger::Debug("Vacuuming table: {}", tableName);
            std::string vacuumSql = "VACUUM ANALYZE " + tableName;
            if (!Execute(vacuumSql)) {
                Logger::Warn("Failed to vacuum table: {}", tableName);
                allSuccess = false;
            }
        }
    }
    if (allSuccess) Logger::Info("Vacuum of distributed tables completed");
    else Logger::Warn("Vacuum completed with some failures");
    return allSuccess;
}

bool CitusClient::AnalyzeDistributedTables() {
    Logger::Info("Starting analyze of distributed tables...");
    std::string sql = "ANALYZE";
    bool success = Execute(sql);
    if (success) Logger::Info("Analyze of distributed tables completed");
    return success;
}

bool CitusClient::ReplicateReferenceTables() {
    std::string sql = sqlProvider_.GetQuery("replicate_reference_tables");
    if (sql.empty()) sql = "SELECT citus_replicate_reference_tables()";
    bool success = Execute(sql);
    if (success) Logger::Info("Reference table replication completed");
    return success;
}

bool CitusClient::ConnectToDatabase(const std::string& dbname) {
    if (!PostgreSqlClient::ConnectToDatabase(dbname)) return false;
    RefreshWorkerNodes();
    RefreshShardPlacements();
    return true;
}

#endif // USE_CITUS
