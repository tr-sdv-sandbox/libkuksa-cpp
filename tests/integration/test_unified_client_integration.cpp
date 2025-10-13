/**
 * @file test_unified_client_integration.cpp
 * @brief Comprehensive integration tests for unified Client
 *
 * Tests the unified Client which combines actuator provider and subscriber
 * in a single client with dual streams over one gRPC channel.
 *
 * Test scenarios:
 * 1. Basic unified client usage (actuator + subscription + publishing)
 * 2. Batch publishing with type-safe API
 * 3. Provider restart resilience (actuator survives restart)
 * 4. Sensor feeder + actuator controller coordination
 * 5. Concurrent operations (publish + actuation + subscription)
 */

#include "kuksa_test_fixture.hpp"
#include <gtest/gtest.h>
#include <kuksa_cpp/client.hpp>
#include <kuksa_cpp/resolver.hpp>
#include <glog/logging.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <queue>
#include <condition_variable>

using namespace kuksa;
using namespace kuksa::test;
using namespace std::chrono_literals;

class UnifiedClientIntegrationTest : public KuksaTestFixture {
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

// ============================================================================
// Test 1: Basic Unified Client Usage
// ============================================================================

TEST_F(UnifiedClientIntegrationTest, BasicUnifiedClient) {
    LOG(INFO) << "Testing basic unified Client with actuator + subscription + publishing";

    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok());
    auto resolver = std::move(*resolver_result);

    // Create unified client
    auto client = *Client::create(getKuksaAddress());

    // 1. Register actuator
    auto door_lock_result = resolver->get<bool>("Vehicle.Private.Test.BoolActuator");
    ASSERT_TRUE(door_lock_result.ok());
    auto door_lock = *door_lock_result;

    std::atomic<bool> actuator_called{false};
    std::atomic<bool> last_target{false};

    client->serve_actuator(door_lock, [&](bool target, const SignalHandle<bool>& handle) {
        LOG(INFO) << "Actuator callback: target=" << target;
        last_target = target;
        actuator_called = true;

        // NOTE: Don't call publish from inside the callback - it runs on gRPC thread
        // In real code, queue to a state machine thread that will publish later
    });

    // 2. Subscribe to a sensor
    auto temp_result = resolver->get<float>("Vehicle.Private.Test.FloatSensor");
    ASSERT_TRUE(temp_result.ok());
    auto temp_sensor = *temp_result;

    std::atomic<float> last_temp{0.0f};
    std::atomic<bool> subscription_called{false};

    client->subscribe(temp_sensor, [&](std::optional<float> value) {
        if (value) {
            LOG(INFO) << "Subscription callback: temp=" << *value;
            last_temp = *value;
            subscription_called = true;
        }
    });

    // 3. Get RW handle for sensor we'll publish to (no registration needed!)
    auto temp_rw_result = resolver->get<float>("Vehicle.Private.Test.FloatSensor");
    ASSERT_TRUE(temp_rw_result.ok());
    auto temp_rw = *temp_rw_result;

    // 4. Start client
    client->start();
    auto ready_status = client->wait_until_ready(5000ms);
    ASSERT_TRUE(ready_status.ok()) << "Client not ready: " << ready_status;

    std::this_thread::sleep_for(100ms);

    // 4. Test actuation
    auto accessor = *Client::create(getKuksaAddress());
    ASSERT_TRUE(accessor) << "Failed to create accessor";

    ASSERT_TRUE(accessor->set(door_lock, true).ok());
    ASSERT_TRUE(wait_for([&]() { return actuator_called.load(); }))
        << "Actuator should be called";
    EXPECT_EQ(last_target.load(), true);

    // 5. Test publishing sensor value
    auto status = client->publish(temp_rw, 22.5f);
    ASSERT_TRUE(status.ok()) << "Publish failed: " << status;

    // Give time for publish to propagate
    std::this_thread::sleep_for(200ms);

    ASSERT_TRUE(wait_for([&]() { return subscription_called.load(); }, 2000ms))
        << "Subscription should receive published value";
    EXPECT_FLOAT_EQ(last_temp.load(), 22.5f);

