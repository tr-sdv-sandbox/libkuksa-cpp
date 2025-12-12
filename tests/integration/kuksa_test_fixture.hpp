/**
 * @file kuksa_test_fixture.hpp
 * @brief Test fixture that automatically manages KUKSA databroker Docker container
 */

#pragma once

#include <gtest/gtest.h>
#include <glog/logging.h>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>

namespace kuksa::test {

class KuksaTestFixture : public ::testing::Test {
protected:
    static constexpr const char* KUKSA_IMAGE = "ghcr.io/eclipse-kuksa/kuksa-databroker:0.6.0";
    static constexpr const char* CONTAINER_NAME = "kuksa-test-broker";
    static constexpr const char* KUKSA_PORT = "55556";  // Different from default to avoid conflicts
    static std::string kuksa_address;
    static bool container_started;

    static void SetUpTestSuite() {
        LOG(INFO) << "=== Setting up KUKSA test environment ===";
        
        // Check if KUKSA_ADDRESS environment variable is set (Docker Compose scenario)
        const char* env_addr = std::getenv("KUKSA_ADDRESS");
        if (env_addr) {
            kuksa_address = env_addr;
            container_started = true; // Mark as started so tests run
            LOG(INFO) << "Using KUKSA from environment: " << kuksa_address;
            return;
        }
        
        // Otherwise, try to start Docker container locally
        if (std::system("docker --version > /dev/null 2>&1") != 0) {
            GTEST_SKIP() << "Docker is not available. Skipping KUKSA integration tests.";
            return;
        }

        // Stop any existing test container
        StopContainer();

        // Create VSS configuration
        CreateVSSConfig();

        // Start KUKSA container
        if (!StartContainer()) {
            GTEST_SKIP() << "Failed to start KUKSA container. Skipping tests.";
            return;
        }

        // Set address for tests
        kuksa_address = std::string("localhost:") + KUKSA_PORT;
        LOG(INFO) << "KUKSA test broker running at: " << kuksa_address;
    }

    static void TearDownTestSuite() {
        LOG(INFO) << "=== Tearing down KUKSA test environment ===";
        // Only stop container if we started it
        if (!std::getenv("KUKSA_ADDRESS")) {
            StopContainer();
            CleanupVSSConfig();
        }
    }

    void SetUp() override {
        if (!container_started) {
            GTEST_SKIP() << "KUKSA container not running";
        }
        LOG(INFO) << "Test: " << ::testing::UnitTest::GetInstance()->current_test_info()->name();
    }

    void TearDown() override {
        // Give time for connections to close cleanly and KUKSA to release ownership
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    const std::string& getKuksaAddress() const {
        return kuksa_address;
    }

private:
    static bool StartContainer() {
        LOG(INFO) << "Starting KUKSA databroker container...";

        // Get absolute path to VSS config
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) == nullptr) {
            LOG(ERROR) << "Failed to get current directory";
            return false;
        }
        std::string vss_path = std::string(cwd) + "/vss_test.json";

        // Construct docker run command
        std::stringstream cmd;
        cmd << "docker run -d --rm "
            << "--name " << CONTAINER_NAME << " "
            << "-p " << KUKSA_PORT << ":55555 "
            << "-v " << vss_path << ":/vss/vss_test.json:ro "
            << KUKSA_IMAGE << " "
            << "--vss /vss/vss_test.json";

        LOG(INFO) << "Docker command: " << cmd.str();

        if (std::system(cmd.str().c_str()) != 0) {
            LOG(ERROR) << "Failed to start Docker container";
            return false;
        }

