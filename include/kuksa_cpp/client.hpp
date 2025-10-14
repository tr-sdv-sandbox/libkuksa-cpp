/**
 * @file client.hpp
 * @brief Unified VSS Client - combines actuator/sensor publishing and subscriptions
 *
 * Client manages both OpenProviderStream and SubscribeById streams over a single
 * gRPC channel, enabling bidirectional communication with KUKSA databroker:
 *
 * - Register and provide actuators (bidirectional - receive requests, publish actuals)
 * - Publish sensor values (no registration needed! KUKSA doesn't enforce sensor ownership)
 * - Subscribe to signal updates
 * - Batch publish support for efficient multi-signal updates
 *
 * This is more efficient than using separate client instances
 * as it shares one TCP connection and enables batch publish operations.
 */

#pragma once

#include <kuksa_cpp/types.hpp>
#include <kuksa_cpp/error.hpp>
#include <glog/logging.h>
#include <absl/strings/str_format.h>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>

namespace kuksa {

/**
 * @brief Unified VSS client with dual streams
 *
 * Key features:
 * - Single gRPC channel (one TCP connection)
 * - Two logical streams:
 *   1. OpenProviderStream (bidirectional) - for actuators/sensors
 *   2. SubscribeById (unidirectional) - for subscriptions
 * - Batch publish support via PublishValuesRequest
 * - Thread-safe batch operations
 *
 * Example usage:
 * @code
 * auto resolver = Resolver::create("localhost:55555");
 * auto client = Client::create("localhost:55555");
 *
 * // Register actuator (handle is passed to callback)
 * auto door_handle = resolver->get<bool>("Vehicle.Cabin.Door.Row1.Left.IsLocked");
 * client->serve_actuator(*door_handle, [](bool target, const SignalHandle<bool>& handle) {
 *     LOG(INFO) << "Lock door: " << target;
 *     // Queue to state machine/hardware controller
 * });
 *
 * // Subscribe to signal
 * auto temp_handle = resolver->get<float>("Vehicle.Cabin.Temperature");
 * client->subscribe(*temp_handle, [](std::optional<float> value) {
 *     if (value) {
 *         LOG(INFO) << "Temperature: " << *value;
 *     }
 * });
 *
 * // Start and wait for ready
 * client->start();
 * client->wait_until_ready(5000ms);
 *
 * // Publish sensor value (no registration needed!)
 * auto speed_handle = resolver->get<float>("Vehicle.Speed");
 * client->publish(*speed_handle, 120.5f);
 *
 * // Batch publish multiple sensors (efficient!)
 * client->publish_batch({
 *     {*speed_handle, 125.0f},
 *     {*temp_handle, 22.5f}
 * });
 *
 * // Publish actuator actual value
 * client->publish(*door_handle, true);
 * @endcode
 */
class Client {
public:
    /**
     * @brief Factory method to create unified client
     *
     * Creates a client instance with a single gRPC channel.
     * Connection happens lazily when start() is called.
     *
     * @param databroker_address Address of KUKSA databroker (e.g., "localhost:55555")
     * @return Result containing Client instance, or error if channel creation fails
     */
    static Result<std::unique_ptr<Client>> create(
        const std::string& databroker_address
    );

    // ========================================================================
    // ACTUATOR PROVIDER API
    // ========================================================================

    /**
     * @brief Register an actuator with typed handle and callback
     *
     * This is an asynchronous configuration operation - it registers the actuator
     * handler but does not immediately connect or validate. Actual registration with
     * the databroker happens when start() is called. Any errors (invalid signal ID,
     * connection failure, permission denied) will be reported via start() or
     * wait_until_ready().
     *
     * The handle is resolved externally and passed in for registration.
     * The same handle is used for both actuation dispatch and publishing.
     *
     * Must be called before start(). Cannot be called while client is running.
     *
     * @param handle Signal handle for the actuator (from Resolver)
     * @param callback Called when actuation request arrives
     * @return Status - OkStatus if queued successfully, FailedPrecondition if client is already running
     *
     * @warning The callback is executed on the provider gRPC thread.
     *          DO NOT call publish() from inside the callback - it will cause
     *          gRPC errors (TOO_MANY_OPERATIONS). Instead, queue work to a
     *          separate thread/state machine that will publish later.
     *
     * Example:
     * @code
     * auto door = resolver->get<bool>("Vehicle.Door.IsLocked");
     * std::queue<Work> work_queue;
     *
     * auto status = client->serve_actuator(door, [&](bool target, const SignalHandle<bool>& handle) {
     *     LOG(INFO) << "Lock door: " << target;
     *     work_queue.push({handle, target});  // Queue for processing
     *     // DON'T: client->publish(handle, target);  // WRONG - causes gRPC error!
     * });
     * if (!status.ok()) {
     *     LOG(ERROR) << "Failed to register actuator: " << status;
     * }
     *
     * // Later, from worker thread:
     * auto [handle, value] = work_queue.pop();
     * client->publish(handle, value);  // OK - different thread
     * @endcode
     */
    template<typename T, typename Callback>
    Status serve_actuator(
        const SignalHandle<T>& handle,
        Callback&& callback) {
        return serve_actuator_impl(
            handle.path(),
            handle.id(),
            vss::types::get_value_type<T>(),
            [callback = std::forward<Callback>(callback), handle](const vss::types::Value& value) mutable {
                callback(std::get<T>(value), handle);
            }
        );
    }

