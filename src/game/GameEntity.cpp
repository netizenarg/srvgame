#include "game/GameEntity.hpp"

// =============== Static Initialization ===============
std::atomic<uint64_t> GameEntity::next_entity_id_(1);

// =============== BoundingBox Methods ===============
bool GameEntity::BoundingBox::Contains(const glm::vec3& point) const {
    return point.x >= min.x && point.x <= max.x &&
           point.y >= min.y && point.y <= max.y &&
           point.z >= min.z && point.z <= max.z;
}

bool GameEntity::BoundingBox::Intersects(const BoundingBox& other) const {
    return (min.x <= other.max.x && max.x >= other.min.x) &&
           (min.y <= other.max.y && max.y >= other.min.y) &&
           (min.z <= other.max.z && max.z >= other.min.z);
}

float GameEntity::BoundingBox::GetDistanceSquared(const glm::vec3& point) const {
    glm::vec3 closest_point;
    
    // Find closest point on box
    closest_point.x = std::max(min.x, std::min(point.x, max.x));
    closest_point.y = std::max(min.y, std::min(point.y, max.y));
    closest_point.z = std::max(min.z, std::min(point.z, max.z));
    
    glm::vec3 diff = point - closest_point;
    return glm::dot(diff, diff);
}

// =============== BoundingSphere Methods ===============
bool GameEntity::BoundingSphere::Contains(const glm::vec3& point) const {
    glm::vec3 diff = point - center;
    float distance_squared = glm::dot(diff, diff);
    return distance_squared <= (radius * radius);
}

bool GameEntity::BoundingSphere::Intersects(const BoundingSphere& other) const {
    glm::vec3 diff = center - other.center;
    float distance_squared = glm::dot(diff, diff);
    float radius_sum = radius + other.radius;
    return distance_squared <= (radius_sum * radius_sum);
}

float GameEntity::BoundingSphere::GetDistanceSquared(const glm::vec3& point) const {
    glm::vec3 diff = point - center;
    float distance_squared = glm::dot(diff, diff);
    return std::max(0.0f, distance_squared - (radius * radius));
}

// =============== Constructor and Destructor ===============
GameEntity::GameEntity(EntityType type, const glm::vec3& position)
    : type_(type),
      id_(GenerateEntityId()),
      position_(position),
      rotation_(0.0f, 0.0f, 0.0f),
      scale_(1.0f, 1.0f, 1.0f),
      velocity_(0.0f, 0.0f, 0.0f),
      acceleration_(0.0f, 0.0f, 0.0f) {
    
    // Set default name
    name_ = std::string(EntityTypeToString(type)) + "_" + std::to_string(id_);
    
    // Default category based on type
    switch (type_) {
        case EntityType::PLAYER: category_ = "player"; break;
        case EntityType::NPC: category_ = "npc"; break;
        case EntityType::ITEM: category_ = "item"; break;
        case EntityType::PROJECTILE: category_ = "projectile"; break;
        default: category_ = "misc"; break;
    }
    
    // Update bounding volumes
    UpdateBoundingVolumes();
    
    Logger::Debug("GameEntity created: {} (ID: {}) at [{:.1f}, {:.1f}, {:.1f}]",
                  name_, id_, position.x, position.y, position.z);
}

GameEntity::~GameEntity() {
    OnDestroy();
    
    // Remove from parent
    if (auto parent = parent_.lock()) {
        parent->RemoveChild(id_);
    }
    
    // Clear children
    children_.clear();
    
    Logger::Debug("GameEntity destroyed: {} (ID: {})", name_, id_);
}

// =============== Bounding Volume Methods ===============
GameEntity::BoundingBox GameEntity::GetBoundingBox() const {
    if (bounding_volumes_dirty_) {
        const_cast<GameEntity*>(this)->UpdateBoundingVolumes();
    }
    return bounding_box_;
}

GameEntity::BoundingSphere GameEntity::GetBoundingSphere() const {
    if (bounding_volumes_dirty_) {
        const_cast<GameEntity*>(this)->UpdateBoundingVolumes();
    }
    return bounding_sphere_;
}

