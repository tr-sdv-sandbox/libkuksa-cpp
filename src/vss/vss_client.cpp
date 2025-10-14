/**
 * @file vss_client.cpp
 * @brief Unified VSS Client implementation with dual streams
 *
 * This is a new implementation that combines provider and subscriber
 * functionality in a single client with two concurrent streams over
 * one gRPC channel.
 */

#include <kuksa_cpp/client.hpp>
#include <kuksa_cpp/error.hpp>
#include <kuksa_cpp/connection_state_machine.hpp>
#include <grpcpp/grpcpp.h>
#include <glog/logging.h>
#include <absl/strings/str_format.h>
#include <absl/strings/str_join.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <map>

// Include KUKSA v2 protobuf definitions
#include "kuksa/val/v2/types.pb.h"
#include "kuksa/val/v2/val.pb.h"
#include "kuksa/val/v2/val.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using kuksa::val::v2::VAL;
using kuksa::val::v2::OpenProviderStreamRequest;
using kuksa::val::v2::OpenProviderStreamResponse;
using kuksa::val::v2::ProvideActuationRequest;
using kuksa::val::v2::ProvideSignalRequest;
using kuksa::val::v2::BatchActuateStreamRequest;
using kuksa::val::v2::BatchActuateStreamResponse;
using kuksa::val::v2::PublishValueRequest;
using kuksa::val::v2::PublishValueResponse;
using kuksa::val::v2::SubscribeByIdRequest;
using kuksa::val::v2::SubscribeByIdResponse;
using kuksa::val::v2::GetValueRequest;
using kuksa::val::v2::GetValueResponse;
using kuksa::val::v2::ActuateRequest;
using kuksa::val::v2::ActuateResponse;
using kuksa::val::v2::ListMetadataRequest;
using kuksa::val::v2::ListMetadataResponse;
using kuksa::val::v2::Datapoint;

namespace kuksa {

// ============================================================================
// Helper functions (shared by both streams)
// ============================================================================

// Convert vss::types::Value to protobuf Value
static kuksa::val::v2::Value to_proto_value(const vss::types::Value& value) {
    kuksa::val::v2::Value proto_value;

    std::visit([&proto_value](auto&& v) {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, std::monostate>) {
            // Empty value - don't set anything in protobuf
        } else if constexpr (std::is_same_v<T, bool>) {
            proto_value.set_bool_(v);
        } else if constexpr (std::is_same_v<T, int32_t>) {
            proto_value.set_int32(v);
        } else if constexpr (std::is_same_v<T, uint32_t>) {
            proto_value.set_uint32(v);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            proto_value.set_int64(v);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            proto_value.set_uint64(v);
        } else if constexpr (std::is_same_v<T, float>) {
            proto_value.set_float_(v);
        } else if constexpr (std::is_same_v<T, double>) {
            proto_value.set_double_(v);
        } else if constexpr (std::is_same_v<T, std::string>) {
            proto_value.set_string(v);
        }
        // Array types
        else if constexpr (std::is_same_v<T, std::vector<bool>>) {
            auto* arr = proto_value.mutable_bool_array();
            for (bool val : v) arr->add_values(val);
        } else if constexpr (std::is_same_v<T, std::vector<int32_t>>) {
            auto* arr = proto_value.mutable_int32_array();
            for (int32_t val : v) arr->add_values(val);
        } else if constexpr (std::is_same_v<T, std::vector<uint32_t>>) {
            auto* arr = proto_value.mutable_uint32_array();
            for (uint32_t val : v) arr->add_values(val);
        } else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
            auto* arr = proto_value.mutable_int64_array();
            for (int64_t val : v) arr->add_values(val);
        } else if constexpr (std::is_same_v<T, std::vector<uint64_t>>) {
            auto* arr = proto_value.mutable_uint64_array();
            for (uint64_t val : v) arr->add_values(val);
        } else if constexpr (std::is_same_v<T, std::vector<float>>) {
            auto* arr = proto_value.mutable_float_array();
            for (float val : v) arr->add_values(val);
        } else if constexpr (std::is_same_v<T, std::vector<double>>) {
            auto* arr = proto_value.mutable_double_array();
            for (double val : v) arr->add_values(val);
        } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
            auto* arr = proto_value.mutable_string_array();
            for (const auto& val : v) arr->add_values(val);
        }
    }, value);

    return proto_value;
}

