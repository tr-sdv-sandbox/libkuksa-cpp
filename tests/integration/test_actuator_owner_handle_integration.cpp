/**
 * @file test_actuator_owner_handle_integration.cpp
 * @brief Integration tests for ActuatorOwnerHandle pattern with KUKSA
 *
 * Tests both typed (compile-time) and dynamic (runtime) ActuatorOwnerHandle APIs
 * in a real KUKSA environment using Client.
 */

#include "kuksa_test_fixture.hpp"
#include <gtest/gtest.h>
#include <kuksa_cpp/client.hpp>
#include <kuksa_cpp/resolver.hpp>
#include <glog/logging.h>
#include <atomic>
#include <mutex>
#include <thread>

using namespace kuksa;
using namespace kuksa::test;
using namespace std::chrono_literals;

class ActuatorOwnerHandleIntegrationTest : public KuksaTestFixture {
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
// Test 1: Typed API (Compile-time type safety)
// ============================================================================

TEST_F(ActuatorOwnerHandleIntegrationTest, TypedAPI) {
    LOG(INFO) << "Testing typed ActuatorOwnerHandle API (compile-time types)";

    // Track what was received
    std::atomic<int32_t> target_received{0};
    std::atomic<int32_t> actual_received{0};
    std::atomic<int> target_count{0};
    std::atomic<int> actual_count{0};

    // Get resolver to resolve handles
    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok()) << "Failed to create resolver: " << resolver_result.status();
    auto resolver = std::move(*resolver_result);

    // Resolve actuator handle (needed for both registration and publishing)
    auto actuator_result = resolver->get<int32_t>("Vehicle.Private.Test.Int32Actuator");
    ASSERT_TRUE(actuator_result.ok()) << "Failed to get actuator: " << actuator_result.status();
    auto actuator = *actuator_result;

    // Create unified client
    auto client = *Client::create(getKuksaAddress());

    // Store raw pointer for use in callback (capturing unique_ptr in lambda is tricky)
    Client* client_ptr = client.get();

    // Register actuator with typed handler - handle is passed in, same one used for publishing
    client->serve_actuator(actuator, [&, client_ptr](int32_t target, const SignalHandle<int32_t>& handle) {
        LOG(INFO) << "Client received target: " << target;
        target_received = target;
        target_count++;

        // Simulate hardware processing
        std::this_thread::sleep_for(10ms);

        // Publish actual value (using handle from callback parameter)
        // Note: Can't call publish from callback thread - causes gRPC errors!
        // In real code, queue to worker thread. For test, we'll risk it for simplicity.
        auto status = client_ptr->publish(handle, target);
        if (!status.ok()) {
            LOG(ERROR) << "Failed to publish actual: " << status;
        }
    });

    // Subscribe to monitor actual values (same client!)
    client->subscribe(actuator, [&](vss::types::QualifiedValue<int32_t> qvalue) {
        if (qvalue.is_valid()) {
            LOG(INFO) << "Client received actual: " << *qvalue.value;
            actual_received = *qvalue.value;
            actual_count++;
        }
    });

    auto start_status = client->start();
    ASSERT_TRUE(start_status.ok()) << "Failed to start client: " << start_status;

    // Wait for client to be ready
    auto client_ready_status = client->wait_until_ready(std::chrono::milliseconds(5000));
    ASSERT_TRUE(client_ready_status.ok()) << "Client not ready: " << client_ready_status;

    std::this_thread::sleep_for(100ms);

    // Test 1: Normal actuation flow (accessor -> client -> publish via handler)
    LOG(INFO) << "Test 1: Normal actuation flow";
    auto accessor = *Client::create(getKuksaAddress());
    ASSERT_TRUE(accessor) << "Failed to create accessor";


    ASSERT_TRUE(accessor->set(actuator, 42).ok());

    ASSERT_TRUE(wait_for([&]() { return target_count.load() > 0; }))
        << "Client should receive target";
    EXPECT_EQ(target_received.load(), 42);

    ASSERT_TRUE(wait_for([&]() { return actual_count.load() > 0; }))
        << "Client should receive actual via subscription";
    EXPECT_EQ(actual_received.load(), 42);

