/**
 * @file resolver.cpp
 * @brief Implementation of VSS signal metadata resolver
 */

#include <kuksa_cpp/resolver.hpp>
#include <grpcpp/grpcpp.h>
#include <glog/logging.h>
#include <mutex>
#include <unordered_map>

// Include KUKSA v2 protobuf definitions
#include "kuksa/val/v2/types.pb.h"
#include "kuksa/val/v2/val.pb.h"
#include "kuksa/val/v2/val.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using kuksa::val::v2::VAL;
using kuksa::val::v2::ListMetadataRequest;
using kuksa::val::v2::ListMetadataResponse;

namespace kuksa {

// ============================================================================
// Signal metadata structure
// ============================================================================

struct SignalMetadata {
    int32_t id;
    vss::types::ValueType type;  // UNSPECIFIED serves as sentinel value
    SignalClass signal_class;
};

// ============================================================================
// Resolver Implementation
// ============================================================================

class VSSResolverImpl : public Resolver {
public:
    explicit VSSResolverImpl(const std::string& address) : address_(address), connected_(false) {
        LOG(INFO) << "Creating Resolver for " << address;
    }

    Status connect(int timeout_seconds) {
        std::lock_guard<std::mutex> lock(mutex_);

        channel_ = grpc::CreateChannel(address_, grpc::InsecureChannelCredentials());
        stub_ = VAL::NewStub(channel_);

        // Test connection with configurable timeout
        auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(timeout_seconds);
        if (!channel_->WaitForConnected(deadline)) {
            return VSSError::ConnectionFailed(address_, "Connection timeout");
        }

        connected_ = true;
        LOG(INFO) << "Resolver connected to KUKSA";
        return absl::OkStatus();
    }

    SignalMetadata query_metadata(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!connected_) {
            return {-1, vss::types::ValueType::UNSPECIFIED, SignalClass::UNKNOWN};
        }

        ClientContext context;
        ListMetadataRequest request;
        request.set_root(path);  // Query specific signal

        ListMetadataResponse response;
        grpc::Status grpc_status = stub_->ListMetadata(&context, request, &response);

        if (!grpc_status.ok()) {
            LOG(ERROR) << "Failed to query metadata for " << path << ": " << grpc_status.error_message();
            return {-1, vss::types::ValueType::UNSPECIFIED, SignalClass::UNKNOWN};
        }

        // Find the matching metadata entry
        for (const auto& metadata : response.metadata()) {
            if (metadata.path() == path && metadata.id() != 0) {
                // Convert protobuf DataType to ValueType
                vss::types::ValueType vtype = static_cast<vss::types::ValueType>(metadata.data_type());

                // Determine signal class from entry_type
                SignalClass sclass = SignalClass::UNKNOWN;
                switch (metadata.entry_type()) {
                    case kuksa::val::v2::ENTRY_TYPE_SENSOR:
                        sclass = SignalClass::SENSOR;
                        break;
                    case kuksa::val::v2::ENTRY_TYPE_ACTUATOR:
                        sclass = SignalClass::ACTUATOR;
                        break;
                    case kuksa::val::v2::ENTRY_TYPE_ATTRIBUTE:
                        sclass = SignalClass::ATTRIBUTE;
                        break;
                    default:
                        sclass = SignalClass::UNKNOWN;
                        break;
                }

                return {metadata.id(), vtype, sclass};
            }
        }

        LOG(WARNING) << "No signal metadata found for " << path;
        return {-1, vss::types::ValueType::UNSPECIFIED, SignalClass::UNKNOWN};
    }

    // Unified handle implementation (read and write)
    template<typename T>
    Result<SignalHandle<T>> get_impl(const std::string& path) {
        // Check cache first
        auto dynamic = get_or_create_handle(path);
        if (!dynamic.ok()) {
            return dynamic.status();
        }

        ValueType expected_type = get_value_type<T>();
        if (!are_types_compatible((*dynamic)->type(), expected_type)) {
            return VSSError::TypeMismatch(path,
                                         value_type_to_string(expected_type),
                                         value_type_to_string((*dynamic)->type()));
        }

        // Wrap cached handle in typed handle
        return SignalHandle<T>(*dynamic);
    }

    Result<std::shared_ptr<DynamicSignalHandle>> get_dynamic_impl(const std::string& path) {
        // Return cached handle directly
        return get_or_create_handle(path);
    }

    // Cache lookup/creation helper
    Result<std::shared_ptr<DynamicSignalHandle>> get_or_create_handle(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Check cache
        auto it = handle_cache_.find(path);
        if (it != handle_cache_.end()) {
            LOG(INFO) << "Returning cached handle for " << path;
            return it->second;
        }

        // Not cached - query metadata
        LOG(INFO) << "Cache miss - querying metadata for " << path;
        auto metadata = query_metadata_unlocked(path);
        if (metadata.id < 0 || !metadata.type) {
            return VSSError::SignalNotFound(path);
        }

        // Create and cache handle
        auto handle = std::shared_ptr<DynamicSignalHandle>(
            new DynamicSignalHandle(path, metadata.id, *metadata.type, metadata.signal_class)
        );
        handle_cache_[path] = handle;

        LOG(INFO) << "Cached new handle for " << path << " (ID: " << metadata.id << ")";
        return handle;
    }

