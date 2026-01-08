#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <glm/glm.hpp>

struct GridCell {
    int32_t x;
    int32_t z;
    
    GridCell(int32_t x = 0, int32_t z = 0) : x(x), z(z) {}
    
    bool operator==(const GridCell& other) const {
        return x == other.x && z == other.z;
    }
    
    bool operator!=(const GridCell& other) const {
        return !(*this == other);
    }
    
    GridCell operator+(const GridCell& other) const {
        return GridCell(x + other.x, z + other.z);
    }
    
    GridCell operator-(const GridCell& other) const {
        return GridCell(x - other.x, z - other.z);
    }
    
    struct Hash {
        size_t operator()(const GridCell& cell) const {
            return ((static_cast<size_t>(cell.x) << 32) | static_cast<size_t>(cell.z));
        }
    };
};

class SpatialGrid {
public:
    SpatialGrid(float cell_size = 50.0f, int32_t max_cells = 10000);
    
    // Entity management
    void AddEntity(uint64_t entity_id, const glm::vec3& position, float radius = 1.0f);
    void UpdateEntity(uint64_t entity_id, const glm::vec3& position);
    void RemoveEntity(uint64_t entity_id);
    
    // Query methods
    std::vector<uint64_t> GetEntitiesInRadius(const glm::vec3& position, float radius) const;
    std::vector<uint64_t> GetEntitiesInCell(const GridCell& cell) const;
    std::vector<GridCell> GetCellsInRadius(const glm::vec3& position, float radius) const;
    
    // Broad-phase collision detection
    std::vector<uint64_t> GetPotentialCollisions(uint64_t entity_id, const glm::vec3& position, float radius) const;
    
    // Statistics
    size_t GetEntityCount() const { return entity_to_cell_.size(); }
    size_t GetCellCount() const { return cell_to_entities_.size(); }
    GridCell GetEntityCell(uint64_t entity_id) const;
    
    // Clear all data
    void Clear();
    
    // Thread-safe operations
    void LockRead() const;
    void UnlockRead() const;
    void LockWrite();
    void UnlockWrite();
    
private:
    float cell_size_;
    int32_t max_cells_;
    
    mutable std::shared_mutex grid_mutex_;
    
    // Entity to cell mapping
    std::unordered_map<uint64_t, GridCell> entity_to_cell_;
    
    // Cell to entities mapping
    std::unordered_map<GridCell, std::unordered_set<uint64_t>, GridCell::Hash> cell_to_entities_;
    
    // Entity metadata
    struct EntityInfo {
        glm::vec3 position;
        float radius;
    };
    std::unordered_map<uint64_t, EntityInfo> entity_info_;
    
    // Helper methods
    GridCell PositionToCell(const glm::vec3& position) const;
    float CellDistance(const GridCell& cell1, const GridCell& cell2) const;
    bool CellsIntersect(const GridCell& cell, const glm::vec3& center, float radius) const;
    
    // Cell management
    void CleanupEmptyCells();
};

// Specialized spatial grid for broadcasting
class BroadcastGrid : public SpatialGrid {
public:
    BroadcastGrid(float cell_size = 50.0f, int32_t max_cells = 10000);
    
    // Session management
    void AddSession(uint64_t session_id, const glm::vec3& position);
    void UpdateSession(uint64_t session_id, const glm::vec3& position);
    void RemoveSession(uint64_t session_id);
    
    // Get sessions in radius for broadcasting
    std::vector<uint64_t> GetSessionsInRadius(const glm::vec3& position, float radius) const;
    
    // Bulk operations
    void AddSessions(const std::unordered_map<uint64_t, glm::vec3>& sessions);
    void UpdateSessions(const std::unordered_map<uint64_t, glm::vec3>& sessions);
    
private:
    // Session to entity ID mapping
    std::unordered_map<uint64_t, uint64_t> session_to_entity_;
    std::unordered_map<uint64_t, uint64_t> entity_to_session_;
    
    uint64_t next_entity_id_{1};
    
    uint64_t GetOrCreateEntityId(uint64_t session_id);
};