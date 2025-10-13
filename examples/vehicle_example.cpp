/**
 * @file vehicle_example.cpp
 * @brief Example of a vehicle state machine with hierarchical states
 */

#include <kuksa_cpp/state_machine/hierarchical_state_machine.hpp>
#include <glog/logging.h>
#include <thread>
#include <chrono>

// Vehicle top-level states
enum class VehicleState {
    Parked,
    Driving,
    Charging,
    Maintenance,
    _Count
};

// Driving substates
enum class DrivingMode {
    Manual,
    CruiseControl,
    Autonomous,
    _Count
};

// State name helpers
std::string vehicle_state_name(VehicleState state) {
    switch (state) {
        case VehicleState::Parked:      return "PARKED";
        case VehicleState::Driving:     return "DRIVING";
        case VehicleState::Charging:    return "CHARGING";
        case VehicleState::Maintenance: return "MAINTENANCE";
        default:                        return "UNKNOWN";
    }
}

std::string driving_mode_name(DrivingMode mode) {
    switch (mode) {
        case DrivingMode::Manual:       return "MANUAL";
        case DrivingMode::CruiseControl: return "CRUISE_CONTROL";
        case DrivingMode::Autonomous:   return "AUTONOMOUS";
        default:                        return "UNKNOWN";
    }
}

// Simulated vehicle sensors
struct VehicleSensors {
    double speed_kmh = 0.0;
    double battery_percent = 75.0;
    bool charging_cable_connected = false;
    bool driver_present = true;
};

