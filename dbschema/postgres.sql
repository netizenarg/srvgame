-- [create_table_players]
CREATE TABLE IF NOT EXISTS players (
    id BIGINT PRIMARY KEY,
    data JSONB NOT NULL,
    password_hash TEXT NOT NULL,
    position_x REAL DEFAULT 0,
    position_y REAL DEFAULT 0,
    position_z REAL DEFAULT 0,
    level INTEGER DEFAULT 1,
    experience REAL DEFAULT 0,
    health INTEGER DEFAULT 100,
    max_health INTEGER DEFAULT 100,
    mana INTEGER DEFAULT 50,
    max_mana INTEGER DEFAULT 50,
    currency_gold INTEGER DEFAULT 0,
    currency_gems INTEGER DEFAULT 0,
    total_playtime INTEGER DEFAULT 0,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW()
);

-- [create_table_game_state]
CREATE TABLE IF NOT EXISTS game_state (
    key VARCHAR(64) PRIMARY KEY,
    value JSONB NOT NULL,
    updated_at TIMESTAMPTZ DEFAULT NOW()
);

-- [create_table_world_chunks]
CREATE TABLE IF NOT EXISTS world_chunks (
    chunk_x INTEGER NOT NULL,
    chunk_z INTEGER NOT NULL,
    biome INTEGER NOT NULL,
    data JSONB NOT NULL,
    last_updated TIMESTAMPTZ DEFAULT NOW(),
    PRIMARY KEY (chunk_x, chunk_z)
);

-- [create_table_player_inventory]
CREATE TABLE IF NOT EXISTS player_inventory (
    player_id BIGINT PRIMARY KEY REFERENCES players(id) ON DELETE CASCADE,
    data JSONB NOT NULL,
    last_updated TIMESTAMPTZ DEFAULT NOW()
);

-- [create_table_player_quests]
CREATE TABLE IF NOT EXISTS player_quests (
    player_id BIGINT REFERENCES players(id) ON DELETE CASCADE,
    quest_id VARCHAR(64) NOT NULL,
    progress JSONB NOT NULL,
    last_updated TIMESTAMPTZ DEFAULT NOW(),
    PRIMARY KEY (player_id, quest_id)
);

-- [create_table_npcs]
CREATE TABLE IF NOT EXISTS npcs (
    id BIGINT PRIMARY KEY,
    type INTEGER NOT NULL,
    position JSONB NOT NULL,
    level INTEGER NOT NULL DEFAULT 1,
    data JSONB NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW()
);

-- [create_table_loot_tables]
CREATE TABLE IF NOT EXISTS loot_tables (
    table_id VARCHAR(64) PRIMARY KEY,
    name VARCHAR(128) NOT NULL,
    data JSONB NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

-- [create_table_schema_migrations]
CREATE TABLE IF NOT EXISTS schema_migrations (
    version INTEGER PRIMARY KEY,
    name VARCHAR(255) NOT NULL,
    applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    checksum VARCHAR(64)
);

-- [save_player_data]
INSERT INTO players (id, data, password_hash, updated_at) VALUES ($1, $2, $3, NOW())
ON CONFLICT (id) DO UPDATE SET data = EXCLUDED.data, password_hash = EXCLUDED.password_hash, updated_at = NOW();

-- [load_player_data]
SELECT data FROM players WHERE id = $1;

-- [update_player_position]
UPDATE players SET position_x = $1, position_y = $2, position_z = $3, updated_at = NOW() WHERE id = $4;

-- [player_exists]
SELECT EXISTS(SELECT 1 FROM players WHERE id = $1);

-- [get_player_stats]
SELECT level, experience, health, max_health, mana, max_mana, currency_gold, currency_gems, total_playtime FROM players WHERE id = $1;

-- [get_player]
SELECT * FROM players WHERE id = $1;

-- [get_player_by_username]
SELECT id, password_hash FROM players WHERE data->>'username' = $1;

-- [save_game_state]
INSERT INTO game_state (key, value, updated_at) VALUES ($1, $2, NOW())
ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value, updated_at = NOW();

-- [load_game_state]
SELECT value FROM game_state WHERE key = $1;

-- [delete_game_state]
DELETE FROM game_state WHERE key = $1;

-- [list_game_states]
SELECT key FROM game_state ORDER BY key;

-- [save_chunk_data]
INSERT INTO world_chunks (chunk_x, chunk_z, biome, data, last_updated) VALUES ($1, $2, $3, $4, NOW())
ON CONFLICT (chunk_x, chunk_z) DO UPDATE SET biome = EXCLUDED.biome, data = EXCLUDED.data, last_updated = NOW();

-- [load_chunk_data]
SELECT data FROM world_chunks WHERE chunk_x = $1 AND chunk_z = $2;

-- [delete_chunk_data]
DELETE FROM world_chunks WHERE chunk_x = $1 AND chunk_z = $2;

-- [list_chunks_in_range]
SELECT chunk_x, chunk_z FROM world_chunks WHERE chunk_x BETWEEN $1 AND $2 AND chunk_z BETWEEN $3 AND $4;

-- [save_inventory]
INSERT INTO player_inventory (player_id, data, last_updated) VALUES ($1, $2, NOW())
ON CONFLICT (player_id) DO UPDATE SET data = EXCLUDED.data, last_updated = NOW();

-- [load_inventory]
SELECT data FROM player_inventory WHERE player_id = $1;

-- [save_quest_progress]
INSERT INTO player_quests (player_id, quest_id, progress, last_updated) VALUES ($1, $2, $3, NOW())
ON CONFLICT (player_id, quest_id) DO UPDATE SET progress = EXCLUDED.progress, last_updated = NOW();

-- [load_quest_progress]
SELECT progress FROM player_quests WHERE player_id = $1 AND quest_id = $2;

-- [list_active_quests]
SELECT quest_id FROM player_quests WHERE player_id = $1 ORDER BY quest_id;

-- [begin_transaction]
BEGIN;

-- [commit_transaction]
COMMIT;

-- [rollback_transaction]
ROLLBACK;

-- [migration_current_version]
SELECT MAX(version) as current_version FROM schema_migrations;

-- [delete_migration]
DELETE FROM schema_migrations WHERE version = $1;

-- [enable_pg_stat_statements]
CREATE EXTENSION IF NOT EXISTS pg_stat_statements;

-- [disable_pg_stat_statements]
DROP EXTENSION IF EXISTS pg_stat_statements;
