/**
 * @file test_testing_library_integration.cpp
 * @brief Integration tests for the SDV testing library
 *
 * Tests the complete testing library stack:
 * - YAML parsing
 * - Test runner execution
 * - KUKSA client wrapper
 * - inject, expect, wait steps
 *
 * Requires: KUKSA databroker running on localhost:55555
 */

#include <kuksa_cpp/testing/yaml_parser.hpp>
#include <kuksa_cpp/testing/kuksa_client_wrapper.hpp>
#include <kuksa_cpp/testing/test_runner.hpp>
#include <kuksa_cpp/testing/test_models.hpp>
#include <kuksa_cpp/kuksa.hpp>
#include <kuksa_cpp/client.hpp>
#include <kuksa_cpp/resolver.hpp>
#include <gtest/gtest.h>
#include <glog/logging.h>
#include <fstream>
#include <filesystem>
#include <atomic>
#include "kuksa_test_fixture.hpp"

using namespace sdv::testing;
using kuksa::test::KuksaTestFixture;

class TestingLibraryIntegrationTest : public KuksaTestFixture {
protected:
    std::filesystem::path test_dir;

    void SetUp() override {
        KuksaTestFixture::SetUp();
        test_dir = std::filesystem::temp_directory_path() / "sdv_testing_integration";
        std::filesystem::create_directories(test_dir);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir);
        KuksaTestFixture::TearDown();
    }

    std::string create_yaml_file(const std::string& name, const std::string& content) {
        auto path = test_dir / name;
        std::ofstream file(path);
        file << content;
        file.close();
        return path.string();
    }
};

// ============================================================================
// YAML Parser Integration Tests
// ============================================================================

TEST_F(TestingLibraryIntegrationTest, ParseCompleteTestSuite) {
    std::string yaml = R"(
test_suite:
  name: "Integration Test Suite"

  fixtures:
    - name: "battery_sensor"
      type: "periodic_publisher"
      config:
        path: "Vehicle.Battery.Level"
        value: "75.0"
        interval_ms: "1000"

  test_cases:
    - name: "Test Case 1"
      steps:
        - log: "Starting test"
        - inject:
            path: "Vehicle.Private.Test.FloatSensor"
            value: 50.0
        - wait: 0.5
        - expect:
            path: "Vehicle.Private.Test.FloatSensor"
            value: 50.0
            timeout: 2.0
)";

    auto path = create_yaml_file("complete.yaml", yaml);
    YamlParser parser;
    TestSuite suite = parser.parse_file(path);

    EXPECT_EQ(suite.name, "Integration Test Suite");
    EXPECT_EQ(suite.fixtures.size(), 1);
    EXPECT_EQ(suite.test_cases.size(), 1);
    EXPECT_EQ(suite.test_cases[0].steps.size(), 4);
}

// ============================================================================
// KUKSA Client Wrapper Integration Tests
// ============================================================================

TEST_F(TestingLibraryIntegrationTest, ConnectToKuksa) {
    KuksaClientWrapper client(getKuksaAddress());
    EXPECT_TRUE(client.connect());
    client.disconnect();
}

TEST_F(TestingLibraryIntegrationTest, InjectAndGetValue) {
    KuksaClientWrapper client(getKuksaAddress());
    ASSERT_TRUE(client.connect());

    // Inject a float value (sensor - uses PublishValue RPC)
    EXPECT_TRUE(client.inject("Vehicle.Private.Test.FloatSensor", 100.0f));

    // Wait a bit for value to be set
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Get the value back
    auto value = client.get("Vehicle.Private.Test.FloatSensor");
    ASSERT_TRUE(value.has_value());

    // Check if it's a float or double
    if (std::holds_alternative<float>(value.value())) {
        EXPECT_FLOAT_EQ(std::get<float>(value.value()), 100.0f);
    } else if (std::holds_alternative<double>(value.value())) {
        EXPECT_DOUBLE_EQ(std::get<double>(value.value()), 100.0);
    } else {
        FAIL() << "Value is not float or double";
    }

    client.disconnect();
}

