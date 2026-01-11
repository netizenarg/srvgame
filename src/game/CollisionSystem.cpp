#include <algorithm>
#include <cmath>
#include <functional>

#include "../../include/game/CollisionSystem.hpp"

// BoundingSphere implementation
bool BoundingSphere::Intersects(const BoundingSphere& other) const {
    float distanceSquared = glm::distance2(center, other.center);
    float radiusSum = radius + other.radius;
    return distanceSquared <= (radiusSum * radiusSum);
}

bool BoundingSphere::IntersectsRay(const glm::vec3& origin, const glm::vec3& direction, float& distance) const {
    glm::vec3 oc = origin - center;
    float a = glm::dot(direction, direction);
    float b = 2.0f * glm::dot(oc, direction);
    float c = glm::dot(oc, oc) - radius * radius;
    
    float discriminant = b * b - 4 * a * c;
    
    if (discriminant < 0.0f) {
        return false;
    }
    
    float sqrtDiscriminant = std::sqrt(discriminant);
    float t1 = (-b - sqrtDiscriminant) / (2.0f * a);
    float t2 = (-b + sqrtDiscriminant) / (2.0f * a);
    
    if (t1 >= 0.0f) {
        distance = t1;
        return true;
    } else if (t2 >= 0.0f) {
        distance = t2;
        return true;
    }
    
    return false;
}

// BoundingBox implementation
bool BoundingBox::Intersects(const BoundingBox& other) const {
    return (min.x <= other.max.x && max.x >= other.min.x) &&
           (min.y <= other.max.y && max.y >= other.min.y) &&
           (min.z <= other.max.z && max.z >= other.min.z);
}

bool BoundingBox::IntersectsSphere(const glm::vec3& center, float radius) const {
    // Find the closest point on the box to the sphere center
    float closestX = std::clamp(center.x, min.x, max.x);
    float closestY = std::clamp(center.y, min.y, max.y);
    float closestZ = std::clamp(center.z, min.z, max.z);
    
    // Calculate distance between closest point and sphere center
    float distance = glm::distance(glm::vec3(closestX, closestY, closestZ), center);
    
    return distance <= radius;
}

glm::vec3 BoundingBox::GetCenter() const {
    return (min + max) * 0.5f;
}

float BoundingBox::GetRadius() const {
    glm::vec3 halfExtents = (max - min) * 0.5f;
    return glm::length(halfExtents);
}

// CollisionSystem implementation
CollisionSystem::CollisionSystem() {
    // Initialize spatial grid
    spatialGrid_.clear();
    gridCellSize_ = 10.0f;
}

void CollisionSystem::RegisterEntity(uint64_t entityId, const BoundingSphere& bounds, CollisionType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    CollisionEntity entity;
    entity.id = entityId;
    entity.bounds = bounds;
    entity.type = type;
    entity.isStatic = false; // By default, entities are dynamic
    
    entities_[entityId] = entity;
    
    // Add to spatial grid
    std::string gridKey = GetGridKey(bounds.center);
    spatialGrid_[gridKey].entities.insert(entityId);
}

void CollisionSystem::UpdateEntity(uint64_t entityId, const glm::vec3& position) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = entities_.find(entityId);
    if (it != entities_.end()) {
        // Remove from old grid cell
        std::string oldGridKey = GetGridKey(it->second.bounds.center);
        spatialGrid_[oldGridKey].entities.erase(entityId);
        
        // Update position
        it->second.bounds.center = position;
        
        // Add to new grid cell
        std::string newGridKey = GetGridKey(position);
        spatialGrid_[newGridKey].entities.insert(entityId);
    }
}

void CollisionSystem::UnregisterEntity(uint64_t entityId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = entities_.find(entityId);
    if (it != entities_.end()) {
        // Remove from spatial grid
        std::string gridKey = GetGridKey(it->second.bounds.center);
        spatialGrid_[gridKey].entities.erase(entityId);
        
        // Remove from entities map
        entities_.erase(it);
    }
}

