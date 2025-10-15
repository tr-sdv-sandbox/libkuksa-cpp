/**
 * @file test_kuksa_communication.cpp
 * @brief Comprehensive integration tests for KUKSA v2 communication layer
 * 
 * Tests verify:
 * 1. Basic connectivity
 * 2. Actuator provider pattern (receiving actuation commands)
 * 3. Signal subscriptions
 * 4. Publishing values (both standalone RPC and provider stream)
 * 5. Error handling and edge cases
 */

#include "kuksa_test_fixture.hpp"
#include <gtest/gtest.h>
#include <kuksa_cpp/kuksa.hpp>  // Includes subscriber, accessor
#include <kuksa_cpp/client.hpp>  // Unified client replacing provider
#include <kuksa_cpp/resolver.hpp>
#include <glog/logging.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <future>
#include <cstdlib>
#include <cmath>

using namespace kuksa;
using namespace kuksa::test;
using namespace std::chrono_literals;

// Test signals - using private namespace to avoid conflicts
const std::string TEST_ACTUATOR = "Vehicle.Private.Test.Actuator";
const std::string TEST_SENSOR = "Vehicle.Private.Test.Sensor";
const std::string TEST_SIGNAL = "Vehicle.Private.Test.Signal";

class KuksaCommunicationTest : public KuksaTestFixture {
protected:
    // Helper to wait for condition with timeout
    template<typename Predicate>
    bool wait_for(Predicate pred, std::chrono::milliseconds timeout = 5000ms) {
        auto start = std::chrono::steady_clock::now();
        while (!pred()) {
            if (std::chrono::steady_clock::now() - start > timeout) {
                return false;
            }
            std::this_thread::sleep_for(10ms);
        }
        return true;
    }
};

// Test 1: Basic connectivity with new API components
TEST_F(KuksaCommunicationTest, BasicConnectivity) {
    LOG(INFO) << "Testing basic connectivity with new API components at " << getKuksaAddress();
    
    // Test Client connectivity
    {
        auto accessor = *Client::create(getKuksaAddress());
        EXPECT_TRUE(accessor) << "Should create accessor";
    }
    
    // Test Client subscription connectivity
    {
        auto client = *Client::create(getKuksaAddress());
        EXPECT_FALSE(client->is_running()) << "Client should not be running before start()";

        // Note: start() now requires registrations (actuators or subscriptions) first
        // For basic connectivity test, we verify that client requires registrations
        // (calling start() without any registrations will CHECK-fail, so we don't test that)
    }
}

// Test 2: Client connectivity
TEST_F(KuksaCommunicationTest, ProviderConnectivity) {
    LOG(INFO) << "Testing client connectivity";

    // Resolve actuator handle
    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok()) << "Failed to create resolver: " << resolver_result.status();
    auto resolver = std::move(*resolver_result);

    auto actuator_result = resolver->get<int32_t>(TEST_ACTUATOR);
    ASSERT_TRUE(actuator_result.ok()) << "Failed to get actuator: " << actuator_result.status();
    auto actuator = *actuator_result;

    // Create unified client with one actuator handler
    auto client = *Client::create(getKuksaAddress());

    auto serve_status = client->serve_actuator(actuator, [](int32_t target, const SignalHandle<int32_t>& handle) {
        LOG(INFO) << "Dummy handler called with target: " << target;
    });
    ASSERT_TRUE(serve_status.ok()) << "Failed to register actuator: " << serve_status;
    EXPECT_FALSE(client->is_running()) << "Client should not be running before start()";

    auto start_status = client->start();
    ASSERT_TRUE(start_status.ok()) << "Failed to start client: " << start_status;
    EXPECT_TRUE(client->is_running()) << "Client should be running after start()";

    // Wait for client to be ready
    auto ready_status = client->wait_until_ready(std::chrono::milliseconds(5000));
    ASSERT_TRUE(ready_status.ok()) << "Client not ready: " << ready_status;

    client->stop();
    EXPECT_FALSE(client->is_running());

    // Give KUKSA time to fully release the actuator ownership
    std::this_thread::sleep_for(1s);
}