    /**
     * @brief Register an actuator with dynamic handle and callback
     *
     * Runtime version for YAML/config-based actuators.
     *
     * @param handle Dynamic handle for the actuator
     * @param callback Called when actuation request arrives
     * @return Status - FailedPrecondition if client is already running
     */
    template<typename Callback>
    Status serve_actuator(
        const DynamicSignalHandle& handle,
        Callback&& callback) {
        return serve_actuator_impl(
            handle.path(),
            handle.id(),
            handle.type(),
            [callback = std::forward<Callback>(callback), handle](const vss::types::Value& value) mutable {
                callback(value, handle);
            }
        );
    }

    /**
     * @brief Internal implementation for actuator registration
     * @return Status - FailedPrecondition if client is already running
     */
    virtual Status serve_actuator_impl(
        const std::string& path,
        int32_t signal_id,
        vss::types::ValueType type,
        std::function<void(const vss::types::Value&)> handler
    ) = 0;

    // ========================================================================
    // SYNCHRONOUS READ/WRITE API
    // ========================================================================

    /**
     * @brief Synchronously get current signal value with quality
     *
     * Works for all signal types (sensors, attributes, actuators).
     * For actuators, returns the ACTUAL (feedback) value.
     *
     * Returns a QualifiedValue with quality inferred from KUKSA:
     * - Has value → SignalQuality::VALID
     * - No value → SignalQuality::NOT_AVAILABLE
     *
     * Thread-safe. Can be called from any thread, even before start().
     * Does not require starting the client streams.
     *
     * @param signal Signal handle (read-only or read-write)
     * @return Result containing QualifiedValue:
     *         - Success with value and VALID: Signal has valid value
     *         - Success with no value and NOT_AVAILABLE: Signal has no value
     *         - Error: Connection or communication failure
     *
     * Example:
     * @code
     * auto speed = resolver->get<float>("Vehicle.Speed");
     * auto result = client->get(*speed);
     * if (!result.ok()) {
     *     LOG(ERROR) << "Failed to get speed: " << result.status();
     * } else if (result->is_valid()) {
     *     LOG(INFO) << "Speed: " << *result->value;
     * } else {
     *     LOG(INFO) << "Speed not available";
     * }
     * @endcode
     */
    template<typename T>
    Result<vss::types::QualifiedValue<T>> get(const SignalHandle<T>& signal);

    /**
     * @brief Synchronously get value with dynamic handle
     */
    Result<vss::types::DynamicQualifiedValue> get(const DynamicSignalHandle& signal);

    /**
     * @brief Synchronously set signal value with quality
     *
     * Automatically routes to the appropriate RPC based on signal class:
     * - Actuators: Actuate() RPC (sends TARGET command to provider)
     * - Sensors/Attributes: PublishValue() RPC
     *
     * Quality handling:
     * - Only VALID quality values are sent
     * - Non-VALID quality returns InvalidArgumentError
     *
     * Thread-safe. Can be called from any thread, even before start().
     * Does not require starting the client streams.
     *
     * @param signal Signal handle
     * @param qvalue Qualified value with quality indicator
     * @return Status indicating success or failure
     *
     * Example:
     * @code
     * using namespace vss::types;
     *
     * auto door = resolver->get<bool>("Vehicle.Door.IsLocked");
     * auto status = client->set(*door, QualifiedValue{true, SignalQuality::VALID});
     * if (!status.ok()) {
     *     LOG(ERROR) << "Failed to set door lock: " << status;
     * }
     * @endcode
     */
    template<typename T>
    Status set(const SignalHandle<T>& signal, const vss::types::QualifiedValue<T>& qvalue);