void CollisionSystem::RegisterChunk(const WorldChunk& chunk) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Create collision chunk
    CollisionChunk collisionChunk;
    collisionChunk.chunkX = chunk.GetChunkX();
    collisionChunk.chunkZ = chunk.GetChunkZ();
    
    // Calculate bounding box for the chunk
    glm::vec3 worldPos = chunk.GetWorldPosition();
    collisionChunk.bounds.min = worldPos;
    collisionChunk.bounds.max = worldPos + glm::vec3(WorldChunk::CHUNK_WIDTH, 100.0f, WorldChunk::CHUNK_WIDTH);
    
    // Extract collision obstacles from chunk
    const auto& collisionVerts = chunk.GetCollisionVertices();
    const auto& collisionTris = chunk.GetCollisionTriangles();
    
    // For now, we'll create bounding spheres for each triangle
    // In a more optimized system, you'd use the actual triangle mesh
    for (const auto& tri : collisionTris) {
        if (tri.v0 < collisionVerts.size() && 
            tri.v1 < collisionVerts.size() && 
            tri.v2 < collisionVerts.size()) {
            
            glm::vec3 v0 = collisionVerts[tri.v0];
            glm::vec3 v1 = collisionVerts[tri.v1];
            glm::vec3 v2 = collisionVerts[tri.v2];
            
            // Calculate triangle center
            glm::vec3 center = (v0 + v1 + v2) / 3.0f;
            
            // Calculate bounding sphere radius
            float maxDist = std::max({
                glm::distance(center, v0),
                glm::distance(center, v1),
                glm::distance(center, v2)
            });
            
            BoundingSphere sphere;
            sphere.center = center;
            sphere.radius = maxDist;
            
            collisionChunk.obstacles.push_back(sphere);
        }
    }
    
    // Store the chunk
    std::string key = std::to_string(chunk.GetChunkX()) + "_" + std::to_string(chunk.GetChunkZ());
    chunks_[key] = collisionChunk;
}

void CollisionSystem::UnregisterChunk(int chunkX, int chunkZ) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string key = std::to_string(chunkX) + "_" + std::to_string(chunkZ);
    chunks_.erase(key);
}

CollisionResult CollisionSystem::CheckCollision(const glm::vec3& position, float radius, uint64_t excludeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    CollisionResult result;
    BoundingSphere testSphere{position, radius};
    
    // Check against entities
    for (const auto& [entityId, entity] : entities_) {
        if (entityId == excludeId) continue;
        
        if (TestSphereSphere(testSphere, entity.bounds, result)) {
            result.collided = true;
            result.collidedWith = entityId;
            result.type = entity.type;
            return result;
        }
    }
    
    // Check against world chunks
    for (const auto& [key, chunk] : chunks_) {
        // First check bounding box
        if (!chunk.bounds.IntersectsSphere(position, radius)) {
            continue;
        }
        
        // Then check individual obstacles
        for (const auto& obstacle : chunk.obstacles) {
            if (TestSphereSphere(testSphere, obstacle, result)) {
                result.collided = true;
                result.collidedWith = 0; // World collision
                result.type = CollisionType::WORLD;
                result.chunkId = std::stoull(key);
                return result;
            }
        }
    }
    
    return result;
}

