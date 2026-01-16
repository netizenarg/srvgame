#include <algorithm>
#include <cmath>

#include "../../include/network/BinaryProtocol.hpp"
#include "../../include/network/PredictionSystem.hpp"

PredictionSystem::PredictionSystem() {
    last_confirmed_state_.timestamp = 0;
    latest_predicted_state_.timestamp = 0;
}

void PredictionSystem::StoreClientInput(const ClientInput& input) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!input.IsValid()) return;
    
    input_history_.push_back(input);
    
    // Keep history size manageable
    while (input_history_.size() > MAX_INPUT_HISTORY) {
        input_history_.pop_front();
    }
    
    stats_.total_predictions++;
}

void PredictionSystem::StoreServerState(const ServerState& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    server_state_history_.push_back(state);
    last_confirmed_state_ = state;
    
    // Keep history size manageable
    while (server_state_history_.size() > MAX_STATE_HISTORY) {
        server_state_history_.pop_front();
    }
}

ServerState PredictionSystem::PredictPosition(uint64_t current_time) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (input_history_.empty() || server_state_history_.empty()) {
        return last_confirmed_state_;
    }
    
    // Find inputs since last confirmed state
    std::vector<ClientInput> unprocessed_inputs;
    for (const auto& input : input_history_) {
        if (input.input_id > last_confirmed_state_.last_processed_input) {
            unprocessed_inputs.push_back(input);
        }
    }
    
    if (unprocessed_inputs.empty()) {
        return last_confirmed_state_;
    }
    
    // Calculate time delta
    uint64_t time_since_confirmed = current_time - last_confirmed_state_.timestamp;
    float delta_time = time_since_confirmed / 1000.0f; // Convert to seconds
    
    // Simulate movement
    latest_predicted_state_ = SimulateMovement(last_confirmed_state_, unprocessed_inputs, delta_time);
    latest_predicted_state_.timestamp = current_time;
    
    return latest_predicted_state_;
}

PredictionSystem::ReconciliationResult PredictionSystem::ReconcileWithServer(const ServerState& server_state) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    ReconciliationResult result;
    
    // Store the server state
    StoreServerState(server_state);
    
    // Find which inputs have been processed
    std::vector<uint32_t> processed_inputs;
    for (const auto& input : input_history_) {
        if (input.input_id <= server_state.last_processed_input) {
            processed_inputs.push_back(input.input_id);
        }
    }
    
    result.processed_inputs = processed_inputs;
    
    // Calculate correction if needed
    float distance = glm::distance(latest_predicted_state_.position, server_state.position);
    
    if (distance > 0.1f) { // 10cm threshold
        result.needs_correction = true;
        result.corrected_state = server_state;
        
        // Update statistics
        stats_.corrections_received++;
        stats_.average_correction_distance = 
            (stats_.average_correction_distance * (stats_.corrections_received - 1) + distance) / 
            stats_.corrections_received;
        stats_.last_correction_time = server_state.timestamp;
        
        // Remove processed inputs from history
        input_history_.erase(
            std::remove_if(input_history_.begin(), input_history_.end(),
                [&server_state](const ClientInput& input) {
                    return input.input_id <= server_state.last_processed_input;
                }),
            input_history_.end()
        );
        
        // Update latest prediction
        latest_predicted_state_ = server_state;
    }
    
    return result;
}

std::vector<ClientInput> PredictionSystem::GetUnprocessedInputs(uint32_t last_processed) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<ClientInput> unprocessed;
    for (const auto& input : input_history_) {
        if (input.input_id > last_processed) {
            unprocessed.push_back(input);
        }
    }
    
    return unprocessed;
}

ServerState PredictionSystem::SimulateMovement(const ServerState& start_state,
                                              const std::vector<ClientInput>& inputs,
                                              float delta_time) const {
    if (inputs.empty()) return start_state;
    
    ServerState current_state = start_state;
    
    // Process each input
    for (const auto& input : inputs) {
        current_state = ApplyInput(current_state, input, delta_time / inputs.size());
    }
    
    current_state.last_processed_input = inputs.back().input_id;
    
    return current_state;
}

