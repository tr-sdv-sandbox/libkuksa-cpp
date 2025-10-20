/**
 * @file climate_protection_state_machine.hpp
 * @brief Climate protection state machine wrapper for type-safe state management
 */

#pragma once

#include <kuksa_cpp/state_machine/state_machine.hpp>
#include <kuksa_cpp/kuksa.hpp>
#include <glog/logging.h>
#include <functional>

/**
 * @brief Protection states for climate control system
 */
enum class ProtectionState {
    MONITORING,                    // Normal monitoring, no intervention
    BATTERY_LOW_ENGINE_START,      // Battery low, starting engine to charge
    ENGINE_CHARGING,               // Engine running for battery charging
    FUEL_LOW_HVAC_SHUTDOWN,       // Fuel critically low, shutting down HVAC
    EMERGENCY_SHUTDOWN,            // Emergency: both battery and fuel critical
    _Count
};

/**
 * @brief Convert protection state to string
 */
inline std::string protection_state_name(ProtectionState state) {
    switch (state) {
        case ProtectionState::MONITORING:                return "MONITORING";
        case ProtectionState::BATTERY_LOW_ENGINE_START:  return "BATTERY_LOW_ENGINE_START";
        case ProtectionState::ENGINE_CHARGING:           return "ENGINE_CHARGING";
        case ProtectionState::FUEL_LOW_HVAC_SHUTDOWN:   return "FUEL_LOW_HVAC_SHUTDOWN";
        case ProtectionState::EMERGENCY_SHUTDOWN:        return "EMERGENCY_SHUTDOWN";
        default:                                         return "UNKNOWN";
    }
}

/**
 * @brief Climate protection state machine with type-safe transitions
 *
 * Wraps the generic StateMachine<ProtectionState> to provide:
 * - Type-safe trigger methods instead of string-based triggers
 * - Encapsulated HVAC control actions
 * - Observability via state machine logs
 *
 * State flow:
 *   MONITORING --[battery_critical]--> BATTERY_LOW_ENGINE_START
 *   BATTERY_LOW_ENGINE_START --[engine_started]--> ENGINE_CHARGING
 *   ENGINE_CHARGING --[battery_recovered]--> MONITORING
 *   * --[fuel_critical]--> FUEL_LOW_HVAC_SHUTDOWN
 *   FUEL_LOW_HVAC_SHUTDOWN --[battery_critical]--> EMERGENCY_SHUTDOWN
 *   FUEL_LOW_HVAC_SHUTDOWN --[fuel_recovered]--> MONITORING
 */
class ClimateProtectionStateMachine {
public:
    using HvacController = std::function<void(bool active)>;
    using EngineStarter = std::function<void()>;

    /**
     * @brief Construct climate protection state machine
     *
     * @param hvac_controller Callback to control HVAC (true=enable, false=disable)
     * @param engine_starter Callback to start engine for charging
     */
    explicit ClimateProtectionStateMachine(
        HvacController hvac_controller,
        EngineStarter engine_starter
    )
        : hvac_controller_(std::move(hvac_controller))
        , engine_starter_(std::move(engine_starter))
    {
        init_state_machine();
    }

    /**
     * @brief Get current protection state
     */
    ProtectionState current_state() const {
        return state_machine_->current_state();
    }

    /**
     * @brief Check if in normal monitoring state
     */
    bool is_monitoring() const {
        return state_machine_->current_state() == ProtectionState::MONITORING;
    }

    /**
     * @brief Check if engine charging is active
     */
    bool is_engine_charging() const {
        return state_machine_->current_state() == ProtectionState::ENGINE_CHARGING;
    }

    /**
     * @brief Check if in emergency state
     */
    bool is_emergency() const {
        auto state = state_machine_->current_state();
        return state == ProtectionState::EMERGENCY_SHUTDOWN ||
               state == ProtectionState::FUEL_LOW_HVAC_SHUTDOWN;
    }

    // ========================================================================
    // Type-safe state transition triggers
    // ========================================================================

    /**
     * @brief Trigger battery critical condition
     *
     * Transitions:
     * - MONITORING -> BATTERY_LOW_ENGINE_START
     * - FUEL_LOW_HVAC_SHUTDOWN -> EMERGENCY_SHUTDOWN
     */
    void trigger_battery_critical() {
        state_machine_->trigger("battery_critical");
    }

