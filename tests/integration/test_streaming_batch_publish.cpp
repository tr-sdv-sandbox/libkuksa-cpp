/**
 * @file test_streaming_batch_publish.cpp
 * @brief Integration tests for streaming batch publish with signal provider registration
 *
 * Tests the provide_signals() + publish_batch() flow:
 * 1. Register signals with provide_signals() BEFORE start()
 * 2. Claim signals via ProvideSignalRequest on provider stream
 * 3. Batch publish via PublishValuesRequest on same stream
 */

#include "kuksa_test_fixture.hpp"
#include <gtest/gtest.h>
#include <kuksa_cpp/client.hpp>
#include <kuksa_cpp/resolver.hpp>
#include <glog/logging.h>
#include <atomic>
#include <thread>
#include <chrono>

using namespace kuksa;
using namespace kuksa::test;
using namespace std::chrono_literals;

class StreamingBatchPublishTest : public KuksaTestFixture {
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
// Test 1: Basic provide_signals + publish_batch
// ============================================================================

TEST_F(StreamingBatchPublishTest, ProvideSignalsThenPublishBatch) {
    LOG(INFO) << "Testing provide_signals() + publish_batch() streaming flow";

    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok());
    auto resolver = std::move(*resolver_result);

    // Create publisher and subscriber clients
    auto publisher = *Client::create(getKuksaAddress());
    auto subscriber = *Client::create(getKuksaAddress());

    // Get signal handles
    auto speed_result = resolver->get<float>("Vehicle.Private.Test.FloatSensor");
    auto rpm_result = resolver->get<int32_t>("Vehicle.Private.Test.Int32Sensor");

    ASSERT_TRUE(speed_result.ok() && rpm_result.ok());
    auto speed = *speed_result;
    auto rpm = *rpm_result;

    // Subscriber setup
    std::atomic<int> updates_received{0};
    std::atomic<float> last_speed{0.0f};
    std::atomic<int32_t> last_rpm{0};

    subscriber->subscribe(speed, [&](vss::types::QualifiedValue<float> qvalue) {
        if (qvalue.is_valid()) {
            LOG(INFO) << "Received speed: " << *qvalue.value;
            last_speed = *qvalue.value;
            updates_received++;
        }
    });

    subscriber->subscribe(rpm, [&](vss::types::QualifiedValue<int32_t> qvalue) {
        if (qvalue.is_valid()) {
            LOG(INFO) << "Received rpm: " << *qvalue.value;
            last_rpm = *qvalue.value;
            updates_received++;
        }
    });

    // Start subscriber first
    auto sub_start = subscriber->start();
    ASSERT_TRUE(sub_start.ok()) << "Failed to start subscriber: " << sub_start;
    ASSERT_TRUE(subscriber->wait_until_ready(5000ms).ok());

    // KEY: Register signal providers BEFORE start()
    publisher->provide_signals(speed, rpm);

    // Start publisher - this will send ProvideSignalRequest
    auto pub_start = publisher->start();
    ASSERT_TRUE(pub_start.ok()) << "Failed to start publisher: " << pub_start;
    auto ready_status = publisher->wait_until_ready(5000ms);
    ASSERT_TRUE(ready_status.ok()) << "Publisher not ready: " << ready_status;

    LOG(INFO) << "Publisher ready, publishing batch via stream";
    std::this_thread::sleep_for(100ms);

    // Batch publish via streaming API
    std::atomic<bool> batch_callback_called{false};
    std::atomic<bool> batch_succeeded{false};