ServerState PredictionSystem::ApplyInput(const ServerState& state, const ClientInput& input, float delta_time) const {
    ServerState new_state = state;
    
    // Calculate movement direction from rotation
    glm::vec3 forward = glm::vec3(
        sin(input.rotation.y),
        0,
        cos(input.rotation.y)
    );
    
    glm::vec3 right = glm::vec3(
        cos(input.rotation.y),
        0,
        -sin(input.rotation.y)
    );
    
    // Calculate desired velocity
    glm::vec3 desired_velocity(0.0f);
    
    if (input.movement.x != 0.0f) {
        desired_velocity += right * input.movement.x;
    }
    
    if (input.movement.z != 0.0f) {
        desired_velocity += forward * input.movement.z;
    }
    
    if (glm::length(desired_velocity) > 0.0f) {
        desired_velocity = glm::normalize(desired_velocity);
        
        // Apply sprinting
        float speed = MAX_SPEED;
        if (input.sprinting) {
            speed *= 1.5f;
        } else if (input.crouching) {
            speed *= 0.5f;
        }
        
        desired_velocity *= speed;
    }
    
    // Apply acceleration
    glm::vec3 acceleration = (desired_velocity - new_state.velocity) * ACCELERATION;
    new_state.velocity += acceleration * delta_time;
    
    // Apply friction if no input
    if (glm::length(desired_velocity) < 0.1f) {
        glm::vec3 friction = -new_state.velocity * FRICTION * delta_time;
        if (glm::length(friction) > glm::length(new_state.velocity)) {
            new_state.velocity = glm::vec3(0.0f);
        } else {
            new_state.velocity += friction;
        }
    }
    
    // Apply gravity
    if (!new_state.on_ground) {
        new_state.velocity.y += GRAVITY * delta_time;
    }
    
    // Apply jumping
    if (input.jumping && new_state.on_ground) {
        new_state.velocity.y = JUMP_FORCE;
        new_state.on_ground = false;
    }
    
    // Apply velocity
    new_state.position += new_state.velocity * delta_time;
    
    // Simple ground detection
    if (new_state.position.y <= 0.0f) {
        new_state.position.y = 0.0f;
        new_state.velocity.y = 0.0f;
        new_state.on_ground = true;
    }
    
    // Update rotation
    new_state.rotation = input.rotation;
    
    return new_state;
}

void PredictionSystem::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    input_history_.clear();
    server_state_history_.clear();
    stats_.Reset();
}

// Serialization implementations
std::vector<uint8_t> ClientInput::Serialize() const {
    BinaryProtocol::BinaryWriter writer;
    
    writer.WriteUInt32(input_id);
    writer.WriteUInt64(timestamp);
    writer.WriteVector3(movement);
    writer.WriteVector3(rotation);
    writer.WriteUInt8(jumping ? 1 : 0);
    writer.WriteUInt8(crouching ? 1 : 0);
    writer.WriteUInt8(sprinting ? 1 : 0);
    
    return writer.GetBuffer();
}

ClientInput ClientInput::Deserialize(const uint8_t* data, size_t length) {
    BinaryProtocol::BinaryReader reader(data, length);
    ClientInput input;
    
    input.input_id = reader.ReadUInt32();
    input.timestamp = reader.ReadUInt64();
    input.movement = reader.ReadVector3();
    input.rotation = reader.ReadVector3();
    input.jumping = reader.ReadUInt8() != 0;
    input.crouching = reader.ReadUInt8() != 0;
    input.sprinting = reader.ReadUInt8() != 0;
    
    return input;
}

