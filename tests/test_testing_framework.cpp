/**
 * Unit tests for the SDV Testing Framework
 * Tests YAML parsing, test models, and test runner (without requiring Docker/KUKSA)
 */

#include <kuksa_cpp/testing/test_models.hpp>
#include <kuksa_cpp/testing/yaml_parser.hpp>
#include <kuksa_cpp/testing/test_runner.hpp>
#include <kuksa_cpp/testing/kuksa_client_wrapper.hpp>
#include <gtest/gtest.h>
#include <glog/logging.h>
#include <fstream>
#include <filesystem>

using namespace sdv::testing;

class TestingFrameworkTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir = std::filesystem::temp_directory_path() / "sdv_test";
        std::filesystem::create_directories(test_dir);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }

    std::string create_yaml_file(const std::string& name, const std::string& content) {
        auto path = test_dir / name;
        std::ofstream file(path);
        file << content;
        file.close();
        return path.string();
    }

    std::filesystem::path test_dir;
};

// ============================================================================
// YAML Parser Tests
// ============================================================================

TEST_F(TestingFrameworkTest, ParseMinimalTestSuite) {
    std::string yaml = R"(
test_suite:
  name: "Minimal Test"

  test_cases:
    - name: "Test Case 1"
      steps:
        - log: "Hello"
)";

    auto path = create_yaml_file("minimal.yaml", yaml);
    YamlParser parser;
    auto suite = parser.parse_file(path);

    EXPECT_EQ(suite.name, "Minimal Test");
    EXPECT_EQ(suite.test_cases.size(), 1);
    EXPECT_EQ(suite.test_cases[0].name, "Test Case 1");
    EXPECT_EQ(suite.test_cases[0].steps.size(), 1);
}

TEST_F(TestingFrameworkTest, ParseTestWithFixtures) {
    std::string yaml = R"(
test_suite:
  name: "Test with Fixtures"

  fixtures:
    - name: "battery_sensor"
      type: "periodic_publisher"
      config:
        path: "Vehicle.Battery.Level"
        value: "75.0"
        interval_ms: "1000"
    - name: "door_actuator"
      type: "provider"
      config:
        path: "Vehicle.Door.IsOpen"
        initial_value: "false"

  test_cases:
    - name: "Test Case"
      steps:
        - log: "Test"
)";

    auto path = create_yaml_file("fixtures.yaml", yaml);
    YamlParser parser;
    auto suite = parser.parse_file(path);

    EXPECT_EQ(suite.fixtures.size(), 2);
    EXPECT_EQ(suite.fixtures[0].name, "battery_sensor");
    EXPECT_EQ(suite.fixtures[0].type, "periodic_publisher");
    EXPECT_EQ(suite.fixtures[1].name, "door_actuator");
    EXPECT_EQ(suite.fixtures[1].type, "provider");
}

TEST_F(TestingFrameworkTest, ParseInjectStep) {
    std::string yaml = R"(
test_suite:
  name: "Inject Test"

  test_cases:
    - name: "Test"
      steps:
        - inject:
            path: "Vehicle.Speed"
            value: 50.5
)";

    auto path = create_yaml_file("inject.yaml", yaml);
    YamlParser parser;
    auto suite = parser.parse_file(path);

    ASSERT_EQ(suite.test_cases[0].steps.size(), 1);
    auto& step = suite.test_cases[0].steps[0];
    ASSERT_TRUE(std::holds_alternative<InjectData>(step.data));

    auto inject = std::get<InjectData>(step.data);
    EXPECT_EQ(inject.path, "Vehicle.Speed");
    // YAML parser may return float or double depending on precision
    EXPECT_TRUE(std::holds_alternative<float>(inject.value) || std::holds_alternative<double>(inject.value));
    if (std::holds_alternative<float>(inject.value)) {
        EXPECT_FLOAT_EQ(std::get<float>(inject.value), 50.5f);
    } else {
        EXPECT_DOUBLE_EQ(std::get<double>(inject.value), 50.5);
    }
    // RPC type is now determined automatically from KUKSA metadata
}

