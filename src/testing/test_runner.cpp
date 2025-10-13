#include "kuksa_cpp/testing/test_runner.hpp"
#include <glog/logging.h>
#include <chrono>
#include <thread>
#include <iostream>

namespace sdv {
namespace testing {

TestRunner::TestRunner(std::shared_ptr<KuksaClientWrapper> client)
    : client_(client) {
}

bool TestRunner::values_match(const TestValue& a, const TestValue& b) {
    return std::visit([&b](auto&& a_val) -> bool {
        return std::visit([&a_val](auto&& b_val) -> bool {
            using A = std::decay_t<decltype(a_val)>;
            using B = std::decay_t<decltype(b_val)>;

            if constexpr (std::is_same_v<A, B>) {
                return a_val == b_val;
            } else {
                return false;
            }
        }, b);
    }, a);
}

StepResult TestRunner::execute_inject(const InjectData& data) {
    StepResult result;
    result.status = TestStatus::RUNNING;

    // RPC type (Actuate vs PublishValue) is automatically determined
    // by the wrapper based on signal type from KUKSA metadata
    bool success = client_->inject(data.path, data.value);

    if (success) {
        result.status = TestStatus::PASSED;
    } else {
        result.status = TestStatus::FAILED;
        result.message = "Failed to inject value";
    }

    return result;
}

StepResult TestRunner::execute_expect(const ExpectData& data, double timeout) {
    StepResult result;
    result.status = TestStatus::RUNNING;

    auto start = std::chrono::steady_clock::now();
    auto timeout_duration = std::chrono::duration<double>(timeout);

    while (std::chrono::steady_clock::now() - start < timeout_duration) {
        auto actual_value = client_->get(data.path);

        if (actual_value.has_value()) {
            if (values_match(data.value, actual_value.value())) {
                result.status = TestStatus::PASSED;
                return result;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    result.status = TestStatus::FAILED;
    result.message = "Expectation not met within " + std::to_string(timeout) + "s";

    return result;
}

StepResult TestRunner::execute_wait(const WaitData& data) {
    StepResult result;
    result.status = TestStatus::RUNNING;

    LOG(INFO) << "Waiting " << data.seconds << "s";
    std::this_thread::sleep_for(std::chrono::duration<double>(data.seconds));

    result.status = TestStatus::PASSED;
    return result;
}

StepResult TestRunner::execute_log(const LogData& data) {
    StepResult result;
    result.status = TestStatus::RUNNING;

    LOG(INFO) << "[TEST LOG] " << data.message;

    result.status = TestStatus::PASSED;
    return result;
}

StepResult TestRunner::execute_expect_state(const ExpectStateData& data, double timeout) {
    StepResult result;
    result.status = TestStatus::RUNNING;

    // TODO: Implement state machine tracking
    LOG(WARNING) << "expect_state not yet implemented";

    result.status = TestStatus::SKIPPED;
    result.message = "State machine tracking not implemented";

    return result;
}

StepResult TestRunner::execute_expect_transition(const ExpectTransitionData& data, double timeout) {
    StepResult result;
    result.status = TestStatus::RUNNING;

    // TODO: Implement state machine tracking
    LOG(WARNING) << "expect_transition not yet implemented";

    result.status = TestStatus::SKIPPED;
    result.message = "State machine tracking not implemented";

    return result;
}

StepResult TestRunner::run_step(const TestStep& step) {
    auto start = std::chrono::steady_clock::now();

    StepResult result;
    result.step = step;

    result = std::visit([this, &step](auto&& data) -> StepResult {
        using T = std::decay_t<decltype(data)>;

        if constexpr (std::is_same_v<T, InjectData>) {
            return execute_inject(data);
        }
        else if constexpr (std::is_same_v<T, ExpectData>) {
            return execute_expect(data, step.timeout);
        }
        else if constexpr (std::is_same_v<T, WaitData>) {
            return execute_wait(data);
        }
        else if constexpr (std::is_same_v<T, LogData>) {
            return execute_log(data);
        }
        else if constexpr (std::is_same_v<T, ExpectStateData>) {
            return execute_expect_state(data, step.timeout);
        }
        else if constexpr (std::is_same_v<T, ExpectTransitionData>) {
            return execute_expect_transition(data, step.timeout);
        }

        StepResult r;
        r.status = TestStatus::FAILED;
        r.message = "Unknown step type";
        return r;
    }, step.data);

    auto end = std::chrono::steady_clock::now();
    result.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
    result.step = step;

    print_step_result(result);

    return result;
}

void TestRunner::print_step_result(const StepResult& result) {
    std::string status_str;
    std::string color_code;

    switch (result.status) {
        case TestStatus::PASSED:
            status_str = "✓ PASS";
            color_code = "\033[32m";  // Green
            break;
        case TestStatus::FAILED:
            status_str = "✗ FAIL";
            color_code = "\033[31m";  // Red
            break;
        case TestStatus::SKIPPED:
            status_str = "○ SKIP";
            color_code = "\033[33m";  // Yellow
            break;
        default:
            status_str = "?";
            color_code = "\033[0m";
            break;
    }

    std::cout << color_code << "      " << status_str << "\033[0m";

    if (result.message.has_value()) {
        std::cout << " - " << result.message.value();
    }

    std::cout << " (" << static_cast<int>(result.duration_ms) << " ms)" << std::endl;
}

TestCaseResult TestRunner::run_test_case(const TestCase& test_case) {
    TestCaseResult result;
    result.test_case = test_case;
    result.status = TestStatus::RUNNING;

    auto start = std::chrono::steady_clock::now();

    std::cout << "\033[32m[ RUN      ]\033[0m " << test_case.name << std::endl;

    // Run setup steps
    for (const auto& step : test_case.setup) {
        auto step_result = run_step(step);
        result.step_results.push_back(step_result);

        if (step_result.status == TestStatus::FAILED) {
            result.status = TestStatus::FAILED;
            std::cout << "\033[31m[  FAILED  ]\033[0m " << test_case.name << " (setup failed)" << std::endl;
            return result;
        }
    }

    // Run test steps
    for (const auto& step : test_case.steps) {
        auto step_result = run_step(step);
        result.step_results.push_back(step_result);

        if (step_result.status == TestStatus::FAILED) {
            result.status = TestStatus::FAILED;
            auto end = std::chrono::steady_clock::now();
            result.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

            std::cout << "\033[31m[  FAILED  ]\033[0m " << test_case.name
                      << " (" << static_cast<int>(result.duration_ms) << " ms)" << std::endl;
            if (step_result.message.has_value()) {
                std::cout << "           → " << step_result.message.value() << std::endl;
            }
            return result;
        }
    }

    // Run teardown steps
    for (const auto& step : test_case.teardown) {
        auto step_result = run_step(step);
        result.step_results.push_back(step_result);
    }

    result.status = TestStatus::PASSED;
    auto end = std::chrono::steady_clock::now();
    result.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << "\033[32m[  PASSED  ]\033[0m " << test_case.name
              << " (" << static_cast<int>(result.duration_ms) << " ms)" << std::endl;

    return result;
}

TestSuiteResult TestRunner::run_suite(const TestSuite& suite) {
    TestSuiteResult result;
    result.suite = suite;

    auto start = std::chrono::steady_clock::now();

    std::cout << "\n\033[34m━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\033[0m" << std::endl;
    std::cout << "\033[32m[INFO]\033[0m Running test suite: " << suite.name << std::endl;
    std::cout << "\033[34m━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\033[0m\n" << std::endl;

    // Run suite setup
    for (const auto& step : suite.setup) {
        std::cout << "\033[90m      • Suite setup\033[0m" << std::endl;
        run_step(step);
    }

    // Run test cases
    for (const auto& test_case : suite.test_cases) {
        auto test_result = run_test_case(test_case);
        result.test_case_results.push_back(test_result);

        result.total++;
        if (test_result.status == TestStatus::PASSED) {
            result.passed++;
        } else if (test_result.status == TestStatus::FAILED) {
            result.failed++;
        } else if (test_result.status == TestStatus::SKIPPED) {
            result.skipped++;
        }
    }

    auto end = std::chrono::steady_clock::now();
    result.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

    print_test_summary(result);

    return result;
}

void TestRunner::print_test_summary(const TestSuiteResult& result) {
    std::cout << "\n================================================================================\n";
    std::cout << "Test Report: " << result.suite.name << std::endl;
    std::cout << "================================================================================\n";
    std::cout << "Duration: " << static_cast<int>(result.duration_ms) << " ms\n\n";

    std::cout << "Summary:\n";
    std::cout << "  Total:   " << result.total << "\n";
    std::cout << "  Passed:  " << result.passed << " ("
              << (result.total > 0 ? (result.passed * 100 / result.total) : 0) << "%)\n";
    std::cout << "  Failed:  " << result.failed << "\n";
    std::cout << "  Skipped: " << result.skipped << "\n\n";

    if (result.failed > 0) {
        std::cout << "Failed Tests:\n";
        for (const auto& test_result : result.test_case_results) {
            if (test_result.status == TestStatus::FAILED) {
                std::cout << "  ✗ " << test_result.test_case.name << "\n";
            }
        }
    }

    std::cout << std::endl;
}


} // namespace testing
} // namespace kuksa