std::vector<uint8_t> ServerState::Serialize() const {
    BinaryProtocol::BinaryWriter writer;
    
    writer.WriteUInt32(last_processed_input);
    writer.WriteUInt64(timestamp);
    writer.WriteVector3(position);
    writer.WriteVector3(velocity);
    writer.WriteVector3(rotation);
    writer.WriteUInt8(on_ground ? 1 : 0);
    
    return writer.GetBuffer();
}

ServerState ServerState::Deserialize(const uint8_t* data, size_t length) {
    BinaryProtocol::BinaryReader reader(data, length);
    ServerState state;
    
    state.last_processed_input = reader.ReadUInt32();
    state.timestamp = reader.ReadUInt64();
    state.position = reader.ReadVector3();
    state.velocity = reader.ReadVector3();
    state.rotation = reader.ReadVector3();
    state.on_ground = reader.ReadUInt8() != 0;
    
    return state;
}

ServerState ServerState::Interpolate(const ServerState& a, const ServerState& b, float t) {
    t = glm::clamp(t, 0.0f, 1.0f);

    ServerState result;
    result.last_processed_input = 0;
    result.position = glm::mix(a.position, b.position, t);
    result.velocity = glm::mix(a.velocity, b.velocity, t);
    result.rotation = glm::mix(a.rotation, b.rotation, t);
    result.on_ground = t < 0.5f ? a.on_ground : b.on_ground;
    result.timestamp = static_cast<uint64_t>(glm::mix(static_cast<float>(a.timestamp), static_cast<float>(b.timestamp), t));

    return result;
}

void PredictionSystem::PredictionStats::Reset() {
    total_predictions = 0;
    corrections_sent = 0;
    corrections_received = 0;
    average_correction_distance = 0.0f;
    last_correction_time = 0;
}

std::string PredictionSystem::PredictionStats::ToString() const {
    std::stringstream ss;
    ss << "Prediction Stats:\n";
    ss << "  Total Predictions: " << total_predictions << "\n";
    ss << "  Corrections Sent: " << corrections_sent << "\n";
    ss << "  Corrections Received: " << corrections_received << "\n";
    ss << "  Avg Correction Distance: " << average_correction_distance << "m\n";
    ss << "  Last Correction: " << last_correction_time << "ms ago";
    
    return ss.str();
}

// InputBuffer implementation
InputBuffer::InputBuffer(size_t max_size) : max_size_(max_size) {}

void InputBuffer::AddInput(const ClientInput& input) {
    std::lock_guard<std::mutex> lock(mutex_);
    inputs_.push_back(input);
    SortAndTrim();
}

std::vector<ClientInput> InputBuffer::GetOrderedInputs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ClientInput> sorted = inputs_;
    std::sort(sorted.begin(), sorted.end(),
        [](const ClientInput& a, const ClientInput& b) {
            return a.input_id < b.input_id;
        });
    return sorted;
}

ClientInput InputBuffer::GetNextInput() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (inputs_.empty()) {
        return ClientInput();
    }
    
    // Find the input with the smallest ID
    auto it = std::min_element(inputs_.begin(), inputs_.end(),
        [](const ClientInput& a, const ClientInput& b) {
            return a.input_id < b.input_id;
        });
    
    return *it;
}

void InputBuffer::SortAndTrim() {
    // Sort by input ID
    std::sort(inputs_.begin(), inputs_.end(),
        [](const ClientInput& a, const ClientInput& b) {
            return a.input_id < b.input_id;
        });
    
    // Remove duplicates
    inputs_.erase(
        std::unique(inputs_.begin(), inputs_.end(),
            [](const ClientInput& a, const ClientInput& b) {
                return a.input_id == b.input_id;
            }),
        inputs_.end()
    );
    
    // Trim to max size
    if (inputs_.size() > max_size_) {
        inputs_.erase(inputs_.begin(), inputs_.begin() + (inputs_.size() - max_size_));
    }
}

void InputBuffer::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    inputs_.clear();
}
