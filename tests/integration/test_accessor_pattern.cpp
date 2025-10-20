/**
 * @file test_accessor_pattern.cpp
 * @brief Test demonstrating Client usage in state machine pattern
 */

#include "kuksa_test_fixture.hpp"
#include <gtest/gtest.h>
#include <kuksa_cpp/client.hpp>
#include <kuksa_cpp/resolver.hpp>
#include <glog/logging.h>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

using namespace kuksa;
using namespace kuksa::test;
using namespace std::chrono_literals;

// Simple event-driven state machine that uses Client
class HVACStateMachine {
public:
    enum class State {
        IDLE,
        COOLING,
        HEATING
    };
    
    enum class Event {
        TEMP_UPDATE,
        TIMER_EXPIRED,
        TARGET_CHANGED
    };
    
    struct EventData {
        Event type;
        float value;
    };
    
    HVACStateMachine(const std::string& address)
        : address_(address),
          state_(State::IDLE),
          running_(false) {
    }

    bool initialize() {
        // Create accessor for commands
        auto accessor_result = Client::create(address_);
        if (!accessor_result.ok()) {
            LOG(ERROR) << "Failed to create accessor: " << accessor_result.status();
            return false;
        }
        accessor_ = std::move(*accessor_result);

        // Get actuator handle from KUKSA and cache it
        auto resolver_result = Resolver::create(address_);
        if (!resolver_result.ok()) {
            LOG(ERROR) << "Failed to create resolver: " << resolver_result.status();
            return false;
        }
        auto resolver = std::move(*resolver_result);

        auto actuator_result = resolver->template get<int32_t>("Vehicle.Private.Test.Actuator");
        if (!actuator_result.ok()) {
            LOG(ERROR) << "Failed to get actuator handle: " << actuator_result.status();
            return false;
        }
        actuator_handle_ = *actuator_result;

        // Start event processing thread
        running_ = true;
        event_thread_ = std::thread([this]() { event_loop(); });

        return true;
    }
    
    void shutdown() {
        running_ = false;
        cv_.notify_all();
        if (event_thread_.joinable()) {
            event_thread_.join();
        }
    }
    
    // Called from subscription callback (non-blocking)
    void on_temperature_update(float temp) {
        post_event({Event::TEMP_UPDATE, temp});
    }
    
    // Called from timer (non-blocking)
    void on_timer_expired() {
        post_event({Event::TIMER_EXPIRED, 0});
    }
    
