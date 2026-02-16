#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <random>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include "logging/Logger.hpp"

enum class EntityType {
    PLAYER,
    NPC,
    ITEM,
    PROJECTILE,
    VEHICLE,
    ENVIRONMENT,
    COLLECTIBLE,
    CONTAINER,
    DOOR,
    TRAP,
    SPAWNER,
    DECORATION,
    PARTICLE,
    LIGHT,
    SOUND,
    TRIGGER,
    CHECKPOINT,
    WAYPOINT,
    ANY
};

class GameEntity : public std::enable_shared_from_this<GameEntity> {
public:
    GameEntity(EntityType type, const glm::vec3& position);
    virtual ~GameEntity();

    // Core properties
    EntityType GetType() const { return type_; }
    uint64_t GetId() const { return id_; }
    void SetId(uint64_t id) { id_ = id; }

    // Position and transformation
    const glm::vec3& GetPosition() const { return position_; }
    void SetPosition(const glm::vec3& position) { position_ = position; }
    void Translate(const glm::vec3& translation) { position_ += translation; }

    const glm::vec3& GetVelocity() const { return velocity_; }
    void SetVelocity(const glm::vec3& velocity) { velocity_ = velocity; }
    void AddForce(const glm::vec3& force) { velocity_ += force; }

    const glm::vec3& GetRotation() const { return rotation_; }
    void SetRotation(const glm::vec3& rotation) { rotation_ = rotation; }
    void Rotate(const glm::vec3& rotation) { rotation_ += rotation; }

    const glm::vec3& GetScale() const { return scale_; }
    void SetScale(const glm::vec3& scale) { scale_ = scale; }

    // Movement and physics
    void Move(float delta_time);
    void Stop();
    bool IsMoving() const { return glm::length(velocity_) > 0.01f; }
    float GetSpeed() const { return glm::length(velocity_); }

    // Bounding volumes
    struct BoundingBox {
        glm::vec3 min;
        glm::vec3 max;
        glm::vec3 center;
        glm::vec3 size;

        bool Contains(const glm::vec3& point) const;
        bool Intersects(const BoundingBox& other) const;
        float GetDistanceSquared(const glm::vec3& point) const;
    };

    struct BoundingSphere {
        glm::vec3 center;
        float radius;

        bool Contains(const glm::vec3& point) const;
        bool Intersects(const BoundingSphere& other) const;
        float GetDistanceSquared(const glm::vec3& point) const;
    };

    virtual BoundingBox GetBoundingBox() const;
    virtual BoundingSphere GetBoundingSphere() const;
    void UpdateBoundingVolumes();

    // State management
    bool IsActive() const { return active_; }
    void SetActive(bool active) { active_ = active; }

    bool IsVisible() const { return visible_; }
    void SetVisible(bool visible) { visible_ = visible; }

    bool IsCollidable() const { return collidable_; }
    void SetCollidable(bool collidable) { collidable_ = collidable; }

    bool IsPersistent() const { return persistent_; }
    void SetPersistent(bool persistent) { persistent_ = persistent; }

    // Health and combat
    float GetHealth() const { return health_; }
    float GetMaxHealth() const { return max_health_; }
    void SetHealth(float health);
    void SetMaxHealth(float max_health);
    void TakeDamage(float damage, uint64_t source_id = 0);
    void Heal(float amount, uint64_t source_id = 0);
    bool IsAlive() const { return health_ > 0.0f; }
    bool IsDead() const { return health_ <= 0.0f; }

    // Tags and categories
    void AddTag(const std::string& tag);
    void RemoveTag(const std::string& tag);
    bool HasTag(const std::string& tag) const;
    const std::vector<std::string>& GetTags() const { return tags_; }

    void SetCategory(const std::string& category) { category_ = category; }
    const std::string& GetCategory() const { return category_; }

    void SetName(const std::string& name) { name_ = name; }
    const std::string& GetName() const { return name_; }

    // Custom properties
    void SetProperty(const std::string& key, const nlohmann::json& value);
    nlohmann::json GetProperty(const std::string& key, const nlohmann::json& default_value = {}) const;
    bool HasProperty(const std::string& key) const;
    void RemoveProperty(const std::string& key);