    // Give time for any pending operations
    std::this_thread::sleep_for(100ms);

    client->stop();
}

// ============================================================================
// Test 2: Batch Publishing with Type-Safe API
// ============================================================================

TEST_F(UnifiedClientIntegrationTest, BatchPublishing) {
    LOG(INFO) << "Testing type-safe batch publishing";

    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok());
    auto resolver = std::move(*resolver_result);

    // Use TWO separate clients to avoid ownership conflict:
    // - Publisher client: owns and publishes sensors
    // - Subscriber client: subscribes to those sensors
    auto publisher = *Client::create(getKuksaAddress());
    auto subscriber = *Client::create(getKuksaAddress());

    // Subscriber subscribes to sensors
    auto speed_result = resolver->get<float>("Vehicle.Private.Test.FloatSensor");
    auto rpm_result = resolver->get<uint32_t>("Vehicle.Private.Test.UInt32Sensor");

    ASSERT_TRUE(speed_result.ok() && rpm_result.ok());
    auto speed = *speed_result;
    auto rpm = *rpm_result;

    std::atomic<int> updates_received{0};
    std::atomic<float> last_speed{0.0f};
    std::atomic<uint32_t> last_rpm{0};

    subscriber->subscribe(speed, [&](std::optional<float> value) {
        if (value) {
            last_speed = *value;
            updates_received++;
        }
    });

    subscriber->subscribe(rpm, [&](std::optional<uint32_t> value) {
        if (value) {
            last_rpm = *value;
            updates_received++;
        }
    });

    subscriber->start();
    ASSERT_TRUE(subscriber->wait_until_ready(5000ms).ok());

    // Publisher publishes sensors (no registration needed!)
    auto speed_rw_result = resolver->get<float>("Vehicle.Private.Test.FloatSensor");
    auto rpm_rw_result = resolver->get<uint32_t>("Vehicle.Private.Test.UInt32Sensor");

    ASSERT_TRUE(speed_rw_result.ok() && rpm_rw_result.ok());
    auto speed_rw = *speed_rw_result;
    auto rpm_rw = *rpm_rw_result;

    publisher->start();
    ASSERT_TRUE(publisher->wait_until_ready(5000ms).ok());

    std::this_thread::sleep_for(100ms);

    // Batch publish with type-safe API (no Value{} wrappers!)
    std::atomic<bool> batch_callback_called{false};
    std::atomic<bool> batch_succeeded{false};

    auto status = publisher->publish_batch(
        {
            {speed_rw, 120.5f},
            {rpm_rw, uint32_t(3500)}
        },
        [&](const std::map<int32_t, absl::Status>& errors) {
            batch_callback_called = true;
            batch_succeeded = errors.empty();
            if (!errors.empty()) {
                for (const auto& [id, err] : errors) {
                    LOG(ERROR) << "Batch error for signal " << id << ": " << err;
                }
            }
        }
    );

    ASSERT_TRUE(status.ok()) << "Batch publish failed: " << status;

    // Give time for batch to be sent and processed
    std::this_thread::sleep_for(300ms);

    // Wait for all updates
    ASSERT_TRUE(wait_for([&]() { return updates_received.load() >= 2; }, 3000ms))
        << "Should receive all 2 subscription updates, got: " << updates_received.load();

    // Wait for batch callback
    ASSERT_TRUE(wait_for([&]() { return batch_callback_called.load(); }, 2000ms))
        << "Batch callback should be invoked";

    EXPECT_TRUE(batch_succeeded.load()) << "Batch publish should succeed";

    // Verify values
    EXPECT_FLOAT_EQ(last_speed.load(), 120.5f);
    EXPECT_EQ(last_rpm.load(), 3500u);

    // Give time for cleanup
    std::this_thread::sleep_for(100ms);

    publisher->stop();
    subscriber->stop();
}

// ============================================================================
// Test 3: Actuator Provider Restart Resilience
// ============================================================================

