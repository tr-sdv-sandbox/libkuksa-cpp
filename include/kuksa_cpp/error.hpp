/**
 * @file error.hpp
 * @brief Error handling types for libkuksa-cpp
 *
 * Error Handling Convention:
 * -------------------------
 * This library uses Status and Result<T> for all error handling.
 *
 * **Convention**:
 * - Methods that return a value: use Result<T>
 * - Methods that return void: use Status
 *
 * **Examples**:
 * @code
 * Result<SignalHandle<T>> Resolver::get(path);          // Returns handle or error
 * Result<std::optional<T>> Client::get(handle);         // Returns value or error
 * Status Client::set(handle, value);                     // Void operation
 * Status Client::start();                                // Void operation
 * Result<std::unique_ptr<T>> Factory::create(args);     // Factory with construction errors
 * @endcode
 */

#pragma once

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_format.h>
#include <string>

namespace kuksa {

/**
 * @brief Status type for operations that don't return values
 *
 * Alias for absl::Status to provide consistent namespace and hide implementation.
 *
 * Usage:
 * - Check: if (!status.ok()) { handle error }
 * - Message: status.message()
 * - Code: status.code()
 *
 * Example:
 * @code
 * Status status = client->set(handle, value);
 * if (!status.ok()) {
 *     LOG(ERROR) << "Set failed: " << status;
 *     return;
 * }
 * @endcode
 */
using Status = absl::Status;

/**
 * @brief Result type for operations that return values
 *
 * Alias for absl::StatusOr to provide consistent namespace and hide implementation.
 *
 * Usage:
 * - Check: if (!result.ok()) { handle error }
 * - Access value: *result or result.value()
 * - Access status: result.status()
 *
 * Example:
 * @code
 * Result<SignalHandle<float>> result = resolver->get<float>("Vehicle.Speed");
 * if (!result.ok()) {
 *     LOG(ERROR) << "Failed: " << result.status();
 *     return;
 * }
 * auto handle = *result;
 * @endcode
 */
template<typename T>
using Result = absl::StatusOr<T>;

/**
 * @brief Helper functions for creating common VSS errors
 */
class VSSError {
public:
    /**
     * @brief Signal not found in KUKSA metadata
     */
    static Status SignalNotFound(const std::string& path) {
        return absl::NotFoundError(
            absl::StrFormat("Signal not found in VSS schema: %s", path));
    }

    /**
     * @brief Type mismatch between requested type and VSS schema
     */
    static Status TypeMismatch(const std::string& path,
                               const std::string& expected,
                               const std::string& actual) {
        return absl::InvalidArgumentError(
            absl::StrFormat("Type mismatch for %s: expected %s, got %s",
                           path, expected, actual));
    }

    /**
     * @brief Connection to KUKSA databroker failed
     */
    static Status ConnectionFailed(const std::string& address,
                                   const std::string& reason = "") {
        if (reason.empty()) {
            return absl::UnavailableError(
                absl::StrFormat("Failed to connect to KUKSA at %s", address));
        }
        return absl::UnavailableError(
            absl::StrFormat("Failed to connect to KUKSA at %s: %s", address, reason));
    }

    /**
     * @brief Signal value not set (NONE in KUKSA)
     */
    static Status ValueNotSet(const std::string& path) {
        return absl::NotFoundError(
            absl::StrFormat("Signal %s has no value (NONE)", path));
    }

    /**
     * @brief Operation timeout
     */
    static Status Timeout(const std::string& operation) {
        return absl::DeadlineExceededError(
            absl::StrFormat("Operation timed out: %s", operation));
    }

    /**
     * @brief Permission denied
     */
    static Status PermissionDenied(const std::string& operation) {
        return absl::PermissionDeniedError(operation);
    }

    /**
     * @brief Provider not found for actuator
     */
    static Status ProviderNotFound(const std::string& path) {
        return absl::FailedPreconditionError(
            absl::StrFormat("No provider registered for actuator: %s", path));
    }

    /**
     * @brief Generic operation failure
     */
    static Status OperationFailed(const std::string& operation,
                                  const std::string& reason) {
        return absl::InternalError(
            absl::StrFormat("%s failed: %s", operation, reason));
    }
};

}  // namespace kuksa