    auto status = publisher->publish_batch(
        {
            {speed, 88.5f},
            {rpm, int32_t(4200)}
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

    // Wait for subscription updates
    ASSERT_TRUE(wait_for([&]() { return updates_received.load() >= 2; }, 3000ms))
        << "Should receive 2 subscription updates, got: " << updates_received.load();

    // Wait for batch callback
    ASSERT_TRUE(wait_for([&]() { return batch_callback_called.load(); }, 2000ms))
        << "Batch callback should be invoked";

    EXPECT_TRUE(batch_succeeded.load()) << "Batch publish should succeed";

    // Verify values
    EXPECT_FLOAT_EQ(last_speed.load(), 88.5f);
    EXPECT_EQ(last_rpm.load(), 4200);

    std::this_thread::sleep_for(100ms);
    publisher->stop();
    subscriber->stop();
}

// ============================================================================
// Test 2: Multiple batch publishes on same stream
// ============================================================================

TEST_F(StreamingBatchPublishTest, MultipleBatchPublishes) {
    LOG(INFO) << "Testing multiple batch publishes on same provider stream";

    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok());
    auto resolver = std::move(*resolver_result);

    auto publisher = *Client::create(getKuksaAddress());
    auto subscriber = *Client::create(getKuksaAddress());

    // Use DoubleSensor to avoid conflicts with other tests
    auto sensor_result = resolver->get<double>("Vehicle.Private.Test.DoubleSensor");
    ASSERT_TRUE(sensor_result.ok());
    auto sensor = *sensor_result;

    std::atomic<int> updates_received{0};
    std::atomic<double> last_value{0.0};

    subscriber->subscribe(sensor, [&](vss::types::QualifiedValue<double> qvalue) {
        if (qvalue.is_valid()) {
            LOG(INFO) << "Received value: " << *qvalue.value;
            last_value = *qvalue.value;
            updates_received++;
        }
    });

    auto sub_start = subscriber->start();
    ASSERT_TRUE(sub_start.ok());
    ASSERT_TRUE(subscriber->wait_until_ready(5000ms).ok());

    // Register signal provider
    publisher->provide_signal(sensor);

    auto pub_start = publisher->start();
    ASSERT_TRUE(pub_start.ok());
    ASSERT_TRUE(publisher->wait_until_ready(5000ms).ok());

    LOG(INFO) << "Publishing first batch";
    auto status1 = publisher->publish_batch({{sensor, 10.0}}, nullptr);
    ASSERT_TRUE(status1.ok()) << "First batch failed: " << status1;

    ASSERT_TRUE(wait_for([&]() { return last_value.load() == 10.0; }, 2000ms));

    LOG(INFO) << "Publishing second batch";
    auto status2 = publisher->publish_batch({{sensor, 20.0}}, nullptr);
    ASSERT_TRUE(status2.ok()) << "Second batch failed: " << status2;

    ASSERT_TRUE(wait_for([&]() { return last_value.load() == 20.0; }, 2000ms));

    LOG(INFO) << "Publishing third batch";
    auto status3 = publisher->publish_batch({{sensor, 30.0}}, nullptr);
    ASSERT_TRUE(status3.ok()) << "Third batch failed: " << status3;

    ASSERT_TRUE(wait_for([&]() { return last_value.load() == 30.0; }, 2000ms));

    EXPECT_GE(updates_received.load(), 3) << "Should receive at least 3 updates";
    EXPECT_DOUBLE_EQ(last_value.load(), 30.0);

    std::this_thread::sleep_for(100ms);
    publisher->stop();
    subscriber->stop();
}

// ============================================================================
// Test 3: Error - publish_batch without start()
// ============================================================================

TEST_F(StreamingBatchPublishTest, PublishBatchWithoutStart) {
    LOG(INFO) << "Testing publish_batch without start() fails correctly";

    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok());
    auto resolver = std::move(*resolver_result);

    auto client = *Client::create(getKuksaAddress());

    auto sensor_result = resolver->get<float>("Vehicle.Private.Test.FloatSensor");
    ASSERT_TRUE(sensor_result.ok());
    auto sensor = *sensor_result;

    // Register provider but don't start
    client->provide_signal(sensor);

    // Should fail because client not started
    auto status = client->publish_batch({{sensor, 42.0f}}, nullptr);
    EXPECT_FALSE(status.ok()) << "Should fail when client not started";
    LOG(INFO) << "Expected error: " << status;
}

// ============================================================================
// Test 4: Error - provide_signals after start()
// ============================================================================

