#include "kuksa_cpp/testing/gtest_integration.hpp"
#include <glog/logging.h>
#include <chrono>
#include <thread>
#include <sstream>
#include <ctime>
#include <iostream>
#include <fstream>
#include <filesystem>

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

std::string YamlTestFixture::GetCommandOutput(const std::string& cmd) {
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return "";
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }

    pclose(pipe);
    return result;
}

void YamlTestFixture::StartDatabroker() {
    LOG(INFO) << "Starting KUKSA databroker...";

    // Check Docker availability
    if (RunCommand("docker --version > /dev/null 2>&1") != 0) {
        throw std::runtime_error(
            "Docker is not available or not running.\n"
            "Install Docker: https://docs.docker.com/get-docker/\n"
            "Or set KUKSA_ADDRESS environment variable to use existing instance."
        );
    }

    // Check port availability
    std::string port = std::to_string(actual_kuksa_port_);
    if (RunCommand("nc -z localhost " + port + " 2>/dev/null") == 0) {
        throw std::runtime_error(
            "Port " + port + " is already in use.\n"
            "Options:\n"
            "  1. Stop service on port " + port + "\n"
            "  2. Override GetKuksaPort() to use different port\n"
            "  3. Set KUKSA_ADDRESS to use existing KUKSA instance"
        );
    }

    std::stringstream cmd;
    cmd << "docker run -d "
        << "--name " << databroker_name_ << " "
        << "--network " << network_name_ << " "
        << "-p " << actual_kuksa_port_ << ":55555 ";

    // Check for custom VSS schema
    std::string vss_path = GetVssSchema();
    if (!vss_path.empty()) {
        // Validate file exists
        if (!std::filesystem::exists(vss_path)) {
            throw std::runtime_error(
                "VSS schema file not found: " + vss_path + "\n"
                "Provide absolute or relative path to VSS JSON file."
            );
        }

        std::filesystem::path abs_vss = std::filesystem::absolute(vss_path);
        LOG(INFO) << "Using custom VSS schema: " << abs_vss.string();

        cmd << "-v " << abs_vss.string() << ":/vss/custom.json:ro ";
        cmd << "ghcr.io/eclipse-kuksa/kuksa-databroker:0.6.0 ";
        cmd << "--vss /vss/custom.json 2>&1";
    } else {
        LOG(INFO) << "Using built-in VSS 5.1 schema";
        cmd << "ghcr.io/eclipse-kuksa/kuksa-databroker:0.6.0 ";
        cmd << "--vss /vss.json 2>&1";
    }

    int result = RunCommand(cmd.str());
    if (result != 0) {
        // Try to get container logs for debugging
        std::string logs = GetCommandOutput("docker logs " + databroker_name_ + " 2>&1");

        throw std::runtime_error(
            "Failed to start KUKSA databroker.\n\n"
            "Container logs:\n" + logs + "\n\n"
            "Common fixes:\n"
            "  - Pull image: docker pull ghcr.io/eclipse-kuksa/kuksa-databroker:0.6.0\n"
            "  - Check Docker: docker ps\n"
            "  - Check disk space: df -h"
        );
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
    // Check for external KUKSA instance
    const char* env_addr = std::getenv("KUKSA_ADDRESS");
    if (env_addr) {
        kuksa_address_ = env_addr;
        skip_container_management_ = true;
        LOG(INFO) << "Using external KUKSA at: " << kuksa_address_;

        // Connect to existing KUKSA
        kuksa_client_ = std::make_shared<KuksaClientWrapper>(kuksa_address_);
        if (!kuksa_client_->connect()) {
            GTEST_SKIP() << "Cannot connect to KUKSA at " << kuksa_address_;
        }

        test_runner_ = std::make_shared<TestRunner>(kuksa_client_);

        LOG(WARNING) << "Using external KUKSA - fixtures must be managed separately";
        return;
    }

    // Normal flow: start containers
    // Get port configuration
    uint16_t port = GetKuksaPort();
    if (port == 0) {
        // TODO: Implement port auto-selection
        port = 55555;
        LOG(WARNING) << "Port auto-selection not yet implemented, using default 55555";
    }
    actual_kuksa_port_ = port;
    kuksa_address_ = "localhost:" + std::to_string(actual_kuksa_port_);

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
    kuksa_client_ = std::make_shared<KuksaClientWrapper>(kuksa_address_);
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