        // Wait for container to be ready
        LOG(INFO) << "Waiting for KUKSA to be ready...";
        for (int i = 0; i < 30; ++i) {  // 30 seconds timeout
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            // Check if container is running
            if (std::system(("docker ps -q -f name=" + std::string(CONTAINER_NAME) + " | grep -q .").c_str()) != 0) {
                LOG(ERROR) << "Container stopped unexpectedly";
                // Get logs for debugging
                [[maybe_unused]] int log_result = std::system(("docker logs " + std::string(CONTAINER_NAME) + " 2>&1 | tail -20").c_str());
                return false;
            }

            // Check if port is open
            std::string check_cmd = "nc -z localhost " + std::string(KUKSA_PORT) + " 2>/dev/null";
            if (std::system(check_cmd.c_str()) == 0) {
                LOG(INFO) << "KUKSA is ready!";
                container_started = true;
                return true;
            }
        }

        LOG(ERROR) << "Timeout waiting for KUKSA to be ready";
        StopContainer();
        return false;
    }

    static void StopContainer() {
        LOG(INFO) << "Stopping KUKSA container...";
        [[maybe_unused]] int stop_result = std::system(("docker stop " + std::string(CONTAINER_NAME) + " 2>/dev/null").c_str());
        // Force remove to ensure it's completely gone
        [[maybe_unused]] int rm_result = std::system(("docker rm -f " + std::string(CONTAINER_NAME) + " 2>/dev/null").c_str());
        // Small delay to ensure Docker cleans up
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        container_started = false;
    }

