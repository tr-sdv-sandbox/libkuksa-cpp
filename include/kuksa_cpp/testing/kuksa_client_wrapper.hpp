#pragma once

#include "test_models.hpp"
#include <kuksa_cpp/kuksa.hpp>
#include <string>
#include <optional>
#include <unordered_map>

namespace sdv {
namespace testing {

/**
 * @brief Wrapper around Resolver + Client for testing framework
 *
 * This class provides a simplified interface for the testing framework by:
 * - Validating signals against KUKSA schema using Resolver
 * - Automatically selecting correct RPC based on signal type:
 *   • Actuators → Actuate() RPC (set_target)
 *   • Sensors → PublishValue() RPC (publish)
 *   • Attributes → PublishValue() RPC (publish)
 * - Caching resolved handles for efficiency
 * - Converting between TestValue and Value types
 * - Using production APIs (Client) to validate what users will use
 *
 * The wrapper handles dynamic type resolution at runtime since YAML tests
 * don't have compile-time type information.
 */
class KuksaClientWrapper {
public:
    explicit KuksaClientWrapper(const std::string& kuksa_url);
    ~KuksaClientWrapper();

    bool connect();
    void disconnect();

    /**
     * @brief Inject a value into KUKSA databroker
     * @param path VSS signal path
     * @param value Value to inject
     * @return true if successful, false otherwise
     *
     * This method:
     * 1. Resolves the signal type from KUKSA metadata
     * 2. Automatically selects the correct RPC:
     *    - Actuator → Uses Actuate() RPC (routes through provider)
     *    - Sensor/Attribute → Uses PublishValue() RPC (standalone publish)
     * 3. Validates signal exists and has correct type
     */
    bool inject(const std::string& path, const TestValue& value);

    /**
     * @brief Get a value from KUKSA databroker
     * @param path VSS signal path
     * @return Value if available, nullopt otherwise
     *
     * This method resolves the signal type once and caches the handle for efficiency.
     */
    std::optional<TestValue> get(const std::string& path);

private:
    std::string kuksa_url_;
    std::unique_ptr<kuksa::Resolver> resolver_;
    std::unique_ptr<kuksa::Client> accessor_;

    // Handle cache to avoid repeated resolution
    std::unordered_map<std::string, std::shared_ptr<kuksa::DynamicSignalHandle>> handle_cache_;

    // Helper to convert TestValue to Value
    kuksa::Value test_value_to_value(const TestValue& test_value);

    // Helper to convert Value to TestValue (handles subset only)
    std::optional<TestValue> value_to_test_value(const kuksa::Value& value);
};

} // namespace testing
} // namespace sdv
