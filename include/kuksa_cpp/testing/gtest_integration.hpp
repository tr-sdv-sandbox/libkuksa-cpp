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
     * @brief Override to provide custom VSS schema
     * @return Path to complete VSS JSON file containing all signals your application needs
     *
     * The file should contain the full VSS tree with all signals.
     * Leave empty to use databroker's built-in VSS 5.1.
     *
     * Example:
     *   return "../vss_climate_control.json";
     */
    virtual std::string GetVssSchema() const {
        return "";  // Default: use built-in VSS 5.1
    }

    /**
     * @brief Override to use custom KUKSA port (for parallel test execution)
     * @return Port number, or 0 to auto-select available port
     *
     * Default is 55555. Override if running multiple test suites in parallel.
     *
     * Example:
     *   return 55557;  // Avoid conflicts with other tests
     */
    virtual uint16_t GetKuksaPort() const {
        return 55555;  // Default port
    }

    /**
     * @brief Get actual KUKSA port after container startup
     * @return The port number that KUKSA is listening on
     */
    uint16_t GetActualKuksaPort() const {
        return actual_kuksa_port_;
    }

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
    std::string kuksa_address_;
    uint16_t actual_kuksa_port_ = 55555;
    bool skip_container_management_ = false;

    std::shared_ptr<KuksaClientWrapper> kuksa_client_;
    std::shared_ptr<TestRunner> test_runner_;

    void StartDatabroker();
    void StartFixtures(const std::vector<Fixture>& fixtures);
    void StopAllContainers();
    void WaitForDatabroker();

    int RunCommand(const std::string& cmd);
    std::string GetCommandOutput(const std::string& cmd);
    std::string GenerateContainerName(const std::string& prefix);
};

} // namespace testing
} // namespace kuksa