    State get_state() const {
        return state_.load();
    }
    
private:
    void post_event(EventData event) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            event_queue_.push(event);
        }
        cv_.notify_one();
    }
    
    void event_loop() {
        while (running_) {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, 100ms, [this] {
                return !event_queue_.empty() || !running_;
            });
            
            // Process all pending events
            while (!event_queue_.empty()) {
                auto event = event_queue_.front();
                event_queue_.pop();
                lock.unlock();
                
                process_event(event);
                
                lock.lock();
            }
        }
    }
    
    void process_event(const EventData& event) {
        State current = state_.load();
        
        switch (current) {
            case State::IDLE:
                if (event.type == Event::TEMP_UPDATE) {
                    if (event.value > 25.0f) {
                        LOG(INFO) << "Temperature too high (" << event.value << "), starting cooling";

                        // Use accessor to command actuator - thread-safe!
                        auto status = accessor_->set(*actuator_handle_, 1);  // 1 = cooling mode
                        if (!status.ok()) {
                            LOG(ERROR) << "Failed to set cooling mode: " << status;
                            // Don't transition if command failed
                            return;
                        }
                        transition_to(State::COOLING);
                    } else if (event.value < 18.0f) {
                        LOG(INFO) << "Temperature too low (" << event.value << "), starting heating";

                        auto status = accessor_->set(*actuator_handle_, 2);  // 2 = heating mode
                        if (!status.ok()) {
                            LOG(ERROR) << "Failed to set heating mode: " << status;
                            // Don't transition if command failed
                            return;
                        }
                        transition_to(State::HEATING);
                    }
                }
                break;
                
            case State::COOLING:
                if (event.type == Event::TEMP_UPDATE && event.value <= 22.0f) {
                    LOG(INFO) << "Temperature normal (" << event.value << "), stopping cooling";

                    auto status = accessor_->set(*actuator_handle_, 0);  // 0 = off
                    if (!status.ok()) {
                        LOG(ERROR) << "Failed to turn off: " << status;
                        // Don't transition if command failed - stay in COOLING until successful
                        return;
                    }
                    transition_to(State::IDLE);
                }
                break;

            case State::HEATING:
                if (event.type == Event::TEMP_UPDATE && event.value >= 22.0f) {
                    LOG(INFO) << "Temperature normal (" << event.value << "), stopping heating";

                    auto status = accessor_->set(*actuator_handle_, 0);  // 0 = off
                    if (!status.ok()) {
                        LOG(ERROR) << "Failed to turn off: " << status;
                        // Don't transition if command failed - stay in HEATING until successful
                        return;
                    }
                    transition_to(State::IDLE);
                }
                break;
        }
    }
    
    void transition_to(State new_state) {
        LOG(INFO) << "State transition: " << state_name(state_.load()) 
                  << " -> " << state_name(new_state);
        state_ = new_state;
    }
    
    const char* state_name(State s) {
        switch (s) {
            case State::IDLE: return "IDLE";
            case State::COOLING: return "COOLING";
            case State::HEATING: return "HEATING";
            default: return "UNKNOWN";
        }
    }

    std::string address_;
    std::unique_ptr<Client> accessor_;
    std::optional<SignalHandle<int32_t>> actuator_handle_;  // Cached handle
    std::atomic<State> state_;
    std::thread event_thread_;
    std::queue<EventData> event_queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_;
};

class AccessorPatternTest : public KuksaTestFixture {
protected:
    template<typename Pred>
    bool wait_for(Pred pred, std::chrono::milliseconds timeout = 5000ms) {
        auto start = std::chrono::steady_clock::now();
        while (!pred() && std::chrono::steady_clock::now() - start < timeout) {
            std::this_thread::sleep_for(10ms);
        }
        return pred();
    }
};