// Convert protobuf Value to vss::types::Value
static vss::types::Value from_proto_value(const kuksa::val::v2::Value& proto_value) {
    if (proto_value.has_bool_()) return proto_value.bool_();
    if (proto_value.has_int32()) return proto_value.int32();
    if (proto_value.has_uint32()) return proto_value.uint32();
    if (proto_value.has_int64()) return proto_value.int64();
    if (proto_value.has_uint64()) return proto_value.uint64();
    if (proto_value.has_float_()) return proto_value.float_();
    if (proto_value.has_double_()) return proto_value.double_();
    if (proto_value.has_string()) return proto_value.string();

    // Array types
    if (proto_value.has_bool_array()) {
        std::vector<bool> values;
        for (bool v : proto_value.bool_array().values()) values.push_back(v);
        return values;
    }
    if (proto_value.has_int32_array()) {
        std::vector<int32_t> values;
        for (int32_t v : proto_value.int32_array().values()) values.push_back(v);
        return values;
    }
    if (proto_value.has_uint32_array()) {
        std::vector<uint32_t> values;
        for (uint32_t v : proto_value.uint32_array().values()) values.push_back(v);
        return values;
    }
    if (proto_value.has_int64_array()) {
        std::vector<int64_t> values;
        for (int64_t v : proto_value.int64_array().values()) values.push_back(v);
        return values;
    }
    if (proto_value.has_uint64_array()) {
        std::vector<uint64_t> values;
        for (uint64_t v : proto_value.uint64_array().values()) values.push_back(v);
        return values;
    }
    if (proto_value.has_float_array()) {
        std::vector<float> values;
        for (float v : proto_value.float_array().values()) values.push_back(v);
        return values;
    }
    if (proto_value.has_double_array()) {
        std::vector<double> values;
        for (double v : proto_value.double_array().values()) values.push_back(v);
        return values;
    }
    if (proto_value.has_string_array()) {
        std::vector<std::string> values;
        for (const auto& v : proto_value.string_array().values()) values.push_back(v);
        return values;
    }

    return vss::types::Value{std::monostate{}};  // Default to empty
}

// Convert protobuf datapoint to DynamicQualifiedValue (with quality inference)
static vss::types::DynamicQualifiedValue datapoint_to_qualified_value(const Datapoint& dp) {
    vss::types::DynamicQualifiedValue qvalue;

    // Set timestamp
    if (dp.has_timestamp()) {
        auto seconds = dp.timestamp().seconds();
        auto nanos = dp.timestamp().nanos();
        qvalue.timestamp = std::chrono::system_clock::time_point(
            std::chrono::seconds(seconds) + std::chrono::nanoseconds(nanos)
        );
    } else {
        qvalue.timestamp = std::chrono::system_clock::now();
    }

    // Infer quality from presence of value
    if (dp.has_value()) {
        qvalue.value = from_proto_value(dp.value());
        qvalue.quality = vss::types::SignalQuality::VALID;
    } else {
        qvalue.value = vss::types::Value{std::monostate{}};
        qvalue.quality = vss::types::SignalQuality::NOT_AVAILABLE;
    }

    return qvalue;
}

// Convert QualifiedValue to protobuf Datapoint (with quality handling)
static Datapoint qualified_value_to_datapoint(const vss::types::DynamicQualifiedValue& qvalue) {
    Datapoint dp;

    // Set timestamp
    auto time_since_epoch = qvalue.timestamp.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(time_since_epoch);
    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(time_since_epoch - seconds);
    dp.mutable_timestamp()->set_seconds(seconds.count());
    dp.mutable_timestamp()->set_nanos(nanos.count());

    // Only set value if quality is VALID and value is not empty
    if (qvalue.quality == vss::types::SignalQuality::VALID && !vss::types::is_empty(qvalue.value)) {
        *dp.mutable_value() = to_proto_value(qvalue.value);
    }
    // Otherwise leave value unset (empty datapoint)

    return dp;
}

// ============================================================================
// Client Implementation
// ============================================================================

