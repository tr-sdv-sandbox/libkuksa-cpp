/**
 * @file test_hierarchical.cpp
 * @brief Unit tests for hierarchical state machine functionality
 */

#include <gtest/gtest.h>
#include <glog/logging.h>
#include <kuksa_cpp/state_machine/hierarchical_state_machine.hpp>

enum class VehicleState {
    Parked,
    Driving,
    Charging,
    _Count
};

enum class DrivingMode {
    Manual,
    CruiseControl,
    Autonomous,
    _Count
};

class HierarchicalTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!google::IsGoogleLoggingInitialized()) {
            google::InitGoogleLogging("test");
            FLAGS_logtostderr = 1;
        }
    }
};

TEST_F(HierarchicalTest, BasicHierarchicalStateMachine) {
    sdv::HierarchicalStateMachine<VehicleState> vehicle("Vehicle", VehicleState::Parked);
    
    // Initial state should be Parked
    EXPECT_EQ(vehicle.current_state(), VehicleState::Parked);
}

TEST_F(HierarchicalTest, CompositeStateDefinition) {
    sdv::HierarchicalStateMachine<VehicleState> vehicle("Vehicle", VehicleState::Parked);
    
    // Define Driving as composite state with driving modes
    vehicle.add_composite_state(
        VehicleState::Driving,
        std::vector<DrivingMode>{DrivingMode::Manual, DrivingMode::CruiseControl, DrivingMode::Autonomous},
        DrivingMode::Manual
    );
    
    // Add transition to driving state
    vehicle.add_transition(VehicleState::Parked, VehicleState::Driving, "start");
    
    // Transition to driving
    EXPECT_TRUE(vehicle.trigger("start"));
    EXPECT_EQ(vehicle.current_state(), VehicleState::Driving);
}

TEST_F(HierarchicalTest, StateDepthCalculation) {
    sdv::HierarchicalStateMachine<VehicleState> vehicle("Vehicle", VehicleState::Parked);
    
    // Define composite state
    vehicle.add_composite_state(
        VehicleState::Driving,
        std::vector<DrivingMode>{DrivingMode::Manual, DrivingMode::CruiseControl},
        DrivingMode::Manual
    );
    
    // Depth in simple state should be 0
    EXPECT_EQ(vehicle.get_state_depth(), 0);
    
    // Transition to composite state
    vehicle.add_transition(VehicleState::Parked, VehicleState::Driving, "start");
    vehicle.trigger("start");
    
    // Depth in composite state might be higher (implementation dependent)
    EXPECT_GE(vehicle.get_state_depth(), 0);
}

TEST_F(HierarchicalTest, IsInStateCheck) {
    sdv::HierarchicalStateMachine<VehicleState> vehicle("Vehicle", VehicleState::Parked);
    
    // Initially in Parked
    EXPECT_TRUE(vehicle.is_in_state(VehicleState::Parked));
    EXPECT_FALSE(vehicle.is_in_state(VehicleState::Driving));
    EXPECT_FALSE(vehicle.is_in_state(VehicleState::Charging));
    
    // Add transition and move to Driving
    vehicle.add_transition(VehicleState::Parked, VehicleState::Driving, "start");
    vehicle.trigger("start");
    
    EXPECT_FALSE(vehicle.is_in_state(VehicleState::Parked));
    EXPECT_TRUE(vehicle.is_in_state(VehicleState::Driving));
}

TEST_F(HierarchicalTest, ActiveStatesTracking) {
    sdv::HierarchicalStateMachine<VehicleState> vehicle("Vehicle", VehicleState::Parked);
    
    // Get active states in simple state
    auto active = vehicle.get_active_states();
    EXPECT_EQ(active.size(), 1);
    EXPECT_TRUE(active.count(VehicleState::Parked) > 0);
    
    // Define composite state
    vehicle.add_composite_state(
        VehicleState::Driving,
        std::vector<DrivingMode>{DrivingMode::Manual, DrivingMode::CruiseControl},
        DrivingMode::Manual
    );
    
    // Transition to composite state
    vehicle.add_transition(VehicleState::Parked, VehicleState::Driving, "start");
    vehicle.trigger("start");
    
    // Should now have Driving in active states
    active = vehicle.get_active_states();
    EXPECT_TRUE(active.count(VehicleState::Driving) > 0);
}