TEST_F(AccessorPatternTest, StateMachineWithSeparateAccessor) {
    LOG(INFO) << "Testing state machine pattern with Client + Client + Client";

    // First, resolve the actuator handle
    auto resolver_for_actuator_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_for_actuator_result.ok()) << "Failed to create resolver: " << resolver_for_actuator_result.status();
    auto resolver_for_actuator = std::move(*resolver_for_actuator_result);

    auto actuator_rw_result = resolver_for_actuator->get<int32_t>("Vehicle.Private.Test.Actuator");
    ASSERT_TRUE(actuator_rw_result.ok()) << "Failed to get actuator handle: " << actuator_rw_result.status();
    auto actuator_rw = *actuator_rw_result;

    // Create client to own the actuator
    std::atomic<int32_t> last_actuator_command(0);
    Client* client_ptr = nullptr;

    auto client = *Client::create(getKuksaAddress());
    client_ptr = client.get();

    client->serve_actuator(actuator_rw, [&, client_ptr](int32_t target, const SignalHandle<int32_t>& handle) {
        LOG(INFO) << "Client received actuation command: " << target;
        last_actuator_command = target;

        // Simulate hardware response - publish actual value
        auto status = client_ptr->publish(handle, target);
        if (!status.ok()) {
            LOG(ERROR) << "Failed to publish actual: " << status;
        }
    });

    auto start_status = client->start();
    ASSERT_TRUE(start_status.ok()) << "Failed to start client: " << start_status;

    // Wait for client to be ready
    auto client_ready_status = client->wait_until_ready(std::chrono::milliseconds(5000));
    ASSERT_TRUE(client_ready_status.ok()) << "Client not ready: " << client_ready_status;

    // Create state machine with its own accessor
    HVACStateMachine state_machine(getKuksaAddress());
    ASSERT_TRUE(state_machine.initialize());

    // Simulate temperature changes using another accessor
    auto simulator = *Client::create(getKuksaAddress());
    ASSERT_TRUE(simulator) << "Failed to create simulator accessor";

    // Get sensor handle
    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok()) << "Failed to create resolver: " << resolver_result.status();
    auto resolver = std::move(*resolver_result);

    auto cabin_temp_result = resolver->get<float>("Vehicle.Private.Test.Sensor");
    ASSERT_TRUE(cabin_temp_result.ok()) << "Failed to get sensor: " << cabin_temp_result.status();
    auto cabin_temp = *cabin_temp_result;

    // Get RW handle for publishing
    auto cabin_temp_rw_result = resolver->get<float>("Vehicle.Private.Test.Sensor");
    ASSERT_TRUE(cabin_temp_rw_result.ok()) << "Failed to get sensor RW handle: " << cabin_temp_rw_result.status();
    auto cabin_temp_rw = *cabin_temp_rw_result;

    // Create dedicated subscriber (instead of Client)
    auto subscriber = *Client::create(getKuksaAddress());

    // Subscribe to temperature updates (using test sensor)
    subscriber->subscribe(cabin_temp, [&state_machine](vss::types::QualifiedValue<float> qvalue) {
        if (qvalue.is_valid()) {
            // Non-blocking call - just posts event
            state_machine.on_temperature_update(*qvalue.value);
        }
    });

    auto sub_start_status = subscriber->start();
    ASSERT_TRUE(sub_start_status.ok()) << "Failed to start subscriber: " << sub_start_status;

    // Wait for subscriber to be ready
    auto ready_status = subscriber->wait_until_ready(std::chrono::milliseconds(5000));
    ASSERT_TRUE(ready_status.ok()) << "Subscriber not ready: " << ready_status;

    // Test 1: High temperature triggers cooling
    ASSERT_TRUE(simulator->set(cabin_temp_rw, 28.0f).ok());
    
    ASSERT_TRUE(wait_for([&]() {
        return state_machine.get_state() == HVACStateMachine::State::COOLING;
    })) << "State machine should transition to COOLING";
    
    // Verify the actuator received the cooling command (1)
    ASSERT_TRUE(wait_for([&]() {
        return last_actuator_command.load() == 1;
    })) << "Provider should receive cooling command";
    
    // Verify HVAC commands were sent
    std::this_thread::sleep_for(500ms);  // Give commands time to propagate

    // Test 2: Normal temperature stops cooling
    ASSERT_TRUE(simulator->set(cabin_temp_rw, 22.0f).ok());

    ASSERT_TRUE(wait_for([&]() {
        return state_machine.get_state() == HVACStateMachine::State::IDLE;
    })) << "State machine should return to IDLE";

    // Test 3: Low temperature triggers heating
    ASSERT_TRUE(simulator->set(cabin_temp_rw, 15.0f).ok());
    
    ASSERT_TRUE(wait_for([&]() {
        return state_machine.get_state() == HVACStateMachine::State::HEATING;
    })) << "State machine should transition to HEATING";
    
    // Verify the actuator received the heating command (2)
    ASSERT_TRUE(wait_for([&]() {
        return last_actuator_command.load() == 2;
    })) << "Provider should receive heating command";
    
    // Cleanup
    subscriber->stop();
    state_machine.shutdown();
    client->stop();
}

