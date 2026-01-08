#include <cmath>
#include <algorithm>
#include <iostream>

#include "../../include/game/SpatialGrid.hpp"

SpatialGrid::SpatialGrid(float cell_size, int32_t max_cells)
    : cell_size_(std::max(1.0f, cell_size)), max_cells_(std::max(1, max_cells)) {}

GridCell SpatialGrid::PositionToCell(const glm::vec3& position) const {
    return GridCell(
        static_cast<int32_t>(std::floor(position.x / cell_size_)),
        static_cast<int32_t>(std::floor(position.z / cell_size_))
    );
}

void SpatialGrid::AddEntity(uint64_t entity_id, const glm::vec3& position, float radius) {
    std::lock_guard<std::shared_mutex> lock(grid_mutex_);
    
    GridCell cell = PositionToCell(position);
    entity_to_cell_[entity_id] = cell;
    entity_info_[entity_id] = {position, radius};
    cell_to_entities_[cell].insert(entity_id);
    
    // Cleanup if we have too many cells
    if (cell_to_entities_.size() > static_cast<size_t>(max_cells_)) {
        CleanupEmptyCells();
    }
}

void SpatialGrid::UpdateEntity(uint64_t entity_id, const glm::vec3& position) {
    std::lock_guard<std::shared_mutex> lock(grid_mutex_);
    
    auto entity_it = entity_to_cell_.find(entity_id);
    if (entity_it == entity_to_cell_.end()) {
        return; // Entity not found
    }
    
    GridCell old_cell = entity_it->second;
    GridCell new_cell = PositionToCell(position);
    
    // Update entity info
    entity_info_[entity_id].position = position;
    
    // If cell changed, update mappings
    if (old_cell != new_cell) {
        // Remove from old cell
        auto old_cell_it = cell_to_entities_.find(old_cell);
        if (old_cell_it != cell_to_entities_.end()) {
            old_cell_it->second.erase(entity_id);
            if (old_cell_it->second.empty()) {
                cell_to_entities_.erase(old_cell_it);
            }
        }
        
        // Add to new cell
        entity_to_cell_[entity_id] = new_cell;
        cell_to_entities_[new_cell].insert(entity_id);
    }
}

void SpatialGrid::RemoveEntity(uint64_t entity_id) {
    std::lock_guard<std::shared_mutex> lock(grid_mutex_);
    
    auto entity_it = entity_to_cell_.find(entity_id);
    if (entity_it == entity_to_cell_.end()) {
        return;
    }
    
    GridCell cell = entity_it->second;
    
    // Remove from cell
    auto cell_it = cell_to_entities_.find(cell);
    if (cell_it != cell_to_entities_.end()) {
        cell_it->second.erase(entity_id);
        if (cell_it->second.empty()) {
            cell_to_entities_.erase(cell_it);
        }
    }
    
    // Remove from entity maps
    entity_to_cell_.erase(entity_it);
    entity_info_.erase(entity_id);
}

std::vector<uint64_t> SpatialGrid::GetEntitiesInRadius(const glm::vec3& position, float radius) const {
    std::shared_lock<std::shared_mutex> lock(grid_mutex_);
    
    std::vector<uint64_t> entities;
    GridCell center_cell = PositionToCell(position);
    
    // Calculate cell radius
    int32_t cell_radius = static_cast<int32_t>(std::ceil(radius / cell_size_));
    
    // Check cells in radius
    for (int32_t dx = -cell_radius; dx <= cell_radius; ++dx) {
        for (int32_t dz = -cell_radius; dz <= cell_radius; ++dz) {
            GridCell cell(center_cell.x + dx, center_cell.z + dz);
            
            auto cell_it = cell_to_entities_.find(cell);
            if (cell_it != cell_to_entities_.end()) {
                for (uint64_t entity_id : cell_it->second) {
                    // Check actual distance
                    const auto& info = entity_info_.at(entity_id);
                    float distance = glm::distance(position, info.position);
                    
                    if (distance <= radius + info.radius) {
                        entities.push_back(entity_id);
                    }
                }
            }
        }
    }
    
    return entities;
}