bool CollisionSystem::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    glm::vec3 normalizedDir = glm::normalize(direction);
    float closestDistance = maxDistance;
    bool foundHit = false;
    
    // Check against entities
    for (const auto& [entityId, entity] : entities_) {
        float distance;
        if (entity.bounds.IntersectsRay(origin, normalizedDir, distance) && 
            distance < closestDistance && distance >= 0.0f) {
            
            hit.hit = true;
            hit.point = origin + normalizedDir * distance;
            hit.normal = glm::normalize(hit.point - entity.bounds.center);
            hit.distance = distance;
            hit.entityId = entityId;
            closestDistance = distance;
            foundHit = true;
        }
    }
    
    // Check against world chunks
    for (const auto& [key, chunk] : chunks_) {
        // Simple AABB ray test first
        // For simplicity, we'll just check against the bounding box
        // In a real implementation, you'd test against the actual geometry
        
        // Test against bounding box
        // This is a simplified ray-AABB test
        float tMin = 0.0f;
        float tMax = maxDistance;
        
        for (int i = 0; i < 3; i++) {
            float invD = 1.0f / normalizedDir[i];
            float t0 = (chunk.bounds.min[i] - origin[i]) * invD;
            float t1 = (chunk.bounds.max[i] - origin[i]) * invD;
            
            if (invD < 0.0f) std::swap(t0, t1);
            
            tMin = std::max(tMin, t0);
            tMax = std::min(tMax, t1);
            
            if (tMax <= tMin) break;
        }
        
        if (tMax > tMin && tMin < closestDistance) {
            hit.hit = true;
            hit.point = origin + normalizedDir * tMin;
            hit.distance = tMin;
            hit.chunkId = std::stoull(key);
            
            // Calculate normal
            glm::vec3 center = chunk.bounds.GetCenter();
            glm::vec3 pointInBox = hit.point - center;
            glm::vec3 halfSize = (chunk.bounds.max - chunk.bounds.min) * 0.5f;
            
            // Find which face was hit
            glm::vec3 normal(0.0f);
            float minDist = FLT_MAX;
            
            for (int i = 0; i < 3; i++) {
                float distToMin = std::abs(pointInBox[i] - (-halfSize[i]));
                float distToMax = std::abs(pointInBox[i] - halfSize[i]);
                
                if (distToMin < minDist) {
                    minDist = distToMin;
                    normal = glm::vec3(0.0f);
                    normal[i] = -1.0f;
                }
                if (distToMax < minDist) {
                    minDist = distToMax;
                    normal = glm::vec3(0.0f);
                    normal[i] = 1.0f;
                }
            }
            
            hit.normal = normal;
            closestDistance = tMin;
            foundHit = true;
        }
    }
    
    return foundHit;
}

std::vector<uint64_t> CollisionSystem::GetEntitiesInRadius(const glm::vec3& position, float radius) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<uint64_t> entitiesInRadius;
    BoundingSphere testSphere{position, radius};
    
    // Get potential entities from spatial grid cells that intersect the sphere
    // This is a simplified version - in reality you'd get all grid cells within radius
    std::string centerGridKey = GetGridKey(position);
    
    // Check surrounding grid cells (3x3x3 area)
    int gridRadius = static_cast<int>(std::ceil(radius / gridCellSize_));
    
    for (int dx = -gridRadius; dx <= gridRadius; dx++) {
        for (int dy = -gridRadius; dy <= gridRadius; dy++) {
            for (int dz = -gridRadius; dz <= gridRadius; dz++) {
                // Calculate grid cell offset
                glm::vec3 offset(dx * gridCellSize_, dy * gridCellSize_, dz * gridCellSize_);
                std::string gridKey = GetGridKey(position + offset);
                
                auto it = spatialGrid_.find(gridKey);
                if (it != spatialGrid_.end()) {
                    for (uint64_t entityId : it->second.entities) {
                        auto entityIt = entities_.find(entityId);
                        if (entityIt != entities_.end()) {
                            if (testSphere.Intersects(entityIt->second.bounds)) {
                                entitiesInRadius.push_back(entityId);
                            }
                        }
                    }
                }
            }
        }
    }
    
    return entitiesInRadius;
}

void CollisionSystem::UpdateBroadPhase() {
    // Update spatial grid and broad phase pairs
    // This method could be optimized, but for now we'll just update all entities
    // In a real system, you'd only update dynamic entities
}

