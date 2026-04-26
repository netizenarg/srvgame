-- [create_table_players]
CREATE TABLE IF NOT EXISTS players (
    id INTEGER PRIMARY KEY,
    data TEXT NOT NULL,
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
    created_at TEXT DEFAULT (datetime('now')),
    updated_at TEXT DEFAULT (datetime('now'))
);

-- [create_table_game_state]
CREATE TABLE IF NOT EXISTS game_state (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL,
    updated_at TEXT DEFAULT (datetime('now'))
);

-- [create_table_world_chunks]
CREATE TABLE IF NOT EXISTS world_chunks (
    chunk_x INTEGER NOT NULL,
    chunk_z INTEGER NOT NULL,
    biome INTEGER NOT NULL,
    data TEXT NOT NULL,
    last_updated TEXT DEFAULT (datetime('now')),
    PRIMARY KEY (chunk_x, chunk_z)
);

-- [create_table_player_inventory]
CREATE TABLE IF NOT EXISTS player_inventory (
    player_id INTEGER PRIMARY KEY,
    data TEXT NOT NULL,
    last_updated TEXT DEFAULT (datetime('now')),
    FOREIGN KEY (player_id) REFERENCES players(id) ON DELETE CASCADE
);

-- [create_table_player_quests]
CREATE TABLE IF NOT EXISTS player_quests (
    player_id INTEGER NOT NULL,
    quest_id TEXT NOT NULL,
    progress TEXT NOT NULL,
    last_updated TEXT DEFAULT (datetime('now')),
    PRIMARY KEY (player_id, quest_id),
    FOREIGN KEY (player_id) REFERENCES players(id) ON DELETE CASCADE
);

-- [create_table_npcs]
CREATE TABLE IF NOT EXISTS npcs (
    id INTEGER PRIMARY KEY,
    type INTEGER NOT NULL,
    position TEXT NOT NULL,
    level INTEGER NOT NULL DEFAULT 1,
    data TEXT NOT NULL,
    created_at TEXT DEFAULT (datetime('now')),
    updated_at TEXT DEFAULT (datetime('now'))
);

-- [create_table_loot_tables]
CREATE TABLE IF NOT EXISTS loot_tables (
    table_id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    data TEXT NOT NULL,
    created_at TEXT DEFAULT (datetime('now'))
);

-- [create_table_schema_migrations]
CREATE TABLE IF NOT EXISTS schema_migrations (
    version INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    applied_at TEXT DEFAULT (datetime('now')),
    checksum TEXT
);

-- [save_player_data]
INSERT OR REPLACE INTO players (id, data, password_hash, updated_at) VALUES (?, ?, ?, datetime('now'));

-- [load_player_data]
SELECT data FROM players WHERE id = ?;

-- [update_player_position]
UPDATE players SET position_x = ?, position_y = ?, position_z = ?, updated_at = datetime('now') WHERE id = ?;

-- [player_exists]
SELECT 1 FROM players WHERE id = ? LIMIT 1;

-- [get_player_stats]
SELECT level, experience, health, max_health, mana, max_mana, currency_gold, currency_gems, total_playtime FROM players WHERE id = ?;

-- [get_player]
SELECT * FROM players WHERE id = ?;

-- [get_player_by_username]
SELECT id, password_hash FROM players WHERE json_extract(data, '$.username') = ?;

-- [save_game_state]
INSERT OR REPLACE INTO game_state (key, value, updated_at) VALUES (?, ?, datetime('now'));

-- [load_game_state]
SELECT value FROM game_state WHERE key = ?;

-- [delete_game_state]
DELETE FROM game_state WHERE key = ?;

-- [list_game_states]
SELECT key FROM game_state ORDER BY key;

-- [save_chunk_data]
INSERT OR REPLACE INTO world_chunks (chunk_x, chunk_z, biome, data, last_updated) VALUES (?, ?, ?, ?, datetime('now'));

-- [load_chunk_data]
SELECT data FROM world_chunks WHERE chunk_x = ? AND chunk_z = ?;

-- [delete_chunk_data]
DELETE FROM world_chunks WHERE chunk_x = ? AND chunk_z = ?;

-- [list_chunks_in_range]
SELECT chunk_x, chunk_z FROM world_chunks WHERE chunk_x BETWEEN ? AND ? AND chunk_z BETWEEN ? AND ?;

-- [save_inventory]
INSERT OR REPLACE INTO player_inventory (player_id, data, last_updated) VALUES (?, ?, datetime('now'));

-- [load_inventory]
SELECT data FROM player_inventory WHERE player_id = ?;

-- [save_quest_progress]
INSERT OR REPLACE INTO player_quests (player_id, quest_id, progress, last_updated) VALUES (?, ?, ?, datetime('now'));

-- [load_quest_progress]
SELECT progress FROM player_quests WHERE player_id = ? AND quest_id = ?;

-- [list_active_quests]
SELECT quest_id FROM player_quests WHERE player_id = ? ORDER BY quest_id;

-- [begin_transaction]
BEGIN TRANSACTION;

-- [commit_transaction]
COMMIT;

-- [rollback_transaction]
ROLLBACK;

-- [migration_current_version]
SELECT MAX(version) as current_version FROM schema_migrations;

-- [delete_migration]
DELETE FROM schema_migrations WHERE version = ?;
