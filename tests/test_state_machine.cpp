/**
 * @file test_state_machine.cpp
 * @brief Unit tests for core state machine functionality
 */

#include <gtest/gtest.h>
#include <glog/logging.h>
#include <kuksa_cpp/state_machine/state_machine.hpp>
#include <thread>
#include <atomic>
#include <algorithm>

// Test state enum
enum class TestState {
    Initial,
    Middle,
    Final,
    Error,
    _Count
};

class StateMachineTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize glog for tests
        if (!google::IsGoogleLoggingInitialized()) {
            google::InitGoogleLogging("test");
            FLAGS_logtostderr = 1;
        }
    }
};

TEST_F(StateMachineTest, InitialState) {
    sdv::StateMachine<TestState> sm("TestMachine", TestState::Initial);
    EXPECT_EQ(sm.current_state(), TestState::Initial);
}

TEST_F(StateMachineTest, SimpleTransition) {
    sdv::StateMachine<TestState> sm("TestMachine", TestState::Initial);
    
    // Add transition
    sm.add_transition(
        TestState::Initial,
        TestState::Middle,
        "go_middle"
    );
    
    // Trigger transition
    bool result = sm.trigger("go_middle");
    EXPECT_TRUE(result);
    EXPECT_EQ(sm.current_state(), TestState::Middle);
}

TEST_F(StateMachineTest, TransitionWithCondition) {
    sdv::StateMachine<TestState> sm("TestMachine", TestState::Initial);
    
    bool allow_transition = false;
    
    // Add conditional transition
    sm.add_transition(
        TestState::Initial,
        TestState::Middle,
        "go_middle",
        [&allow_transition](const sdv::Context& ctx) {
            return allow_transition;
        }
    );
    
    // Should fail when condition is false
    EXPECT_FALSE(sm.trigger("go_middle"));
    EXPECT_EQ(sm.current_state(), TestState::Initial);
    
    // Should succeed when condition is true
    allow_transition = true;
    EXPECT_TRUE(sm.trigger("go_middle"));
    EXPECT_EQ(sm.current_state(), TestState::Middle);
}

TEST_F(StateMachineTest, TransitionWithAction) {
    sdv::StateMachine<TestState> sm("TestMachine", TestState::Initial);
    
    bool action_called = false;
    
    // Add transition with action
    sm.add_transition(
        TestState::Initial,
        TestState::Middle,
        "go_middle",
        {},  // No condition
        [&action_called](const sdv::Context& ctx) {
            action_called = true;
        }
    );
    
    // Trigger transition
    sm.trigger("go_middle");
    EXPECT_TRUE(action_called);
    EXPECT_EQ(sm.current_state(), TestState::Middle);
}

TEST_F(StateMachineTest, StateEntryExitActions) {
    sdv::StateMachine<TestState> sm("TestMachine", TestState::Initial);
    
    bool initial_exited = false;
    bool middle_entered = false;
    bool middle_exited = false;
    bool final_entered = false;
    
    // Define states with actions
    sm.define_state(TestState::Initial)
        .on_exit([&initial_exited]() {
            initial_exited = true;
        });
    
    sm.define_state(TestState::Middle)
        .on_entry([&middle_entered]() {
            middle_entered = true;
        })
        .on_exit([&middle_exited]() {
            middle_exited = true;
        });
    
    sm.define_state(TestState::Final)
        .on_entry([&final_entered]() {
            final_entered = true;
        });
    
    // Add transitions
    sm.add_transition(TestState::Initial, TestState::Middle, "next");
    sm.add_transition(TestState::Middle, TestState::Final, "next");
    
    // First transition
    sm.trigger("next");
    EXPECT_TRUE(initial_exited);
    EXPECT_TRUE(middle_entered);
    EXPECT_FALSE(middle_exited);
    EXPECT_FALSE(final_entered);
    
    // Second transition
    sm.trigger("next");
    EXPECT_TRUE(middle_exited);
    EXPECT_TRUE(final_entered);
}

TEST_F(StateMachineTest, InvalidTransition) {
    sdv::StateMachine<TestState> sm("TestMachine", TestState::Initial);
    
    // Add only one transition
    sm.add_transition(TestState::Initial, TestState::Middle, "valid_trigger");
    
    // Try invalid trigger
    EXPECT_FALSE(sm.trigger("invalid_trigger"));
    EXPECT_EQ(sm.current_state(), TestState::Initial);
}

TEST_F(StateMachineTest, AvailableTriggers) {
    sdv::StateMachine<TestState> sm("TestMachine", TestState::Initial);
    
    // Add multiple transitions from Initial
    sm.add_transition(TestState::Initial, TestState::Middle, "go_middle");
    sm.add_transition(TestState::Initial, TestState::Final, "go_final");
    sm.add_transition(TestState::Initial, TestState::Error, "error");
    
    // Add transition from Middle
    sm.add_transition(TestState::Middle, TestState::Final, "finish");
    
    // Check available triggers from Initial
    auto triggers = sm.available_triggers();
    EXPECT_EQ(triggers.size(), 3);
    EXPECT_TRUE(std::find(triggers.begin(), triggers.end(), "go_middle") != triggers.end());
    EXPECT_TRUE(std::find(triggers.begin(), triggers.end(), "go_final") != triggers.end());
    EXPECT_TRUE(std::find(triggers.begin(), triggers.end(), "error") != triggers.end());
    
    // Move to Middle and check triggers
    sm.trigger("go_middle");
    triggers = sm.available_triggers();
    EXPECT_EQ(triggers.size(), 1);
    EXPECT_TRUE(std::find(triggers.begin(), triggers.end(), "finish") != triggers.end());
}


TEST_F(StateMachineTest, ContextPassing) {
    sdv::StateMachine<TestState> sm("TestMachine", TestState::Initial);
    
    std::string received_value;
    
    // Add transition that uses context
    sm.add_transition(
        TestState::Initial,
        TestState::Middle,
        "process",
        [](const sdv::Context& ctx) {
            // Check context has required key
            return ctx.find("value") != ctx.end();
        },
        [&received_value](const sdv::Context& ctx) {
            // Extract value from context
            auto it = ctx.find("value");
            if (it != ctx.end()) {
                received_value = std::any_cast<std::string>(it->second);
            }
        }
    );
    
    // Trigger without context - should fail
    EXPECT_FALSE(sm.trigger("process"));
    
    // Trigger with context - should succeed
    sdv::Context ctx{{"value", std::any(std::string("test_data"))}};
    EXPECT_TRUE(sm.trigger("process", ctx));
    EXPECT_EQ(received_value, "test_data");
}


TEST_F(StateMachineTest, ThreadSafety) {
    sdv::StateMachine<TestState> sm("TestMachine", TestState::Initial);
    
    // Add transitions
    sm.add_transition(TestState::Initial, TestState::Middle, "next");
    sm.add_transition(TestState::Middle, TestState::Final, "next");
    sm.add_transition(TestState::Final, TestState::Initial, "reset");
    
    // Counter for successful transitions
    std::atomic<int> success_count{0};
    
    // Launch multiple threads
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&sm, &success_count]() {
            for (int j = 0; j < 100; ++j) {
                if (sm.trigger("next")) {
                    success_count++;
                }
                if (sm.trigger("reset")) {
                    success_count++;
                }
            }
        });
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    // Should have made many transitions
    EXPECT_GT(success_count, 0);
    
    // State machine should still be in valid state
    auto state = sm.current_state();
    EXPECT_TRUE(state == TestState::Initial || 
                state == TestState::Middle || 
                state == TestState::Final);
}