// Test 3: Actuator client pattern - receiving actuation commands
TEST_F(KuksaCommunicationTest, ActuatorClientPattern) {
    LOG(INFO) << "Testing actuator client pattern";

    // Track actuation requests
    std::atomic<bool> actuation_received(false);
    std::atomic<int32_t> actuation_value(0);

    // Resolve actuator handle
    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok()) << "Failed to create resolver: " << resolver_result.status();
    auto resolver = std::move(*resolver_result);

    auto actuator_result = resolver->get<int32_t>(TEST_ACTUATOR);
    ASSERT_TRUE(actuator_result.ok()) << "Failed to get actuator: " << actuator_result.status();
    auto actuator = *actuator_result;

    // Create unified client with handler
    auto client = *Client::create(getKuksaAddress());

    auto serve_status = client->serve_actuator(actuator, [&](int32_t target, const SignalHandle<int32_t>& handle) {
        LOG(INFO) << "Client received actuation for " << handle.path()
                  << " with value: " << target;
        actuation_value = target;
        actuation_received = true;
    });
    ASSERT_TRUE(serve_status.ok()) << "Failed to register actuator: " << serve_status;

    // Start client
    auto start_status = client->start();
    ASSERT_TRUE(start_status.ok()) << "Failed to start client: " << start_status;

    // Wait for client to be ready
    auto ready_status = client->wait_until_ready(std::chrono::milliseconds(5000));
    ASSERT_TRUE(ready_status.ok()) << "Client not ready: " << ready_status;

    // Use Client to send actuation
    auto accessor_result = *Client::create(getKuksaAddress());
    auto accessor = std::move(accessor_result);

    ASSERT_TRUE(accessor->set(actuator, 42).ok());
    
    // Wait for actuation to be received
    ASSERT_TRUE(wait_for([&]() { return actuation_received.load(); }))
        << "Client did not receive actuation request";

    EXPECT_EQ(actuation_value.load(), 42);

    // Cleanup
    client->stop();
}

// Test 4: Publishing values using Client
TEST_F(KuksaCommunicationTest, AccessorPublishing) {
    LOG(INFO) << "Testing publishing with Client";

    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok()) << "Failed to create resolver: " << resolver_result.status();
    auto resolver = std::move(*resolver_result);

    auto accessor_result = *Client::create(getKuksaAddress());
    auto accessor = std::move(accessor_result);

    // Get RW handle for publishing
    auto sensor_result = resolver->get<float>(TEST_SENSOR);
    ASSERT_TRUE(sensor_result.ok()) << "Failed to get sensor handle: " << sensor_result.status();
    auto sensor = *sensor_result;

    // Publish a sensor value
    ASSERT_TRUE(accessor->set(sensor, 23.5f).ok())
        << "Failed to publish value using accessor";

    // Verify value was published by reading it back
    auto value_result = accessor->get(sensor);
    ASSERT_TRUE(value_result.ok()) << "Failed to get value: " << value_result.status();
    ASSERT_TRUE(value_result->is_valid()) << "Value is not valid after publishing";
    EXPECT_FLOAT_EQ(*value_result->value, 23.5f);
}

// Test 5: Subscription to sensor values using Client
TEST_F(KuksaCommunicationTest, SensorSubscription) {
    LOG(INFO) << "Testing sensor subscription with Client";

    // First, publish an initial value so the sensor isn't NONE
    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok()) << "Failed to create resolver: " << resolver_result.status();
    auto resolver = std::move(*resolver_result);

    auto accessor_result = *Client::create(getKuksaAddress());
    auto accessor = std::move(accessor_result);

    auto sensor_rw_result = resolver->get<float>(TEST_SENSOR);
    ASSERT_TRUE(sensor_rw_result.ok()) << "Failed to get sensor RW handle: " << sensor_rw_result.status();
    auto sensor_rw = *sensor_rw_result;
    ASSERT_TRUE(accessor->set(sensor_rw, 23.5f).ok()) << "Failed to publish initial value";

    // Get RO handle for subscription
    auto sensor_result = resolver->get<float>(TEST_SENSOR);
    ASSERT_TRUE(sensor_result.ok()) << "Failed to get sensor handle: " << sensor_result.status();
    auto sensor = *sensor_result;

    // Now create client for subscriptions
    auto client = *Client::create(getKuksaAddress());

    // Set up subscription
    std::atomic<int> update_count(0);
    std::atomic<float> received_value(0);

    client->subscribe(sensor, [&](vss::types::QualifiedValue<float> qvalue) {
        LOG(INFO) << "Subscription received update: "
                  << (qvalue.is_valid() ? std::to_string(*qvalue.value) : "NOT_VALID")
                  << " (count: " << update_count.load() + 1 << ")";
        if (qvalue.is_valid()) {
            received_value.store(*qvalue.value);
            update_count.fetch_add(1);
        } else {
            LOG(INFO) << "Skipping invalid value";
        }
    });

    // Start subscriptions
    auto start_status = client->start();
    ASSERT_TRUE(start_status.ok()) << "Failed to start client: " << start_status;

    // Wait for client to be ready
    auto ready_status = client->wait_until_ready(std::chrono::milliseconds(5000));
    ASSERT_TRUE(ready_status.ok()) << "Client not ready: " << ready_status;

    // Wait for initial value
    ASSERT_TRUE(wait_for([&]() { return update_count.load() >= 1; }))
        << "Did not receive initial value";

    int initial_count = update_count.load();

    // Publish a different value
    ASSERT_TRUE(accessor->set(sensor_rw, 99.9f).ok());
    
    // Wait for the specific value to be received
    ASSERT_TRUE(wait_for([&]() { 
        float current = received_value.load();
        return std::abs(current - 99.9f) < 0.001f;  // Use epsilon comparison
    })) << "Did not receive expected value 99.9";
    
    // Log the received value for debugging
    LOG(INFO) << "Final received value: " << received_value.load();
    LOG(INFO) << "Update count: " << update_count.load();
    
    EXPECT_FLOAT_EQ(received_value.load(), 99.9f);
}