void GameEntity::UpdateBoundingVolumes() {
    // Default bounding box size (1x1x1 unit cube centered at position)
    const float DEFAULT_SIZE = 1.0f;
    const float HALF_SIZE = DEFAULT_SIZE / 2.0f;
    
    bounding_box_.min = position_ - glm::vec3(HALF_SIZE);
    bounding_box_.max = position_ + glm::vec3(HALF_SIZE);
    bounding_box_.center = position_;
    bounding_box_.size = glm::vec3(DEFAULT_SIZE);
    
    // Bounding sphere that contains the box
    bounding_sphere_.center = position_;
    bounding_sphere_.radius = glm::length(bounding_box_.size) / 2.0f;
    
    bounding_volumes_dirty_ = false;
}

// =============== Health Management ===============
void GameEntity::SetHealth(float health) {
    health_ = std::clamp(health, 0.0f, max_health_);
}

void GameEntity::SetMaxHealth(float max_health) {
    max_health_ = std::max(1.0f, max_health);
    health_ = std::min(health_, max_health_);
}

void GameEntity::TakeDamage(float damage, uint64_t source_id) {
    if (!IsAlive() || damage <= 0.0f) return;
    
    float old_health = health_;
    SetHealth(health_ - damage);
    
    Logger::Debug("Entity {} took {} damage (health: {}/{}) from source {}",
                  id_, damage, health_, max_health_, source_id);
    
    // Fire damage event
    if (health_ < old_health) {
        FireEvent("on_damage_taken");
    }
    
    // Check for death
    if (IsDead()) {
        FireEvent("on_death");
        active_ = false;
    }
}

void GameEntity::Heal(float amount, uint64_t source_id) {
    if (!IsAlive() || amount <= 0.0f) return;
    
    float old_health = health_;
    SetHealth(health_ + amount);
    
    Logger::Debug("Entity {} healed for {} (health: {}/{}) from source {}",
                  id_, amount, health_, max_health_, source_id);
    
    if (health_ > old_health) {
        FireEvent("on_healed");
    }
}

// =============== Tag Management ===============
void GameEntity::AddTag(const std::string& tag) {
    if (!HasTag(tag)) {
        tags_.push_back(tag);
    }
}

void GameEntity::RemoveTag(const std::string& tag) {
    tags_.erase(std::remove(tags_.begin(), tags_.end(), tag), tags_.end());
}

bool GameEntity::HasTag(const std::string& tag) const {
    return std::find(tags_.begin(), tags_.end(), tag) != tags_.end();
}

// =============== Property Management ===============
void GameEntity::SetProperty(const std::string& key, const nlohmann::json& value) {
    properties_[key] = value;
}

nlohmann::json GameEntity::GetProperty(const std::string& key, const nlohmann::json& default_value) const {
    auto it = properties_.find(key);
    return it != properties_.end() ? it.value() : default_value;
}

bool GameEntity::HasProperty(const std::string& key) const {
    return properties_.find(key) != properties_.end();
}

void GameEntity::RemoveProperty(const std::string& key) {
    properties_.erase(key);
}

// =============== Hierarchy Management ===============
void GameEntity::SetParent(std::shared_ptr<GameEntity> parent) {
    // Remove from current parent
    if (auto old_parent = parent_.lock()) {
        old_parent->RemoveChild(id_);
    }
    
    // Set new parent
    if (parent) {
        parent_ = parent;
        parent->AddChild(shared_from_this());
    } else {
        parent_.reset();
    }
    
    OnParentChanged();
}

void GameEntity::AddChild(std::shared_ptr<GameEntity> child) {
    if (!child || child.get() == this) {
        return;
    }
    
    // Check if already a child
    auto it = std::find_if(children_.begin(), children_.end(),
        [child](const std::shared_ptr<GameEntity>& c) {
            return c->GetId() == child->GetId();
        });
    
    if (it == children_.end()) {
        children_.push_back(child);
        OnChildAdded(child);
    }
}

