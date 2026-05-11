#include "game/CollisionSystem.hpp"

static constexpr float EPSILON = 1e-6f;
static constexpr float INV_EPSILON = 1e6f;

float distance_between_vec3(glm::vec3 pos0, glm::vec3 pos1)
{
    glm::vec3 delta = pos0 - pos1;
    return glm::dot(delta, delta);
}

size_t GridCell::GetMemoryUsage() const { return entities.size() * sizeof(uint64_t); }

bool GridCellKey::operator==(const GridCellKey& other) const {
    return x == other.x && y == other.y && z == other.z;
}

size_t GridCellHash::operator()(const GridCellKey& key) const {
    size_t h1 = std::hash<int>{}(key.x);
    size_t h2 = std::hash<int>{}(key.y);
    size_t h3 = std::hash<int>{}(key.z);
    return h1 ^ (h2 << 1) ^ (h3 << 2);
}

bool BoundingSphere::IsValid() const {
    return radius >= 0.0f &&
    std::isfinite(center.x) && std::isfinite(center.y) && std::isfinite(center.z);
}

bool BoundingSphere::Intersects(const BoundingSphere& other) const {
    if (!IsValid() || !other.IsValid()) return false;
    float distanceSquared = distance_between_vec3(center, other.center);
    float radiusSum = radius + other.radius;
    return distanceSquared <= (radiusSum * radiusSum);
}

bool BoundingSphere::IntersectsRay(const glm::vec3& origin, const glm::vec3& direction, float& distance) const {
    if (!IsValid()) return false;
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

bool BoundingBox::IsValid() const {
    return std::isfinite(min.x) && std::isfinite(min.y) && std::isfinite(min.z) &&
    std::isfinite(max.x) && std::isfinite(max.y) && std::isfinite(max.z) &&
    glm::all(glm::lessThanEqual(min, max));
}

bool BoundingBox::Intersects(const BoundingBox& other) const {
    if (!IsValid() || !other.IsValid()) return false;
    return (min.x <= other.max.x && max.x >= other.min.x) &&
           (min.y <= other.max.y && max.y >= other.min.y) &&
           (min.z <= other.max.z && max.z >= other.min.z);
}

bool BoundingBox::IntersectsSphere(const glm::vec3& center, float radius) const {
    if (!IsValid() || radius < 0.0f) return false;
    float closestX = std::clamp(center.x, min.x, max.x);
    float closestY = std::clamp(center.y, min.y, max.y);
    float closestZ = std::clamp(center.z, min.z, max.z);
    float distanceSquared =
        (center.x - closestX) * (center.x - closestX) +
        (center.y - closestY) * (center.y - closestY) +
        (center.z - closestZ) * (center.z - closestZ);
    return distanceSquared <= (radius * radius);
}

glm::vec3 BoundingBox::GetCenter() const {
    return (min + max) * 0.5f;
}

float BoundingBox::GetRadius() const {
    glm::vec3 halfExtents = (max - min) * 0.5f;
    return glm::length(halfExtents);
}

CollisionSystem::CollisionSystem() {
    spatialGrid_.clear();
    spatialGrid_.reserve(1024);
}

void CollisionSystem::SetGridCellSize(float size) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (size < 0.1f) size = 0.1f;
    if (size > 100.0f) size = 100.0f;
    gridCellSize_ = size;
    spatialGrid_.clear();
    for (const auto& [entityId, entity] : entities_) {
        GridCellKey key = GetGridKey(entity.bounds.center);
        spatialGrid_[key].entities.insert(entityId);
    }
}

void CollisionSystem::SetWorldBounds(const BoundingBox& bounds) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (bounds.IsValid()) {
        worldBounds_ = bounds;
    }
}

