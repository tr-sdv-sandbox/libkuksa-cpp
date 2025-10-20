/**
 * @file test_all_vss_types.cpp
 * @brief Compilation test to verify all VSS data types have proper template specializations
 */

#include <kuksa_cpp/kuksa.hpp>  // Use public API
#include <kuksa_cpp/testing/test_utils.hpp>
#include <gtest/gtest.h>
#include <glog/logging.h>

using namespace kuksa;

// This test verifies that all types in the Value variant have proper specializations
// It doesn't actually run the methods, just checks they compile and link

TEST(VSSTypeCompilation, AllScalarSensorTypesWithAccessor) {
    // Note: This test only checks compilation, not actual functionality
    // Using dummy address to test compilation - will skip if connection fails
    auto accessor = *Client::create("dummy:12345");
    if (!accessor) {
        GTEST_SKIP() << "Skipping compilation test - connection expected to fail with dummy address";
    }

    // Bool
    {
        auto sensor_ro = TestResolver::signal<bool>("test");
        auto sensor_rw = TestResolver::signal<bool>("test");
        [[maybe_unused]] auto s1 = accessor->set(sensor_rw, true);
        [[maybe_unused]] auto s2 = accessor->get(sensor_ro);
    }

    // int32_t
    {
        auto sensor_ro = TestResolver::signal<int32_t>("test");
        auto sensor_rw = TestResolver::signal<int32_t>("test");
        [[maybe_unused]] auto s1 = accessor->set(sensor_rw, 42);
        [[maybe_unused]] auto s2 = accessor->get(sensor_ro);
    }

    // uint32_t
    {
        auto sensor_ro = TestResolver::signal<uint32_t>("test");
        auto sensor_rw = TestResolver::signal<uint32_t>("test");
        [[maybe_unused]] auto s1 = accessor->set(sensor_rw, 42u);
        [[maybe_unused]] auto s2 = accessor->get(sensor_ro);
    }

    // int64_t
    {
        auto sensor_ro = TestResolver::signal<int64_t>("test");
        auto sensor_rw = TestResolver::signal<int64_t>("test");
        [[maybe_unused]] auto s1 = accessor->set(sensor_rw, int64_t(42));
        [[maybe_unused]] auto s2 = accessor->get(sensor_ro);
    }

    // uint64_t
    {
        auto sensor_ro = TestResolver::signal<uint64_t>("test");
        auto sensor_rw = TestResolver::signal<uint64_t>("test");
        [[maybe_unused]] auto s1 = accessor->set(sensor_rw, uint64_t(42));
        [[maybe_unused]] auto s2 = accessor->get(sensor_ro);
    }

    // float
    {
        auto sensor_ro = TestResolver::signal<float>("test");
        auto sensor_rw = TestResolver::signal<float>("test");
        [[maybe_unused]] auto s1 = accessor->set(sensor_rw, 42.0f);
        [[maybe_unused]] auto s2 = accessor->get(sensor_ro);
    }

    // double
    {
        auto sensor_ro = TestResolver::signal<double>("test");
        auto sensor_rw = TestResolver::signal<double>("test");
        [[maybe_unused]] auto s1 = accessor->set(sensor_rw, 42.0);
        [[maybe_unused]] auto s2 = accessor->get(sensor_ro);
    }

    // string
    {
        auto sensor_ro = TestResolver::signal<std::string>("test");
        auto sensor_rw = TestResolver::signal<std::string>("test");
        [[maybe_unused]] auto s1 = accessor->set(sensor_rw, std::string("test"));
        [[maybe_unused]] auto s2 = accessor->set(sensor_rw, "test");  // const char* overload
        [[maybe_unused]] auto s3 = accessor->get(sensor_ro);
    }
}

