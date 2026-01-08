#pragma once

#include <deque>
#include <vector>
#include <mutex>
#include <glm/glm.hpp>
#include <chrono>

struct ClientInput {
    uint32_t input_id;
    uint64_t timestamp;
    glm::vec3 movement;
    glm::vec3 rotation;
    bool jumping{false};
    bool crouching{false};
    bool sprinting{false};
    
    // Serialization
    std::vector<uint8_t> Serialize() const;
    static ClientInput Deserialize(const uint8_t* data, size_t length);
    
    // Validity check
    bool IsValid() const { return input_id > 0; }
};

struct ServerState {
    uint32_t last_processed_input;
    uint64_t timestamp;
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 rotation;
    bool on_ground{true};
    
    // Serialization
    std::vector<uint8_t> Serialize() const;
    static ServerState Deserialize(const uint8_t* data, size_t length);
    
    // Interpolation
    static ServerState Interpolate(const ServerState& a, const ServerState& b, float t);
};

class PredictionSystem {
public:
    PredictionSystem();
    
    // Client-side methods
    void StoreClientInput(const ClientInput& input);
    ServerState PredictPosition(uint64_t current_time) const;
    
    // Server-side methods
    void StoreServerState(const ServerState& state);
    std::vector<ClientInput> GetUnprocessedInputs(uint32_t last_processed) const;
    
    // Reconciliation
    struct ReconciliationResult {
        bool needs_correction{false};
        ServerState corrected_state;
        std::vector<uint32_t> processed_inputs;
    };
    
    ReconciliationResult ReconcileWithServer(const ServerState& server_state);
    
    // State management
    ServerState GetLastConfirmedState() const { return last_confirmed_state_; }
    ServerState GetLatestPredictedState() const { return latest_predicted_state_; }
    
    // Input history
    const std::deque<ClientInput>& GetInputHistory() const { return input_history_; }
    
    // Clear history
    void Clear();
    
    // Statistics
    struct PredictionStats {
        uint32_t total_predictions{0};
        uint32_t corrections_sent{0};
        uint32_t corrections_received{0};
        float average_correction_distance{0.0f};
        uint64_t last_correction_time{0};
        
        void Reset();
        std::string ToString() const;
    };
    
    const PredictionStats& GetStats() const { return stats_; }
    
private:
    mutable std::mutex mutex_;
    
    // Input history (client-side)
    std::deque<ClientInput> input_history_;
    static constexpr size_t MAX_INPUT_HISTORY = 1000;
    
    // Server state history
    std::deque<ServerState> server_state_history_;
    static constexpr size_t MAX_STATE_HISTORY = 100;
    
    // Current states
    ServerState last_confirmed_state_;
    ServerState latest_predicted_state_;
    
    // Statistics
    PredictionStats stats_;
    
    // Helper methods
    ServerState SimulateMovement(const ServerState& start_state,
                                const std::vector<ClientInput>& inputs,
                                float delta_time) const;
    
    ServerState ApplyInput(const ServerState& state, const ClientInput& input, float delta_time) const;
    
    // Physics constants
    static constexpr float GRAVITY = -9.81f;
    static constexpr float MAX_SPEED = 10.0f;
    static constexpr float ACCELERATION = 20.0f;
    static constexpr float FRICTION = 10.0f;
    static constexpr float JUMP_FORCE = 5.0f;
};

// Input buffer for handling out-of-order inputs
class InputBuffer {
public:
    InputBuffer(size_t max_size = 1000);
    
    void AddInput(const ClientInput& input);
    std::vector<ClientInput> GetOrderedInputs() const;
    ClientInput GetNextInput() const;
    bool HasInputs() const { return !inputs_.empty(); }
    void Clear();
    size_t Size() const { return inputs_.size(); }
    
private:
    mutable std::mutex mutex_;
    std::vector<ClientInput> inputs_;
    size_t max_size_;
    
    void SortAndTrim();
};