TEST_F(UnifiedClientIntegrationTest, ProviderRestartResilience) {
    LOG(INFO) << "Testing actuator survives provider restart";

    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok());
    auto resolver = std::move(*resolver_result);

    // Get handles
    auto actuator_result = resolver->get<int32_t>("Vehicle.Private.Test.Int32Actuator");
    ASSERT_TRUE(actuator_result.ok());
    auto actuator = *actuator_result;

    std::atomic<int> actuator_call_count{0};
    std::atomic<int32_t> last_value{0};

    // Helper to create and start client
    auto create_client = [&]() {
        auto client = *Client::create(getKuksaAddress());

        client->serve_actuator(actuator, [&](int32_t target, const SignalHandle<int32_t>& handle) {
            LOG(INFO) << "Actuator called with: " << target;
            last_value = target;
            actuator_call_count++;

            // NOTE: Don't publish from callback - runs on gRPC thread
        });

        client->start();
        auto ready = client->wait_until_ready(5000ms);
        EXPECT_TRUE(ready.ok()) << "Client not ready: " << ready;

        return client;
    };

    // Create first client
    LOG(INFO) << "Starting first client instance";
    auto client1 = create_client();
    std::this_thread::sleep_for(200ms);

    // Test actuation with first client
    auto accessor = *Client::create(getKuksaAddress());
    ASSERT_TRUE(accessor) << "Failed to create accessor";

    LOG(INFO) << "Sending actuation #1";
    ASSERT_TRUE(accessor->set(actuator, 100).ok());
    ASSERT_TRUE(wait_for([&]() { return actuator_call_count.load() >= 1; }));
    EXPECT_EQ(last_value.load(), 100);

    int count_after_first = actuator_call_count.load();

    // Stop first client (simulates crash/restart)
    LOG(INFO) << "Stopping first client (simulating restart)";
    client1->stop();
    client1.reset();
    std::this_thread::sleep_for(1500ms);  // Allow KUKSA to detect disconnect and release ownership

    // Create second client (restart)
    LOG(INFO) << "Starting second client instance (restart)";
    auto client2 = create_client();
    std::this_thread::sleep_for(200ms);

    // Test actuation with restarted client
    LOG(INFO) << "Sending actuation #2 (after restart)";
    ASSERT_TRUE(accessor->set(actuator, 200).ok());
    ASSERT_TRUE(wait_for([&]() { return actuator_call_count.load() > count_after_first; }))
        << "Actuator should work after restart";
    EXPECT_EQ(last_value.load(), 200);

    // Cleanup
    client2->stop();
}

// ============================================================================
// Test 4: Sensor Feeder + Actuator Controller Coordination
// ============================================================================