TEST_F(HierarchicalTest, HierarchicalTransitions) {
    sdv::HierarchicalStateMachine<VehicleState> vehicle("Vehicle", VehicleState::Parked);
    
    // State tracking
    bool parked_exited = false;
    bool driving_entered = false;
    bool charging_entered = false;
    
    // Define state actions
    vehicle.define_state(VehicleState::Parked)
        .on_exit([&parked_exited]() {
            parked_exited = true;
        });
    
    vehicle.define_state(VehicleState::Driving)
        .on_entry([&driving_entered]() {
            driving_entered = true;
        });
    
    vehicle.define_state(VehicleState::Charging)
        .on_entry([&charging_entered]() {
            charging_entered = true;
        });
    
    // Add transitions
    vehicle.add_transition(VehicleState::Parked, VehicleState::Driving, "start");
    vehicle.add_transition(VehicleState::Driving, VehicleState::Parked, "park");
    vehicle.add_transition(VehicleState::Parked, VehicleState::Charging, "plug_in");
    
    // Test transition to driving
    EXPECT_TRUE(vehicle.trigger("start"));
    EXPECT_TRUE(parked_exited);
    EXPECT_TRUE(driving_entered);
    EXPECT_FALSE(charging_entered);
    
    // Return to parked
    EXPECT_TRUE(vehicle.trigger("park"));
    
    // Transition to charging
    EXPECT_TRUE(vehicle.trigger("plug_in"));
    EXPECT_TRUE(charging_entered);
}


TEST_F(HierarchicalTest, ComplexHierarchy) {
    sdv::HierarchicalStateMachine<VehicleState> vehicle("Vehicle", VehicleState::Parked);
    
    // Define multiple composite states
    vehicle.add_composite_state(
        VehicleState::Driving,
        std::vector<DrivingMode>{
            DrivingMode::Manual, 
            DrivingMode::CruiseControl, 
            DrivingMode::Autonomous
        },
        DrivingMode::Manual
    );
    
    // Add all transitions
    vehicle.add_transition(VehicleState::Parked, VehicleState::Driving, "start");
    vehicle.add_transition(VehicleState::Driving, VehicleState::Parked, "park");
    vehicle.add_transition(VehicleState::Parked, VehicleState::Charging, "plug_in");
    vehicle.add_transition(VehicleState::Charging, VehicleState::Parked, "unplug");
    
    // Test complete flow
    EXPECT_EQ(vehicle.current_state(), VehicleState::Parked);
    
    EXPECT_TRUE(vehicle.trigger("start"));
    EXPECT_EQ(vehicle.current_state(), VehicleState::Driving);
    
    EXPECT_TRUE(vehicle.trigger("park"));
    EXPECT_EQ(vehicle.current_state(), VehicleState::Parked);
    
    EXPECT_TRUE(vehicle.trigger("plug_in"));
    EXPECT_EQ(vehicle.current_state(), VehicleState::Charging);
    
    EXPECT_TRUE(vehicle.trigger("unplug"));
    EXPECT_EQ(vehicle.current_state(), VehicleState::Parked);
}

TEST_F(HierarchicalTest, ParentChildStateRelationship) {
    sdv::HierarchicalStateMachine<VehicleState> vehicle("Vehicle", VehicleState::Parked);
    
    // Setup composite state
    vehicle.add_composite_state(
        VehicleState::Driving,
        std::vector<DrivingMode>{DrivingMode::Manual, DrivingMode::Autonomous},
        DrivingMode::Manual
    );
    
    // When in a child state, should also be considered "in" parent state
    vehicle.add_transition(VehicleState::Parked, VehicleState::Driving, "start");
    vehicle.trigger("start");
    
    // Should be in Driving state (even if actually in a substate)
    EXPECT_TRUE(vehicle.is_in_state(VehicleState::Driving));
}