void GameEntity::RemoveChild(uint64_t child_id) {
    auto it = std::find_if(children_.begin(), children_.end(),
        [child_id](const std::shared_ptr<GameEntity>& child) {
            return child->GetId() == child_id;
        });
    
    if (it != children_.end()) {
        auto child = *it;
        children_.erase(it);
        OnChildRemoved(child);
    }
}

// =============== World Transforms ===============
glm::vec3 GameEntity::GetWorldPosition() const {
    if (auto parent = parent_.lock()) {
        glm::mat4 parent_transform = glm::translate(glm::mat4(1.0f), parent->GetWorldPosition());
        glm::vec3 local_pos = position_;
        glm::vec4 world_pos = parent_transform * glm::vec4(local_pos, 1.0f);
        return glm::vec3(world_pos);
    }
    return position_;
}

glm::vec3 GameEntity::GetWorldRotation() const {
    if (auto parent = parent_.lock()) {
        return parent->GetWorldRotation() + rotation_;
    }
    return rotation_;
}

glm::vec3 GameEntity::GetWorldScale() const {
    if (auto parent = parent_.lock()) {
        return parent->GetWorldScale() * scale_;
    }
    return scale_;
}

// =============== Update Methods ===============
void GameEntity::Update(float delta_time) {
    // Apply movement
    Move(delta_time);
    
    // Update bounding volumes if needed
    if (bounding_volumes_dirty_) {
        UpdateBoundingVolumes();
    }
    
    // Update children
    for (auto& child : children_) {
        if (child->IsActive()) {
            child->Update(delta_time);
        }
    }
}

void GameEntity::FixedUpdate(float delta_time) {
    // Physics update (can be overridden by derived classes)
    // Update children
    for (auto& child : children_) {
        if (child->IsActive()) {
            child->FixedUpdate(delta_time);
        }
    }
}

void GameEntity::LateUpdate(float delta_time) {
    // Post-update logic (can be overridden by derived classes)
    // Update children
    for (auto& child : children_) {
        if (child->IsActive()) {
            child->LateUpdate(delta_time);
        }
    }
}

void GameEntity::Move(float delta_time) {
    if (!IsMoving()) return;
    
    // Apply acceleration
    velocity_ += acceleration_ * delta_time;
    
    // Apply velocity to position
    position_ += velocity_ * delta_time;
    
    // Apply damping (simple friction)
    velocity_ *= 0.95f;
    
    // If velocity is very small, stop moving
    if (glm::length(velocity_) < 0.001f) {
        velocity_ = glm::vec3(0.0f);
        acceleration_ = glm::vec3(0.0f);
    }
    
    // Mark bounding volumes as dirty
    bounding_volumes_dirty_ = true;
}

void GameEntity::Stop() {
    velocity_ = glm::vec3(0.0f);
    acceleration_ = glm::vec3(0.0f);
}

// =============== Event Handlers ===============
void GameEntity::OnCreate() {
    Logger::Debug("Entity {} onCreate", id_);
    FireEvent("on_create");
}

void GameEntity::OnDestroy() {
    Logger::Debug("Entity {} onDestroy", id_);
    FireEvent("on_destroy");
}

void GameEntity::OnCollision(std::shared_ptr<GameEntity> other) {
    Logger::Debug("Entity {} collided with entity {}", id_, other ? other->GetId() : 0);
    FireEvent("on_collision");
}

void GameEntity::OnTriggerEnter(std::shared_ptr<GameEntity> other) {
    Logger::Debug("Entity {} trigger enter with entity {}", id_, other ? other->GetId() : 0);
    FireEvent("on_trigger_enter");
}

void GameEntity::OnTriggerExit(std::shared_ptr<GameEntity> other) {
    Logger::Debug("Entity {} trigger exit with entity {}", id_, other ? other->GetId() : 0);
    FireEvent("on_trigger_exit");
}

