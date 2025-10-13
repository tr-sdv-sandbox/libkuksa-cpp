/**
 * @file types.hpp
 * @brief VSS type definitions and abstractions
 */

#pragma once

#include <string>
#include <functional>
#include <variant>
#include <optional>
#include <cstdint>
#include <vector>
#include <memory>

namespace kuksa {

// Forward declarations
class Resolver;
class VSSResolverImpl;
class TestResolver;
class VSSClientImpl;

/**
 * @brief VSS value types - includes both scalar and array types
 */
using Value = std::variant<
    // Scalar types
    bool, int32_t, uint32_t, int64_t, uint64_t, float, double, std::string,
    // Array types
    std::vector<bool>, std::vector<int32_t>, std::vector<uint32_t>,
    std::vector<int64_t>, std::vector<uint64_t>, std::vector<float>,
    std::vector<double>, std::vector<std::string>
>;

/**
 * @brief Enum representing all VSS data types
 *
 * This enum matches the types in the Value variant and provides
 * type-safe runtime type identification.
 */
/**
 * @brief VSS value types matching KUKSA DataType enum values
 *
 * Values are aligned with kuksa.val.v2.DataType protobuf enum for efficient conversion.
 * We only include the types we support (no INT8/INT16/UINT8/UINT16/TIMESTAMP).
 */
enum class ValueType {
    // Scalar types (matching protobuf DataType values)
    STRING       = 1,   // DATA_TYPE_STRING
    BOOL         = 2,   // DATA_TYPE_BOOLEAN
    INT32        = 5,   // DATA_TYPE_INT32
    INT64        = 6,   // DATA_TYPE_INT64
    UINT32       = 9,   // DATA_TYPE_UINT32
    UINT64       = 10,  // DATA_TYPE_UINT64
    FLOAT        = 11,  // DATA_TYPE_FLOAT
    DOUBLE       = 12,  // DATA_TYPE_DOUBLE

