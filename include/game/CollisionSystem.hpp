#pragma once

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "game/WorldChunk.hpp"

enum class CollisionType : uint8_t;

struct GridCell {
    std::unordered_set<uint64_t> entities;
    size_t GetMemoryUsage() const;
};

struct GridCellKey {
    int x, y, z;
    bool operator==(const GridCellKey& other) const;
};

struct GridCellHash {
    size_t operator()(const GridCellKey& key) const;
};

struct RaycastHit {
    bool hit = false;
    glm::vec3 point;
    glm::vec3 normal;
    float distance = 0.0f;
    uint64_t entity_id = 0;
    uint64_t chunk_id = 0;
};

enum class CollisionType : uint8_t {
    NONE,
    WORLD,
    ENTITY,
    TRIGGER
};

struct CollisionResult {
    bool collided = false;
    glm::vec3 resolution = glm::vec3(0.0f, 0.0f, 0.0f);
    float penetration = 0.0f;
    uint64_t collided_id = 0;
    uint64_t chunk_id = 0;
    CollisionType type = CollisionType::NONE;
};

struct BoundingSphere {
    glm::vec3 center;
    float radius;
    bool IsValid() const;
    bool Intersects(const BoundingSphere& other) const;
    bool IntersectsRay(const glm::vec3& origin, const glm::vec3& direction, float& distance) const;
};

struct BoundingBox {
    glm::vec3 min;
    glm::vec3 max;
    bool IsValid() const;
    bool Intersects(const BoundingBox& other) const;
    bool IntersectsSphere(const glm::vec3& center, float radius) const;
    glm::vec3 GetCenter() const;
    float GetRadius() const;
};

struct CollisionEntity {
    uint64_t id = 0;
    BoundingSphere bounds;
    CollisionType type = CollisionType::ENTITY;
    bool isStatic = false;
    bool isValid = false;
    glm::vec3 previousPosition;
};

struct CollisionChunk {
    int chunkX = 0;
    int chunkZ = 0;
    uint64_t chunk_id = 0;
    BoundingBox bounds;
    std::vector<glm::vec3> vertices;
    std::vector<std::array<uint32_t, 3>> triangles;
    bool hasCollisionData = false;
};

class CollisionSystem {
public:
    CollisionSystem();

    void SetGridCellSize(float size);
    void SetWorldBounds(const BoundingBox& bounds);

    bool RegisterEntity(uint64_t entityId, const BoundingSphere& bounds, CollisionType type = CollisionType::ENTITY, bool isStatic = false);
    bool UpdateEntity(uint64_t entityId, const glm::vec3& position);
    bool UnregisterEntity(uint64_t entityId);

    const BoundingSphere* GetEntityBounds(uint64_t entityId) const;
    bool IsEntityRegistered(uint64_t entityId) const;

    void RegisterChunk(const WorldChunk& chunk);
    void UnregisterChunk(int chunkX, int chunkZ);
    void ClearAllChunks();

    CollisionResult CheckCollision(const glm::vec3& position, float radius, uint64_t excludeId = 0);
    bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit, uint64_t excludeId = 0);
    std::vector<uint64_t> GetEntitiesInRadius(const glm::vec3& position, float radius);

    void UpdateBroadPhase();
    void Update(float deltaTime);
    std::vector<std::pair<uint64_t, uint64_t>> GetPotentialCollisions();

    size_t GetEntityCount() const;
    size_t GetChunkCount() const;
    size_t GetGridCellCount() const;

private:

    std::unordered_map<uint64_t, CollisionEntity> entities_;
    std::unordered_map<uint64_t, CollisionChunk> chunks_;

    float gridCellSize_ = 10.0f;
    std::unordered_map<GridCellKey, GridCell, GridCellHash> spatialGrid_;

    BoundingBox worldBounds_ = {
        glm::vec3(-10000.0f, -1000.0f, -10000.0f),
        glm::vec3(10000.0f, 1000.0f, 10000.0f)
    };

    mutable std::mutex mutex_;

    GridCellKey GetGridKey(const glm::vec3& position) const;
    void RemoveFromGrid(uint64_t entityId, const GridCellKey& oldKey);
    void AddToGrid(uint64_t entityId, const GridCellKey& newKey);
    void CleanEmptyGridCells();

    bool TestSphereSphere(const BoundingSphere& a, const BoundingSphere& b, CollisionResult& result) const;
    bool TestSphereBox(const BoundingSphere& sphere, const BoundingBox& box, CollisionResult& result) const;
    bool TestSphereTriangle(const glm::vec3& sphereCenter, float radius,
                           const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                           CollisionResult& result) const;
    bool TestRayAABB(const glm::vec3& origin, const glm::vec3& dir,
                    const BoundingBox& box, float& tMin, float& tMax) const;
    bool TestRayTriangle(const glm::vec3& origin, const glm::vec3& dir,
                        const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                        float& t, glm::vec3& normal) const;

    bool ValidatePosition(const glm::vec3& position) const;
    bool ValidateBounds(const BoundingSphere& bounds) const;
    bool ValidateBounds(const BoundingBox& bounds) const;

    uint64_t CalculateChunkId(int chunkX, int chunkZ) const;
    void BuildChunkCollisionData(CollisionChunk& chunk, const WorldChunk& worldChunk);

    void UpdateEntityInGrid(uint64_t entityId, const glm::vec3& oldPos, const glm::vec3& newPos);

    void PerformContinuousCollisionDetection(float deltaTime);
    bool SweptSphereSphere(const glm::vec3& startA, const glm::vec3& endA, float radiusA,
                           const glm::vec3& startB, const glm::vec3& endB, float radiusB,
                           float& t) const;
};
