#pragma once

#include <cstdint>
#include <variant>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

enum class LootRarity {
    COMMON,
    UNCOMMON,
    RARE,
    EPIC,
    LEGENDARY,
    MYTHIC
};

enum class LootType {
    MISC,
    WEAPON,
    AMMO,
    ARMOR,
    CONSUMABLE,
    MATERIAL,
    QUEST,
    KEY,
    CURRENCY,
    JEWELRY
};

enum class InventoryMoveType {
    REMOVE,
    USE, // use for self
    TRADE
};

struct WeaponStats {
    uint16_t damage; // use as default if weapon like knife (without ammo, ammo_capacity always zero)
    uint8_t ammo_capacity;
    uint8_t delay_between_shots; // milliseconds, delay reloading between shots
    uint8_t element;          // 0=physical, 1=fire, etc.
    uint8_t required_level;
};

struct AmmoStats {
    float speed;
    uint16_t damage;
};

struct ArmorStats {
    uint16_t defense;
    uint16_t durability;
    uint16_t max_durability;
    uint8_t armor_class;      // light/medium/heavy
    uint8_t required_level;
};

struct ConsumableStats {
    uint16_t effect_id;       // e.g., healing potion, mana elixir
    uint16_t effect_power;
    uint32_t cooldown_ms;
};

struct JewelryStats {
    uint16_t stat_bonus_type; // strength, intelligence, etc.
    uint16_t stat_bonus_value;
    uint8_t socket_count;
};

struct QuestItem {
    uint32_t quest_id;
    uint32_t objective_index;
};

using LootPayload = std::variant<
std::monostate,      // MISC, MATERIAL, KEY, CURRENCY
WeaponStats,
AmmoStats,
ArmorStats,
ConsumableStats,
JewelryStats,
QuestItem
>;


struct AuthenticationData {
    uint64_t timestamp;
    uint64_t session_id;
    std::string username;
    std::string password;
};

struct PlayerStateData {
    uint64_t timestamp;
    uint64_t session_id;
    uint64_t player_id;
    uint32_t input_id;
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 rotation;
    glm::vec3 movement;
    bool on_ground;
    bool jumping;
    bool crouching;
    bool sprinting;
};

struct PlayerSpawnData {
    uint64_t timestamp;
    uint64_t player_id;
    std::string name;
    glm::vec3 position;
    float yaw;
    float health;
    float max_health;
};

struct PlayerDespawnData {
    uint64_t timestamp;
    uint64_t player_id;
};

struct PlayerPositionData {
    uint64_t timestamp;
    uint64_t session_id;
    uint64_t player_id;
    glm::vec3 position;
    glm::vec3 velocity;
};

struct PlayerUpdateData {
    uint64_t timestamp;
    uint64_t session_id;
    uint64_t player_id;
    glm::vec3 position;
    float yaw;
    float health;
    float max_health;
    std::string name;
};

struct StoneData {
    float x, y, z;
    float trunkHeight;
    float foliageRadius;
    float rotationY;
};

struct TreeData {
    float x, y, z;
    float trunkHeight;
    float foliageRadius;
    float rotationY;
};

struct PortalData {
    float x, y, z;
    float rotationY;
    float scale;
    bool active = false;
};

struct ChunkParams {
    uint64_t timestamp;
    uint64_t session_id;
    int size;
    float spacing;
};

struct ChunkData {
    uint64_t timestamp;
    uint64_t session_id;
    int x;
    int z;
    uint8_t lod;
    float player_x;
    float player_y;
    float player_z;
    std::vector<float> vertices;
    std::vector<uint32_t> indices;
};

struct CollisionData {
    uint64_t timestamp;
    uint64_t session_id;
    glm::vec3 position;
    float radius;
};

struct NpcData {
    uint64_t timestamp;
    uint64_t session_id;
    uint64_t player_id;
    uint64_t npc_id;
    float damage;
    float health;
    bool is_dead;
    std::string type; // "combat" or "dialogue"
    nlohmann::json quests; // for dialogue
};

struct FamiliarData {
    uint64_t timestamp;
    uint64_t session_id;
    uint64_t familiar_id;
    uint64_t target_id;
    std::string command;
};

struct EntitySpawnData {
    uint64_t timestamp;
    uint64_t session_id;
    uint64_t entity_id;
    int type;
    glm::vec3 position;
};

struct LootPickupData {
    uint64_t timestamp;
    uint64_t session_id;
    uint64_t player_id;
    uint64_t loot_id;           // world instance ID
    LootType type;
    LootRarity rarity;
    uint16_t quantity;          // stack size (ignored for non‑stackables)
    LootPayload payload;        // type‑safe extra data
};

struct InventoryData {
    uint64_t timestamp;
    uint64_t session_id;
    uint64_t player_id;
    uint64_t loot_id; // world instance ID
    int use_slot_id; // equipmentSlots
    int inv_slot_id; // inventorySlots
    uint64_t target_id; // optional, if move_type TRADE
    uint16_t quantity;
    InventoryMoveType move_type;
};