TEST(VSSTypeCompilation, AllScalarSensorTypesWithSubscriber) {
    // We can't actually create a subscriber without a valid address,
    // but we can test the template instantiation
    Client* subscriber = nullptr;  // Just for compilation test
    
    if (subscriber) {  // Will never execute, just for compilation
        // Bool
        {
            auto sensor = TestResolver::signal<bool>("test");
            subscriber->subscribe(sensor, [](vss::types::QualifiedValue<bool>){});
        }

        // int32_t
        {
            auto sensor = TestResolver::signal<int32_t>("test");
            subscriber->subscribe(sensor, [](vss::types::QualifiedValue<int32_t>){});
        }

        // uint32_t
        {
            auto sensor = TestResolver::signal<uint32_t>("test");
            subscriber->subscribe(sensor, [](vss::types::QualifiedValue<uint32_t>){});
        }

        // int64_t
        {
            auto sensor = TestResolver::signal<int64_t>("test");
            subscriber->subscribe(sensor, [](vss::types::QualifiedValue<int64_t>){});
        }

        // uint64_t
        {
            auto sensor = TestResolver::signal<uint64_t>("test");
            subscriber->subscribe(sensor, [](vss::types::QualifiedValue<uint64_t>){});
        }

        // float
        {
            auto sensor = TestResolver::signal<float>("test");
            subscriber->subscribe(sensor, [](vss::types::QualifiedValue<float>){});
        }

        // double
        {
            auto sensor = TestResolver::signal<double>("test");
            subscriber->subscribe(sensor, [](vss::types::QualifiedValue<double>){});
        }

        // string
        {
            auto sensor = TestResolver::signal<std::string>("test");
            subscriber->subscribe(sensor, [](vss::types::QualifiedValue<std::string>){});
        }
    }
}

TEST(VSSTypeCompilation, AllActuatorTypes) {
    // Note: This test only checks compilation, not actual functionality
    // Using dummy address to test compilation - will skip if connection fails
    auto accessor = *Client::create("dummy:12345");
    if (!accessor) {
        GTEST_SKIP() << "Skipping compilation test - connection expected to fail with dummy address";
    }
    
    // Bool
    {
        auto actuator = TestResolver::signal<bool>("test");
        [[maybe_unused]] auto s = accessor->set(actuator, true);
    }

    // int32_t
    {
        auto actuator = TestResolver::signal<int32_t>("test");
        [[maybe_unused]] auto s = accessor->set(actuator, 42);
    }

    // uint32_t
    {
        auto actuator = TestResolver::signal<uint32_t>("test");
        [[maybe_unused]] auto s = accessor->set(actuator, 42u);
    }

    // int64_t
    {
        auto actuator = TestResolver::signal<int64_t>("test");
        [[maybe_unused]] auto s = accessor->set(actuator, int64_t(42));
    }

    // uint64_t
    {
        auto actuator = TestResolver::signal<uint64_t>("test");
        [[maybe_unused]] auto s = accessor->set(actuator, uint64_t(42));
    }

    // float
    {
        auto actuator = TestResolver::signal<float>("test");
        [[maybe_unused]] auto s = accessor->set(actuator, 42.0f);
    }

    // double
    {
        auto actuator = TestResolver::signal<double>("test");
        [[maybe_unused]] auto s = accessor->set(actuator, 42.0);
    }

    // string
    {
        auto actuator = TestResolver::signal<std::string>("test");
        [[maybe_unused]] auto s1 = accessor->set(actuator, std::string("test"));
        [[maybe_unused]] auto s2 = accessor->set(actuator, "test");  // const char* overload
    }
}

TEST(VSSTypeCompilation, AllAttributeTypes) {
    // Note: This test only checks compilation, not actual functionality
    // Using dummy address to test compilation - will skip if connection fails
    auto accessor = *Client::create("dummy:12345");
    if (!accessor) {
        GTEST_SKIP() << "Skipping compilation test - connection expected to fail with dummy address";
    }
    
    // Bool
    {
        auto attr = TestResolver::signal<bool>("test");
        [[maybe_unused]] auto v = accessor->get(attr);
    }

    // int32_t
    {
        auto attr = TestResolver::signal<int32_t>("test");
        [[maybe_unused]] auto v = accessor->get(attr);
    }

    // uint32_t
    {
        auto attr = TestResolver::signal<uint32_t>("test");
        [[maybe_unused]] auto v = accessor->get(attr);
    }

    // int64_t
    {
        auto attr = TestResolver::signal<int64_t>("test");
        [[maybe_unused]] auto v = accessor->get(attr);
    }

    // uint64_t
    {
        auto attr = TestResolver::signal<uint64_t>("test");
        [[maybe_unused]] auto v = accessor->get(attr);
    }

    // float
    {
        auto attr = TestResolver::signal<float>("test");
        [[maybe_unused]] auto v = accessor->get(attr);
    }

    // double
    {
        auto attr = TestResolver::signal<double>("test");
        [[maybe_unused]] auto v = accessor->get(attr);
    }

    // string
    {
        auto attr = TestResolver::signal<std::string>("test");
        [[maybe_unused]] auto v = accessor->get(attr);
    }
}

TEST(VSSTypeCompilation, AllArrayTypes) {
    // TODO: Add array type tests when implemented in Client/Client
    // Currently only scalar types are implemented
}