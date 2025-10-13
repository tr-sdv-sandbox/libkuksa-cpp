#include "kuksa_cpp/testing/gtest_integration.hpp"
#include <glog/logging.h>
#include <chrono>
#include <thread>
#include <sstream>
#include <ctime>
#include <iostream>
#include <fstream>

namespace sdv {
namespace testing {

std::string YamlTestFixture::GenerateContainerName(const std::string& prefix) {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::system_clock::to_time_t(now);
    return prefix + "-" + std::to_string(timestamp);
}

int YamlTestFixture::RunCommand(const std::string& cmd) {
    return system(cmd.c_str());
}

void YamlTestFixture::StartDatabroker() {
    LOG(INFO) << "Starting KUKSA databroker...";

    std::string cmd = "docker run -d "
        "--name " + databroker_name_ + " "
        "--network " + network_name_ + " "
        "-p 55555:55555 "
        "ghcr.io/eclipse-kuksa/kuksa-databroker:0.6.0 "
        "--metadata /vss.json 2>&1";

    int result = RunCommand(cmd);
    if (result != 0) {
        throw std::runtime_error("Failed to start databroker");
    }
}

void YamlTestFixture::WaitForDatabroker() {
    LOG(INFO) << "Waiting for databroker to be ready...";

    for (int i = 0; i < 20; i++) {
        int result = RunCommand("nc -z localhost 55555 2>/dev/null");
        if (result == 0) {
            LOG(INFO) << "Databroker is ready";
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    throw std::runtime_error("Databroker failed to start");
}

void YamlTestFixture::StartFixtures(const std::vector<Fixture>& fixtures) {
    if (fixtures.empty()) {
        return;
    }

    LOG(INFO) << "Starting fixture runner with " << fixtures.size() << " fixture(s)...";

    // Create JSON config for fixtures
    std::stringstream json;
    json << "{\"fixtures\":[";
    for (size_t i = 0; i < fixtures.size(); i++) {
        if (i > 0) json << ",";
        json << "{\"name\":\"" << fixtures[i].name << "\","
             << "\"type\":\"" << fixtures[i].type << "\","
             << "\"config\":{";

        bool first = true;
        for (const auto& [key, value] : fixtures[i].config) {
            if (!first) json << ",";
            json << "\"" << key << "\":\"" << value << "\"";
            first = false;
        }
        json << "}}";
    }
    json << "]}";

    // Write to temp file
    std::string fixture_file = "/tmp/fixtures-" + std::to_string(std::time(nullptr)) + ".json";
    std::ofstream ofs(fixture_file);
    ofs << json.str();
    ofs.close();

    std::string cmd = "docker run -d "
        "--name " + fixture_name_ + " "
        "--network " + network_name_ + " "
        "-v " + fixture_file + ":/app/fixtures.json "
        "sdv-fixture-runner:latest "
        "fixture-runner --config /app/fixtures.json 2>&1";

    int result = RunCommand(cmd);
    if (result != 0) {
        throw std::runtime_error("Failed to start fixture runner");
    }

    // Wait for fixtures to register
    std::this_thread::sleep_for(std::chrono::seconds(3));
}

void YamlTestFixture::StopAllContainers() {
    LOG(INFO) << "Stopping test containers...";

    RunCommand("docker stop --time 1 " + databroker_name_ + " 2>/dev/null");
    RunCommand("docker rm " + databroker_name_ + " 2>/dev/null");

    if (!fixture_name_.empty()) {
        RunCommand("docker stop --time 1 " + fixture_name_ + " 2>/dev/null");
        RunCommand("docker rm " + fixture_name_ + " 2>/dev/null");
    }

    RunCommand("docker network rm " + network_name_ + " 2>/dev/null");
}

void YamlTestFixture::SetUp() {
    // Generate unique names
    network_name_ = GenerateContainerName("test-network");
    databroker_name_ = GenerateContainerName("databroker");
    fixture_name_ = GenerateContainerName("fixture");

    // Create network
    LOG(INFO) << "Creating test network: " + network_name_;
    RunCommand("docker network create " + network_name_ + " 2>/dev/null");

    // Start databroker
    StartDatabroker();
    WaitForDatabroker();

    // Connect KUKSA client
    kuksa_client_ = std::make_shared<KuksaClientWrapper>("localhost:55555");
    if (!kuksa_client_->connect()) {
        throw std::runtime_error("Failed to connect to KUKSA");
    }

    // Create test runner
    test_runner_ = std::make_shared<TestRunner>(kuksa_client_);
}

void YamlTestFixture::TearDown() {
    // Stop test subject
    try {
        StopTestSubject();
    } catch (...) {
        // Ignore errors during cleanup
    }

    // Disconnect KUKSA
    if (kuksa_client_) {
        kuksa_client_->disconnect();
    }

    // Stop containers
    StopAllContainers();
}

void YamlTestFixture::RunYamlTestSuite(const std::string& yaml_path) {
    // Parse YAML
    TestSuite suite = YamlParser::parse_file(yaml_path);

    // Start fixtures if present
    if (!suite.fixtures.empty()) {
        StartFixtures(suite.fixtures);
    }

    // Start test subject (user's code)
    StartTestSubject();

    // Give test subject time to initialize
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Run each test case
    for (const auto& test_case : suite.test_cases) {
        std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
        std::cout << "Running: " << test_case.name << std::endl;
        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" << std::endl;

        TestCaseResult result = test_runner_->run_test_case(test_case);

        EXPECT_EQ(result.status, TestStatus::PASSED)
            << "Test case '" << test_case.name << "' failed";

        if (result.status != TestStatus::PASSED && result.step_results.size() > 0) {
            for (const auto& step_result : result.step_results) {
                if (step_result.status == TestStatus::FAILED && step_result.message.has_value()) {
                    std::cout << "  Step failed: " << step_result.message.value() << std::endl;
                }
            }
        }
    }
}

void YamlTestFixture::RunYamlTestCase(const std::string& yaml_path, const std::string& test_name) {
    // Parse YAML
    TestSuite suite = YamlParser::parse_file(yaml_path);

    // Start fixtures if present
    if (!suite.fixtures.empty()) {
        StartFixtures(suite.fixtures);
    }

    // Start test subject (user's code)
    StartTestSubject();

    // Give test subject time to initialize
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Find and run specific test case
    for (const auto& test_case : suite.test_cases) {
        if (test_case.name == test_name) {
            std::cout << "\nRunning: " << test_case.name << std::endl;

            TestCaseResult result = test_runner_->run_test_case(test_case);

            EXPECT_EQ(result.status, TestStatus::PASSED)
                << "Test case '" << test_case.name << "' failed";

            if (result.status != TestStatus::PASSED && result.step_results.size() > 0) {
                for (const auto& step_result : result.step_results) {
                    if (step_result.status == TestStatus::FAILED && step_result.message.has_value()) {
                        std::cout << "  Step failed: " << step_result.message.value() << std::endl;
                    }
                }
            }

            return;
        }
    }

    FAIL() << "Test case '" << test_name << "' not found in " << yaml_path;
}

} // namespace testing
} // namespace kuksa
