/**
 * @file connection_state_machine.hpp
 * @brief Reusable connection state machine for databroker clients
 */

#pragma once

#include <kuksa_cpp/state_machine/state_machine.hpp>
#include <absl/status/status.h>
#include <string>
#include <mutex>
#include <thread>
#include <chrono>
#include <glog/logging.h>

namespace kuksa {

/**
 * @brief Connection states for databroker clients (subscriber, provider, accessor)
 */
enum class ConnectionState {
    DISCONNECTED,    // Not started or stopped
    CONNECTING,      // Establishing gRPC channel to databroker
    ESTABLISHING,    // Channel ready, setting up stream/subscription
    ACTIVE,          // Stream active and operational
    FAILED           // Connection or stream failed (will retry)
};

/**
 * @brief Convert connection state to string
 */
inline std::string connection_state_name(ConnectionState state) {
    switch (state) {
        case ConnectionState::DISCONNECTED:  return "DISCONNECTED";
        case ConnectionState::CONNECTING:    return "CONNECTING";
        case ConnectionState::ESTABLISHING:  return "ESTABLISHING";
        case ConnectionState::ACTIVE:        return "ACTIVE";
        case ConnectionState::FAILED:        return "FAILED";
        default:                             return "UNKNOWN";
    }
}

/**
 * @brief Reusable connection state machine for databroker clients
 *
 * This class encapsulates the connection lifecycle for Client, providing:
 * - Structured state transitions
 * - Error tracking
 * - Observability via state machine logs
 * - Thread-safe status queries
 *
 * State flow:
 *   DISCONNECTED --[start]--> CONNECTING
 *   CONNECTING --[channel_ready]--> ESTABLISHING
 *   CONNECTING --[connect_failed]--> FAILED
 *   ESTABLISHING --[stream_ready]--> ACTIVE
 *   ESTABLISHING --[stream_failed]--> FAILED
 *   ACTIVE --[stream_ended]--> FAILED
 *   FAILED --[retry]--> CONNECTING
 *   * --[stop]--> DISCONNECTED
 */
class DatabrokerConnectionStateMachine {
public:
    /**
     * @brief Construct connection state machine
     *
     * @param client_name Name for logging (e.g., "Client")
     * @param establishing_name Custom name for ESTABLISHING state (e.g., "SUBSCRIBING", "REGISTERING")
     * @param active_name Custom name for ACTIVE state (e.g., "STREAMING", "SERVING")
     */
    explicit DatabrokerConnectionStateMachine(
        std::string client_name,
        std::string establishing_name = "ESTABLISHING",
        std::string active_name = "ACTIVE"
    )
        : client_name_(std::move(client_name))
        , establishing_name_(std::move(establishing_name))
        , active_name_(std::move(active_name))
        , last_error_(absl::OkStatus())
        , is_connection_error_(false)
    {
        init_state_machine();
    }

    /**
     * @brief Get current connection state
     * @return Current state (thread-safe)
     */
    ConnectionState current_state() const {
        return state_machine_->current_state();
    }

    /**
     * @brief Get operational status based on current state
     *
     * @return Status indicating operational readiness:
     *   - OkStatus(): ACTIVE state, fully operational
     *   - UnavailableError(): CONNECTING or ESTABLISHING (in progress)
     *   - FailedPreconditionError(): DISCONNECTED (not started)
     *   - Actual error: FAILED state with details
     */
    Status status() const {
        auto state = state_machine_->current_state();

        std::lock_guard<std::mutex> lock(error_mutex_);

        switch (state) {
            case ConnectionState::DISCONNECTED:
                return absl::FailedPreconditionError(client_name_ + " not started");

            case ConnectionState::CONNECTING:
                return absl::UnavailableError("Connecting to databroker...");

            case ConnectionState::ESTABLISHING:
                return absl::UnavailableError(establishing_name_ + " in progress...");

            case ConnectionState::ACTIVE:
                return absl::OkStatus();

            case ConnectionState::FAILED:
                // Return the recorded error
                return last_error_;

            default:
                return absl::UnknownError("Unknown state");
        }
    }

    /**
     * @brief Check if in ACTIVE state
     */
    bool is_active() const {
        return state_machine_->current_state() == ConnectionState::ACTIVE;
    }