    /**
     * @brief Convenience: Set with plain value (assumes VALID quality)
     */
    template<typename T>
    Status set(const SignalHandle<T>& signal, T value) {
        return set(signal, vss::types::QualifiedValue<T>{value, vss::types::SignalQuality::VALID});
    }

    /**
     * @brief Convenience overload for string signals with const char*
     */
    Status set(const SignalHandle<std::string>& signal, const char* value);

    /**
     * @brief Synchronously set value with dynamic handle
     */
    Status set(const DynamicSignalHandle& signal, const vss::types::DynamicQualifiedValue& qvalue);

    // ========================================================================
    // PUBLISH API (Single and Batch)
    // ========================================================================

    // NOTE: Sensors do NOT need registration! KUKSA doesn't enforce sensor
    // ownership - any client can publish sensor values at any time using
    // PublishValuesRequest. Only actuators need registration via
    // ProvideActuationRequest because actuation requires bidirectional
    // communication (receiving target values from KUKSA).

    /**
     * @brief Publish a single value with quality
     *
     * Quality mapping:
     * - VALID quality → value is sent
     * - Any other quality → empty datapoint sent (notifies subscribers)
     *
     * Thread-safe. Can be called from any thread after start().
     *
     * @param handle Signal handle
     * @param qvalue Qualified value with quality indicator
     * @return Status indicating success or failure
     */
    template<typename T>
    Status publish(const SignalHandle<T>& handle, const vss::types::QualifiedValue<T>& qvalue) {
        vss::types::DynamicQualifiedValue dyn_qvalue;
        if (qvalue.value.has_value()) {
            dyn_qvalue.value = vss::types::Value(*qvalue.value);
        } else {
            dyn_qvalue.value = vss::types::Value(std::monostate{});
        }
        dyn_qvalue.quality = qvalue.quality;
        dyn_qvalue.timestamp = qvalue.timestamp;
        return publish_impl(handle.id(), dyn_qvalue);
    }

    /**
     * @brief Convenience: Publish plain value (assumes VALID quality)
     */
    template<typename T>
    Status publish(const SignalHandle<T>& handle, T value) {
        return publish(handle, vss::types::QualifiedValue<T>{value, vss::types::SignalQuality::VALID});
    }

    /**
     * @brief Publish using dynamic handle
     */
    Status publish(const DynamicSignalHandle& handle, const vss::types::DynamicQualifiedValue& qvalue) {
        return publish_impl(handle.id(), qvalue);
    }

    /**
     * @brief Helper struct for type-safe batch publishing
     *
     * Allows {handle, value} pairs without explicit QualifiedValue wrapper.
     */
    struct PublishEntry {
        int32_t signal_id;
        vss::types::DynamicQualifiedValue qvalue;

        // Construct from typed handle and QualifiedValue
        template<typename T>
        PublishEntry(const SignalHandle<T>& handle, const vss::types::QualifiedValue<T>& qv)
            : signal_id(handle.id()) {
            if (qv.value.has_value()) {
                qvalue.value = vss::types::Value{*qv.value};
            }
            qvalue.quality = qv.quality;
            qvalue.timestamp = qv.timestamp;
        }

        // Construct from typed handle and plain value (assumes VALID)
        template<typename T>
        PublishEntry(const SignalHandle<T>& handle, T val)
            : signal_id(handle.id()), qvalue(vss::types::Value{val}, vss::types::SignalQuality::VALID) {}

        // Construct from dynamic handle and QualifiedValue
        PublishEntry(const DynamicSignalHandle& handle, vss::types::DynamicQualifiedValue qv)
            : signal_id(handle.id()), qvalue(std::move(qv)) {}
    };

    /**
     * @brief Batch publish multiple values (efficient!)
     *
     * Uses PublishValuesRequest on the provider stream to send multiple
     * values in a single RPC. Much more efficient than multiple publish() calls.
     *
     * Thread-safe. Can be called from any thread after start().
     *
     * @param values List of {handle, value} pairs to publish
     * @param callback Optional callback invoked when batch completes (on provider thread)
     *                 - Receives map of signal_id -> Status for each signal
     *                 - Only called for signals with errors (empty map = all succeeded)
     * @return Status indicating if batch was queued successfully
     *
     * Example:
     * @code
     * client->publish_batch(
     *     {
     *         {speed_handle, 125.0f},
     *         {temp_handle, 22.5f},
     *         {rpm_handle, 3500u}
     *     },
     *     [](const std::map<int32_t, absl::Status>& errors) {
     *         if (errors.empty()) {
     *             LOG(INFO) << "All values published successfully";
     *         } else {
     *             for (const auto& [id, status] : errors) {
     *                 LOG(ERROR) << "Signal " << id << " failed: " << status;
     *             }
     *         }
     *     }
     * );
     * @endcode
     */
    Status publish_batch(
        std::initializer_list<PublishEntry> entries,
        std::function<void(const std::map<int32_t, Status>&)> callback = nullptr
    ) {
        std::map<int32_t, vss::types::DynamicQualifiedValue> values;
        for (const auto& entry : entries) {
            values[entry.signal_id] = entry.qvalue;
        }
        return publish_batch_impl(values, callback);
    }