bool CollisionSystem::RegisterEntity(uint64_t entityId, const BoundingSphere& bounds, CollisionType type, bool isStatic) {
    if (entityId == 0) return false; // 0 is reserved for world
    if (!ValidateBounds(bounds)) return false;
    if (!ValidatePosition(bounds.center)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (entities_.find(entityId) != entities_.end()) {
        return false;
    }
    CollisionEntity entity;
    entity.id = entityId;
    entity.bounds = bounds;
    entity.type = type;
    entity.isStatic = isStatic;
    entity.isValid = true;
    entity.previousPosition = bounds.center;
    entities_[entityId] = entity;
    GridCellKey key = GetGridKey(bounds.center);
    spatialGrid_[key].entities.insert(entityId);
    return true;
}

bool CollisionSystem::UpdateEntity(uint64_t entityId, const glm::vec3& position) {
    if (!ValidatePosition(position)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entities_.find(entityId);
    if (it == entities_.end() || !it->second.isValid) {
        return false;
    }
    if (it->second.isStatic) {
        return true;
    }
    GridCellKey oldKey = GetGridKey(it->second.previousPosition);
    RemoveFromGrid(entityId, oldKey);
    it->second.bounds.center = position;
    GridCellKey newKey = GetGridKey(position);
    AddToGrid(entityId, newKey);
    return true;
}

bool CollisionSystem::UnregisterEntity(uint64_t entityId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entities_.find(entityId);
    if (it == entities_.end()) {
        return false;
    }
    GridCellKey key = GetGridKey(it->second.bounds.center);
    RemoveFromGrid(entityId, key);
    entities_.erase(it);
    if (entities_.empty() || entities_.size() % 100 == 0) {
        CleanEmptyGridCells();
    }
    return true;
}

const BoundingSphere* CollisionSystem::GetEntityBounds(uint64_t entityId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entities_.find(entityId);
    if (it == entities_.end() || !it->second.isValid) {
        return nullptr;
    }
    return &it->second.bounds;
}

bool CollisionSystem::IsEntityRegistered(uint64_t entityId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entities_.find(entityId) != entities_.end();
}

void CollisionSystem::RegisterChunk(const WorldChunk& chunk) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t chunk_id = CalculateChunkId(chunk.GetChunkX(), chunk.GetChunkZ());
    if (chunks_.find(chunk_id) != chunks_.end()) {
        return;
    }
    CollisionChunk collisionChunk;
    collisionChunk.chunkX = chunk.GetChunkX();
    collisionChunk.chunkZ = chunk.GetChunkZ();
    collisionChunk.chunk_id = chunk_id;
    glm::vec3 worldPos = chunk.GetWorldPosition();
    collisionChunk.bounds.min = worldPos;
    collisionChunk.bounds.max = worldPos + glm::vec3(WorldChunk::CHUNK_WIDTH, 100.0f, WorldChunk::CHUNK_WIDTH);
    if (!collisionChunk.bounds.IsValid()) {
        return;
    }
    BuildChunkCollisionData(collisionChunk, chunk);
    chunks_[chunk_id] = std::move(collisionChunk);
}

void CollisionSystem::UnregisterChunk(int chunkX, int chunkZ) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t chunk_id = CalculateChunkId(chunkX, chunkZ);
    chunks_.erase(chunk_id);
}

void CollisionSystem::ClearAllChunks() {
    std::lock_guard<std::mutex> lock(mutex_);
    chunks_.clear();
}

CollisionResult CollisionSystem::CheckCollision(const glm::vec3& position, float radius, uint64_t excludeId) {
    if (!ValidatePosition(position) || radius < 0.0f) {
        return CollisionResult(); // Invalid input
    }
    std::lock_guard<std::mutex> lock(mutex_);
    CollisionResult result;
    BoundingSphere testSphere{position, radius};
    if (!testSphere.IsValid()) {
        return result;
    }
    for (const auto& [entityId, entity] : entities_) {
        if (entityId == excludeId || !entity.isValid) continue;
        if (TestSphereSphere(testSphere, entity.bounds, result)) {
            result.collided = true;
            result.collided_id = entityId;
            result.type = entity.type;
            return result;
        }
    }
    for (const auto& [chunk_id, chunk] : chunks_) {
        if (!chunk.bounds.IntersectsSphere(position, radius)) {
            continue;
        }
        if (TestSphereBox(testSphere, chunk.bounds, result)) {
            result.collided = true;
            result.collided_id = 0; // World collision
            result.type = CollisionType::WORLD;
            result.chunk_id = chunk_id;
            if (chunk.hasCollisionData) {
                bool detailedCollision = false;
                CollisionResult detailedResult;
                for (const auto& tri : chunk.triangles) {
                    if (tri[0] < chunk.vertices.size() &&
                        tri[1] < chunk.vertices.size() &&
                        tri[2] < chunk.vertices.size()) {
                        const glm::vec3& v0 = chunk.vertices[tri[0]];
                        const glm::vec3& v1 = chunk.vertices[tri[1]];
                        const glm::vec3& v2 = chunk.vertices[tri[2]];
                        if (TestSphereTriangle(position, radius, v0, v1, v2, detailedResult)) {
                            if (!detailedCollision || detailedResult.penetration < result.penetration) {
                                detailedCollision = true;
                                result = detailedResult;
                                result.collided_id = 0;
                                result.type = CollisionType::WORLD;
                                result.chunk_id = chunk_id;
                            }
                        }
                    }
                }
            }
            return result;
        }
    }
    return result;
}

