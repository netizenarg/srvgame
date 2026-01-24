#include <chrono>
#include <glm/gtc/noise.hpp>
#include <thread>
#include <algorithm>
#include <string>

#include "game/ChunkLOD.hpp"
#include "game/WorldGenerator.hpp"

LODChunk::LODChunk(int x, int z, ChunkLOD lod)
    : WorldChunk(x, z, lod), lod_(lod) {
}

void LODChunk::SetLOD(ChunkLOD lod) {
    if (lod_ == lod) return;
    
    lod_ = lod;
    
    // Regenerate geometry for new LOD
    GenerateGeometry();
    if (GetLOD() <= ChunkLOD::MEDIUM) {
        GenerateCollisionMesh();
    }
}

void LODChunk::GenerateGeometry() {
    auto start_time = std::chrono::steady_clock::now();
    
    switch (lod_) {
        case ChunkLOD::HIGH:
            GenerateHighLOD();
            break;
        case ChunkLOD::MEDIUM:
            GenerateMediumLOD();
            break;
        case ChunkLOD::LOW:
            GenerateLowLOD();
            break;
        case ChunkLOD::BILLBOARD:
            GenerateBillboard();
            break;
        default:
            WorldChunk::GenerateGeometry();
    }
    
    auto end_time = std::chrono::steady_clock::now();
    generation_time_ms_ = std::chrono::duration<float, std::milli>(
        end_time - start_time).count();
}

void LODChunk::GenerateCollisionMesh() {
    // Only generate collision for high/medium LOD
    if (lod_ <= ChunkLOD::MEDIUM) {
        WorldChunk::GenerateCollisionMesh();
    } else {
        // For low LOD, use simplified collision
        collisionVertices_.clear();
        collisionTriangles_.clear();
        
        // Create simple bounding box collision
        glm::vec3 center = GetCenter();
        float halfWidth = CHUNK_WIDTH / 2.0f;
        
        // Create 8 vertices of a box
        collisionVertices_ = {
            {center.x - halfWidth, 0.0f, center.z - halfWidth},
            {center.x + halfWidth, 0.0f, center.z - halfWidth},
            {center.x + halfWidth, 0.0f, center.z + halfWidth},
            {center.x - halfWidth, 0.0f, center.z + halfWidth},
            {center.x - halfWidth, 50.0f, center.z - halfWidth},
            {center.x + halfWidth, 50.0f, center.z - halfWidth},
            {center.x + halfWidth, 50.0f, center.z + halfWidth},
            {center.x - halfWidth, 50.0f, center.z + halfWidth}
        };
        
        // Create 12 triangles for box faces
        // Bottom
        collisionTriangles_.push_back({0, 1, 2});
        collisionTriangles_.push_back({0, 2, 3});
        // Top
        collisionTriangles_.push_back({4, 5, 6});
        collisionTriangles_.push_back({4, 6, 7});
        // Sides
        collisionTriangles_.push_back({0, 1, 5});
        collisionTriangles_.push_back({0, 5, 4});
        collisionTriangles_.push_back({1, 2, 6});
        collisionTriangles_.push_back({1, 6, 5});
        collisionTriangles_.push_back({2, 3, 7});
        collisionTriangles_.push_back({2, 7, 6});
        collisionTriangles_.push_back({3, 0, 4});
        collisionTriangles_.push_back({3, 4, 7});
    }
}

void LODChunk::GenerateHighLODGeometry() {
    GenerateHighLOD();
}

void LODChunk::GenerateMediumLODGeometry() {
    GenerateMediumLOD();
}

void LODChunk::GenerateLowLODGeometry() {
    GenerateLowLOD();
}

void LODChunk::GenerateBillboardGeometry() {
    GenerateBillboard();
}

bool LODChunk::CanUpgradeLOD() const {
    // Can only upgrade from lower LOD to higher
    return lod_ > ChunkLOD::HIGH;
}

bool LODChunk::CanDowngradeLOD() const {
    // Can only downgrade from higher LOD to lower
    return lod_ < ChunkLOD::BILLBOARD;
}

std::shared_ptr<LODChunk> LODChunk::UpgradeLOD() {
    if (!CanUpgradeLOD()) return nullptr;
    
    auto new_lod = static_cast<ChunkLOD>(static_cast<int>(lod_) - 1);
    auto upgraded = std::make_shared<LODChunk>(chunkX_, chunkZ_, new_lod);
    
    // Copy basic properties
    upgraded->SetBiome(biome_);
    upgraded->GenerateGeometry();
    
    return upgraded;
}

std::shared_ptr<LODChunk> LODChunk::DowngradeLOD() {
    if (!CanDowngradeLOD()) return nullptr;
    
    auto new_lod = static_cast<ChunkLOD>(static_cast<int>(lod_) + 1);
    auto downgraded = std::make_shared<LODChunk>(chunkX_, chunkZ_, new_lod);
    
    // Copy basic properties
    downgraded->SetBiome(biome_);
    downgraded->GenerateGeometry();
    
    return downgraded;
}