class VSSClientImpl : public Client {
public:
    explicit VSSClientImpl(const std::string& address)
        : address_(address)
        , running_(false)
        , provider_sm_(std::make_unique<DatabrokerConnectionStateMachine>(
              "Provider",
              "REGISTERING",
              "STREAMING"
          ))
        , subscriber_sm_(std::make_unique<DatabrokerConnectionStateMachine>(
              "Subscriber",
              "SUBSCRIBING",
              "STREAMING"
          )) {
    }

    ~VSSClientImpl() override {
        if (running_) {
            stop();
        }
    }

    void initialize_connection() {
        // Create single gRPC channel (shared by both streams)
        channel_ = grpc::CreateChannel(address_, grpc::InsecureChannelCredentials());
        stub_ = VAL::NewStub(channel_);
        LOG(INFO) << "Created unified client for " << address_;
    }

    // ========================================================================
    // Actuator/Sensor Registration
    // ========================================================================

    Status serve_actuator_impl(
        const std::string& path,
        int32_t signal_id,
        vss::types::ValueType type,
        std::function<void(const vss::types::Value&)> handler) override {

        if (running_) {
            return absl::FailedPreconditionError("Cannot serve actuator while client is running");
        }
        actuator_handlers_.push_back({path, signal_id, type, handler});
        LOG(INFO) << "Registered actuator: " << path << " (ID: " << signal_id << ", type: " << vss::types::value_type_to_string(type) << ")";
        return absl::OkStatus();
    }

    // ========================================================================
    // Subscription
    // ========================================================================

    void subscribe_impl(
        std::shared_ptr<DynamicSignalHandle> handle,
        std::function<void(const vss::types::DynamicQualifiedValue&)> callback) override {

        LOG(INFO) << "Registering subscription to " << handle->path();
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        subscriptions_[handle->id()] = callback;
        id_to_handle_[handle->id()] = handle;
    }

    bool unsubscribe_impl(int32_t signal_id) override {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        auto it = subscriptions_.find(signal_id);
        if (it != subscriptions_.end()) {
            subscriptions_.erase(it);
            id_to_handle_.erase(signal_id);
            LOG(INFO) << "Unsubscribed from signal ID: " << signal_id;
            return true;
        }
        return false;
    }

    void clear_subscriptions() override {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        if (running_) {
            stop();
        }
        subscriptions_.clear();
        id_to_handle_.clear();
        LOG(INFO) << "Cleared all subscriptions";
    }

    size_t subscription_count() const override {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        return subscriptions_.size();
    }

    // ========================================================================
    // Synchronous Read/Write
    // ========================================================================
    //
    // Thread Safety Note:
    // These methods access stub_ and channel_ without explicit synchronization.
    // This is safe because gRPC stubs and channels are internally thread-safe
    // and designed for concurrent use from multiple threads. See:
    // https://grpc.io/docs/languages/cpp/basics/#thread-safety
    //
    // stub_ is set once during construction and never modified, so concurrent
    // reads are safe. All RPC calls (GetValue, Actuate, PublishValue) use
    // per-call ClientContext which is not shared across threads.

    Result<vss::types::DynamicQualifiedValue> get_impl(int32_t signal_id) override {
        if (!stub_) {
            return absl::FailedPreconditionError("Not connected to databroker");
        }

        ClientContext context;
        GetValueRequest request;
        request.mutable_signal_id()->set_id(signal_id);

        GetValueResponse response;
        grpc::Status grpc_status = stub_->GetValue(&context, request, &response);

        if (!grpc_status.ok()) {
            return absl::Status(
                static_cast<absl::StatusCode>(grpc_status.error_code()),
                grpc_status.error_message()
            );
        }

        return datapoint_to_qualified_value(response.data_point());
    }

    Status set_impl(
        int32_t signal_id,
        const vss::types::DynamicQualifiedValue& qvalue,
        SignalClass signal_class) override {

        if (!stub_) {
            return absl::FailedPreconditionError("Not connected to databroker");
        }

        // Check quality - only allow VALID for synchronous set
        if (qvalue.quality != vss::types::SignalQuality::VALID || vss::types::is_empty(qvalue.value)) {
            return absl::InvalidArgumentError("Cannot set value with quality != VALID");
        }

        // Route based on signal class
        if (signal_class == SignalClass::ACTUATOR) {
            // Use Actuate RPC for actuators (extract value)
            return actuate_signal(signal_id, qvalue.value);
        } else {
            // Use PublishValue RPC for sensors/attributes
            return publish_impl(signal_id, qvalue);
        }
    }