    /**
     * @brief Wait for ACTIVE state with timeout
     *
     * Blocks until the state machine reaches ACTIVE state or timeout occurs.
     *
     * @param timeout Maximum time to wait
     * @return Status:
     *   - OkStatus(): Reached ACTIVE state
     *   - DeadlineExceededError(): Timeout
     *   - Other: Last recorded error if in FAILED state
     */
    Status wait_until_active(std::chrono::milliseconds timeout) {
        auto deadline = std::chrono::steady_clock::now() + timeout;

        while (std::chrono::steady_clock::now() < deadline) {
            auto state = state_machine_->current_state();

            if (state == ConnectionState::ACTIVE) {
                return absl::OkStatus();
            }

            if (state == ConnectionState::FAILED) {
                // Return the recorded error
                std::lock_guard<std::mutex> lock(error_mutex_);
                return last_error_;
            }

            // Still connecting/establishing - wait a bit
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Timeout
        return absl::DeadlineExceededError(
            "Timeout waiting for " + client_name_ + " to become active"
        );
    }

    // ========================================================================
    // State transition triggers (called from client implementation)
    // ========================================================================

    void trigger_start() {
        state_machine_->trigger("start");
    }

    void trigger_channel_ready() {
        state_machine_->trigger("channel_ready");
    }

    void trigger_connect_failed(const absl::Status& error) {
        record_error(error, true);
        state_machine_->trigger("connect_failed");
    }

    void trigger_stream_ready() {
        // Clear error on success
        {
            std::lock_guard<std::mutex> lock(error_mutex_);
            last_error_ = absl::OkStatus();
        }
        state_machine_->trigger("stream_ready");
    }

    void trigger_stream_failed(const absl::Status& error, bool is_connection_error = false) {
        record_error(error, is_connection_error);
        state_machine_->trigger("stream_failed");
    }

    void trigger_stream_ended(const absl::Status& error) {
        record_error(error, true);
        state_machine_->trigger("stream_ended");
    }

    void trigger_retry() {
        state_machine_->trigger("retry");
    }

    void trigger_stop() {
        state_machine_->trigger("stop");
    }

private:
    void init_state_machine() {
        state_machine_ = std::make_unique<sdv::StateMachine<ConnectionState>>(
            client_name_, ConnectionState::DISCONNECTED
        );

        // Set custom state names
        state_machine_->set_state_name_function([this](ConnectionState state) {
            switch (state) {
                case ConnectionState::DISCONNECTED:  return std::string("DISCONNECTED");
                case ConnectionState::CONNECTING:    return std::string("CONNECTING");
                case ConnectionState::ESTABLISHING:  return establishing_name_;
                case ConnectionState::ACTIVE:        return active_name_;
                case ConnectionState::FAILED:        return std::string("FAILED");
                default:                             return std::string("UNKNOWN");
            }
        });

        // Define state entry actions
        state_machine_->define_state(ConnectionState::CONNECTING)
            .on_entry([this]() {
                LOG(INFO) << "[" << client_name_ << "] Attempting connection to databroker";
            });

        state_machine_->define_state(ConnectionState::ESTABLISHING)
            .on_entry([this]() {
                LOG(INFO) << "[" << client_name_ << "] " << establishing_name_ << " stream";
            });

        state_machine_->define_state(ConnectionState::ACTIVE)
            .on_entry([this]() {
                LOG(INFO) << "[" << client_name_ << "] " << active_name_ << " - fully operational";
            });

        state_machine_->define_state(ConnectionState::FAILED)
            .on_entry([this]() {
                std::lock_guard<std::mutex> lock(error_mutex_);
                LOG(WARNING) << "[" << client_name_ << "] Failed: " << last_error_;
            });

        // Define transitions
        state_machine_->add_transition(
            ConnectionState::DISCONNECTED,
            ConnectionState::CONNECTING,
            "start"
        );

        state_machine_->add_transition(
            ConnectionState::CONNECTING,
            ConnectionState::ESTABLISHING,
            "channel_ready"
        );

        state_machine_->add_transition(
            ConnectionState::CONNECTING,
            ConnectionState::FAILED,
            "connect_failed"
        );

        state_machine_->add_transition(
            ConnectionState::ESTABLISHING,
            ConnectionState::ACTIVE,
            "stream_ready"
        );

        state_machine_->add_transition(
            ConnectionState::ESTABLISHING,
            ConnectionState::FAILED,
            "stream_failed"
        );

        state_machine_->add_transition(
            ConnectionState::ACTIVE,
            ConnectionState::FAILED,
            "stream_ended"
        );

        state_machine_->add_transition(
            ConnectionState::FAILED,
            ConnectionState::CONNECTING,
            "retry"
        );

        // Stop from any state
        for (auto state : {ConnectionState::CONNECTING, ConnectionState::ESTABLISHING,
                          ConnectionState::ACTIVE, ConnectionState::FAILED}) {
            state_machine_->add_transition(state, ConnectionState::DISCONNECTED, "stop");
        }
    }

    void record_error(const absl::Status& error, bool is_connection_error) {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_ = error;
        is_connection_error_ = is_connection_error;
    }

    std::string client_name_;
    std::string establishing_name_;
    std::string active_name_;

    std::unique_ptr<sdv::StateMachine<ConnectionState>> state_machine_;

    mutable std::mutex error_mutex_;
    Status last_error_;
    bool is_connection_error_;  // true = connection error, false = stream/subscription error
};

} // namespace kuksa