TEST_F(UnifiedClientIntegrationTest, SensorFeederActuatorCoordination) {
    LOG(INFO) << "Testing sensor feeder coordinating with actuator controller";

    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok());
    auto resolver = std::move(*resolver_result);

    // Scenario: Temperature controller
    // - Sensor feeder publishes temperature readings
    // - When temp > 25, actuate HVAC to cooling
    // - HVAC controller receives actuation and publishes actual state

    auto temp_sensor_result = resolver->get<float>("Vehicle.Private.Test.FloatSensor");
    auto hvac_actuator_result = resolver->get<bool>("Vehicle.Private.Test.BoolActuator");
    ASSERT_TRUE(temp_sensor_result.ok() && hvac_actuator_result.ok());
    auto temp_sensor = *temp_sensor_result;
    auto hvac_actuator = *hvac_actuator_result;

    // === HVAC Controller (unified client with actuator + subscription) ===
    auto hvac_controller = *Client::create(getKuksaAddress());

    std::atomic<bool> hvac_cooling_actual{false};
    std::atomic<float> current_temp{0.0f};

    // Subscribe to temperature
    hvac_controller->subscribe(temp_sensor, [&](std::optional<float> value) {
        if (value) {
            LOG(INFO) << "HVAC: Temperature reading: " << *value;
            current_temp = *value;
        }
    });

    // Serve HVAC actuator
    hvac_controller->serve_actuator(hvac_actuator, [&](bool cooling_target, const SignalHandle<bool>& handle) {
        LOG(INFO) << "HVAC: Received cooling command: " << cooling_target;
        hvac_cooling_actual = cooling_target;

        // NOTE: Don't publish from callback - runs on gRPC thread
        // In real code, queue to state machine
    });

    hvac_controller->start();
    ASSERT_TRUE(hvac_controller->wait_until_ready(5000ms).ok());
    std::this_thread::sleep_for(100ms);

    // === Sensor Feeder (unified client for publishing) ===
    auto sensor_feeder = *Client::create(getKuksaAddress());

    // No actuators or subscriptions - just for publishing
    // We'll publish using accessor instead

    // === Temperature Monitor (decides when to actuate HVAC) ===
    auto temp_monitor = *Client::create(getKuksaAddress());

    std::atomic<bool> should_cool{false};
    std::atomic<bool> saw_low_temp{false};  // Track if we've seen expected low temp

    temp_monitor->subscribe(temp_sensor, [&](std::optional<float> value) {
        if (value) {
            // Track if we've seen the expected low temperature (20°C)
            if (*value >= 19.0f && *value <= 21.0f) {
                saw_low_temp = true;
            }

            // Only activate cooling after we've seen the low temp (ignores stale values)
            if (saw_low_temp.load() && *value > 25.0f && !should_cool.load()) {
                LOG(INFO) << "Monitor: Temp " << *value << " > 25, activating cooling";
                should_cool = true;
            }
        }
    });

    temp_monitor->start();
    ASSERT_TRUE(temp_monitor->wait_until_ready(5000ms).ok());
    std::this_thread::sleep_for(100ms);

    // === Test Scenario ===

    // 1. Publish low temperature
    auto accessor = *Client::create(getKuksaAddress());
    ASSERT_TRUE(accessor) << "Failed to create accessor";

    auto temp_rw_result = resolver->get<float>("Vehicle.Private.Test.FloatSensor");
    ASSERT_TRUE(temp_rw_result.ok());
    auto temp_rw = *temp_rw_result;

    LOG(INFO) << "Publishing temperature: 20°C";
    ASSERT_TRUE(accessor->set(temp_rw, 20.0f).ok());
    ASSERT_TRUE(wait_for([&]() { return current_temp.load() > 19.0f; }));
    EXPECT_FALSE(should_cool.load()) << "Should not cool at 20°C";

    // 2. Publish high temperature
    LOG(INFO) << "Publishing temperature: 30°C";
    ASSERT_TRUE(accessor->set(temp_rw, 30.0f).ok());
    ASSERT_TRUE(wait_for([&]() { return should_cool.load(); }))
        << "Monitor should decide to cool";

    // 3. Actuate HVAC
    LOG(INFO) << "Actuating HVAC cooling";
    ASSERT_TRUE(accessor->set(hvac_actuator, true).ok());
    ASSERT_TRUE(wait_for([&]() { return hvac_cooling_actual.load(); }))
        << "HVAC should receive actuation";

    // Cleanup
    temp_monitor->stop();
    hvac_controller->stop();
}

// ============================================================================
// Test 5: Concurrent Operations
// ============================================================================