void LODChunk::GenerateHighLOD() {
    // Use full detail generation
    WorldChunk::GenerateGeometry();
}

void LODChunk::GenerateMediumLOD() {
    // Simplify mesh by factor of 2
    WorldChunk::GenerateGeometry();
    SimplifyMesh(2);
}

void LODChunk::GenerateLowLOD() {
    // Simplify mesh by factor of 4
    WorldChunk::GenerateGeometry();
    SimplifyMesh(4);
}

void LODChunk::GenerateBillboard() {
    vertices_.clear();
    triangles_.clear();
    
    glm::vec3 center = GetCenter();
    
    // Create a simple quad billboard
    float halfSize = CHUNK_WIDTH / 4.0f;
    
    vertices_ = {
        {glm::vec3(center.x - halfSize, 25.0f, center.z), 
         glm::vec3(0.0f, 0.0f, 1.0f), 
         glm::vec3(0.5f, 0.8f, 0.3f), 
         glm::vec2(0.0f, 0.0f)},
        {glm::vec3(center.x + halfSize, 25.0f, center.z), 
         glm::vec3(0.0f, 0.0f, 1.0f), 
         glm::vec3(0.5f, 0.8f, 0.3f), 
         glm::vec2(1.0f, 0.0f)},
        {glm::vec3(center.x + halfSize, 75.0f, center.z), 
         glm::vec3(0.0f, 0.0f, 1.0f), 
         glm::vec3(0.5f, 0.8f, 0.3f), 
         glm::vec2(1.0f, 1.0f)},
        {glm::vec3(center.x - halfSize, 75.0f, center.z), 
         glm::vec3(0.0f, 0.0f, 1.0f), 
         glm::vec3(0.5f, 0.8f, 0.3f), 
         glm::vec2(0.0f, 1.0f)}
    };
    
    triangles_ = {
        {0, 1, 2},
        {0, 2, 3}
    };
    
    // Store billboard data
    BillboardData billboard;
    billboard.position = center;
    billboard.normal = glm::vec3(0.0f, 0.0f, 1.0f);
    billboard.color = glm::vec4(0.5f, 0.8f, 0.3f, 1.0f);
    billboard.size = glm::vec2(CHUNK_WIDTH / 2.0f, 50.0f);
    billboard.texture_id = "chunk_billboard";
    
    billboards_.push_back(billboard);
}