    /**
     * @brief Trigger battery recovered condition
     *
     * Transitions:
     * - ENGINE_CHARGING -> MONITORING
     */
    void trigger_battery_recovered() {
        state_machine_->trigger("battery_recovered");
    }

    /**
     * @brief Trigger engine started event
     *
     * Transitions:
     * - BATTERY_LOW_ENGINE_START -> ENGINE_CHARGING
     */
    void trigger_engine_started() {
        state_machine_->trigger("engine_started");
    }

    /**
     * @brief Trigger fuel critical condition
     *
     * Transitions:
     * - MONITORING -> FUEL_LOW_HVAC_SHUTDOWN
     * - ENGINE_CHARGING -> FUEL_LOW_HVAC_SHUTDOWN
     */
    void trigger_fuel_critical() {
        state_machine_->trigger("fuel_critical");
    }

    /**
     * @brief Trigger fuel recovered condition
     *
     * Transitions:
     * - FUEL_LOW_HVAC_SHUTDOWN -> MONITORING
     */
    void trigger_fuel_recovered() {
        state_machine_->trigger("fuel_recovered");
    }

private:
    void init_state_machine() {
        state_machine_ = std::make_unique<sdv::StateMachine<ProtectionState>>(
            "ClimateProtection", ProtectionState::MONITORING
        );

        // Set state name function for observability
        state_machine_->set_state_name_function(protection_state_name);

        // Define state entry actions
        state_machine_->define_state(ProtectionState::MONITORING)
            .on_entry([]() {
                LOG(INFO) << "Protection: Normal monitoring mode";
            });

        state_machine_->define_state(ProtectionState::BATTERY_LOW_ENGINE_START)
            .on_entry([this]() {
                LOG(WARNING) << "Protection: Battery low, attempting to start engine";
                engine_starter_();
            });

        state_machine_->define_state(ProtectionState::ENGINE_CHARGING)
            .on_entry([]() {
                LOG(INFO) << "Protection: Engine running for battery charging";
            });

        state_machine_->define_state(ProtectionState::FUEL_LOW_HVAC_SHUTDOWN)
            .on_entry([this]() {
                LOG(WARNING) << "Protection: Fuel critically low, shutting down HVAC";
                hvac_controller_(false);  // Emergency shutoff HVAC
            });

        state_machine_->define_state(ProtectionState::EMERGENCY_SHUTDOWN)
            .on_entry([this]() {
                LOG(ERROR) << "Protection: EMERGENCY - Both battery and fuel critical!";
                hvac_controller_(false);  // Emergency shutoff everything
            });

        // Define transitions
        state_machine_->add_transition(
            ProtectionState::MONITORING,
            ProtectionState::BATTERY_LOW_ENGINE_START,
            "battery_critical"
        );

        state_machine_->add_transition(
            ProtectionState::BATTERY_LOW_ENGINE_START,
            ProtectionState::ENGINE_CHARGING,
            "engine_started"
        );

        state_machine_->add_transition(
            ProtectionState::ENGINE_CHARGING,
            ProtectionState::MONITORING,
            "battery_recovered"
        );

        state_machine_->add_transition(
            ProtectionState::MONITORING,
            ProtectionState::FUEL_LOW_HVAC_SHUTDOWN,
            "fuel_critical"
        );

        state_machine_->add_transition(
            ProtectionState::ENGINE_CHARGING,
            ProtectionState::FUEL_LOW_HVAC_SHUTDOWN,
            "fuel_critical"
        );

        state_machine_->add_transition(
            ProtectionState::FUEL_LOW_HVAC_SHUTDOWN,
            ProtectionState::EMERGENCY_SHUTDOWN,
            "battery_critical"
        );

        state_machine_->add_transition(
            ProtectionState::FUEL_LOW_HVAC_SHUTDOWN,
            ProtectionState::MONITORING,
            "fuel_recovered"
        );
    }

    std::unique_ptr<sdv::StateMachine<ProtectionState>> state_machine_;
    HvacController hvac_controller_;
    EngineStarter engine_starter_;
};