TEST_F(UnifiedClientIntegrationTest, ConcurrentOperations) {
    LOG(INFO) << "Testing concurrent publish + actuation + subscription";

    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok());
    auto resolver = std::move(*resolver_result);

    // Use separate clients: one for actuator+subscription, one for publishing
    auto actuator_client = *Client::create(getKuksaAddress());
    auto publisher_client = *Client::create(getKuksaAddress());

    // Actuator client handles actuator
    auto actuator_result = resolver->get<int32_t>("Vehicle.Private.Test.Int32Actuator");
    ASSERT_TRUE(actuator_result.ok());
    auto actuator = *actuator_result;

    std::atomic<int> actuation_count{0};

    actuator_client->serve_actuator(actuator, [&](int32_t target, const SignalHandle<int32_t>& handle) {
        actuation_count++;
        // NOTE: Don't publish from callback
    });

    // Actuator client subscribes to sensor
    auto sensor_result = resolver->get<float>("Vehicle.Private.Test.FloatSensor");
    ASSERT_TRUE(sensor_result.ok());
    auto sensor = *sensor_result;

    std::atomic<int> subscription_count{0};

    actuator_client->subscribe(sensor, [&](std::optional<float> value) {
        if (value) {
            subscription_count++;
        }
    });

    actuator_client->start();
    ASSERT_TRUE(actuator_client->wait_until_ready(5000ms).ok());

    // Publisher client publishes sensor (no registration needed!)
    auto sensor_rw_result = resolver->get<float>("Vehicle.Private.Test.FloatSensor");
    ASSERT_TRUE(sensor_rw_result.ok());
    auto sensor_rw = *sensor_rw_result;

    publisher_client->start();
    ASSERT_TRUE(publisher_client->wait_until_ready(5000ms).ok());
    std::this_thread::sleep_for(100ms);

    // Get accessor for actuations
    auto accessor = *Client::create(getKuksaAddress());
    ASSERT_TRUE(accessor) << "Failed to create accessor";

    // Launch concurrent operations
    const int NUM_ITERATIONS = 10;  // Reduced for reliability
    std::atomic<bool> running{true};

    // Thread 1: Publish sensor values
    std::thread publisher([&]() {
        for (int i = 0; i < NUM_ITERATIONS && running; ++i) {
            auto pub_status = publisher_client->publish(sensor_rw, static_cast<float>(i));
            if (!pub_status.ok()) {
                LOG(WARNING) << "Publish failed: " << pub_status;
            }
            std::this_thread::sleep_for(50ms);  // Slower to reduce contention
        }
    });

    // Thread 2: Actuate
    std::thread actuator_thread([&]() {
        std::this_thread::sleep_for(100ms);  // Let provider fully register
        for (int i = 0; i < NUM_ITERATIONS && running; ++i) {
            auto act_status = accessor->set(actuator, i * 10);
            if (!act_status.ok()) {
                LOG(WARNING) << "Actuation failed: " << act_status;
            }
            std::this_thread::sleep_for(70ms);
        }
    });

    // Thread 3: Batch publish
    std::thread batch_publisher([&]() {
        for (int i = 0; i < NUM_ITERATIONS / 2 && running; ++i) {
            publisher_client->publish_batch({
                {sensor_rw, static_cast<float>(i * 100)}
            });
            std::this_thread::sleep_for(100ms);
        }
    });

    // Wait for all threads
    publisher.join();
    actuator_thread.join();
    batch_publisher.join();
    running = false;

    std::this_thread::sleep_for(1000ms);  // More time for all operations to complete

    // Verify operations occurred
    LOG(INFO) << "Actuation count: " << actuation_count.load();
    LOG(INFO) << "Subscription count: " << subscription_count.load();

    EXPECT_GT(actuation_count.load(), NUM_ITERATIONS * 0.5)
        << "At least half of actuations should succeed";
    EXPECT_GT(subscription_count.load(), NUM_ITERATIONS * 0.3)
        << "Should receive some subscriptions";

    // Give time for cleanup
    std::this_thread::sleep_for(200ms);

    actuator_client->stop();
    publisher_client->stop();
}

// ============================================================================
// Test 6: Actuator Feedback Loop (target → actual → subscription)
// ============================================================================