    static void CreateVSSConfig() {
        LOG(INFO) << "Creating VSS test configuration...";
        
        std::ofstream vss_file("vss_test.json");
        vss_file << R"JSON({
  "Vehicle": {
    "type": "branch",
    "description": "High-level vehicle data",
    "children": {
      "Private": {
        "type": "branch", 
        "description": "Private test signals",
        "children": {
          "Test": {
            "type": "branch",
            "description": "Test signals for integration testing",
            "children": {
              "Actuator": {
                "type": "actuator",
                "datatype": "int32",
                "description": "Test actuator for integration tests"
              },
              "Sensor": {
                "type": "sensor",
                "datatype": "float",
                "description": "Test sensor for integration tests"
              },
              "Signal": {
                "type": "sensor",
                "datatype": "int32",
                "description": "Generic test signal"
              },
              "Sensor1": {
                "type": "sensor",
                "datatype": "float",
                "description": "Test sensor 1"
              },
              "Sensor2": {
                "type": "sensor",
                "datatype": "int32",
                "description": "Test sensor 2"
              },
              "Sensor3": {
                "type": "sensor",
                "datatype": "boolean",
                "description": "Test sensor 3"
              },
              
              "BoolSensor": {
                "type": "sensor",
                "datatype": "boolean",
                "description": "Test bool sensor"
              },
              "Int32Sensor": {
                "type": "sensor",
                "datatype": "int32",
                "description": "Test int32 sensor"
              },
              "UInt32Sensor": {
                "type": "sensor",
                "datatype": "uint32",
                "description": "Test uint32 sensor"
              },
              "Int64Sensor": {
                "type": "sensor",
                "datatype": "int64",
                "description": "Test int64 sensor"
              },
              "UInt64Sensor": {
                "type": "sensor",
                "datatype": "uint64",
                "description": "Test uint64 sensor"
              },
              "FloatSensor": {
                "type": "sensor",
                "datatype": "float",
                "description": "Test float sensor"
              },
              "DoubleSensor": {
                "type": "sensor",
                "datatype": "double",
                "description": "Test double sensor"
              },
              "StringSensor": {
                "type": "sensor",
                "datatype": "string",
                "description": "Test string sensor"
              },
              "Int8Sensor": {
                "type": "sensor",
                "datatype": "int8",
                "description": "Test int8 sensor (narrowing type)"
              },
              "UInt8Sensor": {
                "type": "sensor",
                "datatype": "uint8",
                "description": "Test uint8 sensor (narrowing type)"
              },
              "Int16Sensor": {
                "type": "sensor",
                "datatype": "int16",
                "description": "Test int16 sensor (narrowing type)"
              },
              "UInt16Sensor": {
                "type": "sensor",
                "datatype": "uint16",
                "description": "Test uint16 sensor (narrowing type)"
              },
              "Int8ArraySensor": {
                "type": "sensor",
                "datatype": "int8[]",
                "description": "Test int8 array sensor (narrowing type)"
              },
              "UInt8ArraySensor": {
                "type": "sensor",
                "datatype": "uint8[]",
                "description": "Test uint8 array sensor (narrowing type)"
              },
              "Int16ArraySensor": {
                "type": "sensor",
                "datatype": "int16[]",
                "description": "Test int16 array sensor (narrowing type)"
              },
              "UInt16ArraySensor": {
                "type": "sensor",
                "datatype": "uint16[]",
                "description": "Test uint16 array sensor (narrowing type)"
              },
              "BoolArraySensor": {
                "type": "sensor",
                "datatype": "boolean[]",
                "description": "Test bool array sensor"
              },
              "Int32ArraySensor": {
                "type": "sensor",
                "datatype": "int32[]",
                "description": "Test int32 array sensor"
              },
              "UInt32ArraySensor": {
                "type": "sensor",
                "datatype": "uint32[]",
                "description": "Test uint32 array sensor"
              },
              "Int64ArraySensor": {
                "type": "sensor",
                "datatype": "int64[]",
                "description": "Test int64 array sensor"
              },
              "UInt64ArraySensor": {
                "type": "sensor",
                "datatype": "uint64[]",
                "description": "Test uint64 array sensor"
              },
              "FloatArraySensor": {
                "type": "sensor",
                "datatype": "float[]",
                "description": "Test float array sensor"
              },
              "DoubleArraySensor": {
                "type": "sensor",
                "datatype": "double[]",
                "description": "Test double array sensor"
              },
              "StringArraySensor": {
                "type": "sensor",
                "datatype": "string[]",
                "description": "Test string array sensor"
              },
              
              "BoolActuator": {
                "type": "actuator",
                "datatype": "boolean",
                "description": "Test bool actuator"
              },
              "Int32Actuator": {
                "type": "actuator",
                "datatype": "int32",
                "description": "Test int32 actuator"
              },
              "UInt32Actuator": {
                "type": "actuator",
                "datatype": "uint32",
                "description": "Test uint32 actuator"
              },
              "Int64Actuator": {
                "type": "actuator",
                "datatype": "int64",
                "description": "Test int64 actuator"
              },
              "UInt64Actuator": {
                "type": "actuator",
                "datatype": "uint64",
                "description": "Test uint64 actuator"
              },
              "FloatActuator": {
                "type": "actuator",
                "datatype": "float",
                "description": "Test float actuator"
              },
              "DoubleActuator": {
                "type": "actuator",
                "datatype": "double",
                "description": "Test double actuator"
              },
              "StringActuator": {
                "type": "actuator",
                "datatype": "string",
                "description": "Test string actuator"
              },
              "Int8Actuator": {
                "type": "actuator",
                "datatype": "int8",
                "description": "Test int8 actuator (narrowing type)"
              },
              "UInt8Actuator": {
                "type": "actuator",
                "datatype": "uint8",
                "description": "Test uint8 actuator (narrowing type)"
              },
              "Int16Actuator": {
                "type": "actuator",
                "datatype": "int16",
                "description": "Test int16 actuator (narrowing type)"
              },
              "UInt16Actuator": {
                "type": "actuator",
                "datatype": "uint16",
                "description": "Test uint16 actuator (narrowing type)"
              },
              "BoolArrayActuator": {
                "type": "actuator",
                "datatype": "boolean[]",
                "description": "Test bool array actuator"
              },
              "Int32ArrayActuator": {
                "type": "actuator",
                "datatype": "int32[]",
                "description": "Test int32 array actuator"
              },
              "UInt32ArrayActuator": {
                "type": "actuator",
                "datatype": "uint32[]",
                "description": "Test uint32 array actuator"
              },
              "Int64ArrayActuator": {
                "type": "actuator",
                "datatype": "int64[]",
                "description": "Test int64 array actuator"
              },
              "UInt64ArrayActuator": {
                "type": "actuator",
                "datatype": "uint64[]",
                "description": "Test uint64 array actuator"
              },
              "FloatArrayActuator": {
                "type": "actuator",
                "datatype": "float[]",
                "description": "Test float array actuator"
              },
              "DoubleArrayActuator": {
                "type": "actuator",
                "datatype": "double[]",
                "description": "Test double array actuator"
              },
              "StringArrayActuator": {
                "type": "actuator",
                "datatype": "string[]",
                "description": "Test string array actuator"
              },
              
              "BoolAttribute": {
                "type": "attribute",
                "datatype": "boolean",
                "description": "Test bool attribute"
              },
              "Int32Attribute": {
                "type": "attribute",
                "datatype": "int32",
                "description": "Test int32 attribute"
              },
              "UInt32Attribute": {
                "type": "attribute",
                "datatype": "uint32",
                "description": "Test uint32 attribute"
              },
              "Int64Attribute": {
                "type": "attribute",
                "datatype": "int64",
                "description": "Test int64 attribute"
              },
              "UInt64Attribute": {
                "type": "attribute",
                "datatype": "uint64",
                "description": "Test uint64 attribute"
              },
              "FloatAttribute": {
                "type": "attribute",
                "datatype": "float",
                "description": "Test float attribute"
              },
              "DoubleAttribute": {
                "type": "attribute",
                "datatype": "double",
                "description": "Test double attribute"
              },
              "StringAttribute": {
                "type": "attribute",
                "datatype": "string",
                "description": "Test string attribute"
              },
              "Int8Attribute": {
                "type": "attribute",
                "datatype": "int8",
                "description": "Test int8 attribute (narrowing type)"
              },
              "UInt8Attribute": {
                "type": "attribute",
                "datatype": "uint8",
                "description": "Test uint8 attribute (narrowing type)"
              },
              "Int16Attribute": {
                "type": "attribute",
                "datatype": "int16",
                "description": "Test int16 attribute (narrowing type)"
              },
              "UInt16Attribute": {
                "type": "attribute",
                "datatype": "uint16",
                "description": "Test uint16 attribute (narrowing type)"
              },
              "BoolArrayAttribute": {
                "type": "attribute",
                "datatype": "boolean[]",
                "description": "Test bool array attribute"
              },
              "Int32ArrayAttribute": {
                "type": "attribute",
                "datatype": "int32[]",
                "description": "Test int32 array attribute"
              },
              "UInt32ArrayAttribute": {
                "type": "attribute",
                "datatype": "uint32[]",
                "description": "Test uint32 array attribute"
              },
              "Int64ArrayAttribute": {
                "type": "attribute",
                "datatype": "int64[]",
                "description": "Test int64 array attribute"
              },
              "UInt64ArrayAttribute": {
                "type": "attribute",
                "datatype": "uint64[]",
                "description": "Test uint64 array attribute"
              },
              "FloatArrayAttribute": {
                "type": "attribute",
                "datatype": "float[]",
                "description": "Test float array attribute"
              },
              "DoubleArrayAttribute": {
                "type": "attribute",
                "datatype": "double[]",
                "description": "Test double array attribute"
              },
              "StringArrayAttribute": {
                "type": "attribute",
                "datatype": "string[]",
                "description": "Test string array attribute"
              }
            }
          }
        }
      }
    }
  }
})JSON";
        vss_file.close();
    }

    static void CleanupVSSConfig() {
        std::remove("vss_test.json");
    }
};

// Static member definitions
std::string KuksaTestFixture::kuksa_address;
bool KuksaTestFixture::container_started = false;

} // namespace kuksa::test