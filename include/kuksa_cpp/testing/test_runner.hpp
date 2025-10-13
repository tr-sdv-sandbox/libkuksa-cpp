#pragma once

#include "test_models.hpp"
#include "kuksa_client_wrapper.hpp"
#include <memory>

namespace sdv {
namespace testing {

class TestRunner {
public:
    explicit TestRunner(std::shared_ptr<KuksaClientWrapper> client);

    TestSuiteResult run_suite(const TestSuite& suite);
    TestCaseResult run_test_case(const TestCase& test_case);

private:
    std::shared_ptr<KuksaClientWrapper> client_;

    StepResult run_step(const TestStep& step);

    // Step executors
    StepResult execute_inject(const InjectData& data);
    StepResult execute_expect(const ExpectData& data, double timeout);
    StepResult execute_wait(const WaitData& data);
    StepResult execute_log(const LogData& data);
    StepResult execute_expect_state(const ExpectStateData& data, double timeout);
    StepResult execute_expect_transition(const ExpectTransitionData& data, double timeout);

    // Helpers
    bool values_match(const TestValue& a, const TestValue& b);
    void print_step_result(const StepResult& result);
    void print_test_summary(const TestSuiteResult& result);
};

} // namespace testing
} // namespace kuksa
