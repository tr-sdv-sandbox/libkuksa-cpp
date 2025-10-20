/**
 * @file test_all_types_integration.cpp
 * @brief Integration test that verifies all VSS data types work end-to-end with KUKSA
 */

#include "kuksa_test_fixture.hpp"
#include <gtest/gtest.h>
#include <kuksa_cpp/kuksa.hpp>  // Includes subscriber, accessor
#include <kuksa_cpp/client.hpp>
#include <kuksa_cpp/resolver.hpp>
#include <glog/logging.h>
#include <atomic>

using namespace kuksa;
using namespace kuksa::test;
using namespace std::chrono_literals;

class AllTypesIntegrationTest : public KuksaTestFixture {
protected:
    // Helper to wait for a condition
    template<typename Pred>
    bool wait_for(Pred pred, std::chrono::milliseconds timeout = 5000ms) {
        auto start = std::chrono::steady_clock::now();
        while (!pred() && std::chrono::steady_clock::now() - start < timeout) {
            std::this_thread::sleep_for(10ms);
        }
        return pred();
    }

    // Test scalar sensor types
    template<typename T>
    bool test_sensor_type(const std::string& path, T test_value) {
        auto resolver_result = Resolver::create(getKuksaAddress());
        if (!resolver_result.ok()) {
            LOG(ERROR) << "Failed to create resolver: " << resolver_result.status();
            return false;
        }
        auto resolver = std::move(*resolver_result);

        auto accessor_result = *Client::create(getKuksaAddress());
        if (!accessor_result) {
            return false;
        }
        auto accessor = std::move(accessor_result);
        auto subscriber = *Client::create(getKuksaAddress());

        // Get sensor handle for reading/subscribing
        auto sensor_result = resolver->get<T>(path);
        if (!sensor_result.ok()) {
            LOG(ERROR) << "Failed to get sensor handle for " << path << ": " << sensor_result.status();
            return false;
        }
        auto sensor = *sensor_result;

        // Get RW handle for publishing
        auto sensor_rw_result = resolver->get<T>(path);
        if (!sensor_rw_result.ok()) {
            LOG(ERROR) << "Failed to get sensor RW handle for " << path << ": " << sensor_rw_result.status();
            return false;
        }
        auto sensor_rw = *sensor_rw_result;

        std::atomic<bool> received(false);
        T received_value{};

        // Subscribe
        subscriber->subscribe(sensor, [&](vss::types::QualifiedValue<T> qvalue) {
            if (qvalue.is_valid()) {
                received_value = *qvalue.value;
                received = true;
            }
        });

        auto start_status = subscriber->start();
        if (!start_status.ok()) {
            return false;
        }

        // Wait for subscriber to be ready
        if (!subscriber->wait_until_ready(std::chrono::milliseconds(5000)).ok()) {
            return false;
        }

        std::this_thread::sleep_for(100ms);

        // Publish using unified set() method
        if (!accessor->set(sensor_rw, test_value).ok()) {
            return false;
        }

        // Wait for update
        return wait_for([&]() { return received.load() && received_value == test_value; });
    }