    // Parent-child hierarchy
    void SetParent(std::shared_ptr<GameEntity> parent);
    std::shared_ptr<GameEntity> GetParent() const { return parent_.lock(); }
    void AddChild(std::shared_ptr<GameEntity> child);
    void RemoveChild(uint64_t child_id);
    const std::vector<std::shared_ptr<GameEntity>>& GetChildren() const { return children_; }
    bool HasChildren() const { return !children_.empty(); }

    // Local vs world transforms
    glm::vec3 GetWorldPosition() const;
    glm::vec3 GetWorldRotation() const;
    glm::vec3 GetWorldScale() const;

    // Update and lifecycle
    virtual void Update(float delta_time);
    virtual void FixedUpdate(float delta_time);
    virtual void LateUpdate(float delta_time);
    virtual void OnCreate();
    virtual void OnDestroy();
    virtual void OnCollision(std::shared_ptr<GameEntity> other);
    virtual void OnTriggerEnter(std::shared_ptr<GameEntity> other);
    virtual void OnTriggerExit(std::shared_ptr<GameEntity> other);

    // Serialization
    virtual nlohmann::json Serialize() const;
    virtual void Deserialize(const nlohmann::json& data);

    // Factory method
    static std::shared_ptr<GameEntity> CreateFromJson(const nlohmann::json& data);

    // Utility methods
    float GetDistanceSquared(const glm::vec3& point) const;
    float GetDistance(const glm::vec3& point) const;
    float GetDistanceSquared(std::shared_ptr<GameEntity> other) const;
    float GetDistance(std::shared_ptr<GameEntity> other) const;

    bool IsInRange(const glm::vec3& point, float range) const;
    bool IsInRange(std::shared_ptr<GameEntity> other, float range) const;

    void LookAt(const glm::vec3& target);
    void LookAt(std::shared_ptr<GameEntity> target);

    // Debug and logging
    void DumpInfo() const;
    std::string ToString() const;

    // Events and callbacks
    using EventToken = uint64_t;
    using EventCallback = std::function<void(std::shared_ptr<GameEntity>)>;
    // void SubscribeToEvent(const std::string& event_name, EventCallback callback);
    // void UnsubscribeFromEvent(const std::string& event_name, EventCallback callback);
    EventToken SubscribeToEvent(const std::string& event_name, EventCallback callback);
    void UnsubscribeFromEvent(EventToken token);
    void FireEvent(const std::string& event_name);

    // Static utilities
    static const char* EntityTypeToString(EntityType type);
    static EntityType StringToEntityType(const std::string& type_str);
    static uint64_t GenerateEntityId();

    // JSON
    nlohmann::json ToJson() const;
    nlohmann::json JsonGetPosition() const;
    nlohmann::json JsonGetAttribute(const std::string& key, const nlohmann::json& defaultValue = {}) const;
    void JsonSetAttribute(const std::string& key, const nlohmann::json& value);
    nlohmann::json JsonGetAttributes() const;

protected:
    EntityType type_;
    uint64_t id_;

    // Transform
    glm::vec3 position_;
    glm::vec3 rotation_;
    glm::vec3 scale_;
    glm::vec3 velocity_;
    glm::vec3 acceleration_;

    // State
    bool active_ = true;
    bool visible_ = true;
    bool collidable_ = true;
    bool persistent_ = false;

    // Health
    float health_ = 100.0f;
    float max_health_ = 100.0f;

    // Metadata
    std::string name_;
    std::string category_;
    std::vector<std::string> tags_;
    nlohmann::json properties_;

    // Hierarchy
    std::weak_ptr<GameEntity> parent_;
    std::vector<std::shared_ptr<GameEntity>> children_;

    // Bounding volumes
    BoundingBox bounding_box_;
    BoundingSphere bounding_sphere_;
    bool bounding_volumes_dirty_ = true;

    // Private methods
    void UpdateWorldTransform();
    void UpdateLocalTransform();
    void OnParentChanged();
    void OnChildAdded(std::shared_ptr<GameEntity> child);
    void OnChildRemoved(std::shared_ptr<GameEntity> child);

    // Static ID generator
    static std::atomic<uint64_t> next_entity_id_;

private:
    mutable std::shared_mutex mutex_;
    std::chrono::steady_clock::time_point last_movement_;

    // Events
    //std::unordered_map<std::string, std::vector<EventCallback>> event_callbacks_;
    std::unordered_map<std::string, std::vector<std::pair<EventToken, EventCallback>>> event_callbacks_;
    std::atomic<EventToken> next_token_{1};
};