std::vector<std::pair<uint64_t, uint64_t>> CollisionSystem::GetPotentialCollisions() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::pair<uint64_t, uint64_t>> potentialCollisions;
    
    // For each grid cell, check all entity pairs
    for (const auto& [gridKey, cell] : spatialGrid_) {
        if (cell.entities.size() < 2) continue;
        
        std::vector<uint64_t> entities(cell.entities.begin(), cell.entities.end());
        
        for (size_t i = 0; i < entities.size(); i++) {
            auto entityA = entities_.find(entities[i]);
            if (entityA == entities_.end()) continue;
            
            for (size_t j = i + 1; j < entities.size(); j++) {
                auto entityB = entities_.find(entities[j]);
                if (entityB == entities_.end()) continue;
                
                // Check if they might collide (broad phase using AABB)
                // For spheres, we can use the actual sphere test
                if (entityA->second.bounds.Intersects(entityB->second.bounds)) {
                    potentialCollisions.emplace_back(entities[i], entities[j]);
                }
            }
        }
    }
    
    return potentialCollisions;
}

// Private helper methods
std::string CollisionSystem::GetGridKey(const glm::vec3& position) const {
    int gridX = static_cast<int>(std::floor(position.x / gridCellSize_));
    int gridY = static_cast<int>(std::floor(position.y / gridCellSize_));
    int gridZ = static_cast<int>(std::floor(position.z / gridCellSize_));
    
    return std::to_string(gridX) + "_" + std::to_string(gridY) + "_" + std::to_string(gridZ);
}

bool CollisionSystem::TestSphereSphere(const BoundingSphere& a, const BoundingSphere& b, CollisionResult& result) const {
    glm::vec3 delta = b.center - a.center;
    float distanceSquared = glm::dot(delta, delta);
    float radiusSum = a.radius + b.radius;
    
    if (distanceSquared <= radiusSum * radiusSum) {
        float distance = std::sqrt(distanceSquared);
        if (distance > 0.0f) {
            result.resolution = delta * (radiusSum - distance) / distance;
            result.penetration = radiusSum - distance;
        } else {
            // Spheres are exactly overlapping
            result.resolution = glm::vec3(0.0f, b.radius, 0.0f);
            result.penetration = radiusSum;
        }
        return true;
    }
    
    return false;
}

bool CollisionSystem::TestSphereBox(const BoundingSphere& sphere, const BoundingBox& box, CollisionResult& result) const {
    // Find the closest point on the box to the sphere center
    glm::vec3 closestPoint;
    closestPoint.x = std::clamp(sphere.center.x, box.min.x, box.max.x);
    closestPoint.y = std::clamp(sphere.center.y, box.min.y, box.max.y);
    closestPoint.z = std::clamp(sphere.center.z, box.min.z, box.max.z);
    
    // Calculate distance between closest point and sphere center
    glm::vec3 delta = sphere.center - closestPoint;
    float distanceSquared = glm::dot(delta, delta);
    
    if (distanceSquared <= sphere.radius * sphere.radius) {
        float distance = std::sqrt(distanceSquared);
        if (distance > 0.0f) {
            result.resolution = delta * (sphere.radius - distance) / distance;
            result.penetration = sphere.radius - distance;
        } else {
            // Sphere center is inside the box
            // Push out in the direction of the nearest face
            glm::vec3 boxCenter = box.GetCenter();
            glm::vec3 toCenter = sphere.center - boxCenter;
            glm::vec3 halfSize = (box.max - box.min) * 0.5f;
            
            // Find the minimum penetration
            float minPenetration = sphere.radius + halfSize.x - std::abs(toCenter.x);
            glm::vec3 normal = glm::vec3(toCenter.x > 0 ? 1.0f : -1.0f, 0.0f, 0.0f);
            
            float penetrationY = sphere.radius + halfSize.y - std::abs(toCenter.y);
            if (penetrationY < minPenetration) {
                minPenetration = penetrationY;
                normal = glm::vec3(0.0f, toCenter.y > 0 ? 1.0f : -1.0f, 0.0f);
            }
            
            float penetrationZ = sphere.radius + halfSize.z - std::abs(toCenter.z);
            if (penetrationZ < minPenetration) {
                minPenetration = penetrationZ;
                normal = glm::vec3(0.0f, 0.0f, toCenter.z > 0 ? 1.0f : -1.0f);
            }
            
            result.resolution = normal * minPenetration;
            result.penetration = minPenetration;
        }
        return true;
    }
    
    return false;
}