TEST_F(AccessorPatternTest, CompletePatternShowcase) {
    LOG(INFO) << "Showcasing complete VSS pattern: Subscriber + Accessor + Client";

    // Step 1: Resolve actuator handle
    auto resolver_for_client_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_for_client_result.ok()) << "Failed to create resolver: " << resolver_for_client_result.status();
    auto resolver_for_client = std::move(*resolver_for_client_result);

    auto client_actuator_result = resolver_for_client->get<int32_t>("Vehicle.Private.Test.Int32Actuator");
    ASSERT_TRUE(client_actuator_result.ok()) << "Failed to get actuator handle: " << client_actuator_result.status();
    auto client_actuator = *client_actuator_result;

    // Create Client to own the actuator
    std::atomic<int32_t> actuator_commands_received(0);
    Client* client_ptr = nullptr;

    auto client = *Client::create(getKuksaAddress());
    client_ptr = client.get();

    client->serve_actuator(client_actuator, [&, client_ptr](int32_t target, const SignalHandle<int32_t>& handle) {
        LOG(INFO) << "Client processing command: " << target;
        actuator_commands_received++;

        // Simulate processing delay
        std::this_thread::sleep_for(10ms);

        // Publish actual value
        auto status = client_ptr->publish(handle, target);
        if (!status.ok()) {
            LOG(ERROR) << "Failed to publish actual: " << status;
        }
    });

    auto start_status2 = client->start();
    ASSERT_TRUE(start_status2.ok()) << "Failed to start client: " << start_status2;

    // Wait for client to be ready
    auto ready_status = client->wait_until_ready(std::chrono::milliseconds(5000));
    ASSERT_TRUE(ready_status.ok()) << "Client not ready: " << ready_status;

    // Get handles for subscribing
    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok()) << "Failed to create resolver: " << resolver_result.status();
    auto resolver = std::move(*resolver_result);

    auto test_sensor_result = resolver->get<float>("Vehicle.Private.Test.Sensor");
    ASSERT_TRUE(test_sensor_result.ok()) << "Failed to get sensor: " << test_sensor_result.status();
    auto test_sensor = *test_sensor_result;

    auto test_actuator_result = resolver->get<int32_t>("Vehicle.Private.Test.Int32Actuator");
    ASSERT_TRUE(test_actuator_result.ok()) << "Failed to get actuator: " << test_actuator_result.status();
    auto test_actuator = *test_actuator_result;

    // Step 2: Create Subscriber to monitor values
    auto subscriber = *Client::create(getKuksaAddress());

    std::atomic<int32_t> sensor_updates_received(0);
    std::atomic<float> last_sensor_value(0.0f);

    subscriber->subscribe(test_sensor, [&](vss::types::QualifiedValue<float> qvalue) {
        if (qvalue.is_valid()) {
            LOG(INFO) << "Subscriber received sensor update: " << *qvalue.value;
            last_sensor_value = *qvalue.value;
            sensor_updates_received++;
        }
    });

    std::atomic<int32_t> actuator_updates_received(0);
    std::atomic<int32_t> last_actuator_value(0);

    subscriber->subscribe(test_actuator, [&](vss::types::QualifiedValue<int32_t> qvalue) {
        if (qvalue.is_valid()) {
            LOG(INFO) << "Subscriber received actuator feedback: " << *qvalue.value;
            last_actuator_value = *qvalue.value;
            actuator_updates_received++;
        }
    });

    auto sub_start_status2 = subscriber->start();
    ASSERT_TRUE(sub_start_status2.ok()) << "Failed to start subscriber: " << sub_start_status2;

    // Wait for subscriber to be ready
    auto ready_status2 = subscriber->wait_until_ready(std::chrono::milliseconds(5000));
    ASSERT_TRUE(ready_status2.ok()) << "Subscriber not ready: " << ready_status2;

    std::this_thread::sleep_for(200ms);  // Give subscriber time to connect

    // Step 3: Create Accessor for synchronous operations
    auto accessor = *Client::create(getKuksaAddress());
    ASSERT_TRUE(accessor) << "Failed to create accessor";

    // Get handles from resolver
    auto resolver2_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver2_result.ok()) << "Failed to create resolver: " << resolver2_result.status();
    auto resolver2 = std::move(*resolver2_result);

    auto sensor_handle_result = resolver2->get<float>("Vehicle.Private.Test.Sensor");
    ASSERT_TRUE(sensor_handle_result.ok()) << "Failed to get sensor: " << sensor_handle_result.status();
    auto sensor_handle = *sensor_handle_result;

    auto actuator_handle_result = resolver2->get<int32_t>("Vehicle.Private.Test.Int32Actuator");
    ASSERT_TRUE(actuator_handle_result.ok()) << "Failed to get actuator: " << actuator_handle_result.status();
    auto actuator_handle = *actuator_handle_result;

    // Test flow:
    // 1. Accessor publishes sensor value
    LOG(INFO) << "Accessor publishing sensor value...";

    // Reset counters to ensure clean test
    sensor_updates_received = 0;
    last_sensor_value = 0.0f;

    ASSERT_TRUE(accessor->set(sensor_handle, 42.5f).ok());

    ASSERT_TRUE(wait_for([&]() { return sensor_updates_received.load() > 0; }))
        << "Subscriber should receive sensor update";

    // Wait a bit more to ensure value is fully propagated
    std::this_thread::sleep_for(100ms);
    EXPECT_FLOAT_EQ(last_sensor_value.load(), 42.5f);

    // 2. Accessor commands actuator
    LOG(INFO) << "Accessor commanding actuator...";
    ASSERT_TRUE(accessor->set(actuator_handle, 100).ok());
    
    ASSERT_TRUE(wait_for([&]() { return actuator_commands_received.load() > 0; }))
        << "Provider should receive actuation command";
    
    ASSERT_TRUE(wait_for([&]() { return actuator_updates_received.load() > 0; }))
        << "Subscriber should receive actuator feedback";
    EXPECT_EQ(last_actuator_value.load(), 100);

    // 3. Accessor can also read values
    LOG(INFO) << "Accessor reading current values...";
    auto sensor_value_result = accessor->get(sensor_handle);
    ASSERT_TRUE(sensor_value_result.ok()) << "Failed to get sensor value: " << sensor_value_result.status();
    ASSERT_TRUE(sensor_value_result->is_valid()) << "Sensor value is not valid";
    EXPECT_FLOAT_EQ(*sensor_value_result->value, 42.5f);
    
    // Summary
    LOG(INFO) << "Pattern demonstration complete:";
    LOG(INFO) << "  - Client handled " << actuator_commands_received.load() << " commands";
    LOG(INFO) << "  - Subscriber received " << sensor_updates_received.load() << " sensor updates";
    LOG(INFO) << "  - Subscriber received " << actuator_updates_received.load() << " actuator updates";
    LOG(INFO) << "  - Accessor performed synchronous read/write operations";

    // Cleanup
    subscriber->stop();
    client->stop();
}