    // Query metadata without taking lock (caller must hold lock)
    SignalMetadata query_metadata_unlocked(const std::string& path) {
        if (!connected_) {
            return {-1, vss::types::ValueType::UNSPECIFIED, SignalClass::UNKNOWN};
        }

        ClientContext context;
        ListMetadataRequest request;
        request.set_root(path);

        ListMetadataResponse response;
        grpc::Status grpc_status = stub_->ListMetadata(&context, request, &response);

        if (!grpc_status.ok()) {
            LOG(ERROR) << "Failed to query metadata for " << path << ": " << grpc_status.error_message();
            return {-1, vss::types::ValueType::UNSPECIFIED, SignalClass::UNKNOWN};
        }

        // Find the matching metadata entry
        for (const auto& metadata : response.metadata()) {
            if (metadata.path() == path && metadata.id() != 0) {
                vss::types::ValueType vtype = static_cast<vss::types::ValueType>(metadata.data_type());

                SignalClass sclass = SignalClass::UNKNOWN;
                switch (metadata.entry_type()) {
                    case kuksa::val::v2::ENTRY_TYPE_SENSOR:
                        sclass = SignalClass::SENSOR;
                        break;
                    case kuksa::val::v2::ENTRY_TYPE_ACTUATOR:
                        sclass = SignalClass::ACTUATOR;
                        break;
                    case kuksa::val::v2::ENTRY_TYPE_ATTRIBUTE:
                        sclass = SignalClass::ATTRIBUTE;
                        break;
                    default:
                        sclass = SignalClass::UNKNOWN;
                        break;
                }

                return {metadata.id(), vtype, sclass};
            }
        }

        LOG(WARNING) << "No signal metadata found for " << path;
        return {-1, vss::types::ValueType::UNSPECIFIED, SignalClass::UNKNOWN};
    }

private:
    std::string address_;
    bool connected_;
    std::shared_ptr<Channel> channel_;
    std::unique_ptr<VAL::Stub> stub_;
    std::mutex mutex_;

    // Handle cache - avoids repeated metadata queries
    std::unordered_map<std::string, std::shared_ptr<DynamicSignalHandle>> handle_cache_;
};

// ============================================================================
// Resolver Factory
// ============================================================================

Result<std::unique_ptr<Resolver>> Resolver::create(
    const std::string& address,
    int timeout_seconds
) {
    auto impl = std::make_unique<VSSResolverImpl>(address);
    auto status = impl->connect(timeout_seconds);
    if (!status.ok()) {
        return status;
    }
    return std::unique_ptr<Resolver>(std::move(impl));
}

// ============================================================================
// Template Explicit Instantiations
// ============================================================================

// Typed handles (scalar types)
template Result<SignalHandle<bool>> Resolver::get<bool>(const std::string&);
template Result<SignalHandle<int32_t>> Resolver::get<int32_t>(const std::string&);
template Result<SignalHandle<uint32_t>> Resolver::get<uint32_t>(const std::string&);
template Result<SignalHandle<int64_t>> Resolver::get<int64_t>(const std::string&);
template Result<SignalHandle<uint64_t>> Resolver::get<uint64_t>(const std::string&);
template Result<SignalHandle<float>> Resolver::get<float>(const std::string&);
template Result<SignalHandle<double>> Resolver::get<double>(const std::string&);
template Result<SignalHandle<std::string>> Resolver::get<std::string>(const std::string&);

// Typed handles (array types)
template Result<SignalHandle<std::vector<bool>>> Resolver::get<std::vector<bool>>(const std::string&);
template Result<SignalHandle<std::vector<int32_t>>> Resolver::get<std::vector<int32_t>>(const std::string&);
template Result<SignalHandle<std::vector<uint32_t>>> Resolver::get<std::vector<uint32_t>>(const std::string&);
template Result<SignalHandle<std::vector<int64_t>>> Resolver::get<std::vector<int64_t>>(const std::string&);
template Result<SignalHandle<std::vector<uint64_t>>> Resolver::get<std::vector<uint64_t>>(const std::string&);
template Result<SignalHandle<std::vector<float>>> Resolver::get<std::vector<float>>(const std::string&);
template Result<SignalHandle<std::vector<double>>> Resolver::get<std::vector<double>>(const std::string&);
template Result<SignalHandle<std::vector<std::string>>> Resolver::get<std::vector<std::string>>(const std::string&);

// ============================================================================
// Resolver Virtual Method Implementations
// ============================================================================

template<typename T>
Result<SignalHandle<T>> Resolver::get(const std::string& path) {
    return static_cast<VSSResolverImpl*>(this)->get_impl<T>(path);
}

Result<std::shared_ptr<DynamicSignalHandle>> Resolver::get_dynamic(const std::string& path) {
    return static_cast<VSSResolverImpl*>(this)->get_dynamic_impl(path);
}

} // namespace kuksa
