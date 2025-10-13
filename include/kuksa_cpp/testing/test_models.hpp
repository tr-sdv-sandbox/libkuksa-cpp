#pragma once

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <optional>
#include <kuksa_cpp/types.hpp>

namespace sdv {
namespace testing {

// Value types that can be in test data
using TestValue = std::variant<bool, int32_t, float, double, std::string>;

enum class StepType {
    INJECT,
    EXPECT,
    WAIT,
    LOG,
    EXPECT_STATE,
    EXPECT_TRANSITION
};

/*enum class ActuatorMode {
    TARGET,  // Use Actuate() RPC
    ACTUAL   // Use PublishValue() RPC
};
*/

enum class TestStatus {
    PENDING,
    RUNNING,
    PASSED,
    FAILED,
    SKIPPED
};

struct InjectData {
    std::string path;
    TestValue value;
    // Note: RPC type (Actuate vs PublishValue) is determined automatically
    // based on signal type from KUKSA metadata (actuator vs sensor)
};

struct ExpectData {
    std::string path;
    TestValue value;
};

struct WaitData {
    double seconds;
};

struct LogData {
    std::string message;
};

struct ExpectStateData {
    std::string state_machine;
    std::string state;
};

struct ExpectTransitionData {
    std::string state_machine;
    std::string from_state;
    std::string to_state;
};

struct TestStep {
    StepType type;
    std::variant<InjectData, ExpectData, WaitData, LogData, ExpectStateData, ExpectTransitionData> data;
    double timeout = 5.0;
    std::optional<std::string> description;
};

struct TestCase {
    std::string name;
    std::optional<std::string> description;
    std::vector<TestStep> setup;
    std::vector<TestStep> steps;
    std::vector<TestStep> teardown;
    std::vector<std::string> tags;
};

struct Fixture {
    std::string name;
    std::string type;
    std::map<std::string, std::string> config;
};

struct TestSuite {
    std::string name;
    std::optional<std::string> description;
    std::vector<Fixture> fixtures;
    std::vector<TestStep> setup;
    std::vector<TestStep> teardown;
    std::vector<TestCase> test_cases;
};

struct StepResult {
    TestStep step;
    TestStatus status;
    std::optional<std::string> message;
    double duration_ms = 0.0;
};

struct TestCaseResult {
    TestCase test_case;
    TestStatus status;
    std::vector<StepResult> step_results;
    double duration_ms = 0.0;
};

struct TestSuiteResult {
    TestSuite suite;
    std::vector<TestCaseResult> test_case_results;
    int total = 0;
    int passed = 0;
    int failed = 0;
    int skipped = 0;
    double duration_ms = 0.0;
};

} // namespace testing
} // namespace kuksa