TEST_F(TestingLibraryIntegrationTest, InjectMultipleTypeSensors) {
    KuksaClientWrapper client(getKuksaAddress());
    ASSERT_TRUE(client.connect());

    // Test boolean sensor (PublishValue RPC)
    EXPECT_TRUE(client.inject("Vehicle.Private.Test.BoolSensor", true));

    // Test integer sensor (PublishValue RPC)
    EXPECT_TRUE(client.inject("Vehicle.Private.Test.Int32Sensor", static_cast<int32_t>(42)));

    // Test float sensor (PublishValue RPC)
    EXPECT_TRUE(client.inject("Vehicle.Private.Test.FloatSensor", 60.5f));

    // Test string sensor (PublishValue RPC)
    EXPECT_TRUE(client.inject("Vehicle.Private.Test.StringSensor", std::string("TEST123456")));

    // Wait for values to be set
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Verify all values were set correctly
    auto bool_val = client.get("Vehicle.Private.Test.BoolSensor");
    ASSERT_TRUE(bool_val.has_value());
    EXPECT_TRUE(std::get<bool>(*bool_val));

    auto int_val = client.get("Vehicle.Private.Test.Int32Sensor");
    ASSERT_TRUE(int_val.has_value());
    EXPECT_EQ(std::get<int32_t>(*int_val), 42);

    auto float_val = client.get("Vehicle.Private.Test.FloatSensor");
    ASSERT_TRUE(float_val.has_value());
    EXPECT_FLOAT_EQ(std::get<float>(*float_val), 60.5f);

    auto string_val = client.get("Vehicle.Private.Test.StringSensor");
    ASSERT_TRUE(string_val.has_value());
    EXPECT_EQ(std::get<std::string>(*string_val), "TEST123456");

    client.disconnect();
}

TEST_F(TestingLibraryIntegrationTest, InjectActuatorsUsesCorrectRPC) {
    KuksaClientWrapper client(getKuksaAddress());
    ASSERT_TRUE(client.connect());

    // Test actuators - these should use Actuate() RPC (set_target)
    // Note: Actuators require a provider to be running, so inject will FAIL
    // with "Provider does not exist". This verifies we're correctly identifying
    // actuators and using the Actuate() RPC (not PublishValue()).

    // Test boolean actuator (should try Actuate RPC and fail - no provider)
    EXPECT_FALSE(client.inject("Vehicle.Private.Test.BoolActuator", true));

    // Test integer actuator (should try Actuate RPC and fail - no provider)
    EXPECT_FALSE(client.inject("Vehicle.Private.Test.Int32Actuator", static_cast<int32_t>(100)));

    // Test float actuator (should try Actuate RPC and fail - no provider)
    EXPECT_FALSE(client.inject("Vehicle.Private.Test.FloatActuator", 75.5f));

    // Test string actuator (should try Actuate RPC and fail - no provider)
    EXPECT_FALSE(client.inject("Vehicle.Private.Test.StringActuator", std::string("ACTUATE_TEST")));

    client.disconnect();
}

TEST_F(TestingLibraryIntegrationTest, ActuatorWithProviderFullFlow) {
    // This test verifies the complete actuator flow:
    // 1. Create a client that owns an actuator
    // 2. inject() uses Actuate() RPC to set TARGET
    // 3. Client responds and sets ACTUAL
    // 4. get() reads the ACTUAL value back

    using namespace kuksa;

    // Track what was received by the client
    std::atomic<float> received_target{0.0f};
    std::atomic<int> callback_count{0};

    // Resolve actuator handle
    auto resolver_result = Resolver::create(getKuksaAddress());
    ASSERT_TRUE(resolver_result.ok()) << "Failed to create resolver: " << resolver_result.status();
    auto resolver = std::move(*resolver_result);

    auto actuator_result = resolver->get<float>("Vehicle.Private.Test.FloatActuator");
    ASSERT_TRUE(actuator_result.ok()) << "Failed to get actuator: " << actuator_result.status();
    auto actuator = *actuator_result;

    // Create unified client for the actuator
    auto client = *Client::create(getKuksaAddress());

    // Keep raw pointer for use inside callback
    Client* client_ptr = client.get();

    // Register handler for actuation requests BEFORE starting
    client->serve_actuator(actuator, [&, client_ptr](float target, const SignalHandle<float>& handle) {
        LOG(INFO) << "Client received actuation request: target=" << target;
        received_target = target;
        callback_count++;

        // Simulate hardware processing
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Publish ACTUAL value to match TARGET
        auto status = client_ptr->publish(handle, target);
        if (!status.ok()) {
            LOG(ERROR) << "Failed to publish actual: " << status;
        }
    });

    // Start client (must be after serve_actuator())
    client->start();

    // Wait for client to be ready
    auto ready_status = client->wait_until_ready(std::chrono::milliseconds(5000));
    ASSERT_TRUE(ready_status.ok()) << "Client not ready: " << ready_status;

    // Now use KuksaClientWrapper to inject to the actuator
    KuksaClientWrapper test_client(getKuksaAddress());
    ASSERT_TRUE(test_client.connect());

    // Inject should succeed now because client exists
    // This should use Actuate() RPC to set TARGET
    LOG(INFO) << "Injecting to actuator via KuksaClientWrapper";
    EXPECT_TRUE(test_client.inject("Vehicle.Private.Test.FloatActuator", 75.5f));

    // Wait for client to process the actuation
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Verify client received the target value
    EXPECT_EQ(callback_count.load(), 1) << "Client callback should have been called once";
    EXPECT_FLOAT_EQ(received_target.load(), 75.5f);

    // Get the ACTUAL value back (should read what client published)
    auto value = test_client.get("Vehicle.Private.Test.FloatActuator");
    ASSERT_TRUE(value.has_value()) << "Failed to get actuator ACTUAL value";

    // Verify it's a float and matches what we set
    ASSERT_TRUE(std::holds_alternative<float>(*value)) << "Value is not a float";
    EXPECT_FLOAT_EQ(std::get<float>(*value), 75.5f);

    // Test another actuation to ensure it works repeatedly
    LOG(INFO) << "Testing second actuation";
    EXPECT_TRUE(test_client.inject("Vehicle.Private.Test.FloatActuator", 42.0f));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    EXPECT_EQ(callback_count.load(), 2) << "Client callback should have been called twice";
    EXPECT_FLOAT_EQ(received_target.load(), 42.0f);

    value = test_client.get("Vehicle.Private.Test.FloatActuator");
    ASSERT_TRUE(value.has_value());
    ASSERT_TRUE(std::holds_alternative<float>(*value));
    EXPECT_FLOAT_EQ(std::get<float>(*value), 42.0f);

    test_client.disconnect();
    client->stop();
}

