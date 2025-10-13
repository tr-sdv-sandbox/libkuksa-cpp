/**
 * @file resolver.hpp
 * @brief VSS signal metadata resolver for creating handles
 */

#pragma once

#include <string>
#include <memory>
#include <kuksa_cpp/types.hpp>
#include <kuksa_cpp/error.hpp>

namespace kuksa {

/**
 * @brief Resolves VSS signal paths to typed handles by querying KUKSA metadata
 *
 * Resolver is responsible for looking up signal metadata from KUKSA databroker
 * and creating properly typed handles. This separates metadata resolution from
 * actual operations (get/set/subscribe).
 *
 * Usage:
 * @code
 *   auto resolver = Resolver::create("localhost:55555");
 *   if (!resolver.ok()) {
 *       LOG(ERROR) << "Failed to connect: " << resolver.status();
 *       return;
 *   }
 *
 *   // Get handle for any operation (read, write, subscribe)
 *   auto speed = (*resolver)->get<float>("Vehicle.Speed");
 *   auto door = (*resolver)->get<bool>("Vehicle.Door.IsLocked");
 *
 *   // Use with client for all operations
 *   client->get(*speed);        // Read
 *   client->set(*door, true);   // Write
 *   client->subscribe(*speed, callback);  // Subscribe
 * @endcode
 */
class Resolver {
public:
    /**
     * @brief Create a resolver connected to KUKSA databroker
     * @param address KUKSA databroker address (e.g., "localhost:55555")
     * @param timeout_seconds Connection timeout in seconds (default: 2)
     * @return Result containing resolver or error status
     */
    static Result<std::unique_ptr<Resolver>> create(
        const std::string& address,
        int timeout_seconds = 2
    );

    virtual ~Resolver() = default;

    // ========================================================================
    // TYPED HANDLES (Compile-time type safety)
    // ========================================================================

    /**
     * @brief Get a typed handle for a VSS signal
     *
     * Returns a handle that can be used for all operations:
     * - client->get() - Read current value
     * - client->set() - Write value (auto-routes based on signal class)
     * - client->subscribe() - Receive updates
     * - client->publish() - Publish via provider stream
     *
     * Works for all signal types: sensors, attributes, and actuators.
     *
     * @tparam T The C++ type of the signal value
     * @param path The VSS signal path (e.g., "Vehicle.Speed", "Vehicle.Door.IsLocked")
     * @return Result containing SignalHandle<T> or error
     *
     * Example:
     * @code
     * auto speed = resolver->get<float>("Vehicle.Speed");
     * auto door = resolver->get<bool>("Vehicle.Door.IsLocked");
     *
     * // All operations work with the same handle
     * client->get(*speed);           // Read sensor
     * client->set(*speed, 100.0f);   // Publish sensor value
     * client->set(*door, true);      // Command actuator (Actuate RPC)
     * client->subscribe(*speed, callback);
     * @endcode
     */
    template<typename T>
    Result<SignalHandle<T>> get(const std::string& path);

    // ========================================================================
    // DYNAMIC HANDLES (Runtime type resolution)
    // ========================================================================

    /**
     * @brief Get a dynamic handle for runtime type resolution
     *
     * Use when the signal type is not known at compile time (e.g., from YAML config).
     * The dynamic handle can be used for both reading and writing.
     *
     * Returns a shared_ptr so the handle can be stored and shared across components.
     *
     * @param path The VSS signal path
     * @return Result containing shared_ptr<DynamicSignalHandle> or error
     *
     * Example:
     * @code
     * auto handle = resolver->get_dynamic("Vehicle.Speed");
     * auto value = accessor->get(**handle);  // Returns std::optional<Value>
     * accessor->set(**handle, 100.0f);       // Accepts Value variant
     * @endcode
     */
    Result<std::shared_ptr<DynamicSignalHandle>> get_dynamic(const std::string& path);

protected:
    Resolver() = default;
};

} // namespace kuksa