// Test 6: Multiple subscriptions
TEST_F(KuksaCommunicationTest, MultipleSubscriptions) {
    LOG(INFO) << "Testing multiple simultaneous subscriptions";

    // Get resolver and handles first
    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok()) << "Failed to create resolver: " << resolver_result.status();
    auto resolver = std::move(*resolver_result);

    auto accessor_result = *Client::create(getKuksaAddress());
    auto accessor = std::move(accessor_result);

    // Get RW handles for publishing
    auto sensor1_rw_result = resolver->get<float>("Vehicle.Private.Test.Sensor1");
    auto sensor2_rw_result = resolver->get<int32_t>("Vehicle.Private.Test.Sensor2");
    auto sensor3_rw_result = resolver->get<bool>("Vehicle.Private.Test.Sensor3");

    ASSERT_TRUE(sensor1_rw_result.ok()) << "Failed to get sensor1: " << sensor1_rw_result.status();
    ASSERT_TRUE(sensor2_rw_result.ok()) << "Failed to get sensor2: " << sensor2_rw_result.status();
    ASSERT_TRUE(sensor3_rw_result.ok()) << "Failed to get sensor3: " << sensor3_rw_result.status();
    auto sensor1_rw = *sensor1_rw_result;
    auto sensor2_rw = *sensor2_rw_result;
    auto sensor3_rw = *sensor3_rw_result;

    // Get RO handles for subscription
    auto sensor1_result = resolver->get<float>("Vehicle.Private.Test.Sensor1");
    auto sensor2_result = resolver->get<int32_t>("Vehicle.Private.Test.Sensor2");
    auto sensor3_result = resolver->get<bool>("Vehicle.Private.Test.Sensor3");

    ASSERT_TRUE(sensor1_result.ok()) << "Failed to get sensor1: " << sensor1_result.status();
    ASSERT_TRUE(sensor2_result.ok()) << "Failed to get sensor2: " << sensor2_result.status();
    ASSERT_TRUE(sensor3_result.ok()) << "Failed to get sensor3: " << sensor3_result.status();
    auto sensor1 = *sensor1_result;
    auto sensor2 = *sensor2_result;
    auto sensor3 = *sensor3_result;

    auto subscriber = *Client::create(getKuksaAddress());

    // Set up multiple subscriptions
    std::atomic<int> updates_received(0);

    subscriber->subscribe(sensor1, [&](vss::types::QualifiedValue<float> qvalue) {
        if (qvalue.is_valid()) {
            LOG(INFO) << "Sensor1 update: " << *qvalue.value;
            updates_received++;
        }
    });

    subscriber->subscribe(sensor2, [&](vss::types::QualifiedValue<int32_t> qvalue) {
        if (qvalue.is_valid()) {
            LOG(INFO) << "Sensor2 update: " << *qvalue.value;
            updates_received++;
        }
    });

    subscriber->subscribe(sensor3, [&](vss::types::QualifiedValue<bool> qvalue) {
        if (qvalue.is_valid()) {
            LOG(INFO) << "Sensor3 update: " << *qvalue.value;
            updates_received++;
        }
    });

    auto start_status = subscriber->start();
    ASSERT_TRUE(start_status.ok()) << "Failed to start subscriber: " << start_status;

    // Wait for subscriber to be ready
    auto ready_status = subscriber->wait_until_ready(std::chrono::milliseconds(5000));
    ASSERT_TRUE(ready_status.ok()) << "Subscriber not ready: " << ready_status;

    // Publish to all sensors using Client
    ASSERT_TRUE(accessor->set(sensor1_rw, 1.1f).ok());
    ASSERT_TRUE(accessor->set(sensor2_rw, 22).ok());
    ASSERT_TRUE(accessor->set(sensor3_rw, true).ok());
    
    // Wait for all updates
    ASSERT_TRUE(wait_for([&]() { return updates_received.load() >= 3; }))
        << "Did not receive all subscription updates";
}