bool CollisionSystem::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                             RaycastHit& hit, uint64_t excludeId) {
    if (!ValidatePosition(origin) || maxDistance <= 0.0f) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    glm::vec3 normalizedDir = glm::normalize(direction);
    float closestDistance = maxDistance;
    bool foundHit = false;

    for (const auto& [entityId, entity] : entities_) {
        if (entityId == excludeId || !entity.isValid) continue;

        float distance;
        if (entity.bounds.IntersectsRay(origin, normalizedDir, distance) &&
            distance < closestDistance && distance >= 0.0f) {

            hit.hit = true;
            hit.point = origin + normalizedDir * distance;
            hit.normal = glm::normalize(hit.point - entity.bounds.center);
            hit.distance = distance;
            hit.entity_id = entityId;
            closestDistance = distance;
            foundHit = true;
        }
    }

    for (const auto& [chunk_id, chunk] : chunks_) {
        float tMin, tMax;
        if (!TestRayAABB(origin, normalizedDir, chunk.bounds, tMin, tMax)) {
            continue;
        }

        if (tMin < 0.0f) tMin = tMax;
        if (tMin < 0.0f || tMin > closestDistance) {
            continue;
        }

        if (chunk.hasCollisionData) {
            bool chunkHit = false;
            float chunkDistance = closestDistance;
            glm::vec3 chunkNormal;

            for (const auto& tri : chunk.triangles) {
                if (tri[0] >= chunk.vertices.size() ||
                    tri[1] >= chunk.vertices.size() ||
                    tri[2] >= chunk.vertices.size()) {
                    continue;
                }

                float t;
                glm::vec3 normal;
                const glm::vec3& v0 = chunk.vertices[tri[0]];
                const glm::vec3& v1 = chunk.vertices[tri[1]];
                const glm::vec3& v2 = chunk.vertices[tri[2]];

                if (TestRayTriangle(origin, normalizedDir, v0, v1, v2, t, normal) &&
                    t < chunkDistance && t >= 0.0f) {
                    chunkHit = true;
                    chunkDistance = t;
                    chunkNormal = normal;
                }
            }

            if (chunkHit) {
                hit.hit = true;
                hit.point = origin + normalizedDir * chunkDistance;
                hit.normal = chunkNormal;
                hit.distance = chunkDistance;
                hit.chunk_id = chunk_id;
                closestDistance = chunkDistance;
                foundHit = true;
            }
        } else {
            hit.hit = true;
            hit.point = origin + normalizedDir * tMin;
            hit.distance = tMin;
            hit.chunk_id = chunk_id;

            glm::vec3 center = chunk.bounds.GetCenter();
            glm::vec3 pointInBox = hit.point - center;
            glm::vec3 halfSize = (chunk.bounds.max - chunk.bounds.min) * 0.5f;

            glm::vec3 normal(0.0f);
            float minPenetration = FLT_MAX;

            for (int i = 0; i < 3; i++) {
                float distToMin = std::abs(pointInBox[i] - (-halfSize[i]));
                float distToMax = std::abs(pointInBox[i] - halfSize[i]);

                if (distToMin < minPenetration) {
                    minPenetration = distToMin;
                    normal = glm::vec3(0.0f);
                    normal[i] = -1.0f;
                }
                if (distToMax < minPenetration) {
                    minPenetration = distToMax;
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
    if (!ValidatePosition(position) || radius < 0.0f) {
        return {};
    }

    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<uint64_t> entitiesInRadius;
    BoundingSphere testSphere{position, radius};

    if (!testSphere.IsValid()) {
        return entitiesInRadius;
    }

    GridCellKey minKey = GetGridKey(position - glm::vec3(radius));
    GridCellKey maxKey = GetGridKey(position + glm::vec3(radius));

    std::unordered_set<uint64_t> processed;

    for (int x = minKey.x; x <= maxKey.x; x++) {
        for (int y = minKey.y; y <= maxKey.y; y++) {
            for (int z = minKey.z; z <= maxKey.z; z++) {
                GridCellKey cellKey{x, y, z};
                auto it = spatialGrid_.find(cellKey);
                if (it == spatialGrid_.end()) continue;

                for (uint64_t entityId : it->second.entities) {
                    if (processed.find(entityId) != processed.end()) continue;

                    auto entityIt = entities_.find(entityId);
                    if (entityIt != entities_.end() && entityIt->second.isValid) {
                        if (testSphere.Intersects(entityIt->second.bounds)) {
                            entitiesInRadius.push_back(entityId);
                            processed.insert(entityId);
                        }
                    }
                }
            }
        }
    }

    return entitiesInRadius;
}

void CollisionSystem::UpdateBroadPhase() {
    std::lock_guard<std::mutex> lock(mutex_);
    CleanEmptyGridCells();
}

void CollisionSystem::Update(float deltaTime) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [id, entity] : entities_) {
        if (!entity.isValid) continue;
        if (entity.isStatic) continue;

        glm::vec3& current = entity.bounds.center;
        glm::vec3& previous = entity.previousPosition;

        if (distance_between_vec3(current, previous) > EPSILON) {
            GridCellKey oldKey = GetGridKey(previous);
            RemoveFromGrid(id, oldKey);

            glm::vec3 minPos = glm::min(previous, current) - entity.bounds.radius;
            glm::vec3 maxPos = glm::max(previous, current) + entity.bounds.radius;

            GridCellKey minKey = GetGridKey(minPos);
            GridCellKey maxKey = GetGridKey(maxPos);

            for (int x = minKey.x; x <= maxKey.x; ++x) {
                for (int y = minKey.y; y <= maxKey.y; ++y) {
                    for (int z = minKey.z; z <= maxKey.z; ++z) {
                        GridCellKey key{x, y, z};
                        spatialGrid_[key].entities.insert(id);
                    }
                }
            }

            entity.previousPosition = current;
        }
    }

    PerformContinuousCollisionDetection(deltaTime);

    CleanEmptyGridCells();
}

size_t CollisionSystem::GetEntityCount() const { return entities_.size(); }

size_t CollisionSystem::GetChunkCount() const { return chunks_.size(); }

size_t CollisionSystem::GetGridCellCount() const { return spatialGrid_.size(); }

std::vector<std::pair<uint64_t, uint64_t>> CollisionSystem::GetPotentialCollisions() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::pair<uint64_t, uint64_t>> potentialCollisions;

    for (const auto& [cellKey, cell] : spatialGrid_) {
        if (cell.entities.size() < 2) continue;

        std::vector<uint64_t> entities(cell.entities.begin(), cell.entities.end());

        for (size_t i = 0; i < entities.size(); i++) {
            auto entityA = entities_.find(entities[i]);
            if (entityA == entities_.end() || !entityA->second.isValid) continue;

            for (size_t j = i + 1; j < entities.size(); j++) {
                auto entityB = entities_.find(entities[j]);
                if (entityB == entities_.end() || !entityB->second.isValid) continue;

                if (entityA->second.isStatic && entityB->second.isStatic) {
                    continue;
                }

                if (entityA->second.bounds.Intersects(entityB->second.bounds)) {
                    potentialCollisions.emplace_back(entities[i], entities[j]);
                }
            }
        }
    }

    return potentialCollisions;
}

GridCellKey CollisionSystem::GetGridKey(const glm::vec3& position) const {
    int gridX = static_cast<int>(std::floor(position.x / gridCellSize_));
    int gridY = static_cast<int>(std::floor(position.y / gridCellSize_));
    int gridZ = static_cast<int>(std::floor(position.z / gridCellSize_));
    return GridCellKey{gridX, gridY, gridZ};
}

void CollisionSystem::RemoveFromGrid(uint64_t entityId, const GridCellKey& oldKey) {
    auto it = spatialGrid_.find(oldKey);
    if (it != spatialGrid_.end()) {
        it->second.entities.erase(entityId);
    }
}

void CollisionSystem::AddToGrid(uint64_t entityId, const GridCellKey& newKey) {
    spatialGrid_[newKey].entities.insert(entityId);
}

void CollisionSystem::CleanEmptyGridCells() {
    for (auto it = spatialGrid_.begin(); it != spatialGrid_.end(); ) {
        if (it->second.entities.empty()) {
            it = spatialGrid_.erase(it);
        } else {
            ++it;
        }
    }
}

bool CollisionSystem::TestSphereSphere(const BoundingSphere& a, const BoundingSphere& b, CollisionResult& result) const {
    if (!a.IsValid() || !b.IsValid()) return false;

    glm::vec3 delta = b.center - a.center;
    float distanceSquared = glm::dot(delta, delta);
    float radiusSum = a.radius + b.radius;

    if (distanceSquared <= radiusSum * radiusSum) {
        float distance = std::sqrt(distanceSquared);
        if (distance > EPSILON) {
            result.resolution = delta * (radiusSum - distance) / distance;
            result.penetration = radiusSum - distance;
        } else {
            // Spheres are exactly overlapping
            result.resolution = glm::vec3(0.0f, 1.0f, 0.0f) * radiusSum;
            result.penetration = radiusSum;
        }
        return true;
    }

    return false;
}

bool CollisionSystem::TestSphereBox(const BoundingSphere& sphere, const BoundingBox& box, CollisionResult& result) const {
    if (!sphere.IsValid() || !box.IsValid()) return false;

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
        if (distance > EPSILON) {
            result.resolution = delta * (sphere.radius - distance) / distance;
            result.penetration = sphere.radius - distance;
        } else {
            // Sphere center is inside the box
            // Find the minimum penetration axis
            glm::vec3 boxCenter = box.GetCenter();
            glm::vec3 toCenter = sphere.center - boxCenter;
            glm::vec3 halfSize = (box.max - box.min) * 0.5f;

            // Calculate penetration on each axis
            glm::vec3 penetrations;
            penetrations.x = sphere.radius + halfSize.x - std::abs(toCenter.x);
            penetrations.y = sphere.radius + halfSize.y - std::abs(toCenter.y);
            penetrations.z = sphere.radius + halfSize.z - std::abs(toCenter.z);

            // Find the minimum positive penetration
            float minPenetration = penetrations.x;
            glm::vec3 normal = glm::vec3((toCenter.x > 0) ? 1.0f : -1.0f, 0.0f, 0.0f);

            if (penetrations.y < minPenetration) {
                minPenetration = penetrations.y;
                normal = glm::vec3(0.0f, (toCenter.y > 0) ? 1.0f : -1.0f, 0.0f);
            }

            if (penetrations.z < minPenetration) {
                minPenetration = penetrations.z;
                normal = glm::vec3(0.0f, 0.0f, (toCenter.z > 0) ? 1.0f : -1.0f);
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
    // Check for degenerate triangle
    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;
    glm::vec3 normal = glm::cross(edge1, edge2);

    float areaSquared = glm::dot(normal, normal);
    if (areaSquared < EPSILON) {
        return false; // Degenerate triangle
    }

    // Normalize the normal
    normal = glm::normalize(normal);

    // Calculate distance from sphere center to triangle plane
    glm::vec3 toSphere = sphereCenter - v0;
    float distanceToPlane = glm::dot(toSphere, normal);

    // Project sphere center onto triangle plane
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
    if (std::abs(denom) < EPSILON) {
        return false; // Degenerate triangle
    }

    v = (d11 * d20 - d01 * d21) / denom;
    w = (d00 * d21 - d01 * d20) / denom;
    u = 1.0f - v - w;

    // Check if point is inside triangle
    if (u >= -EPSILON && v >= -EPSILON && w >= -EPSILON) {
        // Point is inside triangle (or on edge)
        float signedDistance = distanceToPlane;
        float absoluteDistance = std::abs(signedDistance);

        if (absoluteDistance <= radius) {
            result.resolution = normal * (radius - absoluteDistance) * (signedDistance > 0 ? 1.0f : -1.0f);
            result.penetration = radius - absoluteDistance;
            return true;
        }
    }

    // Point is outside triangle, check against edges and vertices
    float minDistanceSquared = std::numeric_limits<float>::max();
    glm::vec3 closestPoint;

    // Check edges
    auto checkEdge = [&](const glm::vec3& a, const glm::vec3& b) {
        glm::vec3 ab = b - a;
        glm::vec3 ap = sphereCenter - a;
        float t = glm::dot(ap, ab) / glm::dot(ab, ab);
        t = std::clamp(t, 0.0f, 1.0f);
        glm::vec3 closest = a + ab * t;
        float distSq = distance_between_vec3(sphereCenter, closest);
        if (distSq < minDistanceSquared) {
            minDistanceSquared = distSq;
            closestPoint = closest;
        }
    };

    checkEdge(v0, v1);
    checkEdge(v1, v2);
    checkEdge(v2, v0);

    // Check vertices
    auto checkVertex = [&](const glm::vec3& vertex) {
        float distSq = distance_between_vec3(sphereCenter, vertex);
        if (distSq < minDistanceSquared) {
            minDistanceSquared = distSq;
            closestPoint = vertex;
        }
    };

    checkVertex(v0);
    checkVertex(v1);
    checkVertex(v2);

    float minDistance = std::sqrt(minDistanceSquared);
    if (minDistance <= radius) {
        glm::vec3 delta = sphereCenter - closestPoint;
        if (minDistance > EPSILON) {
            result.resolution = delta * (radius - minDistance) / minDistance;
        } else {
            result.resolution = normal * radius;
        }
        result.penetration = radius - minDistance;
        return true;
    }

    return false;
}

bool CollisionSystem::TestRayAABB(const glm::vec3& origin, const glm::vec3& dir,
                                 const BoundingBox& box, float& tMin, float& tMax) const {
    if (!box.IsValid()) return false;

    tMin = 0.0f;
    tMax = std::numeric_limits<float>::max();

    for (int i = 0; i < 3; i++) {
        if (std::abs(dir[i]) < EPSILON) {
            // Ray is parallel to this slab
            if (origin[i] < box.min[i] || origin[i] > box.max[i]) {
                return false;
            }
        } else {
            float invDir = 1.0f / dir[i];
            float t1 = (box.min[i] - origin[i]) * invDir;
            float t2 = (box.max[i] - origin[i]) * invDir;

            if (t1 > t2) std::swap(t1, t2);

            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);

            if (tMin > tMax) return false;
        }
    }

    return true;
}

bool CollisionSystem::TestRayTriangle(const glm::vec3& origin, const glm::vec3& dir,
                                     const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                                     float& t, glm::vec3& normal) const {
    // Möller–Trumbore algorithm
    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;

    glm::vec3 pvec = glm::cross(dir, edge2);
    float det = glm::dot(edge1, pvec);

    // Ray and triangle are parallel if det is close to 0
    if (std::abs(det) < EPSILON) {
        return false;
    }

    float invDet = 1.0f / det;

    glm::vec3 tvec = origin - v0;
    float u = glm::dot(tvec, pvec) * invDet;

    if (u < 0.0f || u > 1.0f) {
        return false;
    }

    glm::vec3 qvec = glm::cross(tvec, edge1);
    float v = glm::dot(dir, qvec) * invDet;

    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }

    t = glm::dot(edge2, qvec) * invDet;

    if (t < EPSILON) {
        return false; // Intersection behind ray origin
    }

    normal = glm::normalize(glm::cross(edge1, edge2));
    return true;
}

bool CollisionSystem::ValidatePosition(const glm::vec3& position) const {
    // Check for NaN and infinite values
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
        return false;
    }

    // Check if within world bounds
    if (!worldBounds_.IntersectsSphere(position, 0.0f)) {
        return false;
    }

    return true;
}

