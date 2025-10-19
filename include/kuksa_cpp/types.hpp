/**
 * @file types.hpp
 * @brief KUKSA-specific types and signal handles
 *
 * This file provides KUKSA-specific types while using vss::types as the
 * foundation for VSS value types, signal quality, and struct support.
 *
 * Users should include both headers:
 * - <vss/types/types.hpp> for VSS types (Value, QualifiedValue, SignalQuality)
 * - <kuksa_cpp/types.hpp> for KUKSA-specific (SignalHandle, SignalClass)
 */

#pragma once

#include <vss/types/types.hpp>
#include <string>
#include <functional>
#include <optional>
#include <cstdint>
#include <memory>

namespace kuksa {

// Forward declarations
class Resolver;
class VSSResolverImpl;
class TestResolver;
class VSSClientImpl;
class Client;

/**
 * @brief Signal classification (KUKSA-specific)
 *
 * Used by resolver to track signal types from KUKSA metadata.
 * This drives the automatic RPC routing in client->set().
 */
enum class SignalClass {
    SENSOR,     // Read-only signal (speed, temperature, etc.)
    ACTUATOR,   // Controllable signal (door lock, HVAC, etc.)
    ATTRIBUTE,  // Static configuration (VIN, brand, etc.)
    UNKNOWN
};

// Forward declaration of the canonical handle type
class DynamicSignalHandle;

// =============================================================================
// Signal Handles
// =============================================================================

/**
 * @brief Type-safe signal handle for all VSS operations
 *
 * SignalHandle provides unified access to VSS signals for all operations:
 * - get() - Read current value with quality
 * - set() - Write value (auto-routes to Actuate or PublishValue RPC)
 * - subscribe() - Receive value updates with quality
 * - publish() - Publish value via provider stream
 *
 * This is a lightweight type-safe wrapper around shared_ptr<DynamicSignalHandle>.
 * Multiple SignalHandle instances can wrap the same underlying handle.
 *
 * Examples:
 * @code
 * #include <vss/types/types.hpp>
 * #include <kuksa_cpp/client.hpp>
 *
 * using namespace vss::types;
 *
 * auto resolver = kuksa::Resolver::create(address);
 * auto client = kuksa::Client::create(address);
 *
 * // Get handle - works for any signal type
 * auto speed = resolver->get<float>("Vehicle.Speed");
 * auto door = resolver->get<bool>("Vehicle.Door.IsLocked");
 *
 * // Read values (returns QualifiedValue)
 * auto result = client->get(*speed);
 * if (result.ok() && result->is_valid()) {
 *     LOG(INFO) << "Speed: " << *result->value;
 * }
 *
 * // Write values (auto-routes based on signal class)
 * client->set(*speed, 100.0f);  // Convenience: assumes VALID
 * client->set(*door, QualifiedValue{true, SignalQuality::VALID});
 *
 * // Subscribe to updates (receives QualifiedValue)
 * client->subscribe(*speed, [](QualifiedValue<float> qv) {
 *     if (qv.is_valid()) {
 *         use_speed(*qv.value);
 *     }
 * });
 * @endcode
 */
template<typename T>
class SignalHandle {
public:
    using Callback = std::function<void(vss::types::QualifiedValue<T>)>;

    /**
     * @brief Default constructor - creates an invalid handle
     *
     * An invalid handle can be used as a placeholder and assigned later.
     * Operations on an invalid handle will fail with appropriate errors.
     *
     * Example:
     * @code
     * class MyClass {
     *     kuksa::SignalHandle<float> speed_;  // Default constructed, invalid
     * public:
     *     bool init(kuksa::Resolver* resolver) {
     *         auto result = resolver->get<float>("Vehicle.Speed");
     *         if (!result.ok()) return false;
     *         speed_ = *result;  // Assign valid handle
     *         return true;
     *     }
     * };
     * @endcode
     */
    SignalHandle() : handle_(nullptr) {}

    /**
     * @brief Check if handle is valid (has been successfully resolved)
     */
    bool is_valid() const { return handle_ != nullptr; }

    /**
     * @brief Explicit bool conversion - returns true if handle is valid
     */
    explicit operator bool() const { return is_valid(); }

    // Accessors delegate to underlying DynamicSignalHandle
    const std::string& path() const;
    int32_t id() const;
    vss::types::ValueType type() const;
    SignalClass signal_class() const;

    // Access underlying dynamic handle
    std::shared_ptr<DynamicSignalHandle> dynamic_handle() const { return handle_; }

protected:
    explicit SignalHandle(std::shared_ptr<DynamicSignalHandle> handle)
        : handle_(std::move(handle)) {}

    std::shared_ptr<DynamicSignalHandle> handle_;

    friend class Client;
    friend class VSSClientImpl;
    friend class Resolver;
    friend class VSSResolverImpl;
    friend class TestResolver;
};

// =============================================================================
// Dynamic/Runtime Handle (for YAML/config-based usage)
// =============================================================================

/**
 * @brief Dynamic signal handle - the canonical handle type (runtime type usage)
 *
 * This is the ONE source of truth for signal metadata. All SignalHandle<T>
 * instances wrap a shared_ptr to DynamicSignalHandle, eliminating duplication.
 *
 * Use when signal type is not known at compile time (e.g., loaded from YAML/config).
 * Works for all signal types: sensors, attributes, and actuators.
 *
 * Supports both read and write operations:
 * - get() returns QualifiedValue (vss::types::DynamicQualifiedValue)
 * - set() accepts QualifiedValue and routes to appropriate RPC
 * - subscribe() calls back with QualifiedValue
 *
 * Example:
 * @code
 * auto resolver = Resolver::create(address);
 * auto client = Client::create(address);
 * auto handle = resolver->get_dynamic("Vehicle.Speed");
 *
 * // Read value (returns DynamicQualifiedValue)
 * auto result = client->get(*handle);
 * if (result.ok() && result->is_valid()) {
 *     float speed = std::get<float>(result->value);
 * }
 *
 * // Write value (automatically routes based on signal class)
 * client->set(*handle, vss::types::Value{100.0f});
 * @endcode
 */
class DynamicSignalHandle {
public:
    const std::string& path() const { return path_; }
    int32_t id() const { return signal_id_; }
    vss::types::ValueType type() const { return type_; }
    SignalClass signal_class() const { return signal_class_; }

protected:
    DynamicSignalHandle(std::string path, int32_t signal_id, vss::types::ValueType type, SignalClass sclass)
        : path_(std::move(path)), signal_id_(signal_id), type_(type), signal_class_(sclass) {}

    std::string path_;
    int32_t signal_id_;
    vss::types::ValueType type_;
    SignalClass signal_class_;

    friend class Client;
    friend class VSSClientImpl;
    friend class Resolver;
    friend class VSSResolverImpl;
    friend class TestResolver;
};

// =============================================================================
// SignalHandle<T> method implementations (must come after DynamicSignalHandle)
// =============================================================================

template<typename T>
inline const std::string& SignalHandle<T>::path() const {
    static const std::string invalid_path = "<invalid>";
    return handle_ ? handle_->path() : invalid_path;
}

template<typename T>
inline int32_t SignalHandle<T>::id() const {
    return handle_ ? handle_->id() : -1;
}

template<typename T>
inline vss::types::ValueType SignalHandle<T>::type() const {
    return handle_ ? handle_->type() : vss::types::ValueType::BOOL;  // Arbitrary default
}

template<typename T>
inline SignalClass SignalHandle<T>::signal_class() const {
    return handle_ ? handle_->signal_class() : SignalClass::UNKNOWN;
}

} // namespace kuksa
