#pragma once

#include "test_models.hpp"
#include "yaml_parser.hpp"
#include "kuksa_client_wrapper.hpp"
#include "test_runner.hpp"
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <cstdlib>

namespace sdv {
namespace testing {

/**
 * Google Test base class for YAML-driven integration tests.
 *
 * Manages lifecycle of Docker containers and YAML test execution:
 * 1. SetUp() starts: databroker -> fixtures -> your code
 * 2. Tests run using YAML test steps
 * 3. TearDown() stops all containers
 *
 * Example usage:
 *
 *   class ClimateControlTest : public YamlTestFixture {
 *   protected:
 *       void StartTestSubject() override {
 *           climate_control_ = std::make_unique<ClimateControl>("localhost:55555");
 *           climate_control_->start();
 *       }
 *
 *       void StopTestSubject() override {
 *           climate_control_->stop();
 *       }
 *
 *       std::unique_ptr<ClimateControl> climate_control_;
 *   };
 *
 *   TEST_F(ClimateControlTest, RunAllTests) {
 *       RunYamlTestSuite("tests/ac_test.yaml");
 *   }
 */
class YamlTestFixture : public ::testing::Test {
protected:
    void SetUp() override;
    void TearDown() override;

    /**
     * Override this to start your application/test subject.
     * Called after databroker and fixtures are running.
     * Your code should connect to localhost:55555
     */
    virtual void StartTestSubject() = 0;

    /**
     * Override this to stop your application/test subject.
     * Called during teardown.
     */
    virtual void StopTestSubject() = 0;

    /**
     * Run all test cases from a YAML test suite.
     * Each test case will be executed and failures will trigger EXPECT failures.
     */
    void RunYamlTestSuite(const std::string& yaml_path);

    /**
     * Run a specific test case by name from a YAML test suite.
     */
    void RunYamlTestCase(const std::string& yaml_path, const std::string& test_name);

    /**
     * Get the test runner for advanced usage
     */
    std::shared_ptr<TestRunner> GetTestRunner() { return test_runner_; }

    /**
     * Get the KUKSA client for manual inject/expect if needed
     */
    std::shared_ptr<KuksaClientWrapper> GetKuksaClient() { return kuksa_client_; }

private:
    std::string network_name_;
    std::string databroker_name_;
    std::string fixture_name_;

    std::shared_ptr<KuksaClientWrapper> kuksa_client_;
    std::shared_ptr<TestRunner> test_runner_;

    void StartDatabroker();
    void StartFixtures(const std::vector<Fixture>& fixtures);
    void StopAllContainers();
    void WaitForDatabroker();

    int RunCommand(const std::string& cmd);
    std::string GenerateContainerName(const std::string& prefix);
};

} // namespace testing
} // namespace kuksa