bool CollisionSystem::ValidateBounds(const BoundingSphere& bounds) const {
    if (!bounds.IsValid()) return false;
    return ValidatePosition(bounds.center) && bounds.radius >= 0.0f;
}

bool CollisionSystem::ValidateBounds(const BoundingBox& bounds) const {
    return bounds.IsValid();
}

uint64_t CollisionSystem::CalculateChunkId(int chunkX, int chunkZ) const {
    // Encode both coordinates into a single 64-bit integer
    // Using 32 bits for each coordinate
    uint64_t x = static_cast<uint32_t>(chunkX);
    uint64_t z = static_cast<uint32_t>(chunkZ);

    return (x << 32) | z;
}

void CollisionSystem::BuildChunkCollisionData(CollisionChunk& chunk, const WorldChunk& worldChunk) {
    const auto& collisionVerts = worldChunk.GetCollisionVertices();
    const auto& collisionTris = worldChunk.GetCollisionTriangles();

    if (collisionVerts.empty() || collisionTris.empty()) {
        chunk.hasCollisionData = false;
        return;
    }

    chunk.hasCollisionData = true;
    chunk.vertices = collisionVerts;
    chunk.triangles.reserve(collisionTris.size());

    // Convert triangle indices
    for (const auto& tri : collisionTris) {
        if (tri.v0 < collisionVerts.size() &&
            tri.v1 < collisionVerts.size() &&
            tri.v2 < collisionVerts.size()) {
            chunk.triangles.push_back({tri.v0, tri.v1, tri.v2});
        }
    }

    // Optional: Build spatial acceleration structure (BVH, Octree) here
    // For now, we just store the raw triangle data
}

