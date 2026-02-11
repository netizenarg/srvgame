#ifdef USE_CITUS

#include "database/CitusClient.hpp"

// =============== Static Members ===============
std::mutex CitusClient::instanceMutex_;
CitusClient* CitusClient::instance_ = nullptr;

// =============== Singleton Access ===============
CitusClient& CitusClient::GetInstance() {
    std::lock_guard<std::mutex> lock(instanceMutex_);
    if (!instance_) {
        // Load configuration
        auto& configManager = ConfigManager::GetInstance();
        nlohmann::json config = {
            {"type", "citus"},
            {"host", configManager.GetString("database.host", "localhost")},
            {"port", configManager.GetInt("database.port", 5432)},
            {"database", configManager.GetString("database.name", "game_db")},
            {"username", configManager.GetString("database.username", "postgres")},
            {"password", configManager.GetString("database.password", "")},
            {"shard_count", configManager.GetInt("database.citus.shard_count", 32)},
            {"replication_factor", configManager.GetInt("database.citus.replication_factor", 2)}
        };
        
        instance_ = new CitusClient(config);
        
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
CitusClient::CitusClient(const nlohmann::json& config)
    : PostgreSqlClient(config),
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
    // First connect to coordinator using parent class
    if (!PostgreSqlClient::Connect()) {
        Logger::Error("Failed to connect to Citus coordinator");
        return false;
    }
    
    // Check if Citus extension is available
    if (!CheckCitusExtension()) {
        Logger::Warn("Citus extension not found, attempting to enable it");
        if (!EnableCitusExtension()) {
            Logger::Error("Failed to enable Citus extension");
            return false;
        }
    }
    
    // Refresh worker node information
    if (!RefreshWorkerNodes()) {
        Logger::Warn("Failed to refresh worker nodes");
    }
    
    // Refresh shard placements
    if (!RefreshShardPlacements()) {
        Logger::Warn("Failed to refresh shard placements");
    }
    
    citusEnabled_ = true;
    Logger::Info("CitusClient connected successfully with {} worker nodes", 
                workerNodes_.size());
    
    // Start shard connection maintenance thread
    std::thread([this]() {
        while (poolInitialized_ && !poolShuttingDown_) {
            std::this_thread::sleep_for(std::chrono::minutes(1));
            if (poolInitialized_ && !poolShuttingDown_) {
                MaintainShardConnections();
            }
        }
    }).detach();
    
    return true;
}

bool CitusClient::CheckCitusExtension() {
    try {
        std::string sql = 
            "SELECT EXISTS(SELECT 1 FROM pg_extension WHERE extname = 'citus')";
        
        auto result = Query(sql);
        
        if (!result.empty() && result[0].contains("exists")) {
            return result[0]["exists"].get<bool>();
        }
        
        return false;
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to check Citus extension: {}", e.what());
        return false;
    }
}

bool CitusClient::EnableCitusExtension() {
    try {
        Logger::Info("Enabling Citus extension...");
        
        // Create Citus extension
        if (!Execute("CREATE EXTENSION IF NOT EXISTS citus")) {
            Logger::Error("Failed to create Citus extension");
            return false;
        }
        
        Logger::Info("Citus extension enabled successfully");
        return true;
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to enable Citus extension: {}", e.what());
        return false;
    }
}

// =============== Worker Node Management ===============
bool CitusClient::RefreshWorkerNodes() {
    try {
        std::lock_guard<std::mutex> lock(workerNodesMutex_);
        
        std::string sql = 
            "SELECT nodeid, nodename, nodeport, noderole, isactive "
            "FROM pg_dist_node ORDER BY nodeid";
        
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
                node.shardCount = 0; // Will be populated separately
                
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
    try {
        std::string sql = 
            "SELECT citus_add_node('" + EscapeString(host) + "', " + 
            std::to_string(port) + ")";
        
        bool success = Execute(sql);
        if (success) {
            // Refresh worker nodes cache
            RefreshWorkerNodes();
            Logger::Info("Added worker node {}:{}", host, port);
        }
        
        return success;
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to add worker node {}:{}: {}", host, port, e.what());
        return false;
    }
}

bool CitusClient::RemoveWorkerNode(const std::string& host, int port) {
    try {
        // First get node ID
        std::string nodeName = host + ":" + std::to_string(port);
        
        std::string sql = 
            "SELECT nodeid FROM pg_dist_node "
            "WHERE nodename = '" + EscapeString(host) + "' "
            "AND nodeport = " + std::to_string(port);
        
        auto result = Query(sql);
        
        if (result.empty() || !result[0].contains("nodeid")) {
            Logger::Error("Worker node {}:{} not found", host, port);
            return false;
        }
        
        int nodeId = result[0]["nodeid"].get<int>();
        
        // Remove the node
        sql = "SELECT citus_remove_node(" + std::to_string(nodeId) + ")";
        bool success = Execute(sql);
        
        if (success) {
            // Refresh caches
            RefreshWorkerNodes();
            RefreshShardPlacements();
            Logger::Info("Removed worker node {}:{} (ID: {})", host, port, nodeId);
        }
        
        return success;
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to remove worker node {}:{}: {}", host, port, e.what());
        return false;
    }
}

bool CitusClient::DisableWorkerNode(const std::string& host, int port) {
    try {
        std::string sql = 
            "UPDATE pg_dist_node SET isactive = false "
            "WHERE nodename = '" + EscapeString(host) + "' "
            "AND nodeport = " + std::to_string(port);
        
        bool success = Execute(sql);
        if (success) {
            RefreshWorkerNodes();
            Logger::Info("Disabled worker node {}:{}", host, port);
        }
        
        return success;
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to disable worker node {}:{}: {}", host, port, e.what());
        return false;
    }
}

bool CitusClient::EnableWorkerNode(const std::string& host, int port) {
    try {
        std::string sql = 
            "UPDATE pg_dist_node SET isactive = true "
            "WHERE nodename = '" + EscapeString(host) + "' "
            "AND nodeport = " + std::to_string(port);
        
        bool success = Execute(sql);
        if (success) {
            RefreshWorkerNodes();
            Logger::Info("Enabled worker node {}:{}", host, port);
        }
        
        return success;
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to enable worker node {}:{}: {}", host, port, e.what());
        return false;
    }
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
    try {
        std::string sql = 
            "SELECT nodename, nodeport, "
            "COUNT(DISTINCT shardid) as shard_count, "
            "SUM(shardsize) as total_size_bytes "
            "FROM pg_dist_placement p "
            "JOIN pg_dist_node n ON p.groupid = n.groupid "
            "GROUP BY nodename, nodeport "
            "ORDER BY nodename, nodeport";
        
        return Query(sql);
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to get worker node stats: {}", e.what());
        return nlohmann::json::array();
    }
}

// =============== Shard Management ===============
bool CitusClient::RefreshShardPlacements() {
    try {
        std::lock_guard<std::mutex> lock(shardPlacementsMutex_);
        
        std::string sql = 
            "SELECT shardid, nodename, nodeport, placementid "
            "FROM pg_dist_placement p "
            "JOIN pg_dist_node n ON p.groupid = n.groupid "
            "ORDER BY shardid, placementid";
        
        auto result = Query(sql);
        
        shardPlacements_.clear();
        
        // Update worker node shard counts
        std::lock_guard<std::mutex> workerLock(workerNodesMutex_);
        for (auto& [name, node] : workerNodes_) {
            node.shardCount = 0;
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
                
                // Update worker node shard count
                auto it = workerNodes_.find(nodeName);
                if (it != workerNodes_.end()) {
                    it->second.shardCount++;
                }
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
    // Use consistent hash for shard distribution
    // This matches Citus's hash distribution
    uint32_t hash = 0;
    
    // Simple hash function (FNV-1a)
    const uint8_t* data = reinterpret_cast<const uint8_t*>(&entityId);
    for (size_t i = 0; i < sizeof(entityId); ++i) {
        hash = (hash ^ data[i]) * 16777619;
    }
    
    // Map to shard ID
    return static_cast<int>(hash % shardCount_) + 1; // Shard IDs start from 1 in Citus
}

int CitusClient::GetTotalShards() const {
    return shardCount_;
}

bool CitusClient::CreateDistributedTable(const std::string& tableName, 
                                        const std::string& distributionColumn) {
    try {
        std::string sql = 
            "SELECT create_distributed_table('" + EscapeString(tableName) + 
            "', '" + EscapeString(distributionColumn) + "')";
        
        bool success = Execute(sql);
        if (success) {
            Logger::Info("Created distributed table '{}' on column '{}'", 
                        tableName, distributionColumn);
        }
        
        return success;
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to create distributed table '{}': {}", tableName, e.what());
        return false;
    }
}

bool CitusClient::CreateReferenceTable(const std::string& tableName) {
    try {
        std::string sql = 
            "SELECT create_reference_table('" + EscapeString(tableName) + "')";
        
        bool success = Execute(sql);
        if (success) {
            Logger::Info("Created reference table '{}'", tableName);
        }
        
        return success;
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to create reference table '{}': {}", tableName, e.what());
        return false;
    }
}

bool CitusClient::CreateDistributedFunction(const std::string& functionName, 
                                          const std::string& functionDefinition) {
    try {
        std::string sql = 
            "CREATE OR REPLACE FUNCTION " + functionName + " " + functionDefinition;
        
        bool success = Execute(sql);
        if (success) {
            // Make it distributed
            sql = "SELECT create_distributed_function('" + EscapeString(functionName) + "')";
            success = Execute(sql);
            
            if (success) {
                Logger::Info("Created distributed function '{}'", functionName);
            }
        }
        
        return success;
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to create distributed function '{}': {}", functionName, e.what());
        return false;
    }
}

bool CitusClient::RebalanceShards() {
    try {
        Logger::Info("Starting shard rebalancing...");
        
        std::string sql = "SELECT rebalance_table_shards()";
        
        bool success = Execute(sql);
        if (success) {
            // Refresh caches after rebalancing
            RefreshWorkerNodes();
            RefreshShardPlacements();
            Logger::Info("Shard rebalancing completed");
        }
        
        return success;
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to rebalance shards: {}", e.what());
        return false;
    }
}

bool CitusClient::MoveShard(int shardId, const std::string& sourceNode, 
                           const std::string& targetNode) {
    try {
        // Parse node names
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
        std::string sql = 
            "SELECT nodeid FROM pg_dist_node "
            "WHERE nodename = '" + EscapeString(sourceHost) + "' "
            "AND nodeport = " + std::to_string(sourcePort);
        
        auto result = Query(sql);
        if (result.empty() || !result[0].contains("nodeid")) {
            Logger::Error("Source node {} not found", sourceNode);
            return false;
        }
        int sourceNodeId = result[0]["nodeid"].get<int>();
        
        sql = 
            "SELECT nodeid FROM pg_dist_node "
            "WHERE nodename = '" + EscapeString(targetHost) + "' "
            "AND nodeport = " + std::to_string(targetPort);
        
        result = Query(sql);
        if (result.empty() || !result[0].contains("nodeid")) {
            Logger::Error("Target node {} not found", targetNode);
            return false;
        }
        int targetNodeId = result[0]["nodeid"].get<int>();
        
        // Move the shard
        sql = "SELECT citus_move_shard_placement(" +
              std::to_string(shardId) + ", " +
              std::to_string(sourceNodeId) + ", " +
              std::to_string(targetNodeId) + ")";
        
        bool success = Execute(sql);
        if (success) {
            RefreshShardPlacements();
            Logger::Info("Moved shard {} from {} to {}", shardId, sourceNode, targetNode);
        }
        
        return success;
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to move shard {}: {}", shardId, e.what());
        return false;
    }
}

bool CitusClient::IsolateShard(int shardId) {
    try {
        std::string sql = 
            "UPDATE pg_dist_placement SET shardstate = 3 "  // 3 = ISOLATED
            "WHERE shardid = " + std::to_string(shardId);
        
        bool success = Execute(sql);
        if (success) {
            RefreshShardPlacements();
            Logger::Warn("Isolated shard {}", shardId);
        }
        
        return success;
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to isolate shard {}: {}", shardId, e.what());
        return false;
    }
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
    try {
        std::string sql = 
            "SELECT shardid, COUNT(*) as replica_count, "
            "SUM(CASE WHEN shardstate = 1 THEN 1 ELSE 0 END) as active_replicas, "
            "SUM(CASE WHEN shardstate = 3 THEN 1 ELSE 0 END) as isolated_replicas "
            "FROM pg_dist_placement "
            "GROUP BY shardid "
            "ORDER BY shardid";
        
        return Query(sql);
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to get shard statistics: {}", e.what());
        return nlohmann::json::array();
    }
}

// =============== Shard Connection Management ===============
PGconn* CitusClient::GetOrCreateShardConnection(int shardId) {
    std::lock_guard<std::mutex> lock(shardConnectionsMutex_);
    
    // Check if we have cached placements for this shard
    auto placementsIt = shardPlacements_.find(shardId);
    if (placementsIt == shardPlacements_.end() || placementsIt->second.empty()) {
        Logger::Error("No placements found for shard {}", shardId);
        return nullptr;
    }
    
    // For now, use the first placement (primary)
    // In production, you might want to implement load balancing
    const auto& placement = placementsIt->second[0];
    
    // Check for existing idle connection
    auto& connections = shardConnections_[shardId];
    for (auto& conn : connections) {
        if (!conn.inUse) {
            // Test connection
            if (TestConnection(conn.conn)) {
                conn.inUse = true;
                conn.lastUsed = std::chrono::steady_clock::now();
                return conn.conn;
            } else {
                // Close broken connection
                CloseConnection(conn.conn);
                conn.conn = nullptr;
            }
        }
    }
    
    // Remove null connections
    connections.erase(
        std::remove_if(connections.begin(), connections.end(),
                      [](const ShardConnection& c) { return c.conn == nullptr; }),
        connections.end()
    );
    
    // Check if we can create new connection
    if (connections.size() >= maxShardConnectionsPerNode_) {
        Logger::Error("Maximum connections reached for shard {}", shardId);
        return nullptr;
    }
    
    // Create new connection
    std::string connString = 
        "host=" + placement.host + " " +
        "port=" + std::to_string(placement.port) + " " +
        "dbname=" + config_.value("database", "game_db") + " " +
        "user=" + config_.value("username", "postgres") + " " +
        "password=" + config_.value("password", "") + " " +
        "connect_timeout=5";
    
    PGconn* newConn = PQconnectdb(connString.c_str());
    if (!newConn || PQstatus(newConn) != CONNECTION_OK) {
        if (newConn) {
            PQfinish(newConn);
        }
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
    if (!conn) {
        return;
    }
    
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
    
    // Connection not found in pool, close it
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
                // Close connections idle for more than 10 minutes
                auto idleTime = std::chrono::duration_cast<std::chrono::seconds>(now - it->lastUsed);
                if (idleTime > std::chrono::minutes(10)) {
                    CloseConnection(it->conn);
                    it = connections.erase(it);
                    continue;
                }
                
                // Test and fix broken connections
                if (!TestConnection(it->conn)) {
                    // Try to reconnect
                    CloseConnection(it->conn);
                    it->conn = PQconnectdb(it->connectionString.c_str());
                    
                    if (!it->conn || !TestConnection(it->conn)) {
                        // Remove if can't reconnect
                        if (it->conn) {
                            CloseConnection(it->conn);
                        }
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
            if (conn.conn) {
                CloseConnection(conn.conn);
            }
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
        if (i > 0) {
            oss << ", ";
        }
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
    
    // Convert params to C strings
    std::vector<const char*> c_params;
    c_params.reserve(params.size());
    for (const auto& param : params) {
        c_params.push_back(param.c_str());
    }
    
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
    
    // Convert params to C strings
    std::vector<const char*> c_params;
    c_params.reserve(params.size());
    for (const auto& param : params) {
        c_params.push_back(param.c_str());
    }
    
    bool success = ExecuteCommand(conn, sql, c_params);
    ReleaseShardConnection(conn);
    
    return success;
}

nlohmann::json CitusClient::QueryOnShard(int shardId, const std::string& sql,
                                        const std::vector<const char*>& params) {
    PGconn* conn = GetShardConnection(shardId);
    if (!conn) {
        return nlohmann::json::array();
    }
    
    auto result = ExecuteQuery(conn, sql, params);
    ReleaseShardConnection(conn);
    
    return result;
}

bool CitusClient::ExecuteOnShard(int shardId, const std::string& sql,
                                const std::vector<const char*>& params) {
    PGconn* conn = GetShardConnection(shardId);
    if (!conn) {
        return false;
    }
    
    bool success = ExecuteCommand(conn, sql, params);
    ReleaseShardConnection(conn);
    
    return success;
}

// =============== Distributed Transactions ===============
bool CitusClient::BeginDistributedTransaction() {
    try {
        std::string sql = "BEGIN";
        return Execute(sql);
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to begin distributed transaction: {}", e.what());
        return false;
    }
}

bool CitusClient::CommitDistributedTransaction() {
    try {
        std::string sql = "COMMIT";
        return Execute(sql);
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to commit distributed transaction: {}", e.what());
        return false;
    }
}

bool CitusClient::RollbackDistributedTransaction() {
    try {
        std::string sql = "ROLLBACK";
        return Execute(sql);
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to rollback distributed transaction: {}", e.what());
        return false;
    }
}

bool CitusClient::PrepareDistributedTransaction(const std::string& transactionId) {
    try {
        std::string sql = "PREPARE TRANSACTION '" + EscapeString(transactionId) + "'";
        return Execute(sql);
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to prepare distributed transaction '{}': {}", 
                     transactionId, e.what());
        return false;
    }
}

bool CitusClient::CommitPreparedDistributedTransaction(const std::string& transactionId) {
    try {
        std::string sql = "COMMIT PREPARED '" + EscapeString(transactionId) + "'";
        return Execute(sql);
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to commit prepared transaction '{}': {}", 
                     transactionId, e.what());
        return false;
    }
}

bool CitusClient::RollbackPreparedDistributedTransaction(const std::string& transactionId) {
    try {
        std::string sql = "ROLLBACK PREPARED '" + EscapeString(transactionId) + "'";
        return Execute(sql);
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to rollback prepared transaction '{}': {}", 
                     transactionId, e.what());
        return false;
    }
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
    
    // Calculate shard query success rate
    if (citusStats_.shardQueries > 0) {
        double successRate = 100.0 * (1.0 - (double)citusStats_.shardQueryFailures / citusStats_.shardQueries);
        stats["shard_query_success_rate_percent"] = successRate;
    }
    
    return stats;
}

nlohmann::json CitusClient::GetQueryStats() {
    try {
        std::string sql = 
            "SELECT query, calls, total_time, mean_time, rows "
            "FROM pg_stat_statements "
            "ORDER BY total_time DESC "
            "LIMIT 20";
        
        return Query(sql);
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to get query stats: {}", e.what());
        return nlohmann::json::array();
    }
}

nlohmann::json CitusClient::GetClusterStats() {
    try {
        std::string sql = 
            "SELECT "
            "(SELECT COUNT(*) FROM pg_dist_node WHERE noderole = 'primary') as primary_nodes, "
            "(SELECT COUNT(*) FROM pg_dist_node WHERE noderole = 'secondary') as secondary_nodes, "
            "(SELECT COUNT(DISTINCT shardid) FROM pg_dist_placement) as total_shards, "
            "(SELECT COUNT(*) FROM pg_dist_placement WHERE shardstate = 1) as active_placements, "
            "(SELECT COUNT(*) FROM pg_dist_placement WHERE shardstate = 3) as isolated_placements, "
            "(SELECT SUM(shardsize) FROM pg_dist_placement) as total_data_size_bytes";
        
        return Query(sql);
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to get cluster stats: {}", e.what());
        return nlohmann::json::array();
    }
}

nlohmann::json CitusClient::GetShardQueryStats(int shardId) {
    try {
        std::string sql = 
            "SELECT shardid, query, calls, total_time "
            "FROM citus_stat_statements ";
        
        if (shardId >= 0) {
            sql += "WHERE shardid = " + std::to_string(shardId) + " ";
        }
        
        sql += "ORDER BY total_time DESC LIMIT 50";
        
        return Query(sql);
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to get shard query stats: {}", e.what());
        return nlohmann::json::array();
    }
}

// =============== Performance Tuning ===============
bool CitusClient::SetShardCount(int shardCount) {
    if (shardCount <= 0) {
        Logger::Error("Invalid shard count: {}", shardCount);
        return false;
    }
    
    shardCount_ = shardCount;
    Logger::Info("Shard count set to {}", shardCount_);
    
    // Note: Changing shard count on existing tables requires table re-creation
    // or using Citus's shard rebalancing functions
    
    return true;
}

bool CitusClient::SetReplicationFactor(int replicationFactor) {
    if (replicationFactor < 1) {
        Logger::Error("Invalid replication factor: {}", replicationFactor);
        return false;
    }
    
    replicationFactor_ = replicationFactor;
    Logger::Info("Replication factor set to {}", replicationFactor_);
    
    // Note: Changing replication factor requires rebalancing
    
    return true;
}

bool CitusClient::EnableQueryMetrics(bool enabled) {
    try {
        std::string sql;
        if (enabled) {
            sql = "CREATE EXTENSION IF NOT EXISTS pg_stat_statements";
        } else {
            sql = "DROP EXTENSION IF EXISTS pg_stat_statements";
        }
        
        return Execute(sql);
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to {} query metrics: {}", 
                     enabled ? "enable" : "disable", e.what());
        return false;
    }
}

// =============== Maintenance Operations ===============
bool CitusClient::VacuumDistributedTables() {
    try {
        Logger::Info("Starting vacuum of distributed tables...");
        
        // Get all distributed tables
        std::string sql = 
            "SELECT logicalrelid::regclass as table_name "
            "FROM pg_dist_partition "
            "ORDER BY logicalrelid";
        
        auto tables = Query(sql);
        
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
        
        if (allSuccess) {
            Logger::Info("Vacuum of distributed tables completed");
        } else {
            Logger::Warn("Vacuum completed with some failures");
        }
        
        return allSuccess;
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to vacuum distributed tables: {}", e.what());
        return false;
    }
}

bool CitusClient::AnalyzeDistributedTables() {
    try {
        Logger::Info("Starting analyze of distributed tables...");
        
        std::string sql = "ANALYZE";
        bool success = Execute(sql);
        
        if (success) {
            Logger::Info("Analyze of distributed tables completed");
        }
        
        return success;
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to analyze distributed tables: {}", e.what());
        return false;
    }
}

bool CitusClient::ReplicateReferenceTables() {
    try {
        Logger::Info("Replicating reference tables to all worker nodes...");
        
        std::string sql = 
            "SELECT citus_replicate_reference_tables()";
        
        bool success = Execute(sql);
        
        if (success) {
            Logger::Info("Reference table replication completed");
        }
        
        return success;
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to replicate reference tables: {}", e.what());
        return false;
    }
}

#endif // USE_CITUS