// ============================================================================
// Test Runner Integration Tests
// ============================================================================

TEST_F(TestingLibraryIntegrationTest, RunSimpleTestCase) {
    std::string yaml = R"(
test_suite:
  name: "Simple Test"

  test_cases:
    - name: "Inject and Expect"
      steps:
        - inject:
            path: "Vehicle.Private.Test.FloatSensor"
            value: 42.0
        - wait: 0.2
        - expect:
            path: "Vehicle.Private.Test.FloatSensor"
            value: 42.0
            timeout: 2.0
)";

    auto path = create_yaml_file("simple.yaml", yaml);
    YamlParser parser;
    TestSuite suite = parser.parse_file(path);

    auto client = std::make_shared<KuksaClientWrapper>(getKuksaAddress());
    ASSERT_TRUE(client->connect());

    TestRunner runner(client);
    TestSuiteResult result = runner.run_suite(suite);

    client->disconnect();

    EXPECT_EQ(result.total, 1);
    EXPECT_EQ(result.passed, 1);
    EXPECT_EQ(result.failed, 0);
}

TEST_F(TestingLibraryIntegrationTest, RunMultipleTestCases) {
    std::string yaml = R"(
test_suite:
  name: "Multiple Tests"

  test_cases:
    - name: "Test 1"
      steps:
        - inject:
            path: "Vehicle.Private.Test.FloatSensor"
            value: 10.0
        - wait: 0.1
        - expect:
            path: "Vehicle.Private.Test.FloatSensor"
            value: 10.0
            timeout: 1.0

    - name: "Test 2"
      steps:
        - inject:
            path: "Vehicle.Private.Test.FloatSensor"
            value: 20.0
        - wait: 0.1
        - expect:
            path: "Vehicle.Private.Test.FloatSensor"
            value: 20.0
            timeout: 1.0
)";

    auto path = create_yaml_file("multiple.yaml", yaml);
    YamlParser parser;
    TestSuite suite = parser.parse_file(path);

    auto client = std::make_shared<KuksaClientWrapper>(getKuksaAddress());
    ASSERT_TRUE(client->connect());

    TestRunner runner(client);
    TestSuiteResult result = runner.run_suite(suite);

    client->disconnect();

    EXPECT_EQ(result.total, 2);
    EXPECT_EQ(result.passed, 2);
    EXPECT_EQ(result.failed, 0);
}