    // Test 2: Independent publish using same handle (no actuation request)
    LOG(INFO) << "Test 2: Independent publish using actuator handle";
    int prev_target_count = target_count.load();

    // Simulate hardware state change detected independently
    // We have the handle (actuator) already resolved - can publish directly
    ASSERT_TRUE(client->publish(actuator, 123).ok());

    // Subscription should receive the new value
    ASSERT_TRUE(wait_for([&]() { return actual_received.load() == 123; }))
        << "Subscription should receive independently published value";
    EXPECT_EQ(actual_received.load(), 123);

    // No new actuation request should have been received
    EXPECT_EQ(target_count.load(), prev_target_count)
        << "publish should not trigger actuation handler";

    // Cleanup
    client->stop();
}

// ============================================================================
// Test 2: Dynamic API (Runtime types from config)
// ============================================================================

TEST_F(ActuatorOwnerHandleIntegrationTest, DynamicAPI) {
    LOG(INFO) << "Testing dynamic ActuatorOwnerHandle API (runtime types)";

    std::atomic<int> handler_calls{0};
    std::atomic<int32_t> last_value{0};
    std::atomic<int32_t> actual_received{0};
    std::atomic<int> actual_count{0};

    // Get resolver for handle lookup
    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok()) << "Failed to create resolver: " << resolver_result.status();
    auto resolver = std::move(*resolver_result);

    // Resolve actuator handle dynamically
    auto actuator_handle_result = resolver->get_dynamic("Vehicle.Private.Test.Actuator");
    ASSERT_TRUE(actuator_handle_result.ok()) << "Failed to get dynamic actuator: " << actuator_handle_result.status();
    auto actuator_handle = **actuator_handle_result;

    // Also get typed handle for accessor/subscriber
    auto actuator_result = resolver->get<int32_t>("Vehicle.Private.Test.Actuator");
    ASSERT_TRUE(actuator_result.ok()) << "Failed to get actuator: " << actuator_result.status();
    auto actuator = *actuator_result;

    // Create unified client
    auto client = *Client::create(getKuksaAddress());
    Client* client_ptr = client.get();

    // Add dynamic handler BEFORE starting (simulating YAML/config loading)
    LOG(INFO) << "Adding dynamic handler for INT32 actuator";
    client->serve_actuator(actuator_handle, [&, client_ptr](const vss::types::Value& value, const DynamicSignalHandle& handle) {
        int32_t val = std::get<int32_t>(value);
        LOG(INFO) << "Dynamic handler received: " << val;
        last_value = val;
        handler_calls++;

        // Publish actual (handle is available from parameter)
        vss::types::DynamicQualifiedValue qvalue{value, vss::types::SignalQuality::VALID};
        auto status = client_ptr->publish(handle, qvalue);
        if (!status.ok()) {
            LOG(ERROR) << "Failed to publish: " << status;
        }
    });

    // Subscribe to monitor actual values
    client->subscribe(actuator, [&](vss::types::QualifiedValue<int32_t> qvalue) {
        if (qvalue.is_valid()) {
            LOG(INFO) << "Client received: " << *qvalue.value;
            actual_received = *qvalue.value;
            actual_count++;
        }
    });

    // Start AFTER adding handlers and subscriptions
    auto start_status2 = client->start();
    ASSERT_TRUE(start_status2.ok()) << "Failed to start client: " << start_status2;

    // Wait for client to be ready
    auto client_ready_status = client->wait_until_ready(std::chrono::milliseconds(5000));
    ASSERT_TRUE(client_ready_status.ok()) << "Client not ready: " << client_ready_status;

    std::this_thread::sleep_for(100ms);

    // Test 1: Actuation via accessor
    LOG(INFO) << "Test 1: Send actuation command";
    auto accessor = *Client::create(getKuksaAddress());
    ASSERT_TRUE(accessor) << "Failed to create accessor";

    ASSERT_TRUE(accessor->set(actuator, 999).ok());

    ASSERT_TRUE(wait_for([&]() { return handler_calls.load() > 0; }))
        << "Dynamic handler should be called";
    EXPECT_EQ(last_value.load(), 999);

    ASSERT_TRUE(wait_for([&]() { return actual_received.load() == 999; }))
        << "Subscription should receive published value";

    // Test 2: Independent publish using resolved dynamic handle
    LOG(INFO) << "Test 2: Independent publish with runtime type";

    // Publish using Value variant with dynamic handle (already resolved)
    vss::types::Value runtime_value = int32_t(777);
    vss::types::DynamicQualifiedValue qvalue{runtime_value, vss::types::SignalQuality::VALID};
    ASSERT_TRUE(client->publish(actuator_handle, qvalue).ok());

    ASSERT_TRUE(wait_for([&]() { return actual_received.load() == 777; }))
        << "Subscription should receive independently published value";
    EXPECT_EQ(actual_received.load(), 777);

    // Cleanup
    client->stop();
}

