-- [create_distributed_table_players]
SELECT create_distributed_table('players', 'id');

-- [create_distributed_table_player_inventory]
SELECT create_distributed_table('player_inventory', 'player_id');

-- [create_distributed_table_player_quests]
SELECT create_distributed_table('player_quests', 'player_id');

-- [create_distributed_table_world_chunks]
SELECT create_distributed_table('world_chunks', 'chunk_x');

-- [create_distributed_table_npcs]
SELECT create_distributed_table('npcs', 'id');

-- [create_reference_table_loot_tables]
SELECT create_reference_table('loot_tables');

-- [add_worker_node]
SELECT citus_add_node($1, $2);

-- [remove_worker_node]
SELECT citus_remove_node($1);

-- [get_worker_nodes]
SELECT nodeid, nodename, nodeport, noderole, isactive FROM pg_dist_node ORDER BY nodeid;

-- [get_shard_placements]
SELECT shardid, nodename, nodeport, placementid
FROM pg_dist_placement p
JOIN pg_dist_node n ON p.groupid = n.groupid
ORDER BY shardid, placementid;

-- [rebalance_shards]
SELECT rebalance_table_shards();

-- [move_shard]
SELECT citus_move_shard_placement($1, $2, $3);

-- [isolate_shard]
UPDATE pg_dist_placement SET shardstate = 3 WHERE shardid = $1;

-- [get_shard_statistics]
SELECT shardid,
       COUNT(*) as replica_count,
       SUM(CASE WHEN shardstate = 1 THEN 1 ELSE 0 END) as active_replicas,
       SUM(CASE WHEN shardstate = 3 THEN 1 ELSE 0 END) as isolated_replicas
FROM pg_dist_placement
GROUP BY shardid
ORDER BY shardid;

-- [enable_citus_extension]
CREATE EXTENSION IF NOT EXISTS citus;

-- [check_citus_extension]
SELECT EXISTS(SELECT 1 FROM pg_extension WHERE extname = 'citus');

-- [get_worker_node_stats]
SELECT nodename, nodeport,
       COUNT(DISTINCT shardid) as shard_count,
       SUM(shardsize) as total_size_bytes
FROM pg_dist_placement p
JOIN pg_dist_node n ON p.groupid = n.groupid
GROUP BY nodename, nodeport
ORDER BY nodename, nodeport;

-- [create_distributed_table]
SELECT create_distributed_table($1, $2);

-- [create_reference_table]
SELECT create_reference_table($1);

-- [create_distributed_function]
SELECT create_distributed_function($1);

-- [get_query_stats]
SELECT query, calls, total_time, mean_time, rows
FROM pg_stat_statements
ORDER BY total_time DESC
LIMIT 20;

-- [get_cluster_stats]
SELECT
    (SELECT COUNT(*) FROM pg_dist_node WHERE noderole = 'primary') as primary_nodes,
    (SELECT COUNT(*) FROM pg_dist_node WHERE noderole = 'secondary') as secondary_nodes,
    (SELECT COUNT(DISTINCT shardid) FROM pg_dist_placement) as total_shards,
    (SELECT COUNT(*) FROM pg_dist_placement WHERE shardstate = 1) as active_placements,
    (SELECT COUNT(*) FROM pg_dist_placement WHERE shardstate = 3) as isolated_placements,
    (SELECT SUM(shardsize) FROM pg_dist_placement) as total_data_size_bytes;

-- [get_shard_query_stats]
SELECT shardid, query, calls, total_time
FROM citus_stat_statements
WHERE shardid = $1
ORDER BY total_time DESC
LIMIT 50;

-- [replicate_reference_tables]
SELECT citus_replicate_reference_tables();