    Status actuate_signal(int32_t signal_id, const vss::types::Value& value) {
        // Use the Actuate RPC (not the provider stream)
        using kuksa::val::v2::ActuateRequest;
        using kuksa::val::v2::ActuateResponse;

        ClientContext context;
        ActuateRequest request;
        request.mutable_signal_id()->set_id(signal_id);
        *request.mutable_value() = to_proto_value(value);

        ActuateResponse response;
        grpc::Status grpc_status = stub_->Actuate(&context, request, &response);

        if (!grpc_status.ok()) {
            return absl::Status(
                static_cast<absl::StatusCode>(grpc_status.error_code()),
                grpc_status.error_message()
            );
        }

        return absl::OkStatus();
    }

    // ========================================================================
    // Publishing (Single and Batch)
    // ========================================================================

    Status publish_impl(int32_t signal_id, const vss::types::DynamicQualifiedValue& qvalue) override {
        // Use standalone PublishValue RPC (works for all signals without registration)
        if (!stub_) {
            return absl::FailedPreconditionError("Not connected to databroker");
        }

        ClientContext context;
        PublishValueRequest request;
        auto* sig_id = request.mutable_signal_id();
        sig_id->set_id(signal_id);

        // Convert QualifiedValue to protobuf Datapoint (with quality handling)
        *request.mutable_data_point() = qualified_value_to_datapoint(qvalue);

        PublishValueResponse response;
        grpc::Status grpc_status = stub_->PublishValue(&context, request, &response);

        if (!grpc_status.ok()) {
            LOG(ERROR) << "Failed to publish signal ID " << signal_id << ": " << grpc_status.error_message();
            return absl::Status(
                static_cast<absl::StatusCode>(grpc_status.error_code()),
                grpc_status.error_message()
            );
        }

        LOG(INFO) << "Successfully published signal ID " << signal_id;
        return absl::OkStatus();
    }

    Status publish_batch_impl(
        const std::map<int32_t, vss::types::DynamicQualifiedValue>& values,
        std::function<void(const std::map<int32_t, absl::Status>&)> callback) override {

        // Publish each value using standalone RPC
        std::map<int32_t, absl::Status> errors;

        for (const auto& [signal_id, value] : values) {
            auto status = publish_impl(signal_id, value);
            if (!status.ok()) {
                errors[signal_id] = status;
            }
        }

        // Invoke callback if provided
        if (callback) {
            callback(errors);
        }

        return errors.empty() ? absl::OkStatus() : absl::UnknownError("Some publishes failed");
    }

    // ========================================================================
    // Lifecycle
    // ========================================================================

    Status start() override {
        if (running_) {
            return absl::FailedPreconditionError("Client is already running");
        }

        running_ = true;

        // Start provider thread only if we have actuators
        // (Publishing uses standalone PublishValue RPCs, not the provider stream)
        if (!actuator_handlers_.empty()) {
            provider_thread_ = std::thread([this]() { provider_loop(); });
        }

        // Start subscriber thread (if we have subscriptions)
        if (!subscriptions_.empty()) {
            subscriber_thread_ = std::thread([this]() { subscriber_loop(); });
        }

        LOG(INFO) << "Unified client started (actuators="
                  << !actuator_handlers_.empty() << ", subscriptions="
                  << !subscriptions_.empty() << ")";

        return absl::OkStatus();
    }

    void stop() override {
        if (!running_) return;

        LOG(INFO) << "Stopping unified client";
        running_ = false;

        // Cancel contexts
        if (provider_context_) provider_context_->TryCancel();
        if (subscriber_context_) subscriber_context_->TryCancel();

        // Join threads
        if (provider_thread_.joinable()) provider_thread_.join();
        if (subscriber_thread_.joinable()) subscriber_thread_.join();

        LOG(INFO) << "Unified client stopped";
    }

    bool is_running() const override {
        return running_;
    }

    Status status() const override {
        // If we have actuators, provider must be OK
        if (!actuator_handlers_.empty()) {
            auto provider_status = provider_sm_->status();
            if (!provider_status.ok()) return provider_status;
        }

        // If we have subscriptions, subscriber must be OK
        if (!subscriptions_.empty()) {
            auto subscriber_status = subscriber_sm_->status();
            if (!subscriber_status.ok()) return subscriber_status;
        }

        return absl::OkStatus();
    }