// Test 7: Actuator actual value publishing after actuation
TEST_F(KuksaCommunicationTest, ActuatorActualValueFlow) {
    LOG(INFO) << "Testing complete actuator flow (target -> actual)";

    std::promise<void> actuation_promise;
    auto actuation_future = actuation_promise.get_future();

    // Resolve actuator handle
    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok()) << "Failed to create resolver: " << resolver_result.status();
    auto resolver = std::move(*resolver_result);

    auto actuator_handle_result = resolver->get<int32_t>(TEST_ACTUATOR);
    ASSERT_TRUE(actuator_handle_result.ok()) << "Failed to get actuator: " << actuator_handle_result.status();
    auto actuator_handle = *actuator_handle_result;

    // Set up actuator client with handler
    auto client = *Client::create(getKuksaAddress());
    Client* client_ptr = client.get();

    auto serve_status2 = client->serve_actuator(actuator_handle, [&, client_ptr](int32_t target, const SignalHandle<int32_t>& handle) {
        LOG(INFO) << "Client simulating hardware delay";
        std::this_thread::sleep_for(200ms);

        // Publish actual value using the client
        auto status = client_ptr->publish(handle, target);
        if (status.ok()) {
            LOG(INFO) << "Published actual value: " << target;
        } else {
            LOG(ERROR) << "Failed to publish value: " << target << " - " << status;
        }

        actuation_promise.set_value();
    });
    ASSERT_TRUE(serve_status2.ok()) << "Failed to register actuator: " << serve_status2;

    auto serve_status = client->start();
    ASSERT_TRUE(serve_status.ok()) << "Failed to start client: " << serve_status;

    // Wait for client to be ready
    auto client_ready_status = client->wait_until_ready(std::chrono::milliseconds(5000));
    ASSERT_TRUE(client_ready_status.ok()) << "Client not ready: " << client_ready_status;

    // Get accessor for sending actuation
    auto accessor_result = *Client::create(getKuksaAddress());
    auto accessor = std::move(accessor_result);

    // Get read-only handle for subscription
    auto actuator_ro_result = resolver->get<int32_t>(TEST_ACTUATOR);
    ASSERT_TRUE(actuator_ro_result.ok()) << "Failed to get actuator RO handle: " << actuator_ro_result.status();
    auto actuator_ro = *actuator_ro_result;

    // Subscribe to actual value changes
    auto subscriber = *Client::create(getKuksaAddress());

    std::atomic<bool> actual_updated(false);
    std::atomic<int32_t> actual_value(0);

    subscriber->subscribe(actuator_ro, [&](vss::types::QualifiedValue<int32_t> qvalue) {
        if (qvalue.is_valid()) {
            LOG(INFO) << "Actual value updated to: " << *qvalue.value;
            actual_value = *qvalue.value;
            actual_updated = true;
        }
    });

    auto start_status = subscriber->start();
    ASSERT_TRUE(start_status.ok()) << "Failed to start subscriber: " << start_status;

    // Wait for subscriber to be ready
    auto ready_status = subscriber->wait_until_ready(std::chrono::milliseconds(5000));
    ASSERT_TRUE(ready_status.ok()) << "Subscriber not ready: " << ready_status;

    // Send actuation command using Client
    ASSERT_TRUE(accessor->set(actuator_handle, 123).ok());

    // Wait for actuation to be processed
    ASSERT_EQ(actuation_future.wait_for(5s), std::future_status::ready)
        << "Actuation was not processed";

    // Wait for actual value update
    ASSERT_TRUE(wait_for([&]() { return actual_updated.load(); }))
        << "Actual value was not updated";

    EXPECT_EQ(actual_value.load(), 123);

    client->stop();

    // Give KUKSA extra time to fully release the actuator ownership
    // This prevents the next test from failing due to "actuator already claimed"
    std::this_thread::sleep_for(1s);
}

