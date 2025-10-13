/**
 * @file vss.hpp
 * @brief Main header for libkuksa-cpp - include this to use the library
 */

#pragma once

#include "types.hpp"
#include "error.hpp"
#include "resolver.hpp"
#include "client.hpp"

/**
 * @brief libkuksa-cpp - C++ library for KUKSA.val databroker
 *
 * The library provides two main components:
 *
 * 1. Resolver - For looking up signal metadata and creating handles
 * 2. Client - Unified client for all operations (sync get/set, async subscribe/publish/serve)
 *
 * Example usage:
 *
 * @code
 * using namespace kuksa;
 *
 * // === Step 1: Create resolver to get handles ===
 * auto resolver_result = Resolver::create("localhost:55555");
 * if (!resolver_result.ok()) {
 *     LOG(ERROR) << "Failed to create resolver: " << resolver_result.status();
 *     return;
 * }
 * auto resolver = std::move(*resolver_result);
 *
 * // Get read-only handles (for subscribing/reading)
 * auto speed_result = resolver->get<float>("Vehicle.Speed");
 * auto temp_result = resolver->get<float>("Vehicle.Cabin.Temperature");
 *
 * // Get read-write handles (for publishing/actuating)
 * auto ac_result = resolver->get_rw<bool>("Vehicle.Cabin.HVAC.IsAirConditioningActive");
 * auto temp_rw_result = resolver->get_rw<float>("Vehicle.Cabin.Temperature");
 *
 * if (!speed_result.ok() || !ac_result.ok() || !temp_result.ok() || !temp_rw_result.ok()) {
 *     LOG(ERROR) << "Failed to resolve signals";
 *     return;
 * }
 * auto speed = *speed_result;
 * auto ac = *ac_result;
 * auto temp = *temp_result;
 * auto temp_rw = *temp_rw_result;
 *
 * // === Step 2: Use Client for subscriptions ===
 * auto client_result = Client::create("localhost:55555");
 * if (!client_result.ok()) {
 *     LOG(ERROR) << "Failed to create client: " << client_result.status();
 *     return;
 * }
 * auto client = std::move(*client_result);
 *
 * client->subscribe(speed, [](std::optional<float> kmh) {
 *     if (kmh) {
 *         LOG(INFO) << "Speed: " << *kmh << " km/h";
 *     }
 * });
 *
 * client->start();
 * auto ready_status = client->wait_until_ready(std::chrono::milliseconds(5000));
 * if (!ready_status.ok()) {
 *     LOG(ERROR) << "Client not ready: " << ready_status;
 *     return;
 * }
 *
 * // === Step 3: Read/write values synchronously (Client has sync get/set!) ===
 * // Read current sensor value
 * auto current_speed = client->get(speed);
 * if (current_speed.ok() && current_speed->has_value()) {
 *     LOG(INFO) << "Current speed: " << **current_speed << " km/h";
 * }
 *
 * // Command actuator (auto-routes to Actuate() RPC)
 * auto set_status = client->set(ac, true);
 * if (!set_status.ok()) {
 *     LOG(ERROR) << "Failed to actuate: " << set_status;
 * }
 *
 * // Publish sensor value (auto-routes to PublishValue() RPC)
 * auto publish_status = client->set(temp_rw, 22.5f);
 * if (!publish_status.ok()) {
 *     LOG(ERROR) << "Failed to publish: " << publish_status;
 * }
 *
 * // === Step 4: Use Client for actuator ownership and publishing ===
 * // (Register actuator - claims ownership)
 * // SAFE: Client outlives callback - captured shared_ptr keeps client alive
 * auto client_shared = std::shared_ptr<Client>(std::move(client));
 * client_shared->serve_actuator(ac, [client_weak = std::weak_ptr<Client>(client_shared)](
 *         bool target, const SignalHandleRW<bool>& handle) {
 *     // Control hardware...
 *     LOG(INFO) << "Setting AC to: " << target;
 *
 *     // Publish actual value back (from worker thread!)
 *     if (auto client = client_weak.lock()) {
 *         auto status = client->publish(handle, target);
 *         if (!status.ok()) {
 *             LOG(ERROR) << "Failed to publish actual: " << status;
 *         }
 *     }
 * });
 *
 * // Publish sensor values (no registration needed!)
 * auto publish_sensor_status = client_shared->publish(temp_rw, 22.5f);
 * if (!publish_sensor_status.ok()) {
 *     LOG(ERROR) << "Failed to publish sensor: " << publish_sensor_status;
 * }
 * @endcode
 *
 * Key API Features:
 * - Resolver: get<T>(path) returns read-only SignalHandle<T>, get_rw<T>(path) for read-write
 * - Client: Unified client with both sync and async operations
 *   - Sync: get(handle), set(handle, value) - auto-routes to correct RPC
 *   - Async: subscribe(handle, callback), publish(handle, value), serve_actuator(handle, handler)
 * - Sensors don't require registration - just publish/set anytime!
 * - Factory methods return Result<T> for proper error handling
 *
 * Threading model:
 * - Resolver: No threads. All operations are synchronous and thread-safe.
 * - Client: Optional threads for streaming operations (subscribe/serve_actuator).
 *              Sync operations (get/set) work without starting threads.
 *
 * @warning Callbacks and handlers MUST NOT block or perform long-running operations.
 * Queue work to another thread if heavy processing is needed.
 *
 * @warning CALLBACK LIFETIME SAFETY:
 * If your callback captures a pointer or reference to the Client, you MUST ensure
 * the Client outlives all pending callbacks. There are two safe patterns:
 *
 * 1. Client outlives callbacks naturally (recommended for simple cases):
 *    @code
 *    auto client = std::move(*Client::create("localhost:55555"));
 *    client->subscribe(handle, [&client](...) {
 *        // Safe: client is on stack and outlives the subscription
 *        client->publish(...);
 *    });
 *    client->start();
 *    // ... keep client alive until done ...
 *    @endcode
 *
 * 2. Use weak_ptr for shared ownership (recommended for complex scenarios):
 *    @code
 *    auto client_shared = std::shared_ptr<Client>(std::move(*Client::create(...)));
 *    client_shared->subscribe(handle, [client_weak = std::weak_ptr<Client>(client_shared)](...) {
 *        if (auto client = client_weak.lock()) {
 *            // Safe: checks if client still alive
 *            client->publish(...);
 *        }
 *    });
 *    @endcode
 *
 * @warning UNSAFE - DO NOT DO THIS:
 * @code
 * auto client = std::move(*Client::create(...));
 * Client* client_ptr = client.get();  // Dangerous!
 * client->subscribe(handle, [client_ptr](...) {
 *     client_ptr->publish(...);  // CRASH if client destroyed before callback runs!
 * });
 * @endcode
 */

namespace kuksa {
    using namespace kuksa;
}