void CollisionSystem::UpdateEntityInGrid(uint64_t entityId, const glm::vec3& oldPos, const glm::vec3& newPos) {
    std::lock_guard<std::mutex> lock(mutex_);  // Add lock

    auto entityIt = entities_.find(entityId);
    if (entityIt == entities_.end() || !entityIt->second.isValid) {
        return;
    }

    GridCellKey oldKey = GetGridKey(oldPos);
    RemoveFromGrid(entityId, oldKey);

    GridCellKey newKey = GetGridKey(newPos);
    AddToGrid(entityId, newKey);

    entityIt->second.bounds.center = newPos;
}

bool CollisionSystem::SweptSphereSphere(const glm::vec3& startA, const glm::vec3& endA, float radiusA,
                                        const glm::vec3& startB, const glm::vec3& endB, float radiusB,
                                        float& t) const {
    glm::vec3 va = endA - startA;
    glm::vec3 vb = endB - startB;
    glm::vec3 v = va - vb;          // relative velocity
    glm::vec3 s = startA - startB;  // relative start position

    float a = glm::dot(v, v);
    float b = 2.0f * glm::dot(s, v);
    float c = glm::dot(s, s) - (radiusA + radiusB) * (radiusA + radiusB);

    // Solve quadratic a*t^2 + b*t + c = 0
    float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) return false;

    float sqrtDisc = std::sqrt(disc);
    float t1 = (-b - sqrtDisc) / (2.0f * a);
    float t2 = (-b + sqrtDisc) / (2.0f * a);

    // We need the smallest t in [0,1]
    t = std::min(t1, t2);
    if (t < 0.0f) t = std::max(t1, t2);
    return (t >= 0.0f && t <= 1.0f);
}