// ============================================================================
// Test 3: Mixed Typed + Dynamic API
// ============================================================================

TEST_F(ActuatorOwnerHandleIntegrationTest, MixedTypedAndDynamicAPI) {
    LOG(INFO) << "Testing mixed typed + dynamic ActuatorOwnerHandle API";

    std::atomic<int> typed_calls{0};
    std::atomic<int> dynamic_calls{0};
    std::atomic<uint32_t> typed_value{0};
    std::atomic<float> dynamic_value{0.0f};
    std::atomic<uint32_t> uint_actual{0};
    std::atomic<float> float_actual{0.0f};

    // Get resolver for handle lookup
    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok()) << "Failed to create resolver: " << resolver_result.status();
    auto resolver = std::move(*resolver_result);

    // Get RW handles (for both registration and publishing)
    auto uint_actuator_result = resolver->get<uint32_t>("Vehicle.Private.Test.UInt32Actuator");
    auto float_actuator_dyn_result = resolver->get_dynamic("Vehicle.Private.Test.FloatActuator");
    auto float_actuator_result = resolver->get<float>("Vehicle.Private.Test.FloatActuator");
    ASSERT_TRUE(uint_actuator_result.ok()) << "Failed to get uint actuator: " << uint_actuator_result.status();
    ASSERT_TRUE(float_actuator_dyn_result.ok()) << "Failed to get float actuator: " << float_actuator_dyn_result.status();
    ASSERT_TRUE(float_actuator_result.ok()) << "Failed to get float actuator typed: " << float_actuator_result.status();
    auto uint_actuator = *uint_actuator_result;
    auto float_actuator_dyn = **float_actuator_dyn_result;
    auto float_actuator = *float_actuator_result;

    // Create unified client
    auto client = *Client::create(getKuksaAddress());
    Client* client_ptr = client.get();

    // Register typed handler
    client->serve_actuator(uint_actuator, [&, client_ptr](uint32_t target, const SignalHandle<uint32_t>& handle) {
        LOG(INFO) << "Typed handler received: " << target;
        typed_value = target;
        typed_calls++;

        auto status = client_ptr->publish(handle, target);
        if (!status.ok()) {
            LOG(ERROR) << "Failed to publish actual: " << status;
        }
    });

    // Register dynamic handler
    client->serve_actuator(float_actuator_dyn, [&, client_ptr](const vss::types::Value& value, const DynamicSignalHandle& handle) {
        float val = std::get<float>(value);
        LOG(INFO) << "Dynamic handler received: " << val;
        dynamic_value = val;
        dynamic_calls++;

        vss::types::DynamicQualifiedValue qvalue{value, vss::types::SignalQuality::VALID};
        auto status = client_ptr->publish(handle, qvalue);
        if (!status.ok()) {
            LOG(ERROR) << "Failed to publish: " << status;
        }
    });

    // Subscribe to both actuators
    client->subscribe(uint_actuator, [&](vss::types::QualifiedValue<uint32_t> qvalue) {
        if (qvalue.is_valid()) {
            LOG(INFO) << "Uint subscription received: " << *qvalue.value;
            uint_actual = *qvalue.value;
        }
    });

    client->subscribe(float_actuator, [&](vss::types::QualifiedValue<float> qvalue) {
        if (qvalue.is_valid()) {
            LOG(INFO) << "Float subscription received: " << *qvalue.value;
            float_actual = *qvalue.value;
        }
    });

    auto start_status3 = client->start();
    ASSERT_TRUE(start_status3.ok()) << "Failed to start client: " << start_status3;

    // Wait for client to be ready
    auto client_ready_status = client->wait_until_ready(std::chrono::milliseconds(5000));
    ASSERT_TRUE(client_ready_status.ok()) << "Client not ready: " << client_ready_status;

    std::this_thread::sleep_for(100ms);

    // First, send actuations to trigger handlers
    LOG(INFO) << "Sending test actuations";
    auto accessor = *Client::create(getKuksaAddress());
    ASSERT_TRUE(accessor) << "Failed to create accessor";

    ASSERT_TRUE(accessor->set(uint_actuator, 1u).ok());
    ASSERT_TRUE(accessor->set(float_actuator, 1.0f).ok());

    // Wait for handlers to be called
    ASSERT_TRUE(wait_for([&]() { return typed_calls.load() > 0 && dynamic_calls.load() > 0; }))
        << "Handlers should be called";

    // Test: Publish independently using resolved handles
    LOG(INFO) << "Publishing using typed handle (compile-time type)";
    ASSERT_TRUE(client->publish(uint_actuator, 12345u).ok());

    ASSERT_TRUE(wait_for([&]() { return uint_actual.load() == 12345u; }))
        << "Typed handle publish should work";

    LOG(INFO) << "Publishing using dynamic handle (runtime type)";
    ASSERT_TRUE(client->publish(float_actuator_dyn, vss::types::DynamicQualifiedValue{vss::types::Value{98.76f}, vss::types::SignalQuality::VALID}).ok());

    ASSERT_TRUE(wait_for([&]() { return float_actual.load() > 98.7f; }))
        << "Dynamic handle publish should work";

    // Verify both work independently
    EXPECT_EQ(uint_actual.load(), 12345u);
    EXPECT_FLOAT_EQ(float_actual.load(), 98.76f);

    // Cleanup
    client->stop();
}

