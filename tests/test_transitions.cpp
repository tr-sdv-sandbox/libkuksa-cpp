/**
 * @file test_transitions.cpp
 * @brief Unit tests for state machine transitions
 */

#include <gtest/gtest.h>
#include <glog/logging.h>
#include <kuksa_cpp/state_machine/state_machine.hpp>
#include <chrono>
#include <thread>

enum class DoorState {
    Closed,
    Opening,
    Open,
    Closing,
    Locked,
    _Count
};

class TransitionTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!google::IsGoogleLoggingInitialized()) {
            google::InitGoogleLogging("test");
            FLAGS_logtostderr = 1;
        }
    }
};

TEST_F(TransitionTest, GuardedTransitions) {
    sdv::StateMachine<DoorState> door("Door", DoorState::Closed);
    
    bool is_locked = true;
    
    // Add guarded transition
    door.add_transition(
        DoorState::Closed,
        DoorState::Opening,
        "open",
        [&is_locked](const sdv::Context& ctx) {
            return !is_locked;
        }
    );
    
    // Should fail when locked
    EXPECT_FALSE(door.trigger("open"));
    EXPECT_EQ(door.current_state(), DoorState::Closed);
    
    // Should succeed when unlocked
    is_locked = false;
    EXPECT_TRUE(door.trigger("open"));
    EXPECT_EQ(door.current_state(), DoorState::Opening);
}

TEST_F(TransitionTest, MultipleTransitionsFromSameState) {
    sdv::StateMachine<DoorState> door("Door", DoorState::Closed);
    
    // Add multiple transitions with different conditions
    door.add_transition(
        DoorState::Closed,
        DoorState::Opening,
        "open",
        [](const sdv::Context& ctx) {
            auto locked = ctx.find("locked");
            return locked == ctx.end() || !std::any_cast<bool>(locked->second);
        }
    );
    
    door.add_transition(
        DoorState::Closed,
        DoorState::Locked,
        "lock"
    );
    
    // Test lock transition
    EXPECT_TRUE(door.trigger("lock"));
    EXPECT_EQ(door.current_state(), DoorState::Locked);
}

TEST_F(TransitionTest, TransitionPriority) {
    sdv::StateMachine<DoorState> door("Door", DoorState::Closed);
    
    int check_order = 0;
    
    // Add multiple transitions for same trigger with different conditions
    // First transition - always false
    door.add_transition(
        DoorState::Closed,
        DoorState::Locked,
        "action",
        [&check_order](const sdv::Context& ctx) {
            check_order = 1;
            return false;
        }
    );
    
    // Second transition - always true
    door.add_transition(
        DoorState::Closed,
        DoorState::Opening,
        "action",
        [&check_order](const sdv::Context& ctx) {
            check_order = 2;
            return true;
        }
    );
    
    // Should use second transition
    EXPECT_TRUE(door.trigger("action"));
    EXPECT_EQ(door.current_state(), DoorState::Opening);
    EXPECT_EQ(check_order, 2);
}

TEST_F(TransitionTest, TransitionActions) {
    sdv::StateMachine<DoorState> door("Door", DoorState::Open);
    
    std::vector<std::string> action_log;
    
    // Add transition with logging action
    door.add_transition(
        DoorState::Open,
        DoorState::Closing,
        "close",
        {},  // No condition
        [&action_log](const sdv::Context& ctx) {
            action_log.push_back("Starting to close door");
            
            // Check for speed parameter
            auto speed_it = ctx.find("speed");
            if (speed_it != ctx.end()) {
                int speed = std::any_cast<int>(speed_it->second);
                action_log.push_back("Closing at speed: " + std::to_string(speed));
            }
        }
    );
    
    // Trigger with context
    sdv::Context ctx{{"speed", std::any(5)}};
    EXPECT_TRUE(door.trigger("close", ctx));
    
    // Check action was called
    EXPECT_EQ(action_log.size(), 2);
    EXPECT_EQ(action_log[0], "Starting to close door");
    EXPECT_EQ(action_log[1], "Closing at speed: 5");
}

