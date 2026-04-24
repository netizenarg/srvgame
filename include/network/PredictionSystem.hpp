#pragma once

#include <deque>
#include <vector>
#include <mutex>
#include <glm/glm.hpp>
#include <chrono>

struct ClientInput {
    uint32_t input_id;
    uint64_t timestamp;
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 rotation;
    glm::vec3 movement;
    bool on_ground{true};
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

struct PredictionStats {
    uint32_t total_predictions{0};
    uint32_t corrections_sent{0};
    uint32_t corrections_received{0};
    float average_correction_distance{0.0f};
    uint64_t last_correction_time{0};
    void Reset();
    std::string ToString() const;
};

class PredictionSystem {
public:
    struct ReconciliationResult {
        bool needs_correction{false};
        ServerState corrected_state;
        std::vector<uint32_t> processed_inputs;
    };

    PredictionSystem();

    void StoreClientInput(const ClientInput& input);
    ServerState PredictPosition(uint64_t current_time) const;

    void StoreServerState(const ServerState& state);
    std::vector<ClientInput> GetUnprocessedInputs(uint32_t last_processed) const;

    ReconciliationResult ReconcileWithServer(const ServerState& server_state);

    ServerState GetLastConfirmedState() const;

    ServerState GetLatestPredictedState() const;

    const std::deque<ClientInput>& GetInputHistory() const;

    const PredictionStats& GetStats() const;

    void Clear();

    ServerState SimulateMovement(const ServerState& start_state,
                                const std::vector<ClientInput>& inputs,
                                float delta_time) const;

    ServerState ApplyInput(const ServerState& state, const ClientInput& input, float delta_time) const;

private:
    mutable std::mutex mutex_;

    std::deque<ClientInput> input_history_;
    static constexpr size_t MAX_INPUT_HISTORY = 1000;

    std::deque<ServerState> server_state_history_;
    static constexpr size_t MAX_STATE_HISTORY = 100;

    ServerState last_confirmed_state_;
    mutable ServerState latest_predicted_state_;

    PredictionStats stats_;

    static constexpr float GRAVITY = -9.81f;
    static constexpr float MAX_SPEED = 10.0f;
    static constexpr float ACCELERATION = 20.0f;
    static constexpr float FRICTION = 10.0f;
    static constexpr float JUMP_FORCE = 5.0f;
};

class InputBuffer {
public:
    InputBuffer(size_t max_size = 1000);
    void AddInput(const ClientInput& input);
    std::vector<ClientInput> GetOrderedInputs() const;
    ClientInput GetNextInput() const;
    void Clear();
    bool HasInputs() const;
    size_t Size() const;

private:
    mutable std::mutex mutex_;
    std::vector<ClientInput> inputs_;
    size_t max_size_;

    void SortAndTrim();
};
