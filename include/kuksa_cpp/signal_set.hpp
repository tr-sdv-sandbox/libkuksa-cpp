/**
 * @file signal_set.hpp
 * @brief Fluent API builder for batch signal resolution
 */

#pragma once

#include <kuksa_cpp/types.hpp>
#include <kuksa_cpp/error.hpp>
#include <string>
#include <vector>
#include <functional>

namespace kuksa {

// Forward declaration
class Resolver;

/**
 * @brief Builder for batch signal resolution with error aggregation
 *
 * Provides fluent API for resolving multiple signals at once, eliminating
 * verbose error handling boilerplate. All resolution errors are aggregated
 * and returned as a single status.
 *
 * Example:
 * @code
 * kuksa::SignalHandle<float> battery_voltage;
 * kuksa::SignalHandle<bool> door_lock;
 *
 * auto status = resolver->signals()
 *     .add(battery_voltage, "Vehicle.LowVoltageBattery.CurrentVoltage")
 *     .add(door_lock, "Vehicle.Cabin.Door.Row1.Left.IsLocked")
 *     .resolve();
 *
 * if (!status.ok()) {
 *     LOG(ERROR) << "Failed to resolve signals: " << status;
 *     return false;
 * }
 * // All handles are now populated!
 * @endcode
 */
class SignalSetBuilder {
public:
    explicit SignalSetBuilder(Resolver* resolver) : resolver_(resolver) {}

    /**
     * @brief Add a signal to the resolution batch
     *
     * Queues a signal for resolution. The handle will be populated when
     * resolve() is called.
     *
     * @tparam T Signal value type
     * @param handle Reference to handle that will receive the resolved signal
     * @param path VSS signal path
     * @return Reference to this builder for chaining
     */
    template<typename T>
    SignalSetBuilder& add(SignalHandle<T>& handle, const std::string& path);

    /**
     * @brief Execute all signal resolutions
     *
     * Resolves all queued signals and populates their handles.
     * If any resolutions fail, returns an aggregated error status containing
     * all failure messages.
     *
     * @return OkStatus if all resolutions succeeded, error status otherwise
     */
    Status resolve();

private:
    struct SignalSpec {
        std::string path;
        std::function<Status()> resolver;
    };

    Resolver* resolver_;
    std::vector<SignalSpec> signal_specs_;
};

} // namespace kuksa