void CollisionSystem::PerformContinuousCollisionDetection(float deltaTime) {
    (void)deltaTime;
    // Get all potential collision pairs (using the updated grid)
    auto pairs = GetPotentialCollisions();

    // We'll store the first impact for each entity (simplest: stop at first collision)
    std::unordered_map<uint64_t, float> firstImpact;
    std::unordered_map<uint64_t, glm::vec3> impactPositions;

    for (const auto& [idA, idB] : pairs) {
        auto& entA = entities_[idA];
        auto& entB = entities_[idB];
        if (!entA.isValid || !entB.isValid) continue;

        // Use previous positions as start, current as end
        glm::vec3 startA = entA.previousPosition;
        glm::vec3 endA   = entA.bounds.center;
        glm::vec3 startB = entB.previousPosition;
        glm::vec3 endB   = entB.bounds.center;

        float t;
        if (SweptSphereSphere(startA, endA, entA.bounds.radius,
                              startB, endB, entB.bounds.radius, t)) {
            // Collision occurs at fraction t of the movement
            // Record earliest impact for each entity
            if (firstImpact.find(idA) == firstImpact.end() || t < firstImpact[idA]) {
                firstImpact[idA] = t;
                impactPositions[idA] = startA + (endA - startA) * t;
            }
            if (firstImpact.find(idB) == firstImpact.end() || t < firstImpact[idB]) {
                firstImpact[idB] = t;
                impactPositions[idB] = startB + (endB - startB) * t;
            }
        }
    }

    // Apply position corrections (stop at first impact)
    for (auto& [id, t] : firstImpact) {
        auto& ent = entities_[id];
        ent.bounds.center = impactPositions[id];
        // Also update grid for corrected position (optional, but we'll leave as is for simplicity)
        // In a full solver you would re-run the CCD after adjustments.
    }
}