std::vector<uint64_t> SpatialGrid::GetPotentialCollisions(uint64_t entity_id, const glm::vec3& position, float radius) const {
    std::shared_lock<std::shared_mutex> lock(grid_mutex_);
    
    std::vector<uint64_t> collisions;
    
    // Skip if entity doesn't exist
    auto entity_info_it = entity_info_.find(entity_id);
    if (entity_info_it == entity_info_.end()) {
        return collisions;
    }
    
    GridCell center_cell = PositionToCell(position);
    int32_t cell_radius = static_cast<int32_t>(std::ceil(radius / cell_size_));
    
    for (int32_t dx = -cell_radius; dx <= cell_radius; ++dx) {
        for (int32_t dz = -cell_radius; dz <= cell_radius; ++dz) {
            GridCell cell(center_cell.x + dx, center_cell.z + dz);
            
            auto cell_it = cell_to_entities_.find(cell);
            if (cell_it != cell_to_entities_.end()) {
                for (uint64_t other_id : cell_it->second) {
                    // Skip self
                    if (other_id == entity_id) continue;
                    
                    // Check actual collision
                    const auto& other_info = entity_info_.at(other_id);
                    float distance = glm::distance(position, other_info.position);
                    
                    if (distance <= radius + other_info.radius) {
                        collisions.push_back(other_id);
                    }
                }
            }
        }
    }
    
    return collisions;
}

GridCell SpatialGrid::GetEntityCell(uint64_t entity_id) const {
    std::shared_lock<std::shared_mutex> lock(grid_mutex_);
    
    auto it = entity_to_cell_.find(entity_id);
    if (it != entity_to_cell_.end()) {
        return it->second;
    }
    return GridCell();
}

void SpatialGrid::CleanupEmptyCells() {
    // Remove empty cells
    for (auto it = cell_to_entities_.begin(); it != cell_to_entities_.end();) {
        if (it->second.empty()) {
            it = cell_to_entities_.erase(it);
        } else {
            ++it;
        }
    }
}

void SpatialGrid::Clear() {
    std::lock_guard<std::shared_mutex> lock(grid_mutex_);
    entity_to_cell_.clear();
    cell_to_entities_.clear();
    entity_info_.clear();
}

void SpatialGrid::LockRead() const {
    grid_mutex_.lock_shared();
}

void SpatialGrid::UnlockRead() const {
    grid_mutex_.unlock_shared();
}

void SpatialGrid::LockWrite() {
    grid_mutex_.lock();
}

void SpatialGrid::UnlockWrite() {
    grid_mutex_.unlock();
}

// BroadcastGrid implementation
BroadcastGrid::BroadcastGrid(float cell_size, int32_t max_cells)
    : SpatialGrid(cell_size, max_cells) {}

uint64_t BroadcastGrid::GetOrCreateEntityId(uint64_t session_id) {
    auto it = session_to_entity_.find(session_id);
    if (it != session_to_entity_.end()) {
        return it->second;
    }
    
    uint64_t entity_id = next_entity_id_++;
    session_to_entity_[session_id] = entity_id;
    entity_to_session_[entity_id] = session_id;
    
    return entity_id;
}

void BroadcastGrid::AddSession(uint64_t session_id, const glm::vec3& position) {
    uint64_t entity_id = GetOrCreateEntityId(session_id);
    AddEntity(entity_id, position, 0.0f); // Sessions don't have collision radius
}

void BroadcastGrid::UpdateSession(uint64_t session_id, const glm::vec3& position) {
    auto it = session_to_entity_.find(session_id);
    if (it != session_to_entity_.end()) {
        UpdateEntity(it->second, position);
    }
}

void BroadcastGrid::RemoveSession(uint64_t session_id) {
    auto it = session_to_entity_.find(session_id);
    if (it != session_to_entity_.end()) {
        RemoveEntity(it->second);
        entity_to_session_.erase(it->second);
        session_to_entity_.erase(it);
    }
}

std::vector<uint64_t> BroadcastGrid::GetSessionsInRadius(const glm::vec3& position, float radius) const {
    std::vector<uint64_t> sessions;
    auto entities = GetEntitiesInRadius(position, radius);
    
    for (uint64_t entity_id : entities) {
        auto it = entity_to_session_.find(entity_id);
        if (it != entity_to_session_.end()) {
            sessions.push_back(it->second);
        }
    }
    
    return sessions;
}

void BroadcastGrid::AddSessions(const std::unordered_map<uint64_t, glm::vec3>& sessions) {
    LockWrite();
    
    for (const auto& [session_id, position] : sessions) {
        uint64_t entity_id = GetOrCreateEntityId(session_id);
        entity_to_cell_[entity_id] = PositionToCell(position);
        entity_info_[entity_id] = {position, 0.0f};
        cell_to_entities_[PositionToCell(position)].insert(entity_id);
    }
    
    UnlockWrite();
}

void BroadcastGrid::UpdateSessions(const std::unordered_map<uint64_t, glm::vec3>& sessions) {
    LockWrite();
    
    for (const auto& [session_id, position] : sessions) {
        auto entity_it = session_to_entity_.find(session_id);
        if (entity_it != session_to_entity_.end()) {
            UpdateEntity(entity_it->second, position);
        }
    }
    
    UnlockWrite();
}