    // Test array sensor types
    template<typename T>
    bool test_array_sensor_type(const std::string& path, T test_value) {
        auto resolver_result = Resolver::create(getKuksaAddress());
        if (!resolver_result.ok()) {
            LOG(ERROR) << "Failed to create resolver: " << resolver_result.status();
            return false;
        }
        auto resolver = std::move(*resolver_result);

        auto accessor_result = *Client::create(getKuksaAddress());
        if (!accessor_result) {
            return false;
        }
        auto accessor = std::move(accessor_result);
        auto subscriber = *Client::create(getKuksaAddress());

        // Get sensor handle for reading/subscribing
        auto sensor_result = resolver->get<T>(path);
        if (!sensor_result.ok()) {
            LOG(ERROR) << "Failed to get array sensor handle for " << path << ": " << sensor_result.status();
            return false;
        }
        auto sensor = *sensor_result;

        // Get RW handle for publishing
        auto sensor_rw_result = resolver->get<T>(path);
        if (!sensor_rw_result.ok()) {
            LOG(ERROR) << "Failed to get sensor RW handle for " << path << ": " << sensor_rw_result.status();
            return false;
        }
        auto sensor_rw = *sensor_rw_result;

        std::atomic<bool> received(false);
        T received_value{};

        // Subscribe
        subscriber->subscribe(sensor, [&](vss::types::QualifiedValue<T> qvalue) {
            if (qvalue.is_valid()) {
                received_value = *qvalue.value;
                received = true;
            }
        });

        auto start_status = subscriber->start();
        if (!start_status.ok()) {
            return false;
        }

        // Wait for subscriber to be ready
        if (!subscriber->wait_until_ready(std::chrono::milliseconds(5000)).ok()) {
            return false;
        }

        std::this_thread::sleep_for(100ms);

        // Publish using unified set() method
        if (!accessor->set(sensor_rw, test_value).ok()) {
            return false;
        }

        // Wait for update
        return wait_for([&]() { return received.load() && received_value == test_value; });
    }

    // Test actuator types
    template<typename T>
    bool test_actuator_type(const std::string& path, T test_value) {
        // Create resolver and subscriber for actual values
        auto resolver_result = Resolver::create(getKuksaAddress());
        if (!resolver_result.ok()) {
            LOG(ERROR) << "Failed to create resolver: " << resolver_result.status();
            return false;
        }
        auto resolver = std::move(*resolver_result);

        auto subscriber = *Client::create(getKuksaAddress());

        // Get actuator handle for subscribing to actual values
        auto actuator_result = resolver->get<T>(path);
        if (!actuator_result.ok()) {
            LOG(ERROR) << "Failed to get actuator handle for " << path << ": " << actuator_result.status();
            return false;
        }
        auto actuator_handle = *actuator_result;

        // Get RW handle for sending target values
        auto actuator_rw_result = resolver->get<T>(path);
        if (!actuator_rw_result.ok()) {
            LOG(ERROR) << "Failed to get actuator RW handle for " << path << ": " << actuator_rw_result.status();
            return false;
        }
        auto actuator_rw_handle = *actuator_rw_result;

        std::atomic<bool> actual_received(false);
        T actual_value{};

        subscriber->subscribe(actuator_handle, [&](vss::types::QualifiedValue<T> qvalue) {
            if (qvalue.is_valid()) {
                actual_value = *qvalue.value;
                actual_received = true;
            }
        });

        auto start_status = subscriber->start();
        if (!start_status.ok()) {
            return false;
        }

        // Wait for subscriber to be ready
        if (!subscriber->wait_until_ready(std::chrono::milliseconds(5000)).ok()) {
            return false;
        }

        std::this_thread::sleep_for(100ms);

        // Create Client to own the actuator
        std::atomic<bool> target_received(false);
        T target_value{};
        Client* client_ptr = nullptr;

        auto client = *Client::create(getKuksaAddress());
        client_ptr = client.get();

        client->serve_actuator(actuator_rw_handle, [&, client_ptr](T target, const SignalHandle<T>& handle) {
            target_value = target;
            target_received = true;

            // Publish actual value (simulating hardware response)
            auto status = client_ptr->publish(handle, target);
            if (!status.ok()) {
                LOG(ERROR) << "Failed to publish actual: " << status;
            }
        });

        auto client_start_status = client->start();
        if (!client_start_status.ok()) {
            return false;
        }

        // Wait for client to be ready
        auto ready_status = client->wait_until_ready(std::chrono::milliseconds(5000));
        if (!ready_status.ok()) {
            LOG(ERROR) << "Client not ready: " << ready_status;
            return false;
        }

        // Send actuation
        auto accessor_result = *Client::create(getKuksaAddress());
        if (!accessor_result) {
            return false;
        }
        auto accessor = std::move(accessor_result);

        auto set_target_status = accessor->set(actuator_rw_handle, test_value);
        if (!set_target_status.ok()) {
            LOG(ERROR) << "Failed to set target: " << set_target_status;
            return false;
        }
        
        // Wait for both target reception and actual value publication
        bool success = wait_for([&]() { 
            return target_received.load() && target_value == test_value &&
                   actual_received.load() && actual_value == test_value;
        });
        
        if (!success) {
            LOG(ERROR) << "Actuator test failed for " << path 
                      << " - target_received: " << target_received.load()
                      << ", actual_received: " << actual_received.load();
        }
        
        return success;
    }
    
