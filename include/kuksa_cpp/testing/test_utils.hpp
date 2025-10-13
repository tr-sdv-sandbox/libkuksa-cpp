/**
 * @file test_utils.hpp
 * @brief Test utilities for VSS unit tests
 */

#pragma once

#include <kuksa_cpp/types.hpp>
#include <string>

namespace kuksa {

/**
 * @brief Test resolver for unit tests - creates handles without KUKSA connection
 *
 * TestResolver allows unit tests to create handles with dummy IDs without
 * requiring a live KUKSA databroker connection.
 *
 * Usage:
 * @code
 *   auto signal = TestResolver::signal<float>("Vehicle.Speed", 1);
 *   auto actuator = TestResolver::signal<int32_t>("Vehicle.Test", 1, SignalClass::ACTUATOR);
 * @endcode
 */
class TestResolver {
public:
    /**
     * @brief Create a test signal handle
     *
     * Creates a handle for testing that works for all operations (read, write, subscribe, publish).
     *
     * @tparam T The C++ type of the signal value
     * @param path The VSS signal path
     * @param id The dummy signal ID (default: 1)
     * @param sclass Signal class (default: SENSOR)
     * @return SignalHandle with specified path and ID
     */
    template<typename T>
    static SignalHandle<T> signal(const std::string& path, int32_t id = 1, SignalClass sclass = SignalClass::SENSOR) {
        auto dynamic = std::shared_ptr<DynamicSignalHandle>(
            new DynamicSignalHandle(path, id, get_value_type<T>(), sclass)
        );
        return SignalHandle<T>(dynamic);
    }

    /**
     * @brief Create a test dynamic signal handle
     * @param path The VSS signal path
     * @param id The dummy signal ID (default: 1)
     * @param type The value type (default: INT32)
     * @param sclass Signal class (default: SENSOR)
     * @return shared_ptr to DynamicSignalHandle with specified parameters
     */
    static std::shared_ptr<DynamicSignalHandle> dynamic_signal(
        const std::string& path,
        int32_t id = 1,
        ValueType type = ValueType::INT32,
        SignalClass sclass = SignalClass::SENSOR) {
        return std::shared_ptr<DynamicSignalHandle>(
            new DynamicSignalHandle(path, id, type, sclass)
        );
    }
};

} // namespace kuksa