int main(int argc, char* argv[]) {
    // Initialize Google logging
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;
    FLAGS_v = 1;  // Enable verbose logging
    
    LOG(INFO) << "=== SDV Vehicle State Machine Example ===";
    
    // Create sensors
    VehicleSensors sensors;
    
    // Create hierarchical state machine
    sdv::HierarchicalStateMachine<VehicleState> vehicle_sm(
        "VehicleController", 
        VehicleState::Parked
    );
    
    // Set state name function for clear logging
    vehicle_sm.set_state_name_function(vehicle_state_name);
    
    // Define composite state for Driving with substates
    vehicle_sm.add_composite_state(
        VehicleState::Driving,
        {DrivingMode::Manual, DrivingMode::CruiseControl, DrivingMode::Autonomous},
        DrivingMode::Manual  // Initial substate
    );
    
    // Define state actions
    vehicle_sm.define_state(VehicleState::Parked)
        .on_entry([]() {
            LOG(INFO) << "Vehicle parked - applying parking brake";
        })
        .on_exit([]() {
            LOG(INFO) << "Releasing parking brake";
        });
    
    vehicle_sm.define_state(VehicleState::Driving)
        .on_entry([&sensors]() {
            LOG(INFO) << "Entering driving mode";
            sensors.speed_kmh = 0.0;
        });
    
    vehicle_sm.define_state(VehicleState::Charging)
        .on_entry([&sensors]() {
            LOG(INFO) << "Starting charging session";
            LOG(INFO) << "Current battery: " << sensors.battery_percent << "%";
        })
        .on_exit([]() {
            LOG(INFO) << "Ending charging session";
        });
    
    // Define transitions
    // PARKED -> DRIVING
    vehicle_sm.add_transition(
        VehicleState::Parked,
        VehicleState::Driving,
        "start_engine",
        [&sensors](const sdv::Context& ctx) {
            if (!sensors.driver_present) {
                    LOG(WARNING) << "Cannot start - no driver present";
                return false;
            }
            if (sensors.battery_percent < 10.0) {
                    LOG(WARNING) << "Cannot start - battery too low";
                return false;
            }
            return true;
        },
        [](const sdv::Context& ctx) {
            LOG(INFO) << "Starting engine...";
        }
    );
    
    // DRIVING -> PARKED
    vehicle_sm.add_transition(
        VehicleState::Driving,
        VehicleState::Parked,
        "park",
        [&sensors](const sdv::Context& ctx) {
            if (sensors.speed_kmh > 0.1) {
                    LOG(WARNING) << "Cannot park - vehicle still moving";
                return false;
            }
            return true;
        }
    );
    
    // PARKED -> CHARGING
    vehicle_sm.add_transition(
        VehicleState::Parked,
        VehicleState::Charging,
        "plug_in",
        [&sensors](const sdv::Context& ctx) {
            if (!sensors.charging_cable_connected) {
                    LOG(WARNING) << "No charging cable connected";
                return false;
            }
            return true;
        }
    );
    
    // CHARGING -> PARKED
    vehicle_sm.add_transition(
        VehicleState::Charging,
        VehicleState::Parked,
        "unplug",
        [&sensors](const sdv::Context& ctx) {
            sensors.charging_cable_connected = false;
            return true;
        }
    );
    
    // Any state -> MAINTENANCE
    for (auto state : {VehicleState::Parked, VehicleState::Driving, VehicleState::Charging}) {
        vehicle_sm.add_transition(
            state,
            VehicleState::Maintenance,
            "enter_maintenance"
        );
    }
    
    // MAINTENANCE -> PARKED (after service)
    vehicle_sm.add_transition(
        VehicleState::Maintenance,
        VehicleState::Parked,
        "exit_maintenance"
    );
    
    // Test the state machine
    LOG(INFO) << "\n=== Testing vehicle state transitions ===";
    
    // Initial state
    LOG(INFO) << "Initial state: " << vehicle_state_name(vehicle_sm.current_state());
    
    // Try to start engine
    {
        LOG(INFO) << "\n1. Starting engine:";
        if (vehicle_sm.trigger("start_engine")) {
            LOG(INFO) << "Vehicle is now: " << vehicle_state_name(vehicle_sm.current_state());
            
            // Check hierarchical state
            if (vehicle_sm.is_in_state(VehicleState::Driving)) {
                LOG(INFO) << "Vehicle is in DRIVING state";
                LOG(INFO) << "Active states: ";
                for (const auto& state : vehicle_sm.get_active_states()) {
                    LOG(INFO) << "  - " << vehicle_state_name(state);
                }
            }
        }
    }
    
    // Park the vehicle
    {
        LOG(INFO) << "\n2. Parking vehicle:";
        // First need to stop (speed = 0)
        sensors.speed_kmh = 0.0;
        
        if (vehicle_sm.trigger("park")) {
            LOG(INFO) << "Vehicle is now: " << vehicle_state_name(vehicle_sm.current_state());
        }
    }
    
    // Try charging
    {
        LOG(INFO) << "\n3. Attempting to charge:";
        
        // First attempt without cable
        if (!vehicle_sm.trigger("plug_in")) {
            LOG(INFO) << "Failed to start charging (no cable)";
        }
        
        // Connect cable and try again
        sensors.charging_cable_connected = true;
        if (vehicle_sm.trigger("plug_in")) {
            LOG(INFO) << "Vehicle is now: " << vehicle_state_name(vehicle_sm.current_state());
            
            // Simulate charging
            LOG(INFO) << "Charging in progress...";
            std::this_thread::sleep_for(std::chrono::seconds(1));
            sensors.battery_percent = 95.0;
            
            // Unplug
            if (vehicle_sm.trigger("unplug")) {
                LOG(INFO) << "Charging complete. Battery: " << sensors.battery_percent << "%";
                LOG(INFO) << "Vehicle is now: " << vehicle_state_name(vehicle_sm.current_state());
            }
        }
    }
    
    // Test low battery scenario
    {
        LOG(INFO) << "\n4. Testing low battery scenario:";
        sensors.battery_percent = 5.0;
        
        if (!vehicle_sm.trigger("start_engine")) {
            LOG(INFO) << "Cannot start engine - battery too low";
        }
        
        // Recharge
        sensors.battery_percent = 75.0;
    }
    
    // Enter maintenance mode
    {
        LOG(INFO) << "\n5. Entering maintenance mode:";
        if (vehicle_sm.trigger("enter_maintenance")) {
            LOG(INFO) << "Vehicle is now: " << vehicle_state_name(vehicle_sm.current_state());
            LOG(INFO) << "Performing diagnostics...";
            
            // Exit maintenance
            if (vehicle_sm.trigger("exit_maintenance")) {
                LOG(INFO) << "Maintenance complete";
                LOG(INFO) << "Vehicle is now: " << vehicle_state_name(vehicle_sm.current_state());
            }
        }
    }
    
    
    
    // Demonstrate current state info
    LOG(INFO) << "\n=== Current state information ===";
    LOG(INFO) << "Current state: " << vehicle_state_name(vehicle_sm.current_state());
    LOG(INFO) << "Available triggers: ";
    for (const auto& trigger : vehicle_sm.available_triggers()) {
        LOG(INFO) << "  - " << trigger;
    }
    
#ifdef SDV_WITH_KUKSA
    LOG(INFO) << "\n=== VSS Integration ===";
    LOG(INFO) << "When SDV_DEV_MODE=true, state information is exposed via VSS at:";
    LOG(INFO) << "  Private.StateMachine.VehicleController.CurrentState";
    LOG(INFO) << "  Private.StateMachine.VehicleController.ActiveStates";
    LOG(INFO) << "  Private.StateMachine.VehicleController.History";
#endif
    
    LOG(INFO) << "\n=== Example completed ===";
    
    google::ShutdownGoogleLogging();
    return 0;
}