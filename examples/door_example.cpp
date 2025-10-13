/**
 * @file door_example.cpp
 * @brief Example of a door control state machine using SDV State Machine SDK
 */

#include <kuksa_cpp/state_machine/state_machine.hpp>
#include <glog/logging.h>
#include <thread>
#include <chrono>

// Define door states
enum class DoorState {
    Closed,
    Opening,
    Open,
    Closing,
    Error,
    _Count  // Used for state enumeration
};

// Helper function to get string representation of state
std::string state_to_string(DoorState state) {
    switch (state) {
        case DoorState::Closed:   return "CLOSED";
        case DoorState::Opening:  return "OPENING";
        case DoorState::Open:     return "OPEN";
        case DoorState::Closing:  return "CLOSING";
        case DoorState::Error:    return "ERROR";
        default:                  return "UNKNOWN";
    }
}

// Door motor controller (simulated)
class DoorMotor {
public:
    void start_opening() {
        LOG(INFO) << "Motor: Starting to open door";
        moving_ = true;
        direction_ = Direction::OPENING;
    }
    
    void start_closing() {
        LOG(INFO) << "Motor: Starting to close door";
        moving_ = true;
        direction_ = Direction::CLOSING;
    }
    
    void stop() {
        LOG(INFO) << "Motor: Stopping";
        moving_ = false;
    }
    
    bool is_moving() const { return moving_; }
    
    // Simulate motor reaching end position
    void simulate_completion() {
        if (moving_) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            LOG(INFO) << "Motor: Reached end position";
            moving_ = false;
        }
    }
    
private:
    enum class Direction { OPENING, CLOSING };
    bool moving_ = false;
    Direction direction_ = Direction::OPENING;
};

int main(int argc, char* argv[]) {
    // Initialize Google logging
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;
    FLAGS_v = 2;  // Enable verbose logging up to level 2
    
    LOG(INFO) << "=== SDV Door Control Example ===";
    
    // Create door motor
    DoorMotor motor;
    
    // Create state machine
    sdv::StateMachine<DoorState> door_sm("DoorController", DoorState::Closed);
    
    // Set state name function for clear logging
    door_sm.set_state_name_function(state_to_string);
    
    // Define states with entry/exit actions
    door_sm.define_state(DoorState::Opening)
        .on_entry([&motor]() {
            motor.start_opening();
        })
        .on_exit([&motor]() {
            motor.stop();
        });
    
    door_sm.define_state(DoorState::Closing)
        .on_entry([&motor]() {
            motor.start_closing();
        })
        .on_exit([&motor]() {
            motor.stop();
        });
    
    door_sm.define_state(DoorState::Error)
        .on_entry([]() {
            LOG(ERROR) << "Door entered ERROR state!";
        });
    
    // Define transitions
    // From CLOSED
    door_sm.add_transition(
        DoorState::Closed,
        DoorState::Opening,
        "open_requested",
        [](const sdv::Context& ctx) {
            // Check if door is unlocked
            auto locked = ctx.find("locked");
            if (locked != ctx.end() && std::any_cast<bool>(locked->second)) {
                LOG(WARNING) << "Cannot open - door is locked";
                return false;
            }
            return true;
        }
    );
    
    // From OPENING
    door_sm.add_transition(
        DoorState::Opening,
        DoorState::Open,
        "door_opened"
    );
    
    door_sm.add_transition(
        DoorState::Opening,
        DoorState::Error,
        "obstruction_detected",
        {},  // No condition
        [](const sdv::Context& ctx) {
            LOG(ERROR) << "Obstruction detected during opening!";
        }
    );
    
    // From OPEN
    door_sm.add_transition(
        DoorState::Open,
        DoorState::Closing,
        "close_requested"
    );
    
    // From CLOSING
    door_sm.add_transition(
        DoorState::Closing,
        DoorState::Closed,
        "door_closed"
    );
    
    door_sm.add_transition(
        DoorState::Closing,
        DoorState::Error,
        "obstruction_detected",
        {},  // No condition
        [](const sdv::Context& ctx) {
            LOG(ERROR) << "Obstruction detected during closing!";
        }
    );
    
    // From ERROR - reset transition
    door_sm.add_transition(
        DoorState::Error,
        DoorState::Closed,
        "reset",
        [&motor](const sdv::Context& ctx) {
            return !motor.is_moving();  // Can only reset when motor is not moving
        }
    );
    
    // Print initial state
    LOG(INFO) << "Initial state: " << state_to_string(door_sm.current_state());
    LOG(INFO) << "Available triggers: ";
    for (const auto& trigger : door_sm.available_triggers()) {
        LOG(INFO) << "  - " << trigger;
    }
    
    // Test sequence
    LOG(INFO) << "\n=== Testing door operations ===";
    
    // Try to open locked door
    {
        LOG(INFO) << "\n1. Attempting to open locked door:";
        sdv::Context ctx{{"locked", std::any(true)}};
        if (!door_sm.trigger("open_requested", ctx)) {
            LOG(INFO) << "Failed to open door (expected - door is locked)";
        }
    }
    
    // Open unlocked door
    {
        LOG(INFO) << "\n2. Opening unlocked door:";
        sdv::Context ctx{{"locked", std::any(false)}};
        if (door_sm.trigger("open_requested", ctx)) {
            LOG(INFO) << "Door is now: " << state_to_string(door_sm.current_state());
            
            // Simulate motor completing the operation
            motor.simulate_completion();
            
            // Signal that door is fully open
            if (door_sm.trigger("door_opened")) {
                LOG(INFO) << "Door is now: " << state_to_string(door_sm.current_state());
            }
        }
    }
    
    // Close the door
    {
        LOG(INFO) << "\n3. Closing door:";
        if (door_sm.trigger("close_requested")) {
            LOG(INFO) << "Door is now: " << state_to_string(door_sm.current_state());
            
            // Simulate motor completing the operation
            motor.simulate_completion();
            
            // Signal that door is fully closed
            if (door_sm.trigger("door_closed")) {
                LOG(INFO) << "Door is now: " << state_to_string(door_sm.current_state());
            }
        }
    }
    
    // Simulate obstruction during opening
    {
        LOG(INFO) << "\n4. Testing obstruction detection:";
        sdv::Context ctx{{"locked", std::any(false)}};
        door_sm.trigger("open_requested", ctx);
        LOG(INFO) << "Door is: " << state_to_string(door_sm.current_state());
        
        // Simulate obstruction
        if (door_sm.trigger("obstruction_detected")) {
            LOG(INFO) << "Door is now: " << state_to_string(door_sm.current_state());
        }
        
        // Reset from error
        LOG(INFO) << "Attempting reset...";
        if (door_sm.trigger("reset")) {
            LOG(INFO) << "Door reset to: " << state_to_string(door_sm.current_state());
        }
    }
    
    
#ifdef SDV_WITH_PROMETHEUS
    // If built with Prometheus support, demonstrate metrics
    LOG(INFO) << "\n=== Prometheus metrics ===";
    LOG(INFO) << "Metrics are available at the Prometheus endpoint";
    LOG(INFO) << "Example queries:";
    LOG(INFO) << "  doorcontroller_state";
    LOG(INFO) << "  rate(doorcontroller_transitions_total[5m])";
    LOG(INFO) << "  histogram_quantile(0.99, doorcontroller_transition_latency_seconds)";
#endif
    
    LOG(INFO) << "\n=== Example completed ===";
    
    google::ShutdownGoogleLogging();
    return 0;
}