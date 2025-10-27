/**
 * @file test_climate_control_integration.cpp
 * @brief Integration test for climate control using YAML test framework
 *
 * Tests the climate protection system with simulated sensor fixtures.
 * This demonstrates the full test framework capabilities:
 * - KUKSA databroker container
 * - Fixture runner container (simulating sensors)
 * - Test subject (climate control app)
 */

#include <kuksa_cpp/testing/gtest_integration.hpp>
#include <gtest/gtest.h>
#include <glog/logging.h>
#include <thread>
#include <chrono>

// Include the climate control system
#include "../../examples/climate_control/climate_control.hpp"

namespace {

/**
 * Integration test fixture for climate control protection system.
 *
 * This test uses:
 * 1. KUKSA databroker (container)
 * 2. Fixture runner (container) - simulates battery sensor
 * 3. Climate control app (host process)
 */
class ClimateControlIntegrationTest : public sdv::testing::YamlTestFixture {
protected:
    void StartTestSubject() override {
        LOG(INFO) << "Starting climate control system...";

        // Get the KUKSA address (will be localhost:55555 by default)
        std::string kuksa_address = "localhost:" + std::to_string(GetActualKuksaPort());

        // Create and start climate control
        climate_control_ = std::make_unique<ClimateProtectionSystem>(kuksa_address);

        if (!climate_control_->connect()) {
            throw std::runtime_error("Failed to connect climate control to KUKSA");
        }

        // Start in separate thread
        climate_thread_ = std::thread([this]() {
            climate_control_->run();
        });

        // Give it a moment to initialize
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        LOG(INFO) << "Climate control system started";
    }

    void StopTestSubject() override {
        LOG(INFO) << "Stopping climate control system...";

        if (climate_control_) {
            climate_control_->stop();
        }

        if (climate_thread_.joinable()) {
            climate_thread_.join();
        }

        LOG(INFO) << "Climate control system stopped";
    }

    std::string GetVssSchema() const override {
        // Use the custom VSS schema with climate control extensions
        return "examples/climate_control/vss_extensions.json";
    }

private:
    std::unique_ptr<ClimateProtectionSystem> climate_control_;
    std::thread climate_thread_;
};

/**
 * Test: Battery protection triggers when voltage drops
 *
 * Scenario:
 * 1. Start with normal battery voltage (24.5V)
 * 2. HVAC is active
 * 3. Battery voltage drops to critical level (23.0V)
 * 4. System should shut down HVAC to protect battery
 */
TEST_F(ClimateControlIntegrationTest, BatteryProtectionTriggersOnLowVoltage) {
    RunYamlTestSuite("tests/integration/yaml/climate_control_battery_protection.yaml");
}

} // anonymous namespace

int main(int argc, char** argv) {
    // Initialize Google's logging library
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;
    FLAGS_v = 0;  // Set to 1 for verbose logging

    // Initialize Google Test
    ::testing::InitGoogleTest(&argc, argv);

    // Run tests
    return RUN_ALL_TESTS();
}