// ============================================================================
// Test 4: Concurrent Publishing (Thread Safety)
// ============================================================================

TEST_F(ActuatorOwnerHandleIntegrationTest, ConcurrentPublishing) {
    LOG(INFO) << "Testing concurrent publish from multiple threads";

    const int NUM_THREADS = 5;
    const int PUBLISHES_PER_THREAD = 20;

    // Get resolver and RW handle
    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok()) << "Failed to create resolver: " << resolver_result.status();
    auto resolver = std::move(*resolver_result);

    auto actuator_result = resolver->get<int32_t>("Vehicle.Private.Test.Int32Actuator");
    ASSERT_TRUE(actuator_result.ok()) << "Failed to get actuator: " << actuator_result.status();
    auto actuator = *actuator_result;

    // Create unified client
    auto client = *Client::create(getKuksaAddress());
    Client* client_ptr = client.get();

    // Register actuator handler
    client->serve_actuator(actuator, [&, client_ptr](int32_t target, const SignalHandle<int32_t>& handle) {
        // Just ack
        auto status = client_ptr->publish(handle, target);
        if (!status.ok()) {
            LOG(ERROR) << "Failed to publish actual: " << status;
        }
    });

    auto start_status4 = client->start();
    ASSERT_TRUE(start_status4.ok()) << "Failed to start client: " << start_status4;

    // Wait for client to be ready
    auto client_ready_status = client->wait_until_ready(std::chrono::milliseconds(5000));
    ASSERT_TRUE(client_ready_status.ok()) << "Client not ready: " << client_ready_status;

    std::atomic<int> successful_publishes{0};
    std::atomic<int> failed_publishes{0};

    // Launch multiple threads all publishing with same handle
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&, thread_id = i]() {
            for (int j = 0; j < PUBLISHES_PER_THREAD; ++j) {
                int32_t value = thread_id * 1000 + j;

                // Publish using the actuator handle (thread-safe)
                if (client->publish(actuator, value).ok()) {
                    successful_publishes++;
                } else {
                    failed_publishes++;
                }

                // Small delay
                std::this_thread::sleep_for(std::chrono::microseconds(rand() % 1000));
            }
        });
    }

    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }

    LOG(INFO) << "Concurrent test results:";
    LOG(INFO) << "  - Successful: " << successful_publishes.load();
    LOG(INFO) << "  - Failed: " << failed_publishes.load();
    LOG(INFO) << "  - Total: " << (NUM_THREADS * PUBLISHES_PER_THREAD);

    // Most publishes should succeed (allow some failures due to timing)
    EXPECT_GT(successful_publishes.load(), NUM_THREADS * PUBLISHES_PER_THREAD * 0.8);

    client->stop();
}