    // Array types (matching protobuf DataType values)
    STRING_ARRAY = 20,  // DATA_TYPE_STRING_ARRAY
    BOOL_ARRAY   = 21,  // DATA_TYPE_BOOLEAN_ARRAY
    INT32_ARRAY  = 24,  // DATA_TYPE_INT32_ARRAY
    INT64_ARRAY  = 25,  // DATA_TYPE_INT64_ARRAY
    UINT32_ARRAY = 28,  // DATA_TYPE_UINT32_ARRAY
    UINT64_ARRAY = 29,  // DATA_TYPE_UINT64_ARRAY
    FLOAT_ARRAY  = 30,  // DATA_TYPE_FLOAT_ARRAY
    DOUBLE_ARRAY = 31   // DATA_TYPE_DOUBLE_ARRAY
};

// Helper for static_assert in template constraints
template<typename T>
inline constexpr bool always_false_v = false;

/**
 * @brief Get ValueType enum from C++ type at compile time
 */
template<typename T>
constexpr ValueType get_value_type() {
    if constexpr (std::is_same_v<T, bool>) return ValueType::BOOL;
    else if constexpr (std::is_same_v<T, int32_t>) return ValueType::INT32;
    else if constexpr (std::is_same_v<T, uint32_t>) return ValueType::UINT32;
    else if constexpr (std::is_same_v<T, int64_t>) return ValueType::INT64;
    else if constexpr (std::is_same_v<T, uint64_t>) return ValueType::UINT64;
    else if constexpr (std::is_same_v<T, float>) return ValueType::FLOAT;
    else if constexpr (std::is_same_v<T, double>) return ValueType::DOUBLE;
    else if constexpr (std::is_same_v<T, std::string>) return ValueType::STRING;
    else if constexpr (std::is_same_v<T, std::vector<bool>>) return ValueType::BOOL_ARRAY;
    else if constexpr (std::is_same_v<T, std::vector<int32_t>>) return ValueType::INT32_ARRAY;
    else if constexpr (std::is_same_v<T, std::vector<uint32_t>>) return ValueType::UINT32_ARRAY;
    else if constexpr (std::is_same_v<T, std::vector<int64_t>>) return ValueType::INT64_ARRAY;
    else if constexpr (std::is_same_v<T, std::vector<uint64_t>>) return ValueType::UINT64_ARRAY;
    else if constexpr (std::is_same_v<T, std::vector<float>>) return ValueType::FLOAT_ARRAY;
    else if constexpr (std::is_same_v<T, std::vector<double>>) return ValueType::DOUBLE_ARRAY;
    else if constexpr (std::is_same_v<T, std::vector<std::string>>) return ValueType::STRING_ARRAY;
    else static_assert(always_false_v<T>, "Unsupported type");
}

/**
 * @brief Get ValueType from a Value variant at runtime
 */
ValueType get_value_type(const Value& value);

/**
 * @brief Convert ValueType enum to string (for logging, YAML, etc.)
 */
const char* value_type_to_string(ValueType type);

/**
 * @brief Parse ValueType from string (for YAML loading)
 * @return std::nullopt if string doesn't match any type
 */
std::optional<ValueType> value_type_from_string(const std::string& str);

// =============================================================================
// Unified Handle Hierarchy
// =============================================================================
//
// This section defines a type-safe handle system for VSS signals.
// All handles contain cached metadata (path, signal_id, type) and are used
// with Client to perform operations.
//
// Handle Types:
// - SignalHandle<T>: Type-safe handle for all operations (read, write, subscribe)
// - DynamicSignalHandle: Runtime type handle (for config-driven usage)
//
// Design principles:
// 1. Handles are lightweight type tokens (pass by const reference)
// 2. Client holds connections and performs operations
// 3. Automatic RPC routing based on signal class (sensor/actuator/attribute)
// 4. Handles support all operations - get(), set(), subscribe(), publish()
//

// Forward declarations
class Client;

/**
 * @brief Check if two types are compatible for VSS operations
 *
 * Types are compatible if they can safely interchange in VSS context:
 * - Floating point: float ↔ double
 * - Signed integers: int32_t ↔ int64_t
 * - Unsigned integers: uint32_t ↔ uint64_t
 * - Arrays: compatible if element types are compatible
 *
 * Incompatible combinations:
 * - Different type families (int ↔ float, bool ↔ int, string ↔ numeric)
 * - Scalar ↔ array
 * - Signed ↔ unsigned integers
 */
inline bool are_types_compatible(ValueType expected, ValueType actual) {
    if (expected == actual) return true;

    // Floating point compatibility: float ↔ double
    if ((expected == ValueType::FLOAT && actual == ValueType::DOUBLE) ||
        (expected == ValueType::DOUBLE && actual == ValueType::FLOAT)) {
        return true;
    }

    // Signed integer compatibility: int32 ↔ int64
    if ((expected == ValueType::INT32 && actual == ValueType::INT64) ||
        (expected == ValueType::INT64 && actual == ValueType::INT32)) {
        return true;
    }

    // Unsigned integer compatibility: uint32 ↔ uint64
    if ((expected == ValueType::UINT32 && actual == ValueType::UINT64) ||
        (expected == ValueType::UINT64 && actual == ValueType::UINT32)) {
        return true;
    }

    // Floating point array compatibility: float[] ↔ double[]
    if ((expected == ValueType::FLOAT_ARRAY && actual == ValueType::DOUBLE_ARRAY) ||
        (expected == ValueType::DOUBLE_ARRAY && actual == ValueType::FLOAT_ARRAY)) {
        return true;
    }

    // Signed integer array compatibility: int32[] ↔ int64[]
    if ((expected == ValueType::INT32_ARRAY && actual == ValueType::INT64_ARRAY) ||
        (expected == ValueType::INT64_ARRAY && actual == ValueType::INT32_ARRAY)) {
        return true;
    }

    // Unsigned integer array compatibility: uint32[] ↔ uint64[]
    if ((expected == ValueType::UINT32_ARRAY && actual == ValueType::UINT64_ARRAY) ||
        (expected == ValueType::UINT64_ARRAY && actual == ValueType::UINT32_ARRAY)) {
        return true;
    }

    return false;
}

/**
 * @brief Signal classification
 *
 * Internal enum used by resolver to track signal types from KUKSA metadata.
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
 * - get() - Read current value
 * - set() - Write value (auto-routes to Actuate or PublishValue RPC)
 * - subscribe() - Receive value updates
 * - publish() - Publish value via provider stream
 *
 * This is a lightweight type-safe wrapper around shared_ptr<DynamicSignalHandle>.
 * Multiple SignalHandle instances can wrap the same underlying handle.
 *
 * Examples:
 * @code
 * auto resolver = Resolver::create(address);
 * auto client = Client::create(address);
 *
 * // Get handle - works for any signal type
 * auto speed = resolver->get<float>("Vehicle.Speed");
 * auto door = resolver->get<bool>("Vehicle.Door.IsLocked");
 *
 * // Read values
 * client->get(*speed);
 * client->get(*door);
 *
 * // Write values (auto-routes based on signal class)
 * client->set(*speed, 100.0f);  // Sensor -> PublishValue RPC
 * client->set(*door, true);      // Actuator -> Actuate RPC
 *
 * // Subscribe to updates
 * client->subscribe(*speed, [](auto value) { ... });
 * @endcode
 */
template<typename T>
class SignalHandle {
public:
    using Callback = std::function<void(std::optional<T> value)>;

    // Accessors delegate to underlying DynamicSignalHandle
    const std::string& path() const;
    int32_t id() const;
    ValueType type() const;
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
// Dynamic/Runtime Handle Variant (for YAML/config-based usage)
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
 * - get() returns std::optional<Value> (variant type)
 * - set() accepts Value and routes to appropriate RPC
 * - subscribe() calls back with std::optional<Value>
 *
 * Example:
 * @code
 * auto resolver = Resolver::create(address);
 * auto client = Client::create(address);
 * auto handle = resolver->get_dynamic("Vehicle.Speed");
 *
 * // Read value (returns variant)
 * auto value = client->get(*handle);
 * if (value) {
 *     float speed = std::get<float>(*value);
 * }
 *
 * // Write value (automatically routes based on signal class)
 * client->set(*handle, 100.0f);
 * @endcode
 */
class DynamicSignalHandle {
public:
    const std::string& path() const { return path_; }
    int32_t id() const { return signal_id_; }
    ValueType type() const { return type_; }
    SignalClass signal_class() const { return signal_class_; }

protected:
    DynamicSignalHandle(std::string path, int32_t signal_id, ValueType type, SignalClass sclass)
        : path_(std::move(path)), signal_id_(signal_id), type_(type), signal_class_(sclass) {}

    std::string path_;
    int32_t signal_id_;
    ValueType type_;
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
    return handle_->path();
}

template<typename T>
inline int32_t SignalHandle<T>::id() const {
    return handle_->id();
}

template<typename T>
inline ValueType SignalHandle<T>::type() const {
    return handle_->type();
}

template<typename T>
inline SignalClass SignalHandle<T>::signal_class() const {
    return handle_->signal_class();
}

} // namespace kuksa