    // Test attribute types
    template<typename T>
    bool test_attribute_type(const std::string& path, T test_value) {
        auto resolver_result = Resolver::create(getKuksaAddress());
        if (!resolver_result.ok()) {
            LOG(ERROR) << "Failed to create resolver: " << resolver_result.status();
            return false;
        }
        auto resolver = std::move(*resolver_result);

        auto accessor_result = *Client::create(getKuksaAddress());
        if (!accessor_result) {
            return false;
        }
        auto accessor = std::move(accessor_result);

        // Get RW handle to write attribute
        auto signal_rw_result = resolver->get<T>(path);
        if (!signal_rw_result.ok()) {
            LOG(ERROR) << "Failed to get RW handle for attribute " << path << ": " << signal_rw_result.status();
            return false;
        }
        auto signal_rw = *signal_rw_result;

        auto publish_status = accessor->set(signal_rw, test_value);
        if (!publish_status.ok()) {
            LOG(ERROR) << "Failed to publish attribute: " << publish_status;
            return false;
        }

        // Get attribute handle to read
        auto attr_result = resolver->get<T>(path);
        if (!attr_result.ok()) {
            LOG(ERROR) << "Failed to get attribute handle for " << path << ": " << attr_result.status();
            return false;
        }
        auto attr = *attr_result;

        auto value_result = accessor->get(attr);
        if (!value_result.ok()) {
            LOG(ERROR) << "Failed to get value: " << value_result.status();
            return false;
        }

        return value_result->is_valid() && *value_result->value == test_value;
    }
};

// Test all scalar sensor types
TEST_F(AllTypesIntegrationTest, AllScalarSensorTypes) {
    // Bool
    EXPECT_TRUE(test_sensor_type<bool>("Vehicle.Private.Test.BoolSensor", true));
    
    // Integers
    EXPECT_TRUE(test_sensor_type<int32_t>("Vehicle.Private.Test.Int32Sensor", -42));
    EXPECT_TRUE(test_sensor_type<uint32_t>("Vehicle.Private.Test.UInt32Sensor", 42u));
    EXPECT_TRUE(test_sensor_type<int64_t>("Vehicle.Private.Test.Int64Sensor", int64_t(-1234567890123)));
    EXPECT_TRUE(test_sensor_type<uint64_t>("Vehicle.Private.Test.UInt64Sensor", uint64_t(1234567890123)));
    
    // Floating point
    EXPECT_TRUE(test_sensor_type<float>("Vehicle.Private.Test.FloatSensor", 3.14f));
    EXPECT_TRUE(test_sensor_type<double>("Vehicle.Private.Test.DoubleSensor", 3.14159265359));
    
    // String
    EXPECT_TRUE(test_sensor_type<std::string>("Vehicle.Private.Test.StringSensor", std::string("Hello KUKSA!")));
}