void LODChunk::SimplifyMesh(int factor) {
    if (factor <= 1 || vertices_.empty()) return;

    std::vector<Vertex> simplified_vertices;
    std::vector<Triangle> simplified_triangles;

    // Use a more efficient key structure: quantize to integer grid
    struct QuantizedKey {
        int x, y, z;

        bool operator==(const QuantizedKey& other) const {
            return x == other.x && y == other.y && z == other.z;
        }

        struct Hash {
            size_t operator()(const QuantizedKey& k) const {
                // Simple hash combining
                size_t h1 = std::hash<int>()(k.x);
                size_t h2 = std::hash<int>()(k.y);
                size_t h3 = std::hash<int>()(k.z);
                return h1 ^ (h2 << 1) ^ (h3 << 2);
            }
        };
    };

    std::unordered_map<QuantizedKey, uint32_t, QuantizedKey::Hash> vertex_map;
    std::unordered_map<uint32_t, std::vector<uint32_t>> vertex_groups; // Map from simplified vertex to original vertices
    std::vector<Vertex> temp_simplified_vertices;

    // First pass: collect vertices into groups based on quantized position
    for (size_t i = 0; i < vertices_.size(); ++i) {
        const auto& v = vertices_[i];

        // Quantize vertex position
        QuantizedKey key = {
            static_cast<int>(std::round(v.position.x / factor)),
            static_cast<int>(std::round(v.position.y / factor)),
            static_cast<int>(std::round(v.position.z / factor))
        };

        if (vertex_map.find(key) == vertex_map.end()) {
            uint32_t new_index = static_cast<uint32_t>(temp_simplified_vertices.size());
            vertex_map[key] = new_index;

            Vertex new_vertex = v;
            new_vertex.position = {
                key.x * static_cast<float>(factor),
                key.y * static_cast<float>(factor),
                key.z * static_cast<float>(factor)
            };
            temp_simplified_vertices.push_back(new_vertex);
            vertex_groups[new_index].push_back(static_cast<uint32_t>(i));
        } else {
            uint32_t existing_index = vertex_map[key];
            vertex_groups[existing_index].push_back(static_cast<uint32_t>(i));
        }
    }

    // Second pass: average attributes for vertices in each group
    simplified_vertices.reserve(temp_simplified_vertices.size());
    for (size_t i = 0; i < temp_simplified_vertices.size(); ++i) {
        const auto& group = vertex_groups[static_cast<uint32_t>(i)];

        if (group.size() == 1) {
            // Single vertex in group, keep as is
            simplified_vertices.push_back(temp_simplified_vertices[i]);
        } else {
            // Multiple vertices in group, average attributes
            glm::vec3 avg_position = {0, 0, 0};
            glm::vec3 avg_normal = {0, 0, 0};
            glm::vec3 avg_color = {0, 0, 0};
            glm::vec2 avg_uv = {0, 0};

            for (uint32_t original_idx : group) {
                const Vertex& v = vertices_[original_idx];
                avg_position += v.position;
                avg_normal += v.normal;
                avg_color += v.color;
                avg_uv += v.uv;
            }

            float inv_count = 1.0f / group.size();
            Vertex averaged_vertex = temp_simplified_vertices[i];
            averaged_vertex.position = avg_position * inv_count;
            averaged_vertex.normal = glm::normalize(avg_normal * inv_count);
            averaged_vertex.color = avg_color * inv_count;
            averaged_vertex.uv = avg_uv * inv_count;

            simplified_vertices.push_back(averaged_vertex);
        }
    }

    // Third pass: remap triangles
    std::unordered_map<uint64_t, bool> triangle_map; // To avoid duplicates
    for (const auto& tri : triangles_) {
        // Get quantized keys for each vertex
        const Vertex& v0 = vertices_[tri.v0];
        const Vertex& v1 = vertices_[tri.v1];
        const Vertex& v2 = vertices_[tri.v2];

        QuantizedKey key0 = {
            static_cast<int>(std::round(v0.position.x / factor)),
            static_cast<int>(std::round(v0.position.y / factor)),
            static_cast<int>(std::round(v0.position.z / factor))
        };
        QuantizedKey key1 = {
            static_cast<int>(std::round(v1.position.x / factor)),
            static_cast<int>(std::round(v1.position.y / factor)),
            static_cast<int>(std::round(v1.position.z / factor))
        };
        QuantizedKey key2 = {
            static_cast<int>(std::round(v2.position.x / factor)),
            static_cast<int>(std::round(v2.position.y / factor)),
            static_cast<int>(std::round(v2.position.z / factor))
        };

        // Check if all vertices have been quantized
        if (vertex_map.find(key0) == vertex_map.end() ||
            vertex_map.find(key1) == vertex_map.end() ||
            vertex_map.find(key2) == vertex_map.end()) {
            continue; // Skip this triangle
            }

            uint32_t new_v0 = vertex_map[key0];
        uint32_t new_v1 = vertex_map[key1];
        uint32_t new_v2 = vertex_map[key2];

        // Check for degenerate triangles (area check)
        if (new_v0 == new_v1 || new_v1 == new_v2 || new_v2 == new_v0) {
            continue; // Skip degenerate triangle
        }

        // Check triangle area to avoid extremely small triangles
        const glm::vec3& p0 = simplified_vertices[new_v0].position;
        const glm::vec3& p1 = simplified_vertices[new_v1].position;
        const glm::vec3& p2 = simplified_vertices[new_v2].position;

        glm::vec3 edge1 = p1 - p0;
        glm::vec3 edge2 = p2 - p0;
        glm::vec3 cross = glm::cross(edge1, edge2);
        float area = glm::length(cross) * 0.5f;

        if (area < 0.0001f) { // Minimum area threshold
            continue; // Skip very small triangles
        }

        // Sort indices to create unique key for triangle (avoid duplicates)
        uint32_t sorted[3] = {new_v0, new_v1, new_v2};
        std::sort(std::begin(sorted), std::end(sorted));
        uint64_t triangle_key = (static_cast<uint64_t>(sorted[0]) << 32) |
        (static_cast<uint64_t>(sorted[1]) << 16) |
        static_cast<uint64_t>(sorted[2]);

        if (triangle_map.find(triangle_key) == triangle_map.end()) {
            triangle_map[triangle_key] = true;
            simplified_triangles.push_back({new_v0, new_v1, new_v2});
        }
    }

    vertices_ = std::move(simplified_vertices);
    triangles_ = std::move(simplified_triangles);
}

nlohmann::json LODChunk::Serialize() const {
    auto json = WorldChunk::Serialize();
    json["lod"] = static_cast<int>(lod_);
    return json;
}

void LODChunk::Deserialize(const nlohmann::json& data) {
    WorldChunk::Deserialize(data);
    lod_ = static_cast<ChunkLOD>(data.value("lod", 0));
}

size_t LODChunk::GetTriangleCount() const {
    return triangles_.size();
}

size_t LODChunk::GetVertexCount() const {
    return vertices_.size();
}