    /**
     * @brief Batch publish from a vector (for dynamic/runtime usage)
     */
    Status publish_batch(
        const std::vector<PublishEntry>& entries,
        std::function<void(const std::map<int32_t, Status>&)> callback = nullptr
    ) {
        std::map<int32_t, vss::types::DynamicQualifiedValue> values;
        for (const auto& entry : entries) {
            values[entry.signal_id] = entry.qvalue;
        }
        return publish_batch_impl(values, callback);
    }

    // ========================================================================
    // SUBSCRIPTION API
    // ========================================================================

    /**
     * @brief Subscribe to signal value changes
     *
     * This is an asynchronous configuration operation - it registers the subscription
     * but does not immediately connect or validate. Actual subscription happens when
     * start() is called. Any errors (invalid signal ID, connection failure) will be
     * reported via start() or wait_until_ready().
     *
     * Must be called before start(). Cannot be called while client is running.
     *
     * @warning The callback is executed on the subscription thread.
     *          It MUST NOT block or perform long-running operations.
     *          Do NOT call publish() from inside the callback - queue work to another thread.
     *
     * @param signal Signal handle (obtained from Resolver)
     * @param callback Called when signal value changes or on initial value
     */
    template<typename T>
    void subscribe(const SignalHandle<T>& signal, typename SignalHandle<T>::Callback callback);

    /**
     * @brief Subscribe with dynamic handle
     */
    void subscribe(const DynamicSignalHandle& signal, std::function<void(const vss::types::DynamicQualifiedValue&)> callback);

    /**
     * @brief Unsubscribe from a signal
     */
    template<typename T>
    bool unsubscribe(const SignalHandle<T>& signal) {
        return unsubscribe_impl(signal.id());
    }

    /**
     * @brief Unsubscribe using dynamic handle
     */
    bool unsubscribe(const DynamicSignalHandle& signal) {
        return unsubscribe_impl(signal.id());
    }

    /**
     * @brief Clear all subscriptions
     */
    virtual void clear_subscriptions() = 0;

    /**
     * @brief Get number of active subscriptions
     */
    virtual size_t subscription_count() const = 0;

    // ========================================================================
    // LIFECYCLE
    // ========================================================================

    /**
     * @brief Start both provider and subscriber streams
     *
     * Launches threads as needed:
     * 1. Provider thread - if actuators registered (manages OpenProviderStream)
     * 2. Subscriber thread - if subscriptions registered (manages SubscribeById)
     *
     * Must be called after registering actuators and/or subscriptions.
     *
     * Connection happens asynchronously - use wait_until_ready() or status()
     * to check when the client is operational.
     *
     * @return Status - FailedPrecondition if client is already running, OkStatus otherwise
     */
    virtual Status start() = 0;

    /**
     * @brief Stop both streams
     */
    virtual void stop() = 0;

    /**
     * @brief Check if client is running
     */
    virtual bool is_running() const = 0;

    /**
     * @brief Get operational status
     *
     * Returns OK only if BOTH streams are operational.
     * Otherwise returns the first error encountered.
     *
     * @return Status indicating if client is fully operational
     */
    virtual Status status() const = 0;

    /**
     * @brief Wait for client to be ready (both streams operational)
     *
     * Blocks until both provider and subscriber streams are active,
     * or timeout occurs.
     *
     * @param timeout Maximum time to wait (default: 10 seconds)
     * @return Status:
     *   - OkStatus(): Client is ready (both streams operational)
     *   - DeadlineExceededError(): Timeout waiting for ready state
     *   - Other errors: Validation or connection failed
     */
    virtual Status wait_until_ready(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(10000)
    ) = 0;

    virtual ~Client() = default;

protected:
    Client() = default;

    // Internal implementations for sync read/write
    virtual Result<vss::types::DynamicQualifiedValue> get_impl(int32_t signal_id) = 0;

    virtual Status set_impl(
        int32_t signal_id,
        const vss::types::DynamicQualifiedValue& qvalue,
        SignalClass signal_class
    ) = 0;