TEST_F(TestingFrameworkTest, ParseExpectStep) {
    std::string yaml = R"(
test_suite:
  name: "Expect Test"

  test_cases:
    - name: "Test"
      steps:
        - expect:
            path: "Vehicle.AC.IsActive"
            value: true
            timeout: 5.0
)";

    auto path = create_yaml_file("expect.yaml", yaml);
    YamlParser parser;
    auto suite = parser.parse_file(path);

    ASSERT_EQ(suite.test_cases[0].steps.size(), 1);
    auto& step = suite.test_cases[0].steps[0];
    ASSERT_TRUE(std::holds_alternative<ExpectData>(step.data));

    auto expect = std::get<ExpectData>(step.data);
    EXPECT_EQ(expect.path, "Vehicle.AC.IsActive");
    EXPECT_TRUE(std::holds_alternative<bool>(expect.value));
    EXPECT_TRUE(std::get<bool>(expect.value));
}

TEST_F(TestingFrameworkTest, ParseWaitStep) {
    std::string yaml = R"(
test_suite:
  name: "Wait Test"

  test_cases:
    - name: "Test"
      steps:
        - wait: 2.5
)";

    auto path = create_yaml_file("wait.yaml", yaml);
    YamlParser parser;
    auto suite = parser.parse_file(path);

    ASSERT_EQ(suite.test_cases[0].steps.size(), 1);
    auto& step = suite.test_cases[0].steps[0];
    ASSERT_TRUE(std::holds_alternative<WaitData>(step.data));

    auto wait = std::get<WaitData>(step.data);
    EXPECT_DOUBLE_EQ(wait.seconds, 2.5);
}

TEST_F(TestingFrameworkTest, ParseLogStep) {
    std::string yaml = R"(
test_suite:
  name: "Log Test"

  test_cases:
    - name: "Test"
      steps:
        - log: "This is a test message"
)";

    auto path = create_yaml_file("log.yaml", yaml);
    YamlParser parser;
    auto suite = parser.parse_file(path);

    ASSERT_EQ(suite.test_cases[0].steps.size(), 1);
    auto& step = suite.test_cases[0].steps[0];
    ASSERT_TRUE(std::holds_alternative<LogData>(step.data));

    auto log = std::get<LogData>(step.data);
    EXPECT_EQ(log.message, "This is a test message");
}

TEST_F(TestingFrameworkTest, ParseMultipleTestCases) {
    std::string yaml = R"(
test_suite:
  name: "Multiple Tests"

  test_cases:
    - name: "Test 1"
      steps:
        - log: "Test 1"
    - name: "Test 2"
      steps:
        - wait: 1
        - log: "Test 2"
    - name: "Test 3"
      steps:
        - log: "Test 3"
)";

    auto path = create_yaml_file("multiple.yaml", yaml);
    YamlParser parser;
    auto suite = parser.parse_file(path);

    EXPECT_EQ(suite.test_cases.size(), 3);
    EXPECT_EQ(suite.test_cases[0].name, "Test 1");
    EXPECT_EQ(suite.test_cases[1].name, "Test 2");
    EXPECT_EQ(suite.test_cases[2].name, "Test 3");
    EXPECT_EQ(suite.test_cases[1].steps.size(), 2);
}