TEST_F(TestingLibraryIntegrationTest, TestWithLogSteps) {
    std::string yaml = R"(
test_suite:
  name: "Test with Logs"

  test_cases:
    - name: "Log Test"
      steps:
        - log: "Starting test"
        - inject:
            path: "Vehicle.Private.Test.FloatSensor"
            value: 55.5
        - log: "Injected sensor value"
        - wait: 0.1
        - expect:
            path: "Vehicle.Private.Test.FloatSensor"
            value: 55.5
            timeout: 1.0
        - log: "Test completed"
)";

    auto path = create_yaml_file("with_logs.yaml", yaml);
    YamlParser parser;
    TestSuite suite = parser.parse_file(path);

    auto client = std::make_shared<KuksaClientWrapper>(getKuksaAddress());
    ASSERT_TRUE(client->connect());

    TestRunner runner(client);
    TestSuiteResult result = runner.run_suite(suite);

    client->disconnect();

    EXPECT_EQ(result.total, 1);
    EXPECT_EQ(result.passed, 1);
    EXPECT_EQ(result.failed, 0);
}

TEST_F(TestingLibraryIntegrationTest, ExpectTimeout) {
    std::string yaml = R"(
test_suite:
  name: "Timeout Test"

  test_cases:
    - name: "Should Timeout"
      steps:
        - inject:
            path: "Vehicle.Private.Test.FloatSensor"
            value: 10.0
        - wait: 0.1
        - expect:
            path: "Vehicle.Private.Test.FloatSensor"
            value: 99.0
            timeout: 0.5
)";

    auto path = create_yaml_file("timeout.yaml", yaml);
    YamlParser parser;
    TestSuite suite = parser.parse_file(path);

    auto client = std::make_shared<KuksaClientWrapper>(getKuksaAddress());
    ASSERT_TRUE(client->connect());

    TestRunner runner(client);
    TestSuiteResult result = runner.run_suite(suite);

    client->disconnect();

    // This test should fail due to timeout
    EXPECT_EQ(result.total, 1);
    EXPECT_EQ(result.passed, 0);
    EXPECT_EQ(result.failed, 1);
}

TEST_F(TestingLibraryIntegrationTest, BooleanValues) {
    std::string yaml = R"(
test_suite:
  name: "Boolean Test"

  test_cases:
    - name: "Boolean Inject and Expect"
      steps:
        - inject:
            path: "Vehicle.Private.Test.BoolSensor"
            value: true
        - wait: 0.1
        - expect:
            path: "Vehicle.Private.Test.BoolSensor"
            value: true
            timeout: 1.0
)";

    auto path = create_yaml_file("boolean.yaml", yaml);
    YamlParser parser;
    TestSuite suite = parser.parse_file(path);

    auto client = std::make_shared<KuksaClientWrapper>(getKuksaAddress());
    ASSERT_TRUE(client->connect());

    TestRunner runner(client);
    TestSuiteResult result = runner.run_suite(suite);

    client->disconnect();

    EXPECT_EQ(result.total, 1);
    EXPECT_EQ(result.passed, 1);
    EXPECT_EQ(result.failed, 0);
}

// ============================================================================
// End-to-End Test (Complete Flow)
// ============================================================================

TEST_F(TestingLibraryIntegrationTest, CompleteEndToEndTest) {
    std::string yaml = R"(
test_suite:
  name: "End-to-End Integration Test"

  test_cases:
    - name: "Vehicle State Test"
      steps:
        - log: "Testing vehicle state signals"

        # Set float sensor
        - inject:
            path: "Vehicle.Private.Test.FloatSensor"
            value: 65.0

        # Set bool sensor
        - inject:
            path: "Vehicle.Private.Test.BoolSensor"
            value: true

        - wait: 0.2

        # Verify float sensor
        - expect:
            path: "Vehicle.Private.Test.FloatSensor"
            value: 65.0
            timeout: 2.0

        # Verify bool sensor
        - expect:
            path: "Vehicle.Private.Test.BoolSensor"
            value: true
            timeout: 2.0

        - log: "All checks passed"
)";

    auto path = create_yaml_file("end_to_end.yaml", yaml);

    // Parse
    YamlParser parser;
    TestSuite suite = parser.parse_file(path);
    ASSERT_EQ(suite.test_cases.size(), 1);

    // Connect
    auto client = std::make_shared<KuksaClientWrapper>(getKuksaAddress());
    ASSERT_TRUE(client->connect());

    // Run
    TestRunner runner(client);
    TestSuiteResult result = runner.run_suite(suite);

    // Cleanup
    client->disconnect();

    // Verify
    EXPECT_EQ(result.total, 1);
    EXPECT_EQ(result.passed, 1);
    EXPECT_EQ(result.failed, 0);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;
    FLAGS_minloglevel = google::GLOG_INFO;

    return RUN_ALL_TESTS();
}