bool CollisionSystem::TestSphereTriangle(const glm::vec3& sphereCenter, float radius,
                                         const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                                         CollisionResult& result) const {
    // Möller–Trumbore algorithm for closest point on triangle to sphere
    
    // Calculate triangle edges
    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;
    glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));
    
    // Calculate vector from vertex to sphere center
    glm::vec3 toSphere = sphereCenter - v0;
    
    // Calculate projection onto triangle plane
    float distanceToPlane = glm::dot(toSphere, normal);
    glm::vec3 projectedPoint = sphereCenter - normal * distanceToPlane;
    
    // Check if projected point is inside triangle using barycentric coordinates
    float u, v, w;
    glm::vec3 v0v1 = v1 - v0;
    glm::vec3 v0v2 = v2 - v0;
    glm::vec3 v0p = projectedPoint - v0;
    
    float d00 = glm::dot(v0v1, v0v1);
    float d01 = glm::dot(v0v1, v0v2);
    float d11 = glm::dot(v0v2, v0v2);
    float d20 = glm::dot(v0p, v0v1);
    float d21 = glm::dot(v0p, v0v2);
    
    float denom = d00 * d11 - d01 * d01;
    if (std::abs(denom) < 1e-6f) {
        // Degenerate triangle
        return false;
    }
    
    v = (d11 * d20 - d01 * d21) / denom;
    w = (d00 * d21 - d01 * d20) / denom;
    u = 1.0f - v - w;
    
    // Check if point is inside triangle
    if (u >= 0.0f && v >= 0.0f && w >= 0.0f) {
        // Point is inside triangle
        float distance = std::abs(distanceToPlane);
        if (distance <= radius) {
            result.resolution = normal * (radius - distance) * (distanceToPlane > 0 ? 1.0f : -1.0f);
            result.penetration = radius - distance;
            return true;
        }
    } else {
        // Point is outside triangle, check against edges
        // This is simplified - in a full implementation you'd check all three edges
        // and the three vertices
        float minDistance = FLT_MAX;
        glm::vec3 closestPoint;
        
        // Check edges
        auto checkEdge = [&](const glm::vec3& a, const glm::vec3& b) {
            glm::vec3 ab = b - a;
            glm::vec3 ap = sphereCenter - a;
            float t = glm::dot(ap, ab) / glm::dot(ab, ab);
            t = std::clamp(t, 0.0f, 1.0f);
            glm::vec3 closest = a + ab * t;
            float dist = glm::distance(sphereCenter, closest);
            if (dist < minDistance) {
                minDistance = dist;
                closestPoint = closest;
            }
        };
        
        checkEdge(v0, v1);
        checkEdge(v1, v2);
        checkEdge(v2, v0);
        
        // Check vertices
        auto checkVertex = [&](const glm::vec3& vertex) {
            float dist = glm::distance(sphereCenter, vertex);
            if (dist < minDistance) {
                minDistance = dist;
                closestPoint = vertex;
            }
        };
        
        checkVertex(v0);
        checkVertex(v1);
        checkVertex(v2);
        
        if (minDistance <= radius) {
            result.resolution = glm::normalize(sphereCenter - closestPoint) * (radius - minDistance);
            result.penetration = radius - minDistance;
            return true;
        }
    }
    
    return false;
}

void CollisionSystem::UpdateEntityInGrid(uint64_t entityId, const glm::vec3& oldPos, const glm::vec3& newPos) {
    // Remove from old grid cell
    std::string oldGridKey = GetGridKey(oldPos);
    auto oldIt = spatialGrid_.find(oldGridKey);
    if (oldIt != spatialGrid_.end()) {
        oldIt->second.entities.erase(entityId);
        if (oldIt->second.entities.empty()) {
            spatialGrid_.erase(oldIt);
        }
    }
    
    // Add to new grid cell
    std::string newGridKey = GetGridKey(newPos);
    spatialGrid_[newGridKey].entities.insert(entityId);
    
    // Update entity position in entity map
    auto entityIt = entities_.find(entityId);
    if (entityIt != entities_.end()) {
        entityIt->second.bounds.center = newPos;
    }
}