    // Internal implementations for async operations
    virtual Status publish_impl(int32_t signal_id, const vss::types::DynamicQualifiedValue& qvalue) = 0;

    virtual Status publish_batch_impl(
        const std::map<int32_t, vss::types::DynamicQualifiedValue>& values,
        std::function<void(const std::map<int32_t, Status>&)> callback
    ) = 0;

    virtual void subscribe_impl(
        std::shared_ptr<DynamicSignalHandle> handle,
        std::function<void(const vss::types::DynamicQualifiedValue&)> callback
    ) = 0;

    virtual bool unsubscribe_impl(int32_t signal_id) = 0;

    /**
     * @brief Create a typed SignalHandle (for derived classes)
     */
    template<typename T>
    static SignalHandle<T> make_typed_handle(const std::string& path, int32_t signal_id, SignalClass sclass = SignalClass::UNKNOWN) {
        auto dynamic = std::shared_ptr<DynamicSignalHandle>(
            new DynamicSignalHandle(path, signal_id, vss::types::get_value_type<T>(), sclass)
        );
        return SignalHandle<T>(dynamic);
    }
};

// ========================================================================
// Template implementations
// ========================================================================

// Synchronous get() implementations
template<typename T>
Result<vss::types::QualifiedValue<T>> Client::get(const SignalHandle<T>& signal) {
    auto result = get_impl(signal.id());
    if (!result.ok()) {
        return result.status();
    }

    const auto& dyn_qvalue = *result;

    // Convert DynamicQualifiedValue to QualifiedValue<T>
    if (vss::types::is_empty(dyn_qvalue.value)) {
        // No value
        vss::types::QualifiedValue<T> qvalue;
        qvalue.value = std::nullopt;
        qvalue.quality = dyn_qvalue.quality;
        qvalue.timestamp = dyn_qvalue.timestamp;
        return qvalue;
    }

    const auto& value = dyn_qvalue.value;
    if (!std::holds_alternative<T>(value)) {
        return absl::InvalidArgumentError(
            absl::StrFormat("Type mismatch for %s: expected type index %d, got %d",
                signal.path(), vss::types::Value(T{}).index(), value.index())
        );
    }

    return vss::types::QualifiedValue<T>{
        std::get<T>(value),
        dyn_qvalue.quality,
        dyn_qvalue.timestamp
    };
}

inline Result<vss::types::DynamicQualifiedValue> Client::get(const DynamicSignalHandle& signal) {
    return get_impl(signal.id());
}

// Synchronous set() implementations
template<typename T>
Status Client::set(const SignalHandle<T>& signal, const vss::types::QualifiedValue<T>& qvalue) {
    // Convert to DynamicQualifiedValue
    vss::types::DynamicQualifiedValue dyn_qvalue;
    if (qvalue.value.has_value()) {
        dyn_qvalue.value = vss::types::Value{*qvalue.value};
    }
    dyn_qvalue.quality = qvalue.quality;
    dyn_qvalue.timestamp = qvalue.timestamp;

    return set_impl(signal.id(), dyn_qvalue, signal.signal_class());
}

inline Status Client::set(const SignalHandle<std::string>& signal, const char* value) {
    return set(signal, vss::types::QualifiedValue<std::string>{std::string(value), vss::types::SignalQuality::VALID});
}

inline Status Client::set(const DynamicSignalHandle& signal, const vss::types::DynamicQualifiedValue& qvalue) {
    return set_impl(signal.id(), qvalue, signal.signal_class());
}

// Subscription implementations
template<typename T>
void Client::subscribe(const SignalHandle<T>& signal, typename SignalHandle<T>::Callback callback) {
    subscribe_impl(signal.dynamic_handle(), [callback, path = signal.path()](const vss::types::DynamicQualifiedValue& dyn_qvalue) {
        // Convert DynamicQualifiedValue to QualifiedValue<T>
        if (vss::types::is_empty(dyn_qvalue.value)) {
            vss::types::QualifiedValue<T> qvalue;
            qvalue.value = std::nullopt;
            qvalue.quality = dyn_qvalue.quality;
            qvalue.timestamp = dyn_qvalue.timestamp;
            callback(qvalue);
        } else {
            const auto& value = dyn_qvalue.value;
            if (std::holds_alternative<T>(value)) {
                callback(vss::types::QualifiedValue<T>{std::get<T>(value), dyn_qvalue.quality, dyn_qvalue.timestamp});
            } else {
                LOG(WARNING) << "Type mismatch in subscription callback for " << path
                            << " - expected type index " << vss::types::Value(T{}).index()
                            << ", got " << value.index();
            }
        }
    });
}

} // namespace kuksa