    Status wait_until_ready(std::chrono::milliseconds timeout) override {
        if (!running_) {
            return absl::FailedPreconditionError("Client not started - call start() first");
        }

        auto deadline = std::chrono::steady_clock::now() + timeout;

        // Wait for provider if we have actuators
        if (!actuator_handlers_.empty()) {
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            auto status = provider_sm_->wait_until_active(remaining);
            if (!status.ok()) return status;
        }

        // Wait for subscriber if needed
        if (!subscriptions_.empty()) {
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            auto status = subscriber_sm_->wait_until_active(remaining);
            if (!status.ok()) return status;
        }

        return absl::OkStatus();
    }

private:
    // ========================================================================
    // Provider Stream Thread
    // ========================================================================

    void provider_loop() {
        provider_sm_->trigger_start();
        LOG(INFO) << "Provider stream thread started";

        // Step 1: Validate all actuators (signal_id already resolved from handle)
        std::vector<std::string> errors;

        // Validate that all actuators exist and types match
        for (const auto& handler : actuator_handlers_) {
            auto metadata = query_signal_metadata(handler.path);

            if (metadata.id <= 0) {
                errors.push_back(absl::StrFormat("  - %s: Signal not found in VSS", handler.path));
                continue;
            }

            // Verify signal_id matches what resolver gave us
            if (metadata.id != handler.signal_id) {
                errors.push_back(absl::StrFormat("  - %s: Signal ID mismatch (expected %d, got %d)",
                    handler.path, handler.signal_id, metadata.id));
                continue;
            }

            if (!metadata.type) {
                errors.push_back(absl::StrFormat("  - %s: No type metadata", handler.path));
                continue;
            }

            if (!are_types_compatible(handler.type, *metadata.type)) {
                errors.push_back(absl::StrFormat("  - %s: Type mismatch (expected %s, got %s)",
                    handler.path,
                    value_type_to_string(handler.type),
                    value_type_to_string(*metadata.type)));
                continue;
            }
        }

        if (!errors.empty()) {
            std::string error_msg = absl::StrFormat("Actuator validation failed:\n%s", absl::StrJoin(errors, "\n"));
            LOG(ERROR) << error_msg;
            provider_sm_->trigger_stream_failed(absl::InvalidArgumentError(error_msg), false);
            provider_sm_->trigger_stop();
            return;
        }

        if (!actuator_handlers_.empty()) {
            LOG(INFO) << "All actuators validated successfully";
        } else {
            LOG(INFO) << "No actuators to validate (sensor-only mode)";
        }

        provider_sm_->trigger_channel_ready();

        // Step 2: Open provider stream
        provider_context_ = std::make_unique<ClientContext>();
        auto stream = stub_->OpenProviderStream(provider_context_.get());
        if (!stream) {
            LOG(ERROR) << "Failed to open provider stream";
            provider_sm_->trigger_stream_failed(absl::UnavailableError("Failed to open stream"), true);
            provider_sm_->trigger_stop();
            return;
        }

        // Step 3: Register actuators (if we have any)
        if (!actuator_handlers_.empty()) {
            OpenProviderStreamRequest request;
            auto* provide_req = request.mutable_provide_actuation_request();
            for (const auto& handler : actuator_handlers_) {
                auto* signal_id = provide_req->add_actuator_identifiers();
                signal_id->set_id(handler.signal_id);
                signal_id->set_path(handler.path);
            }
            if (!stream->Write(request)) {
                LOG(ERROR) << "Failed to register actuators";
                provider_sm_->trigger_stream_failed(absl::UnavailableError("Write failed"), true);
                provider_sm_->trigger_stop();
                return;
            }
            LOG(INFO) << "Sent registration for " << actuator_handlers_.size() << " actuator(s)";
        }

        // Step 4: Wait for responses and handle actuation requests
        bool ready = actuator_handlers_.empty();  // If no actuators, we're ready immediately

        // Mark as ready if no registration sent
        if (ready) {
            LOG(INFO) << "Provider stream ready (no actuators registered)";
            provider_sm_->trigger_stream_ready();
        }

        // Read responses
        OpenProviderStreamResponse response;
        while (running_ && stream->Read(&response)) {
            if (response.has_provide_actuation_response()) {
                if (!ready) {
                    LOG(INFO) << "Actuator registration confirmed";
                    ready = true;
                    provider_sm_->trigger_stream_ready();
                }
            } else if (response.has_batch_actuate_stream_request()) {
                handle_actuation_request(response.batch_actuate_stream_request(), stream.get());
            }
        }

        auto grpc_finish_status = stream->Finish();
        if (running_ && grpc_finish_status.error_code() != grpc::StatusCode::CANCELLED) {
            LOG(ERROR) << "Provider stream ended: " << grpc_finish_status.error_message();
            provider_sm_->trigger_stream_ended(absl::UnavailableError(grpc_finish_status.error_message()));
        } else {
            provider_sm_->trigger_stop();
        }

        LOG(INFO) << "Provider stream thread ended";
    }