TEST_F(UnifiedClientIntegrationTest, ActuatorFeedbackLoop) {
    LOG(INFO) << "Testing complete actuator feedback loop: actuation → publish actual → observe";

    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok());
    auto resolver = std::move(*resolver_result);

    // Scenario: Door lock controller
    // 1. External client sends actuation request (target=true to lock door)
    // 2. Door controller receives actuation callback
    // 3. Door controller publishes actual value from separate thread
    // 4. Observer client subscribes to door lock and sees the actual value

    auto door_lock_result = resolver->get<bool>("Vehicle.Private.Test.BoolActuator");
    ASSERT_TRUE(door_lock_result.ok());
    auto door_lock = *door_lock_result;

    // === Door Lock Controller (provides actuator and publishes actual) ===
    auto door_controller = *Client::create(getKuksaAddress());

    // Queue for work (to avoid publishing from gRPC callback thread)
    struct ActuatorWork {
        SignalHandle<bool> handle;
        bool target_value;
    };
    std::queue<ActuatorWork> work_queue;
    std::mutex work_mutex;
    std::condition_variable work_cv;
    std::atomic<bool> worker_running{true};

    // Register actuator callback (runs on gRPC thread)
    door_controller->serve_actuator(door_lock, [&](bool target, const SignalHandle<bool>& handle) {
        LOG(INFO) << "Door controller: Received lock command: " << target;

        // Queue work instead of publishing directly
        std::lock_guard<std::mutex> lock(work_mutex);
        work_queue.push({handle, target});
        work_cv.notify_one();
    });

    door_controller->start();
    ASSERT_TRUE(door_controller->wait_until_ready(5000ms).ok());

    // Worker thread that processes actuation requests and publishes actual values
    std::thread worker([&]() {
        while (worker_running) {
            std::unique_lock<std::mutex> lock(work_mutex);
            work_cv.wait_for(lock, 100ms, [&]() { return !work_queue.empty() || !worker_running; });

            while (!work_queue.empty()) {
                auto work = work_queue.front();
                work_queue.pop();
                lock.unlock();

                // Simulate applying the actuation (e.g., hardware interaction)
                std::this_thread::sleep_for(50ms);

                // Publish the actual value
                LOG(INFO) << "Door controller: Publishing actual value: " << work.target_value;
                auto status = door_controller->publish(work.handle, work.target_value);
                if (!status.ok()) {
                    LOG(ERROR) << "Failed to publish actual value: " << status;
                }

                lock.lock();
            }
        }
    });

    std::this_thread::sleep_for(100ms);

    // === Observer Client (subscribes to actuator to see actual values) ===
    auto observer = *Client::create(getKuksaAddress());

    std::atomic<int> observation_count{0};
    std::atomic<bool> last_observed_value{false};

    observer->subscribe(door_lock, [&](std::optional<bool> value) {
        if (value) {
            LOG(INFO) << "Observer: Saw door lock actual value: " << *value;
            last_observed_value = *value;
            observation_count++;
        }
    });

    observer->start();
    ASSERT_TRUE(observer->wait_until_ready(5000ms).ok());
    std::this_thread::sleep_for(100ms);

    // === Test Scenario ===

    // Send actuation request via external accessor
    auto accessor = *Client::create(getKuksaAddress());
    ASSERT_TRUE(accessor) << "Failed to create accessor";

    // 1. Lock the door (target=true)
    LOG(INFO) << "Sending actuation: lock door (true)";
    ASSERT_TRUE(accessor->set(door_lock, true).ok());

    // Wait for observer to see the actual value
    ASSERT_TRUE(wait_for([&]() { return observation_count.load() >= 1; }, 3000ms))
        << "Observer should see actual value after actuation";
    EXPECT_TRUE(last_observed_value.load()) << "Door should be locked";

    int count_after_first = observation_count.load();

    // 2. Unlock the door (target=false)
    LOG(INFO) << "Sending actuation: unlock door (false)";
    ASSERT_TRUE(accessor->set(door_lock, false).ok());

    // Wait for second observation
    ASSERT_TRUE(wait_for([&]() { return observation_count.load() > count_after_first; }, 3000ms))
        << "Observer should see second actual value";
    EXPECT_FALSE(last_observed_value.load()) << "Door should be unlocked";

    // Cleanup
    worker_running = false;
    work_cv.notify_all();
    worker.join();
    door_controller->stop();
    observer->stop();

    LOG(INFO) << "Total observations: " << observation_count.load();
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    google::InitGoogleLogging(argv[0]);
    google::SetStderrLogging(google::INFO);

    LOG(INFO) << "Unified Client Integration Tests";
    LOG(INFO) << "===================================";
    LOG(INFO) << "Testing unified client with dual streams (provider + subscriber)";
    LOG(INFO) << "";

    return RUN_ALL_TESTS();
}