TEST_F(StreamingBatchPublishTest, ProvideSignalsAfterStartFails) {
    LOG(INFO) << "Testing provide_signals() after start() throws exception";

    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok());
    auto resolver = std::move(*resolver_result);

    auto client = *Client::create(getKuksaAddress());

    // Use different sensors to avoid conflicts
    auto sensor1_result = resolver->get<float>("Vehicle.Private.Test.Sensor1");
    auto sensor2_result = resolver->get<int32_t>("Vehicle.Private.Test.Sensor2");
    ASSERT_TRUE(sensor1_result.ok() && sensor2_result.ok());
    auto sensor1 = *sensor1_result;
    auto sensor2 = *sensor2_result;

    // Register first sensor before start
    client->provide_signal(sensor1);

    // Start
    auto start_status = client->start();
    ASSERT_TRUE(start_status.ok());
    ASSERT_TRUE(client->wait_until_ready(5000ms).ok());

    // Try to register another signal after start - should throw
    EXPECT_THROW(client->provide_signal(sensor2), std::logic_error);

    std::this_thread::sleep_for(100ms);
    client->stop();
}

// ============================================================================
// Test 5: Combined actuator + signal provider
// ============================================================================

TEST_F(StreamingBatchPublishTest, CombinedActuatorAndSignalProvider) {
    LOG(INFO) << "Testing combined actuator + signal provider on same client";

    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok());
    auto resolver = std::move(*resolver_result);

    auto combined_client = *Client::create(getKuksaAddress());
    auto controller = *Client::create(getKuksaAddress());
    auto subscriber = *Client::create(getKuksaAddress());

    // Get handles
    auto actuator_result = resolver->get<bool>("Vehicle.Private.Test.BoolActuator");
    auto sensor_result = resolver->get<float>("Vehicle.Private.Test.FloatSensor");
    ASSERT_TRUE(actuator_result.ok() && sensor_result.ok());
    auto actuator = *actuator_result;
    auto sensor = *sensor_result;

    // Combined client: serves actuator AND provides sensor
    std::atomic<bool> actuator_called{false};
    std::atomic<bool> actuator_target{false};

    combined_client->serve_actuator(actuator, [&](bool target, const SignalHandle<bool>&) {
        LOG(INFO) << "Actuator callback: target=" << target;
        actuator_target = target;
        actuator_called = true;
    });

    combined_client->provide_signal(sensor);

    // Subscriber monitors sensor
    std::atomic<float> last_sensor_value{0.0f};
    subscriber->subscribe(sensor, [&](vss::types::QualifiedValue<float> qvalue) {
        if (qvalue.is_valid()) {
            LOG(INFO) << "Sensor update: " << *qvalue.value;
            last_sensor_value = *qvalue.value;
        }
    });

    // Start all clients
    auto sub_start = subscriber->start();
    ASSERT_TRUE(sub_start.ok());
    ASSERT_TRUE(subscriber->wait_until_ready(5000ms).ok());

    auto combined_start = combined_client->start();
    ASSERT_TRUE(combined_start.ok()) << "Combined client start failed: " << combined_start;
    auto ready_status = combined_client->wait_until_ready(5000ms);
    ASSERT_TRUE(ready_status.ok()) << "Combined client not ready: " << ready_status;

    std::this_thread::sleep_for(200ms);

    // Test 1: Trigger actuator
    auto set_status = controller->set(actuator, true);
    ASSERT_TRUE(set_status.ok()) << "Set actuator failed: " << set_status;

    ASSERT_TRUE(wait_for([&]() { return actuator_called.load(); }, 3000ms))
        << "Actuator should be called";
    EXPECT_TRUE(actuator_target.load());

    // Test 2: Publish sensor via batch
    auto pub_status = combined_client->publish_batch({{sensor, 99.9f}}, nullptr);
    ASSERT_TRUE(pub_status.ok()) << "Publish batch failed: " << pub_status;

    ASSERT_TRUE(wait_for([&]() { return last_sensor_value.load() > 99.0f; }, 3000ms))
        << "Sensor value should be published";
    EXPECT_FLOAT_EQ(last_sensor_value.load(), 99.9f);

    std::this_thread::sleep_for(100ms);
    combined_client->stop();
    subscriber->stop();
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;
    FLAGS_v = 0;

    ::testing::InitGoogleTest(&argc, argv);

    LOG(INFO) << "Streaming Batch Publish Integration Tests";
    LOG(INFO) << "=========================================";
    LOG(INFO) << "Testing provide_signals() + publish_batch() streaming flow";
    LOG(INFO) << "";

    return RUN_ALL_TESTS();
}