    void handle_actuation_request(
        const BatchActuateStreamRequest& request,
        grpc::ClientReaderWriter<OpenProviderStreamRequest, OpenProviderStreamResponse>* stream) {

        LOG(INFO) << "Received " << request.actuate_requests_size() << " actuation request(s)";

        for (const auto& actuate_req : request.actuate_requests()) {
            int32_t signal_id = actuate_req.signal_id().id();
            Value target_value = from_proto_value(actuate_req.value());

            // Find handler by signal_id and call it
            bool found = false;
            for (const auto& handler : actuator_handlers_) {
                if (handler.signal_id == signal_id) {
                    // Call handler (handle already captured in closure)
                    handler.handler(target_value);
                    found = true;
                    break;
                }
            }

            if (!found) {
                LOG(WARNING) << "No handler registered for signal ID: " << signal_id;
            }
        }

        // Send response
        if (running_) {
            OpenProviderStreamRequest stream_req;
            auto* response = stream_req.mutable_batch_actuate_stream_response();
            stream->Write(stream_req);
        }
    }

    // ========================================================================
    // Subscriber Stream Thread
    // ========================================================================

    void subscriber_loop() {
        subscriber_sm_->trigger_start();
        LOG(INFO) << "Subscriber stream thread started";

        int retry_attempt = 0;
        const int MAX_RETRY_DELAY_MS = 30000;

        while (running_) {
            if (retry_attempt > 0) {
                subscriber_sm_->trigger_retry();

                // Exponential backoff
                int delay_ms = std::min(100 * (1 << (retry_attempt - 1)), MAX_RETRY_DELAY_MS);
                LOG(INFO) << "Waiting " << delay_ms << "ms before reconnection";

                auto sleep_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);
                while (running_ && std::chrono::steady_clock::now() < sleep_until) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (!running_) break;
            }

            // Wait for channel
            auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
            if (!channel_->WaitForConnected(deadline)) {
                LOG(WARNING) << "Subscriber connection timeout";
                subscriber_sm_->trigger_connect_failed(absl::UnavailableError("Connection timeout"));
                retry_attempt++;
                continue;
            }

            subscriber_sm_->trigger_channel_ready();

            // Create subscription
            subscriber_context_ = std::make_unique<ClientContext>();
            SubscribeByIdRequest request;

            {
                std::lock_guard<std::mutex> lock(subscriptions_mutex_);
                for (const auto& [id, _] : subscriptions_) {
                    request.add_signal_ids(id);
                }
            }

            auto reader = stub_->SubscribeById(subscriber_context_.get(), request);

            // Fetch initial values
            if (!fetch_initial_values()) {
                subscriber_sm_->trigger_stream_failed(
                    absl::FailedPreconditionError("Failed to fetch initial values"), false);
                retry_attempt++;
                continue;
            }

            subscriber_sm_->trigger_stream_ready();

            // Read subscription updates
            SubscribeByIdResponse response;
            bool stream_ok = true;
            while (running_ && stream_ok) {
                stream_ok = reader->Read(&response);
                if (stream_ok) {
                    retry_attempt = 0;
                    for (const auto& [signal_id, datapoint] : response.entries()) {
                        handle_subscription_update(signal_id, datapoint);
                    }
                }
            }

            auto grpc_finish_status = reader->Finish();
            if (!running_) break;

            LOG(WARNING) << "Subscription stream ended: " << grpc_finish_status.error_message();
            subscriber_sm_->trigger_stream_ended(absl::UnavailableError(grpc_finish_status.error_message()));
            retry_attempt++;
        }