void GameEntity::OnParentChanged() {
    FireEvent("on_parent_changed");
}

void GameEntity::OnChildAdded(std::shared_ptr<GameEntity> child) {
    Logger::Debug("Entity {} added child {}", id_, child->GetId());
    FireEvent("on_child_added");
}

void GameEntity::OnChildRemoved(std::shared_ptr<GameEntity> child) {
    Logger::Debug("Entity {} removed child {}", id_, child->GetId());
    FireEvent("on_child_removed");
}

// =============== Transform Updates ===============
void GameEntity::UpdateWorldTransform() {
    // Update this entity's world transform based on parent
    // (Currently handled in GetWorld* methods)
}

void GameEntity::UpdateLocalTransform() {
    // Update local transform (could be used for animation)
    bounding_volumes_dirty_ = true;
}

// =============== Serialization ===============
nlohmann::json GameEntity::Serialize() const {
    nlohmann::json json;
    
    // Basic properties
    json["id"] = id_;
    json["type"] = static_cast<int>(type_);
    json["name"] = name_;
    json["category"] = category_;
    
    // Transform
    json["position"] = {position_.x, position_.y, position_.z};
    json["rotation"] = {rotation_.x, rotation_.y, rotation_.z};
    json["scale"] = {scale_.x, scale_.y, scale_.z};
    json["velocity"] = {velocity_.x, velocity_.y, velocity_.z};
    
    // State
    json["active"] = active_;
    json["visible"] = visible_;
    json["collidable"] = collidable_;
    json["persistent"] = persistent_;
    
    // Health
    json["health"] = health_;
    json["max_health"] = max_health_;
    
    // Tags
    if (!tags_.empty()) {
        json["tags"] = tags_;
    }
    
    // Properties
    if (!properties_.empty()) {
        json["properties"] = properties_;
    }
    
    // Hierarchy (only store parent ID, children will be reconstructed)
    if (auto parent = parent_.lock()) {
        json["parent_id"] = parent->GetId();
    }
    
    return json;
}

void GameEntity::Deserialize(const nlohmann::json& data) {
    // ID (optional, can be overridden)
    if (data.contains("id")) {
        id_ = data["id"];
    }
    
    // Name and category
    name_ = data.value("name", name_);
    category_ = data.value("category", category_);
    
    // Transform
    if (data.contains("position") && data["position"].is_array() && data["position"].size() >= 3) {
        position_.x = data["position"][0];
        position_.y = data["position"][1];
        position_.z = data["position"][2];
    }
    
    if (data.contains("rotation") && data["rotation"].is_array() && data["rotation"].size() >= 3) {
        rotation_.x = data["rotation"][0];
        rotation_.y = data["rotation"][1];
        rotation_.z = data["rotation"][2];
    }
    
    if (data.contains("scale") && data["scale"].is_array() && data["scale"].size() >= 3) {
        scale_.x = data["scale"][0];
        scale_.y = data["scale"][1];
        scale_.z = data["scale"][2];
    }
    
    if (data.contains("velocity") && data["velocity"].is_array() && data["velocity"].size() >= 3) {
        velocity_.x = data["velocity"][0];
        velocity_.y = data["velocity"][1];
        velocity_.z = data["velocity"][2];
    }
    
    // State
    active_ = data.value("active", active_);
    visible_ = data.value("visible", visible_);
    collidable_ = data.value("collidable", collidable_);
    persistent_ = data.value("persistent", persistent_);
    
    // Health
    health_ = data.value("health", health_);
    max_health_ = data.value("max_health", max_health_);
    
    // Tags
    if (data.contains("tags") && data["tags"].is_array()) {
        tags_ = data["tags"].get<std::vector<std::string>>();
    }
    
    // Properties
    if (data.contains("properties")) {
        properties_ = data["properties"];
    }
    
    // Update bounding volumes
    UpdateBoundingVolumes();
}