// ============================================================================
// Test 5: Array Type Support
// ============================================================================

TEST_F(ActuatorOwnerHandleIntegrationTest, ArrayTypes) {
    LOG(INFO) << "Testing ActuatorOwnerHandle with array types";

    std::vector<int32_t> last_array;
    std::mutex array_mutex;
    std::atomic<bool> received{false};

    // Get resolver for handle lookup
    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok()) << "Failed to create resolver: " << resolver_result.status();
    auto resolver = std::move(*resolver_result);

    // Resolve array actuator handle
    auto actuator_dyn_result = resolver->get_dynamic("Vehicle.Private.Test.Int32ArrayActuator");
    ASSERT_TRUE(actuator_dyn_result.ok()) << "Failed to get dynamic actuator: " << actuator_dyn_result.status();
    auto actuator_dyn = **actuator_dyn_result;

    auto actuator_result = resolver->get<std::vector<int32_t>>("Vehicle.Private.Test.Int32ArrayActuator");
    ASSERT_TRUE(actuator_result.ok()) << "Failed to get actuator: " << actuator_result.status();
    auto actuator = *actuator_result;

    // Create unified client
    auto client = *Client::create(getKuksaAddress());
    Client* client_ptr = client.get();

    // Register dynamic handler for int32 array
    client->serve_actuator(actuator_dyn, [&, client_ptr](const vss::types::Value& value, const DynamicSignalHandle& handle) {
        auto arr = std::get<std::vector<int32_t>>(value);
        LOG(INFO) << "Array handler received actuation request with " << arr.size() << " elements";

        // Publish the received value back
        vss::types::DynamicQualifiedValue qvalue{value, vss::types::SignalQuality::VALID};
        auto status = client_ptr->publish(handle, qvalue);
        if (!status.ok()) {
            LOG(ERROR) << "Failed to publish: " << status;
        }
    });

    // Subscribe to monitor actual values
    client->subscribe(actuator, [&](vss::types::QualifiedValue<std::vector<int32_t>> qvalue) {
        if (qvalue.is_valid()) {
            LOG(INFO) << "Subscription received array with " << qvalue.value->size() << " elements";
            std::lock_guard<std::mutex> lock(array_mutex);
            last_array = *qvalue.value;
            received = true;
        }
    });

    auto start_status5 = client->start();
    ASSERT_TRUE(start_status5.ok()) << "Failed to start client: " << start_status5;

    // Wait for client to be ready
    auto client_ready_status = client->wait_until_ready(std::chrono::milliseconds(5000));
    ASSERT_TRUE(client_ready_status.ok()) << "Client not ready: " << client_ready_status;

    std::this_thread::sleep_for(100ms);

    // Publish array value using the dynamic handle
    std::vector<int32_t> test_array = {10, 20, 30, 40, 50};
    ASSERT_TRUE(client->publish(actuator_dyn, vss::types::DynamicQualifiedValue{vss::types::Value{test_array}, vss::types::SignalQuality::VALID}).ok());

    // Wait for subscription to receive the value
    ASSERT_TRUE(wait_for([&]() { return received.load(); }))
        << "Subscription should receive array value";

    // Verify
    {
        std::lock_guard<std::mutex> lock(array_mutex);
        EXPECT_EQ(last_array.size(), 5u);
        if (last_array.size() >= 5) {
            EXPECT_EQ(last_array[0], 10);
            EXPECT_EQ(last_array[4], 50);
        }
    }

    client->stop();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    google::InitGoogleLogging(argv[0]);
    google::SetStderrLogging(google::INFO);

    LOG(INFO) << "ActuatorOwnerHandle Integration Tests";
    LOG(INFO) << "==============================";
    LOG(INFO) << "Testing both typed (compile-time) and dynamic (runtime) APIs";
    LOG(INFO) << "";

    return RUN_ALL_TESTS();
}