// Test 8: Provider restart with active subscription
TEST_F(KuksaCommunicationTest, ProviderRestartWithActiveSubscription) {
    LOG(INFO) << "Testing provider restart with active subscription";

    // Get resolver and handles upfront
    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok()) << "Failed to create resolver: " << resolver_result.status();
    auto resolver = std::move(*resolver_result);

    auto accessor_result = *Client::create(getKuksaAddress());
    auto accessor = std::move(accessor_result);

    auto actuator_result = resolver->get<int32_t>(TEST_ACTUATOR);
    ASSERT_TRUE(actuator_result.ok()) << "Failed to get actuator handle: " << actuator_result.status();
    auto actuator = *actuator_result;

    auto actuator_rw_result = resolver->get<int32_t>(TEST_ACTUATOR);
    ASSERT_TRUE(actuator_rw_result.ok()) << "Failed to get actuator RW handle: " << actuator_rw_result.status();
    auto actuator_rw = *actuator_rw_result;

    // Set up subscription BEFORE starting any provider
    auto subscriber = *Client::create(getKuksaAddress());

    std::atomic<int> subscription_updates(0);
    std::vector<int32_t> received_values;
    std::mutex values_mutex;

    subscriber->subscribe(actuator, [&](vss::types::QualifiedValue<int32_t> qvalue) {
        int count = subscription_updates.fetch_add(1) + 1;
        if (qvalue.is_valid()) {
            LOG(INFO) << "Subscription callback #" << count << ": value = " << *qvalue.value;
            std::lock_guard<std::mutex> lock(values_mutex);
            received_values.push_back(*qvalue.value);
        } else {
            LOG(INFO) << "Subscription callback #" << count << ": NOT_VALID";
        }
    });

    auto start_status = subscriber->start();
    ASSERT_TRUE(start_status.ok()) << "Failed to start subscriber: " << start_status;

    // Wait for subscriber to be ready
    auto ready_status = subscriber->wait_until_ready(std::chrono::milliseconds(5000));
    ASSERT_TRUE(ready_status.ok()) << "Subscriber not ready: " << ready_status;

    LOG(INFO) << "Subscriber started (no provider yet)";
    std::this_thread::sleep_for(500ms);

    // ========================================================================
    // PHASE 1: Start first client and actuate
    // ========================================================================
    LOG(INFO) << "\n=== PHASE 1: Starting first client ===";

    std::atomic<int> actuation_count(0);

    auto client = *Client::create(getKuksaAddress());
    Client* client_ptr = client.get();

    auto serve_status3 = client->serve_actuator(actuator_rw, [&, client_ptr](int32_t target, const SignalHandle<int32_t>& handle) {
        int count = actuation_count.fetch_add(1) + 1;
        LOG(INFO) << "Client1 received actuation #" << count << " with value: " << target;

        auto status = client_ptr->publish(handle, target);
        LOG(INFO) << "Client1 published actual value: " << target << ", status: " << status;
    });
    ASSERT_TRUE(serve_status3.ok()) << "Failed to register actuator: " << serve_status3;

    auto start_status2 = client->start();
    ASSERT_TRUE(start_status2.ok()) << "Failed to start client: " << start_status2;
    LOG(INFO) << "Client1 started";

    // Wait for client to be ready
    auto client_ready_status = client->wait_until_ready(std::chrono::milliseconds(5000));
    ASSERT_TRUE(client_ready_status.ok()) << "Client not ready: " << client_ready_status;

    // Actuate with first client
    LOG(INFO) << "Actuating with client1 (value: 100)";
    auto actuate1_status = accessor->set(actuator_rw, 100);
    LOG(INFO) << "Actuation status: " << actuate1_status;

    ASSERT_TRUE(wait_for([&]() { return actuation_count.load() >= 1; }, 5s))
        << "Client1 did not receive actuation";

    ASSERT_TRUE(wait_for([&]() {
        std::lock_guard<std::mutex> lock(values_mutex);
        return !received_values.empty() && received_values.back() == 100;
    }, 5s)) << "Subscriber did not receive value 100";

    LOG(INFO) << "✓ Client1 processed actuation and subscriber received update";

    // ========================================================================
    // PHASE 2: Stop client
    // ========================================================================
    LOG(INFO) << "\n=== PHASE 2: Stopping client ===";

    client->stop();
    client.reset();  // Destroy the client
    client_ptr = nullptr;
    LOG(INFO) << "Client1 stopped and destroyed";
    std::this_thread::sleep_for(1500ms);  // Increased wait time for KUKSA to release ownership

    // ========================================================================
    // PHASE 3: Actuate while NO client is running
    // ========================================================================
    LOG(INFO) << "\n=== PHASE 3: Attempting actuation with NO client ===";

    int updates_before_no_provider = subscription_updates.load();
    LOG(INFO) << "Actuating without client (value: 200)";
    auto actuate_no_provider_status = accessor->set(actuator_rw, 200);
    LOG(INFO) << "Actuation status (no client): " << actuate_no_provider_status;

    if (!actuate_no_provider_status.ok()) {
        LOG(INFO) << "✓ Expected error: " << actuate_no_provider_status.message();
    } else {
        LOG(INFO) << "⚠ Actuation succeeded at KUKSA level, but no client to handle it";
    }

    // Wait to verify no updates arrive
    std::this_thread::sleep_for(2s);
    int updates_after_no_provider = subscription_updates.load();

    EXPECT_EQ(updates_before_no_provider, updates_after_no_provider)
        << "Subscriber should not receive updates when no client is running";
    LOG(INFO) << "✓ No subscription updates received (expected)";

    // ========================================================================
    // PHASE 4: Start NEW client and actuate again
    // ========================================================================
    LOG(INFO) << "\n=== PHASE 4: Starting NEW client ===";

    std::atomic<int> actuation_count2(0);

    auto client2 = *Client::create(getKuksaAddress());
    Client* client2_ptr = client2.get();

    auto serve_status4 = client2->serve_actuator(actuator_rw, [&, client2_ptr](int32_t target, const SignalHandle<int32_t>& handle) {
        int count = actuation_count2.fetch_add(1) + 1;
        LOG(INFO) << "Client2 received actuation #" << count << " with value: " << target;

        auto status = client2_ptr->publish(handle, target);
        LOG(INFO) << "Client2 published actual value: " << target << ", status: " << status;
    });
    ASSERT_TRUE(serve_status4.ok()) << "Failed to register actuator: " << serve_status4;

    auto start_status3 = client2->start();
    ASSERT_TRUE(start_status3.ok()) << "Failed to start client2: " << start_status3;
    LOG(INFO) << "Client2 started";

    // Wait for client to be ready
    auto ready_status2 = client2->wait_until_ready(std::chrono::milliseconds(5000));
    ASSERT_TRUE(ready_status2.ok()) << "Client2 not ready: " << ready_status2;

    // Actuate with second client
    LOG(INFO) << "Actuating with client2 (value: 300)";
    auto actuate2_status = accessor->set(actuator_rw, 300);
    LOG(INFO) << "Actuation status: " << actuate2_status;

    ASSERT_TRUE(wait_for([&]() { return actuation_count2.load() >= 1; }, 5s))
        << "Client2 did not receive actuation";

    ASSERT_TRUE(wait_for([&]() {
        std::lock_guard<std::mutex> lock(values_mutex);
        return !received_values.empty() && received_values.back() == 300;
    }, 5s)) << "Subscriber did not receive value 300 from client2";

    LOG(INFO) << "✓ Client2 processed actuation and subscriber received update";

    // ========================================================================
    // VERIFICATION
    // ========================================================================
    LOG(INFO) << "\n=== TEST RESULTS ===";
    LOG(INFO) << "Total subscription updates: " << subscription_updates.load();
    LOG(INFO) << "Values received by subscriber:";
    {
        std::lock_guard<std::mutex> lock(values_mutex);
        for (size_t i = 0; i < received_values.size(); i++) {
            LOG(INFO) << "  [" << i << "] = " << received_values[i];
        }
    }
    LOG(INFO) << "Subscriber still running: " << (subscriber->is_running() ? "YES" : "NO");

    LOG(INFO) << "\n*** FINDINGS ***";
    LOG(INFO) << "✓ Subscription stream survives client restart";
    LOG(INFO) << "✓ Subscription receives updates from new client";
    LOG(INFO) << "✓ Actuation without client " << (actuate_no_provider_status.ok() ? "succeeds at KUKSA but not processed" : "returns error");
    LOG(INFO) << "✓ System handles client lifecycle correctly";

    // Cleanup
    client2->stop();
    subscriber->stop();
    std::this_thread::sleep_for(1s);
}