std::shared_ptr<GameEntity> GameEntity::CreateFromJson(const nlohmann::json& data) {
    if (!data.contains("type") || !data["type"].is_number()) {
        Logger::Error("Invalid entity JSON: missing or invalid type field");
        return nullptr;
    }
    
    EntityType type = static_cast<EntityType>(data["type"].get<int>());
    glm::vec3 position(0.0f);
    
    if (data.contains("position") && data["position"].is_array() && data["position"].size() >= 3) {
        position.x = data["position"][0];
        position.y = data["position"][1];
        position.z = data["position"][2];
    }
    
    auto entity = std::make_shared<GameEntity>(type, position);
    entity->Deserialize(data);
    
    return entity;
}

// =============== Distance and Range Methods ===============
float GameEntity::GetDistanceSquared(const glm::vec3& point) const {
    glm::vec3 diff = position_ - point;
    return glm::dot(diff, diff);
}

float GameEntity::GetDistance(const glm::vec3& point) const {
    return std::sqrt(GetDistanceSquared(point));
}

float GameEntity::GetDistanceSquared(std::shared_ptr<GameEntity> other) const {
    if (!other) return std::numeric_limits<float>::max();
    return GetDistanceSquared(other->GetPosition());
}

float GameEntity::GetDistance(std::shared_ptr<GameEntity> other) const {
    if (!other) return std::numeric_limits<float>::max();
    return GetDistance(other->GetPosition());
}

bool GameEntity::IsInRange(const glm::vec3& point, float range) const {
    return GetDistanceSquared(point) <= (range * range);
}

bool GameEntity::IsInRange(std::shared_ptr<GameEntity> other, float range) const {
    if (!other) return false;
    return IsInRange(other->GetPosition(), range);
}

// =============== LookAt Methods ===============
void GameEntity::LookAt(const glm::vec3& target) {
    glm::vec3 direction = target - position_;
    
    if (glm::length(direction) > 0.001f) {
        direction = glm::normalize(direction);
        
        // Calculate yaw (rotation around Y axis)
        float yaw = std::atan2(direction.x, direction.z);
        
        // Calculate pitch (rotation around X axis)
        float pitch = std::asin(-direction.y);
        
        rotation_.x = pitch;
        rotation_.y = yaw;
        rotation_.z = 0.0f;
    }
}

void GameEntity::LookAt(std::shared_ptr<GameEntity> target) {
    if (target) {
        LookAt(target->GetPosition());
    }
}

// =============== Event System ===============
void GameEntity::SubscribeToEvent(const std::string& event_name, EventCallback callback) {
    event_callbacks_[event_name].push_back(callback);
}

void GameEntity::UnsubscribeFromEvent(const std::string& event_name, EventCallback callback) {
    auto it = event_callbacks_.find(event_name);
    if (it != event_callbacks_.end()) {
        auto& callbacks = it->second;
        callbacks.erase(std::remove(callbacks.begin(), callbacks.end(), callback), callbacks.end());
    }
}

void GameEntity::FireEvent(const std::string& event_name) {
    auto it = event_callbacks_.find(event_name);
    if (it != event_callbacks_.end()) {
        for (auto& callback : it->second) {
            callback(shared_from_this());
        }
    }
}

// =============== Debug and Logging ===============
void GameEntity::DumpInfo() const {
    Logger::Info("=== Entity Info: {} (ID: {}) ===", name_, id_);
    Logger::Info("  Type: {}", EntityTypeToString(type_));
    Logger::Info("  Category: {}", category_);
    Logger::Info("  Position: [{:.2f}, {:.2f}, {:.2f}]", position_.x, position_.y, position_.z);
    Logger::Info("  Rotation: [{:.2f}, {:.2f}, {:.2f}]", rotation_.x, rotation_.y, rotation_.z);
    Logger::Info("  Scale: [{:.2f}, {:.2f}, {:.2f}]", scale_.x, scale_.y, scale_.z);
    Logger::Info("  Velocity: [{:.2f}, {:.2f}, {:.2f}]", velocity_.x, velocity_.y, velocity_.z);
    Logger::Info("  Health: {:.1f}/{:.1f}", health_, max_health_);
    Logger::Info("  Active: {}", active_);
    Logger::Info("  Visible: {}", visible_);
    Logger::Info("  Collidable: {}", collidable_);
    Logger::Info("  Tags: [{}]", JoinStrings(tags_, ", "));
    Logger::Info("  Children: {}", children_.size());
    if (auto parent = parent_.lock()) {
        Logger::Info("  Parent: {} (ID: {})", parent->GetName(), parent->GetId());
    }
    Logger::Info("=================================");
}