        subscriber_sm_->trigger_stop();
        LOG(INFO) << "Subscriber stream thread ended";
    }

    bool fetch_initial_values() {
        std::vector<std::pair<int32_t, Datapoint>> initial_values;

        {
            std::lock_guard<std::mutex> lock(subscriptions_mutex_);
            for (const auto& [signal_id, _] : subscriptions_) {
                auto value = get_current_value(signal_id);
                if (value && value->has_timestamp()) {
                    initial_values.push_back({signal_id, *value});
                }
            }
        }

        for (const auto& [signal_id, datapoint] : initial_values) {
            handle_subscription_update(signal_id, datapoint);
        }

        return true;
    }

    void handle_subscription_update(int32_t signal_id, const Datapoint& datapoint) {
        std::function<void(const std::optional<Value>&)> callback;

        {
            std::lock_guard<std::mutex> lock(subscriptions_mutex_);
            auto it = subscriptions_.find(signal_id);
            if (it != subscriptions_.end()) {
                callback = it->second;
            }
        }

        if (callback) {
            try {
                auto opt_value = datapoint_to_value(datapoint);
                callback(opt_value);
            } catch (const std::exception& e) {
                LOG(ERROR) << "Exception in subscription callback for ID " << signal_id << ": " << e.what();
            }
        }
    }

    std::optional<Datapoint> get_current_value(int32_t signal_id) {
        ClientContext context;
        GetValueRequest request;
        request.mutable_signal_id()->set_id(signal_id);

        GetValueResponse response;
        grpc::Status grpc_status = stub_->GetValue(&context, request, &response);

        if (!grpc_status.ok()) {
            return std::nullopt;
        }

        return response.data_point();
    }

    // ========================================================================
    // Metadata Query
    // ========================================================================

    struct SignalMetadata {
        int32_t id = -1;
        std::optional<ValueType> type;
    };

    SignalMetadata query_signal_metadata(const std::string& path) {
        ClientContext context;
        ListMetadataRequest request;
        request.set_root(path);

        ListMetadataResponse response;
        grpc::Status grpc_status = stub_->ListMetadata(&context, request, &response);

        if (!grpc_status.ok()) {
            LOG(ERROR) << "Failed to query metadata for " << path;
            return {-1, std::nullopt};
        }

        for (const auto& metadata : response.metadata()) {
            if (metadata.path() == path && metadata.id() != 0) {
                auto type = from_proto_datatype(metadata.data_type());
                return {metadata.id(), type};
            }
        }

        return {-1, std::nullopt};
    }

    // ========================================================================
    // Member Variables
    // ========================================================================

    std::string address_;
    std::atomic<bool> running_;

    // gRPC (single channel, two streams)
    std::shared_ptr<Channel> channel_;
    std::unique_ptr<VAL::Stub> stub_;

    // Provider stream
    std::unique_ptr<ClientContext> provider_context_;
    std::thread provider_thread_;
    std::unique_ptr<DatabrokerConnectionStateMachine> provider_sm_;

    // Subscriber stream
    std::unique_ptr<ClientContext> subscriber_context_;
    std::thread subscriber_thread_;
    std::unique_ptr<DatabrokerConnectionStateMachine> subscriber_sm_;

    // Actuators
    struct ActuatorRegistration {
        std::string path;
        int32_t signal_id;       // Already resolved from handle
        vss::types::ValueType type;
        std::function<void(const vss::types::Value&)> handler;  // Handle already captured in closure
    };

    std::vector<ActuatorRegistration> actuator_handlers_;

    // Subscriptions
    mutable std::mutex subscriptions_mutex_;
    std::map<int32_t, std::function<void(const vss::types::DynamicQualifiedValue&)>> subscriptions_;
    std::map<int32_t, std::shared_ptr<DynamicSignalHandle>> id_to_handle_;
};

// ============================================================================
// Factory Method
// ============================================================================

Result<std::unique_ptr<Client>> Client::create(const std::string& databroker_address) {
    auto impl = std::make_unique<VSSClientImpl>(databroker_address);
    impl->initialize_connection();
    LOG(INFO) << "Created unified Client for " << databroker_address;
    return std::unique_ptr<Client>(std::move(impl));
}

} // namespace kuksa