TEST_F(TransitionTest, TransitionLatency) {
    sdv::StateMachine<DoorState> door("Door", DoorState::Closed);
    
    // Add transition with delay
    door.add_transition(
        DoorState::Closed,
        DoorState::Opening,
        "open",
        {},
        [](const sdv::Context& ctx) {
            // Simulate some work
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    );
    
    // Trigger and measure
    auto start = std::chrono::steady_clock::now();
    EXPECT_TRUE(door.trigger("open"));
    auto end = std::chrono::steady_clock::now();
    
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_GE(duration_ms, 10);
}

TEST_F(TransitionTest, SelfTransition) {
    sdv::StateMachine<DoorState> door("Door", DoorState::Open);
    
    bool refresh_called = false;
    
    // Add self-transition
    door.add_transition(
        DoorState::Open,
        DoorState::Open,
        "refresh",
        {},
        [&refresh_called](const sdv::Context& ctx) {
            refresh_called = true;
        }
    );
    
    // Trigger self-transition
    EXPECT_TRUE(door.trigger("refresh"));
    EXPECT_EQ(door.current_state(), DoorState::Open);
    EXPECT_TRUE(refresh_called);
}

TEST_F(TransitionTest, AsyncTransitions) {
    sdv::StateMachine<DoorState> door("Door", DoorState::Closed);
    
    // Add transitions
    door.add_transition(DoorState::Closed, DoorState::Opening, "open");
    door.add_transition(DoorState::Opening, DoorState::Open, "opened");
    
    // Trigger async
    auto future1 = door.trigger_async("open");
    auto future2 = door.trigger_async("opened");
    
    // Wait for results
    EXPECT_TRUE(future1.get());
    EXPECT_TRUE(future2.get());
    EXPECT_EQ(door.current_state(), DoorState::Open);
}

TEST_F(TransitionTest, TransitionRollback) {
    sdv::StateMachine<DoorState> door("Door", DoorState::Closed);
    
    // Add transition that throws in action
    door.add_transition(
        DoorState::Closed,
        DoorState::Opening,
        "open",
        {},
        [](const sdv::Context& ctx) {
            throw std::runtime_error("Motor failure");
        }
    );
    
    // Transition should fail due to exception
    try {
        door.trigger("open");
        FAIL() << "Expected exception";
    } catch (const std::exception& e) {
        // State should remain unchanged
        EXPECT_EQ(door.current_state(), DoorState::Closed);
        EXPECT_STREQ(e.what(), "Motor failure");
    }
}

TEST_F(TransitionTest, ComplexStateMachine) {
    sdv::StateMachine<DoorState> door("Door", DoorState::Closed);
    
    // Define all transitions for a complete door state machine
    door.add_transition(DoorState::Closed, DoorState::Opening, "open_requested");
    door.add_transition(DoorState::Opening, DoorState::Open, "fully_open");
    door.add_transition(DoorState::Open, DoorState::Closing, "close_requested");
    door.add_transition(DoorState::Closing, DoorState::Closed, "fully_closed");
    
    // Lock/unlock from closed state
    door.add_transition(DoorState::Closed, DoorState::Locked, "lock");
    door.add_transition(DoorState::Locked, DoorState::Closed, "unlock");
    
    // Emergency stop transitions
    door.add_transition(DoorState::Opening, DoorState::Open, "emergency_stop");
    door.add_transition(DoorState::Closing, DoorState::Open, "emergency_stop");
    
    // Test complete cycle
    EXPECT_TRUE(door.trigger("open_requested"));
    EXPECT_EQ(door.current_state(), DoorState::Opening);
    
    EXPECT_TRUE(door.trigger("fully_open"));
    EXPECT_EQ(door.current_state(), DoorState::Open);
    
    EXPECT_TRUE(door.trigger("close_requested"));
    EXPECT_EQ(door.current_state(), DoorState::Closing);
    
    EXPECT_TRUE(door.trigger("fully_closed"));
    EXPECT_EQ(door.current_state(), DoorState::Closed);
    
    // Test lock/unlock
    EXPECT_TRUE(door.trigger("lock"));
    EXPECT_EQ(door.current_state(), DoorState::Locked);
    
    EXPECT_FALSE(door.trigger("open_requested")); // Can't open when locked
    
    EXPECT_TRUE(door.trigger("unlock"));
    EXPECT_EQ(door.current_state(), DoorState::Closed);
}