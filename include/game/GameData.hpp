#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

struct AuthenticationData {
    std::string username;
    std::string password;
    uint64_t session_id;
};

struct PlayerStateData {
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
    uint64_t timestamp;
    uint64_t session_id;
};

struct PlayerPositionData {
    uint64_t player_id;
    glm::vec3 position;
    glm::vec3 velocity;
    uint64_t timestamp;
    uint64_t session_id;
};

struct ChunkRequestData {
    int chunk_x;
    int chunk_z;
    uint8_t lod;
    uint64_t session_id;  // which session requested it
};

struct ChunkData {
    int chunk_x;
    int chunk_z;
    uint8_t lod;
    nlohmann::json chunk_json;  // already serialized
    uint64_t timestamp;
    uint64_t session_id;
};