TEST_F(TestingFrameworkTest, ParseValueTypes) {
    std::string yaml = R"(
test_suite:
  name: "Value Types Test"

  test_cases:
    - name: "Types"
      steps:
        - inject:
            path: "Vehicle.BoolValue"
            value: true
        - inject:
            path: "Vehicle.IntValue"
            value: 42
        - inject:
            path: "Vehicle.FloatValue"
            value: 3.14
        - inject:
            path: "Vehicle.StringValue"
            value: "hello"
)";

    auto path = create_yaml_file("types.yaml", yaml);
    YamlParser parser;
    auto suite = parser.parse_file(path);

    ASSERT_EQ(suite.test_cases[0].steps.size(), 4);

    // Bool
    auto inject0 = std::get<InjectData>(suite.test_cases[0].steps[0].data);
    EXPECT_TRUE(std::holds_alternative<bool>(inject0.value));
    EXPECT_TRUE(std::get<bool>(inject0.value));

    // Int
    auto inject1 = std::get<InjectData>(suite.test_cases[0].steps[1].data);
    EXPECT_TRUE(std::holds_alternative<int>(inject1.value));
    EXPECT_EQ(std::get<int>(inject1.value), 42);

    // Float/Double
    auto inject2 = std::get<InjectData>(suite.test_cases[0].steps[2].data);
    EXPECT_TRUE(std::holds_alternative<float>(inject2.value) || std::holds_alternative<double>(inject2.value));
    if (std::holds_alternative<float>(inject2.value)) {
        EXPECT_FLOAT_EQ(std::get<float>(inject2.value), 3.14f);
    } else {
        EXPECT_DOUBLE_EQ(std::get<double>(inject2.value), 3.14);
    }

    // String
    auto inject3 = std::get<InjectData>(suite.test_cases[0].steps[3].data);
    EXPECT_TRUE(std::holds_alternative<std::string>(inject3.value));
    EXPECT_EQ(std::get<std::string>(inject3.value), "hello");
}

// ============================================================================
// Test Models Tests
// ============================================================================

TEST(TestModelsTest, TestStepVariant) {
    TestStep step;

    // Test with InjectData
    step.type = StepType::INJECT;
    step.data = InjectData{"Vehicle.Speed", 50.0};
    EXPECT_TRUE(std::holds_alternative<InjectData>(step.data));

    // Test with LogData
    step.type = StepType::LOG;
    step.data = LogData{"Test message"};
    EXPECT_TRUE(std::holds_alternative<LogData>(step.data));
}

TEST(TestModelsTest, ValueVariant) {
    TestValue val;

    val = true;
    EXPECT_TRUE(std::holds_alternative<bool>(val));
    EXPECT_TRUE(std::get<bool>(val));

    val = int32_t(42);
    EXPECT_TRUE(std::holds_alternative<int32_t>(val));
    EXPECT_EQ(std::get<int32_t>(val), 42);

    val = 3.14;
    EXPECT_TRUE(std::holds_alternative<double>(val));
    EXPECT_DOUBLE_EQ(std::get<double>(val), 3.14);

    val = std::string("test");
    EXPECT_TRUE(std::holds_alternative<std::string>(val));
    EXPECT_EQ(std::get<std::string>(val), "test");
}

TEST(TestModelsTest, TestCaseStructure) {
    TestCase tc;
    tc.name = "My Test";

    TestStep step1;
    step1.type = StepType::LOG;
    step1.data = LogData{"Step 1"};
    tc.steps.push_back(step1);

    TestStep step2;
    step2.type = StepType::WAIT;
    step2.data = WaitData{1.0};
    tc.steps.push_back(step2);

    EXPECT_EQ(tc.name, "My Test");
    EXPECT_EQ(tc.steps.size(), 2);
}

TEST(TestModelsTest, TestSuiteStructure) {
    TestSuite suite;
    suite.name = "My Suite";

    Fixture fixture;
    fixture.name = "sensor1";
    fixture.type = "periodic_publisher";
    suite.fixtures.push_back(fixture);

    TestCase tc;
    tc.name = "Test 1";
    suite.test_cases.push_back(tc);

    EXPECT_EQ(suite.name, "My Suite");
    EXPECT_EQ(suite.fixtures.size(), 1);
    EXPECT_EQ(suite.test_cases.size(), 1);
}

// ============================================================================
// KuksaClientWrapper Tests (without actual connection)
// ============================================================================

TEST(KuksaClientWrapperTest, Construction) {
    // Should not throw on construction
    EXPECT_NO_THROW({
        KuksaClientWrapper client("localhost:55555");
    });
}

TEST(KuksaClientWrapperTest, DisconnectBeforeConnect) {
    KuksaClientWrapper client("localhost:55555");
    // Should not crash when disconnecting before connecting
    EXPECT_NO_THROW(client.disconnect());
}

// Note: Actual connection tests require KUKSA databroker running
// Those are tested separately with Docker integration tests

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;
    FLAGS_minloglevel = google::GLOG_WARNING; // Reduce log noise in tests

    return RUN_ALL_TESTS();
}