// Test 9: Error handling - invalid signal paths
TEST_F(KuksaCommunicationTest, InvalidSignalPaths) {
    LOG(INFO) << "Testing error handling for invalid paths";

    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok()) << "Failed to create resolver: " << resolver_result.status();
    auto resolver = std::move(*resolver_result);

    // Try to get handle for non-existent signal
    auto invalid_sensor = resolver->get<float>("Vehicle.Invalid.Path.That.Does.Not.Exist");
    EXPECT_FALSE(invalid_sensor.ok()) << "Should not get handle for invalid path";

    // Try to get handle for non-existent actuator
    auto invalid_actuator = resolver->get<int32_t>("Vehicle.Invalid.Actuator");
    EXPECT_FALSE(invalid_actuator.ok()) << "Should not get handle for invalid actuator";
}

// Test 10: Connection resilience
TEST_F(KuksaCommunicationTest, ConnectionResilience) {
    LOG(INFO) << "Testing connection resilience";

    // Test with Client - create always succeeds (connection happens lazily)
    {
        auto accessor = *Client::create("invalid.address:12345");
        EXPECT_TRUE(accessor) << "Should create accessor (connection is lazy)";
        // Note: Connection errors will occur when actual operations are attempted
    }

    // Test with Client - create() always succeeds (lazy connection)
    {
        auto subscriber = *Client::create("invalid.address:12345");
        EXPECT_FALSE(subscriber->is_running()) << "Subscriber should not be running before start()";

        // Note: With auto-reconnect, start() always succeeds and retries in background
        // The subscriber will keep trying to connect with exponential backoff
        // This test verifies that we can create a subscriber before broker is available
        LOG(INFO) << "Subscriber created with invalid address (connection will retry in background)";
    }

    // Now test valid connections
    auto valid_accessor = *Client::create(getKuksaAddress());
    ASSERT_TRUE(valid_accessor) << "Should create valid accessor";
}