// Test all array sensor types
TEST_F(AllTypesIntegrationTest, AllArraySensorTypes) {
    // Bool array
    EXPECT_TRUE(test_array_sensor_type<std::vector<bool>>("Vehicle.Private.Test.BoolArraySensor", std::vector<bool>{true, false, true}));
    
    // Integer arrays
    EXPECT_TRUE(test_array_sensor_type<std::vector<int32_t>>("Vehicle.Private.Test.Int32ArraySensor", std::vector<int32_t>{-1, 0, 42}));
    EXPECT_TRUE(test_array_sensor_type<std::vector<uint32_t>>("Vehicle.Private.Test.UInt32ArraySensor", std::vector<uint32_t>{0, 42, 100}));
    EXPECT_TRUE(test_array_sensor_type<std::vector<int64_t>>("Vehicle.Private.Test.Int64ArraySensor", std::vector<int64_t>{-9999999, 0, 9999999}));
    EXPECT_TRUE(test_array_sensor_type<std::vector<uint64_t>>("Vehicle.Private.Test.UInt64ArraySensor", std::vector<uint64_t>{0, 1000000, 9999999}));
    
    // Float arrays
    EXPECT_TRUE(test_array_sensor_type<std::vector<float>>("Vehicle.Private.Test.FloatArraySensor", std::vector<float>{1.1f, 2.2f, 3.3f}));
    EXPECT_TRUE(test_array_sensor_type<std::vector<double>>("Vehicle.Private.Test.DoubleArraySensor", std::vector<double>{1.111, 2.222, 3.333}));
    
    // String array
    EXPECT_TRUE(test_array_sensor_type<std::vector<std::string>>("Vehicle.Private.Test.StringArraySensor", std::vector<std::string>{"Hello", "KUKSA", "v2"}));
}

// Test all scalar actuator types
TEST_F(AllTypesIntegrationTest, AllScalarActuatorTypes) {
    EXPECT_TRUE(test_actuator_type<bool>("Vehicle.Private.Test.BoolActuator", true));
    EXPECT_TRUE(test_actuator_type<int32_t>("Vehicle.Private.Test.Int32Actuator", 999));
    EXPECT_TRUE(test_actuator_type<uint32_t>("Vehicle.Private.Test.UInt32Actuator", 123u));
    EXPECT_TRUE(test_actuator_type<int64_t>("Vehicle.Private.Test.Int64Actuator", int64_t(-8888888)));
    EXPECT_TRUE(test_actuator_type<uint64_t>("Vehicle.Private.Test.UInt64Actuator", uint64_t(7777777)));
    EXPECT_TRUE(test_actuator_type<float>("Vehicle.Private.Test.FloatActuator", 2.718f));
    EXPECT_TRUE(test_actuator_type<double>("Vehicle.Private.Test.DoubleActuator", 2.718281828));
    EXPECT_TRUE(test_actuator_type<std::string>("Vehicle.Private.Test.StringActuator", std::string("Actuate!")));
}

// Test all scalar attribute types
TEST_F(AllTypesIntegrationTest, AllScalarAttributeTypes) {
    EXPECT_TRUE(test_attribute_type<bool>("Vehicle.Private.Test.BoolAttribute", false));
    EXPECT_TRUE(test_attribute_type<int32_t>("Vehicle.Private.Test.Int32Attribute", -100));
    EXPECT_TRUE(test_attribute_type<uint32_t>("Vehicle.Private.Test.UInt32Attribute", 200u));
    EXPECT_TRUE(test_attribute_type<int64_t>("Vehicle.Private.Test.Int64Attribute", int64_t(-5555555)));
    EXPECT_TRUE(test_attribute_type<uint64_t>("Vehicle.Private.Test.UInt64Attribute", uint64_t(4444444)));
    EXPECT_TRUE(test_attribute_type<float>("Vehicle.Private.Test.FloatAttribute", 1.414f));
    EXPECT_TRUE(test_attribute_type<double>("Vehicle.Private.Test.DoubleAttribute", 1.41421356237));
    EXPECT_TRUE(test_attribute_type<std::string>("Vehicle.Private.Test.StringAttribute", std::string("Attribute Value")));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    google::InitGoogleLogging(argv[0]);
    return RUN_ALL_TESTS();
}