TEST_F(AccessorPatternTest, ThreadSafetyStressTest) {
    LOG(INFO) << "Testing thread safety of Client";

    const int NUM_THREADS = 10;
    const int OPS_PER_THREAD = 100;

    auto accessor = *Client::create(getKuksaAddress());
    ASSERT_TRUE(accessor) << "Failed to create accessor";

    // Get sensor handle once before threads
    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok()) << "Failed to create resolver: " << resolver_result.status();
    auto resolver = std::move(*resolver_result);

    auto test_sensor_result = resolver->get<int32_t>("Vehicle.Private.Test.Int32Sensor");
    ASSERT_TRUE(test_sensor_result.ok()) << "Failed to get sensor: " << test_sensor_result.status();
    auto test_sensor = *test_sensor_result;

    std::atomic<int> successful_ops(0);
    std::vector<std::thread> threads;

    // Launch multiple threads all using the same accessor
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&, thread_id = i]() {

            for (int j = 0; j < OPS_PER_THREAD; ++j) {
                int32_t value = thread_id * 1000 + j;

                // Alternate between set and get
                if (j % 2 == 0) {
                    if (accessor->set(test_sensor, value).ok()) {
                        successful_ops++;
                    }
                } else {
                    auto result = accessor->get(test_sensor);
                    if (result.ok() && result->is_valid()) {
                        successful_ops++;
                    }
                }

                // Small random delay
                std::this_thread::sleep_for(std::chrono::microseconds(rand() % 1000));
            }
        });
    }

    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }

    LOG(INFO) << "Successful operations: " << successful_ops.load()
              << " out of " << (NUM_THREADS * OPS_PER_THREAD);

    // We expect most operations to succeed
    EXPECT_GT(successful_ops.load(), NUM_THREADS * OPS_PER_THREAD * 0.8);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    google::InitGoogleLogging(argv[0]);
    google::SetStderrLogging(google::INFO);
    
    LOG(INFO) << "Client Pattern Integration Tests";
    LOG(INFO) << "=====================================";
    
    return RUN_ALL_TESTS();
}