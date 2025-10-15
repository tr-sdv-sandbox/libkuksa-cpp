#include "kuksa_cpp/testing/kuksa_client_wrapper.hpp"
#include <glog/logging.h>

namespace sdv {
namespace testing {

KuksaClientWrapper::KuksaClientWrapper(const std::string& kuksa_url)
    : kuksa_url_(kuksa_url) {
}

KuksaClientWrapper::~KuksaClientWrapper() {
    disconnect();
}

bool KuksaClientWrapper::connect() {
    LOG(INFO) << "Connecting to KUKSA at: " << kuksa_url_;

    // Create resolver
    auto resolver_result = kuksa::Resolver::create(kuksa_url_);
    if (!resolver_result.ok()) {
        LOG(ERROR) << "Failed to create resolver: " << resolver_result.status();
        return false;
    }
    resolver_ = std::move(*resolver_result);

    // Create accessor
    auto accessor_result = kuksa::Client::create(kuksa_url_);
    if (!accessor_result.ok()) {
        LOG(ERROR) << "Failed to create accessor: " << accessor_result.status();
        return false;
    }
    accessor_ = std::move(*accessor_result);

    LOG(INFO) << "Successfully connected to KUKSA";
    return true;
}

void KuksaClientWrapper::disconnect() {
    resolver_.reset();
    accessor_.reset();
    handle_cache_.clear();
}

vss::types::Value KuksaClientWrapper::test_value_to_value(const TestValue& test_value) {
    return std::visit([](auto&& v) -> vss::types::Value {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, bool>) {
            return vss::types::Value(v);
        }
        else if constexpr (std::is_same_v<T, int32_t>) {
            return vss::types::Value(v);
        }
        else if constexpr (std::is_same_v<T, float>) {
            return vss::types::Value(v);
        }
        else if constexpr (std::is_same_v<T, double>) {
            // Convert double to float for compatibility
            return vss::types::Value(static_cast<float>(v));
        }
        else if constexpr (std::is_same_v<T, std::string>) {
            return vss::types::Value(v);
        }

        // Should not reach here
        throw std::runtime_error("Unsupported TestValue type");
    }, test_value);
}

std::optional<TestValue> KuksaClientWrapper::value_to_test_value(const vss::types::Value& value) {
    return std::visit([](auto&& v) -> std::optional<TestValue> {
        using T = std::decay_t<decltype(v)>;

        // Handle scalar types
        if constexpr (std::is_same_v<T, bool>) {
            return TestValue(v);
        }
        else if constexpr (std::is_same_v<T, int32_t>) {
            return TestValue(v);
        }
        else if constexpr (std::is_same_v<T, uint32_t>) {
            // Convert uint32 to int32 for TestValue compatibility
            return TestValue(static_cast<int32_t>(v));
        }
        else if constexpr (std::is_same_v<T, int64_t>) {
            // Convert int64 to int32 for TestValue compatibility
            return TestValue(static_cast<int32_t>(v));
        }
        else if constexpr (std::is_same_v<T, uint64_t>) {
            // Convert uint64 to int32 for TestValue compatibility
            return TestValue(static_cast<int32_t>(v));
        }
        else if constexpr (std::is_same_v<T, float>) {
            return TestValue(v);
        }
        else if constexpr (std::is_same_v<T, double>) {
            // Convert double to float for TestValue compatibility
            return TestValue(static_cast<float>(v));
        }
        else if constexpr (std::is_same_v<T, std::string>) {
            return TestValue(v);
        }
        else {
            // Arrays and other types not supported in TestValue
            LOG(WARNING) << "Value type not supported in TestValue (likely an array)";
            return std::nullopt;
        }
    }, value);
}

bool KuksaClientWrapper::inject(const std::string& path, const TestValue& value) {
    if (!resolver_ || !accessor_) {
        LOG(ERROR) << "Not connected to KUKSA";
        return false;
    }

    // Convert TestValue to Value
    vss::types::Value vss_value = test_value_to_value(value);

    // Resolve the signal (works for all types: sensors, attributes, actuators)
    auto handle_result = resolver_->get_dynamic(path);
    if (!handle_result.ok()) {
        LOG(ERROR) << "Signal not found in KUKSA schema: " << path;
        LOG(ERROR) << "  Error: " << handle_result.status();
        return false;
    }

    // Cache the handle
    handle_cache_.emplace(path, *handle_result);

    // Use unified set() - automatically routes to correct RPC based on signal class
    LOG(INFO) << "Injecting " << path << " via set() (auto-routes based on signal type)";
    vss::types::DynamicQualifiedValue qvalue{vss_value, vss::types::SignalQuality::VALID};
    auto status = accessor_->set(**handle_result, qvalue);
    if (!status.ok()) {
        LOG(ERROR) << "Failed to set value: " << status;
        return false;
    }
    return true;
}

std::optional<TestValue> KuksaClientWrapper::get(const std::string& path) {
    if (!resolver_ || !accessor_) {
        LOG(ERROR) << "Not connected to KUKSA";
        return std::nullopt;
    }

    // Check cache first
    auto handle_it = handle_cache_.find(path);
    if (handle_it != handle_cache_.end()) {
        // Use cached handle
        auto value_result = accessor_->get(*handle_it->second);
        if (!value_result.ok()) {
            LOG(WARNING) << "Failed to get value: " << value_result.status();
            return std::nullopt;
        }
        if (value_result->is_valid()) {
            return value_to_test_value(value_result->value);
        }
        return std::nullopt;
    }

    // Resolve the signal (works for all types: sensors, attributes, actuators)
    auto handle_result = resolver_->get_dynamic(path);
    if (!handle_result.ok()) {
        LOG(WARNING) << "Could not resolve signal: " << path;
        LOG(WARNING) << "  Error: " << handle_result.status();
        return std::nullopt;
    }

    // Cache the handle
    handle_cache_.emplace(path, *handle_result);

    // Get the value
    auto value_result = accessor_->get(**handle_result);
    if (!value_result.ok()) {
        LOG(WARNING) << "Failed to get value: " << value_result.status();
        return std::nullopt;
    }
    if (value_result->is_valid()) {
        return value_to_test_value(value_result->value);
    }
    return std::nullopt;
}

} // namespace testing
} // namespace sdv