// LODManager implementation
LODManager& LODManager::GetInstance() {
    static LODManager instance;
    return instance;
}

void LODManager::Initialize(const LODConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

ChunkLOD LODManager::CalculateLOD(const glm::vec3& camera_pos, 
                                  const glm::vec3& chunk_pos) const {
    float distance_sq = CalculateDistanceSquared(camera_pos, chunk_pos);
    
    if (distance_sq <= config_.high_distance * config_.high_distance) {
        return ChunkLOD::HIGH;
    } else if (distance_sq <= config_.medium_distance * config_.medium_distance) {
        return ChunkLOD::MEDIUM;
    } else if (distance_sq <= config_.low_distance * config_.low_distance) {
        return ChunkLOD::LOW;
    } else {
        return ChunkLOD::BILLBOARD;
    }
}

std::shared_ptr<LODChunk> LODManager::CreateChunk(int x, int z, ChunkLOD lod) {
    return std::make_shared<LODChunk>(x, z, lod);
}

void LODManager::UpdateChunkLOD(std::shared_ptr<LODChunk> chunk, 
                                const glm::vec3& camera_pos) {
    if (!chunk) return;
    
    ChunkLOD current_lod = chunk->GetLOD();
    ChunkLOD target_lod = CalculateLOD(camera_pos, chunk->GetCenter());
    
    float current_distance = glm::distance(camera_pos, chunk->GetCenter());
    float target_distance_threshold = 0.0f;
    
    switch (target_lod) {
        case ChunkLOD::HIGH: target_distance_threshold = config_.high_distance; break;
        case ChunkLOD::MEDIUM: target_distance_threshold = config_.medium_distance; break;
        case ChunkLOD::LOW: target_distance_threshold = config_.low_distance; break;
        default: target_distance_threshold = config_.low_distance * 2.0f; break;
    }
    
    if (target_lod != current_lod) {
        if (target_lod < current_lod) { // Upgrade LOD
            if (ShouldUpgradeLOD(current_lod, target_lod, 
                                current_distance, target_distance_threshold)) {
                auto upgraded = chunk->UpgradeLOD();
                if (upgraded) {
                    chunk = upgraded;
                    stats_.lod_upgrades++;
                }
            }
        } else { // Downgrade LOD
            if (ShouldDowngradeLOD(current_lod, target_lod,
                                  current_distance, target_distance_threshold)) {
                auto downgraded = chunk->DowngradeLOD();
                if (downgraded) {
                    chunk = downgraded;
                    stats_.lod_downgrades++;
                }
            }
        }
    }
}

void LODManager::UpdateAllChunksLOD(const glm::vec3& camera_pos,
                                   const std::vector<std::shared_ptr<LODChunk>>& chunks) {
    auto start_time = std::chrono::steady_clock::now();
    
    // Reset stats for this update
    stats_.high_lod_chunks = 0;
    stats_.medium_lod_chunks = 0;
    stats_.low_lod_chunks = 0;
    stats_.billboard_chunks = 0;
    
    for (auto& chunk : chunks) {
        UpdateChunkLOD(chunk, camera_pos);
        
        // Update statistics
        switch (chunk->GetLOD()) {
            case ChunkLOD::HIGH: stats_.high_lod_chunks++; break;
            case ChunkLOD::MEDIUM: stats_.medium_lod_chunks++; break;
            case ChunkLOD::LOW: stats_.low_lod_chunks++; break;
            case ChunkLOD::BILLBOARD: stats_.billboard_chunks++; break;
            default: break;
        }
    }
    
    auto end_time = std::chrono::steady_clock::now();
    perf_metrics_.update_time_ms = std::chrono::duration<float, std::milli>(
        end_time - start_time).count();
    perf_metrics_.chunks_updated = chunks.size();
    perf_metrics_.last_update = end_time;
}

float LODManager::CalculateDistanceSquared(const glm::vec3& a, const glm::vec3& b) const {
    glm::vec3 diff = a - b;
    return glm::dot(diff, diff);
}

bool LODManager::ShouldUpgradeLOD(ChunkLOD current, ChunkLOD target, 
                                 float current_distance, float target_distance) const {
    // Add hysteresis to prevent flickering
    // Only upgrade when we're well within the higher LOD threshold
    const float HYSTERESIS_FACTOR = 0.8f;
    return current_distance < target_distance * HYSTERESIS_FACTOR;
}

bool LODManager::ShouldDowngradeLOD(ChunkLOD current, ChunkLOD target,
                                   float current_distance, float target_distance) const {
    // Add hysteresis to prevent flickering
    // Only downgrade when we're well outside the current LOD threshold
    const float HYSTERESIS_FACTOR = 1.2f;
    return current_distance > target_distance * HYSTERESIS_FACTOR;
}

LODManager::LODStats LODManager::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void LODManager::ResetStats() {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_ = LODStats();
}