// Test 11: Concurrent operations
TEST_F(KuksaCommunicationTest, ConcurrentOperations) {
    LOG(INFO) << "Testing concurrent publish/subscribe operations";

    const int NUM_PUBLISHERS = 5;
    const int UPDATES_PER_PUBLISHER = 10;

    // First, publish an initial value to ensure the signal exists
    // Use a unique initial value to distinguish it from test values
    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok()) << "Failed to create resolver: " << resolver_result.status();
    auto resolver = std::move(*resolver_result);

    auto initializer = *Client::create(getKuksaAddress());
    ASSERT_TRUE(initializer) << "Failed to create initializer accessor";

    auto sensor_result = resolver->get<int32_t>(TEST_SIGNAL);
    ASSERT_TRUE(sensor_result.ok()) << "Failed to get sensor handle: " << sensor_result.status();
    auto sensor = *sensor_result;

    const int32_t INITIAL_VALUE = -999;  // Unique value to identify initial update
    ASSERT_TRUE(initializer->set(sensor, INITIAL_VALUE).ok()) << "Failed to publish initial value";

    // Set up subscriber
    auto subscriber = *Client::create(getKuksaAddress());

    std::atomic<int> updates_received(0);
    std::atomic<bool> initial_value_received(false);
    std::set<int32_t> unique_values_received;
    std::mutex values_mutex;

    subscriber->subscribe(sensor, [&](vss::types::QualifiedValue<int32_t> qvalue) {
        if (qvalue.is_valid()) {
            int count = updates_received.fetch_add(1) + 1;
            LOG(INFO) << "Received update #" << count << ": " << *qvalue.value;

            {
                std::lock_guard<std::mutex> lock(values_mutex);
                unique_values_received.insert(*qvalue.value);
            }

            if (*qvalue.value == INITIAL_VALUE) {
                initial_value_received = true;
                LOG(INFO) << "Received initial subscription value";
            }
        }
    });
    
    // Wait a bit before starting subscriber to ensure initial value is in KUKSA
    std::this_thread::sleep_for(100ms);

    auto start_status4 = subscriber->start();
    ASSERT_TRUE(start_status4.ok()) << "Failed to start subscriber: " << start_status4;

    // Wait for subscriber to be ready
    auto ready_status = subscriber->wait_until_ready(std::chrono::milliseconds(5000));
    ASSERT_TRUE(ready_status.ok()) << "Subscriber not ready: " << ready_status;

    // Wait for the initial subscription value
    ASSERT_TRUE(wait_for([&]() { return initial_value_received.load(); }, 5s))
        << "ERROR: Did not receive initial subscription value (" << INITIAL_VALUE << "). "
        << "This indicates the subscriber did not get the current value on startup.";
    
    // Remember how many updates we got initially
    int initial_updates = updates_received.load();
    LOG(INFO) << "Initial updates count: " << initial_updates;
    
    // Add a small delay before publishers start to ensure subscription is fully established
    std::this_thread::sleep_for(200ms);
    
    // Launch multiple publishers with staggered timing
    std::vector<std::thread> publishers;
    std::atomic<int> publish_failures(0);
    std::atomic<int> total_published(0);
    
    for (int i = 0; i < NUM_PUBLISHERS; ++i) {
        publishers.emplace_back([&, publisher_id = i]() {
            auto pub_resolver_result = Resolver::create(getKuksaAddress());
            auto accessor = *Client::create(getKuksaAddress());
            if (!pub_resolver_result.ok() || !accessor) {
                LOG(ERROR) << "Publisher " << publisher_id << " failed to connect";
                publish_failures++;
                return;
            }
            auto pub_resolver = std::move(*pub_resolver_result);

            // Get sensor handle for this publisher
            auto pub_sensor_result = pub_resolver->get<int32_t>(TEST_SIGNAL);
            if (!pub_sensor_result.ok()) {
                LOG(ERROR) << "Publisher " << publisher_id << " failed to get sensor handle: " << pub_sensor_result.status();
                publish_failures++;
                return;
            }
            auto pub_sensor = *pub_sensor_result;

            for (int j = 0; j < UPDATES_PER_PUBLISHER; ++j) {
                int value = publisher_id * 1000 + j;  // Use larger spacing to ensure unique values
                if (!accessor->set(pub_sensor, value).ok()) {
                    LOG(ERROR) << "Publisher " << publisher_id << " failed to publish value " << value;
                    publish_failures++;
                } else {
                    LOG(INFO) << "Publisher " << publisher_id << " published value " << value;
                    total_published++;
                }
                // Stagger updates to reduce collisions
                std::this_thread::sleep_for(std::chrono::milliseconds(100 + publisher_id * 20));
            }
        });
    }
    
    // Wait for all publishers
    for (auto& t : publishers) {
        t.join();
    }
    
    // Check if there were any publish failures
    ASSERT_EQ(publish_failures.load(), 0) << "ERROR: Some publish operations failed";
    
    // Give time for all updates to propagate
    std::this_thread::sleep_for(1000ms);
    
    // Log final statistics
    const int total_updates = updates_received.load();
    const int published_updates = total_updates - initial_updates;
    
    LOG(INFO) << "Test Statistics:";
    LOG(INFO) << "  Initial updates: " << initial_updates;
    LOG(INFO) << "  Published by test: " << total_published.load();
    LOG(INFO) << "  Updates received: " << total_updates;
    LOG(INFO) << "  Published updates received: " << published_updates;
    
    {
        std::lock_guard<std::mutex> lock(values_mutex);
        LOG(INFO) << "  Unique values received: " << unique_values_received.size();
    }
    
    // KUKSA may coalesce rapid updates, so we can't expect to receive every single update
    // We should receive at least some reasonable portion of the updates
    const int minimum_expected = total_published.load() / 2;  // At least 50% should get through
    
    ASSERT_GE(published_updates, minimum_expected) 
        << "ERROR: Too few updates received. This may indicate a subscription problem.\n"
        << "Published: " << total_published.load() << ", Received: " << published_updates << "\n"
        << "KUKSA databroker may coalesce rapid updates, but we should receive at least " 
        << minimum_expected << " updates.";
    
    // Also check that we're not getting duplicate updates
    ASSERT_LE(total_updates, initial_updates + total_published.load()) 
        << "ERROR: Received more updates than published. This may indicate duplicate delivery.";
    
    LOG(INFO) << "Test passed. KUKSA coalesced some updates as expected in a last-value semantics system.";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    google::InitGoogleLogging(argv[0]);
    google::SetStderrLogging(google::INFO);
    
    LOG(INFO) << "KUKSA Communication Integration Tests";
    LOG(INFO) << "======================================";
    LOG(INFO) << "Tests will automatically start KUKSA in Docker";
    LOG(INFO) << "";
    
    return RUN_ALL_TESTS();
}