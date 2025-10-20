/**
 * @file engine_management_state_machine.hpp
 * @brief Engine management state machine wrapper for type-safe state management
 */

#pragma once

#include <kuksa_cpp/state_machine/state_machine.hpp>
#include <glog/logging.h>
#include <functional>
#include <chrono>

/**
 * @brief Engine management states
 */
enum class EngineState {
    STOPPED,                       // Engine not running
    STARTING,                      // Engine start command sent
    RUNNING_FOR_CHARGE,           // Engine running for battery charging
    STOPPING,                      // Engine stop command sent
    _Count
};

/**
 * @brief Convert engine state to string
 */
inline std::string engine_state_name(EngineState state) {
    switch (state) {
        case EngineState::STOPPED:              return "STOPPED";
        case EngineState::STARTING:             return "STARTING";
        case EngineState::RUNNING_FOR_CHARGE:   return "RUNNING_FOR_CHARGE";
        case EngineState::STOPPING:             return "STOPPING";
        default:                                return "UNKNOWN";
    }
}

/**
 * @brief Engine management state machine with type-safe transitions
 *
 * Wraps the generic StateMachine<EngineState> to provide:
 * - Type-safe trigger methods instead of string-based triggers
 * - Encapsulated engine control actions
 * - Runtime tracking for minimum engine runtime enforcement
 * - Observability via state machine logs
 *
 * State flow:
 *   STOPPED --[start_for_charge]--> STARTING
 *   STARTING --[engine_running]--> RUNNING_FOR_CHARGE
 *   RUNNING_FOR_CHARGE --[stop_charging]--> STOPPING
 *   STOPPING --[engine_stopped]--> STOPPED
 */
class EngineManagementStateMachine {
public:
    using EngineController = std::function<void(bool start)>;

    /**
     * @brief Construct engine management state machine
     *
     * @param engine_controller Callback to control engine (true=start, false=stop)
     * @param min_runtime Minimum runtime before allowing engine stop (default: 10 minutes)
     */
    explicit EngineManagementStateMachine(
        EngineController engine_controller,
        std::chrono::minutes min_runtime = std::chrono::minutes(10)
    )
        : engine_controller_(std::move(engine_controller))
        , min_runtime_(min_runtime)
        , engine_started_by_us_(false)
    {
        init_state_machine();
    }

    /**
     * @brief Get current engine state
     */
    EngineState current_state() const {
        return state_machine_->current_state();
    }

    /**
     * @brief Check if engine is running for charge
     */
    bool is_running_for_charge() const {
        return state_machine_->current_state() == EngineState::RUNNING_FOR_CHARGE;
    }

    /**
     * @brief Check if engine is stopped
     */
    bool is_stopped() const {
        return state_machine_->current_state() == EngineState::STOPPED;
    }

    /**
     * @brief Check if engine was started by this system
     */
    bool started_by_us() const {
        return engine_started_by_us_;
    }

    /**
     * @brief Check if engine has met minimum runtime requirement
     */
    bool has_met_minimum_runtime() const {
        if (!engine_started_by_us_) {
            return false;
        }
        auto runtime = std::chrono::steady_clock::now() - engine_start_time_;
        return runtime >= min_runtime_;
    }

    /**
     * @brief Get remaining time until minimum runtime is met
     */
    std::chrono::seconds remaining_runtime() const {
        if (!engine_started_by_us_) {
            return std::chrono::seconds(0);
        }
        auto runtime = std::chrono::steady_clock::now() - engine_start_time_;
        if (runtime >= min_runtime_) {
            return std::chrono::seconds(0);
        }
        return std::chrono::duration_cast<std::chrono::seconds>(min_runtime_ - runtime);
    }

    // ========================================================================
    // Type-safe state transition triggers
    // ========================================================================

    /**
     * @brief Trigger engine start for charging
     *
     * Transitions:
     * - STOPPED -> STARTING
     */
    void trigger_start_for_charge() {
        state_machine_->trigger("start_for_charge");
    }

    /**
     * @brief Trigger engine running event
     *
     * Transitions:
     * - STARTING -> RUNNING_FOR_CHARGE
     */
    void trigger_engine_running() {
        state_machine_->trigger("engine_running");
    }

    /**
     * @brief Trigger stop charging command
     *
     * Transitions:
     * - RUNNING_FOR_CHARGE -> STOPPING
     */
    void trigger_stop_charging() {
        state_machine_->trigger("stop_charging");
    }

    /**
     * @brief Trigger engine stopped event
     *
     * Transitions:
     * - STOPPING -> STOPPED
     */
    void trigger_engine_stopped() {
        state_machine_->trigger("engine_stopped");
    }

    /**
     * @brief Force stop engine (emergency use only)
     *
     * Directly commands engine stop regardless of minimum runtime.
     * Use only for emergency situations (e.g., critical signal loss).
     */
    void force_stop() {
        LOG(WARNING) << "Engine: FORCE STOP requested";
        engine_controller_(false);
        engine_started_by_us_ = false;
    }

private:
    void init_state_machine() {
        state_machine_ = std::make_unique<sdv::StateMachine<EngineState>>(
            "EngineManagement", EngineState::STOPPED
        );

        // Set state name function for observability
        state_machine_->set_state_name_function(engine_state_name);

        // Define state entry actions
        state_machine_->define_state(EngineState::STOPPED)
            .on_entry([this]() {
                LOG(INFO) << "Engine: Stopped";
                engine_started_by_us_ = false;
            });

        state_machine_->define_state(EngineState::STARTING)
            .on_entry([this]() {
                LOG(INFO) << "Engine: Starting without intention to drive...";
                engine_controller_(true);  // Send start command
            });

        state_machine_->define_state(EngineState::RUNNING_FOR_CHARGE)
            .on_entry([this]() {
                LOG(INFO) << "Engine: Running for battery charging";
                engine_started_by_us_ = true;
                engine_start_time_ = std::chrono::steady_clock::now();
            });

        state_machine_->define_state(EngineState::STOPPING)
            .on_entry([this]() {
                LOG(INFO) << "Engine: Stopping stationary engine...";
                engine_controller_(false);  // Send stop command
            });

        // Define transitions
        state_machine_->add_transition(
            EngineState::STOPPED,
            EngineState::STARTING,
            "start_for_charge"
        );

        state_machine_->add_transition(
            EngineState::STARTING,
            EngineState::RUNNING_FOR_CHARGE,
            "engine_running"
        );

        state_machine_->add_transition(
            EngineState::RUNNING_FOR_CHARGE,
            EngineState::STOPPING,
            "stop_charging"
        );

        state_machine_->add_transition(
            EngineState::STOPPING,
            EngineState::STOPPED,
            "engine_stopped"
        );
    }

    std::unique_ptr<sdv::StateMachine<EngineState>> state_machine_;
    EngineController engine_controller_;
    std::chrono::minutes min_runtime_;

    bool engine_started_by_us_;
    std::chrono::steady_clock::time_point engine_start_time_;
};