std::string GameEntity::ToString() const {
    std::stringstream ss;
    ss << name_ << " (ID: " << id_ 
       << ", Type: " << EntityTypeToString(type_)
       << ", Pos: [" << std::fixed << std::setprecision(1)
       << position_.x << ", " << position_.y << ", " << position_.z << "])";
    return ss.str();
}

// =============== Static Methods ===============
const char* GameEntity::EntityTypeToString(EntityType type) {
    switch (type) {
        case EntityType::PLAYER: return "PLAYER";
        case EntityType::NPC: return "NPC";
        case EntityType::ITEM: return "ITEM";
        case EntityType::PROJECTILE: return "PROJECTILE";
        case EntityType::VEHICLE: return "VEHICLE";
        case EntityType::ENVIRONMENT: return "ENVIRONMENT";
        case EntityType::COLLECTIBLE: return "COLLECTIBLE";
        case EntityType::CONTAINER: return "CONTAINER";
        case EntityType::DOOR: return "DOOR";
        case EntityType::TRAP: return "TRAP";
        case EntityType::SPAWNER: return "SPAWNER";
        case EntityType::DECORATION: return "DECORATION";
        case EntityType::PARTICLE: return "PARTICLE";
        case EntityType::LIGHT: return "LIGHT";
        case EntityType::SOUND: return "SOUND";
        case EntityType::TRIGGER: return "TRIGGER";
        case EntityType::CHECKPOINT: return "CHECKPOINT";
        case EntityType::WAYPOINT: return "WAYPOINT";
        case EntityType::ANY: return "ANY";
        default: return "UNKNOWN";
    }
}

GameEntity::EntityType GameEntity::StringToEntityType(const std::string& type_str) {
    static const std::unordered_map<std::string, EntityType> type_map = {
        {"PLAYER", EntityType::PLAYER},
        {"NPC", EntityType::NPC},
        {"ITEM", EntityType::ITEM},
        {"PROJECTILE", EntityType::PROJECTILE},
        {"VEHICLE", EntityType::VEHICLE},
        {"ENVIRONMENT", EntityType::ENVIRONMENT},
        {"COLLECTIBLE", EntityType::COLLECTIBLE},
        {"CONTAINER", EntityType::CONTAINER},
        {"DOOR", EntityType::DOOR},
        {"TRAP", EntityType::TRAP},
        {"SPAWNER", EntityType::SPAWNER},
        {"DECORATION", EntityType::DECORATION},
        {"PARTICLE", EntityType::PARTICLE},
        {"LIGHT", EntityType::LIGHT},
        {"SOUND", EntityType::SOUND},
        {"TRIGGER", EntityType::TRIGGER},
        {"CHECKPOINT", EntityType::CHECKPOINT},
        {"WAYPOINT", EntityType::WAYPOINT},
        {"ANY", EntityType::ANY}
    };
    
    auto it = type_map.find(type_str);
    return it != type_map.end() ? it->second : EntityType::ANY;
}

uint64_t GameEntity::GenerateEntityId() {
    return next_entity_id_++;
}

// =============== Utility Function ===============
namespace {
    std::string JoinStrings(const std::vector<std::string>& strings, const std::string& delimiter) {
        std::stringstream ss;
        for (size_t i = 0; i < strings.size(); ++i) {
            if (i > 0) ss << delimiter;
            ss << strings[i];
        }
        return ss.str();
    }
}
