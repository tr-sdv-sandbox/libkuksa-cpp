/**
 * @file climate_control.hpp
 * @brief Climate Protection System - Observer/Protector Pattern
 *
 * Monitors and protects vehicle from battery/fuel exhaustion during climate control operation.
 * Uses VSS 5.1 signals and libkuksa-cpp SDK.
 */

#pragma once

#include <kuksa_cpp/kuksa.hpp>
#include <kuksa_cpp/state_machine/state_machine.hpp>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstdlib>
#include <glog/logging.h>

// Climate protection states
enum class ProtectionState {
    MONITORING,                    // Normal monitoring, no intervention
    BATTERY_LOW_ENGINE_START,      // Battery low, starting engine to charge
    ENGINE_CHARGING,               // Engine running for battery charging
    FUEL_LOW_HVAC_SHUTDOWN,       // Fuel critically low, shutting down HVAC
    EMERGENCY_SHUTDOWN,            // Emergency: both battery and fuel critical
    _Count
};

// Engine management states
enum class EngineState {
    STOPPED,                       // Engine not running
    STARTING,                      // Engine start command sent
    RUNNING_FOR_CHARGE,           // Engine running for battery charging
    STOPPING,                      // Engine stop command sent
    _Count
};

// State name functions for observability
std::string protection_state_name(ProtectionState state);
std::string engine_state_name(EngineState state);

class ClimateProtectionSystem {
public:
    ClimateProtectionSystem(const std::string& kuksa_url);
    bool connect();
    void run();
    void stop();

private:
    // Setup methods
    void setup_states();
    void setup_transitions();
    void subscribe_to_signals();
    void read_configuration();

    // Signal handlers
    void handle_battery_voltage_change(float voltage);
    void handle_fuel_level_change(float level);
    void handle_hvac_state_change(bool is_active);
    void handle_engine_state_change(bool is_running);
    void handle_coolant_temp_change(float temp);
    void handle_ambient_temp_change(float temp);

    // Protection logic
    void check_battery_protection();
    void check_fuel_protection();
    void check_smart_ventilation();

    // Engine management
    void start_engine_for_charging();
    void stop_engine_after_charging();
    bool should_stop_engine();

    // State machines (with observability)
    sdv::StateMachine<ProtectionState> protection_sm_;
    sdv::StateMachine<EngineState> engine_sm_;

    // KUKSA components
    std::string kuksa_url_;
    std::shared_ptr<kuksa::Resolver> resolver_;
    std::shared_ptr<kuksa::Client> client_;

    // VSS 5.1 Signal handles (inputs - monitoring)
    kuksa::SignalHandle<float> battery_voltage_;           // Vehicle.LowVoltageBattery.CurrentVoltage
    kuksa::SignalHandle<float> fuel_level_;                // Vehicle.OBD.FuelLevel
    kuksa::SignalHandle<bool> hvac_is_active_;             // Vehicle.Cabin.HVAC.IsAirConditioningActive (read + write)
    kuksa::SignalHandle<bool> engine_is_running_;          // Vehicle.Powertrain.CombustionEngine.IsRunning
    kuksa::SignalHandle<float> coolant_temp_;              // Vehicle.OBD.CoolantTemperature
    kuksa::SignalHandle<float> ambient_temp_;              // Vehicle.Cabin.HVAC.AmbientAirTemperature
    kuksa::SignalHandle<float> cabin_temp_;                // Vehicle.Cabin.HVAC.Station.Row1.Driver.Temperature

    // VSS 5.1 Signal handles (outputs - protection actions)
    // NOTE: VSS specifies uint8 for Window.Position, but KUKSA uses uint32 (see TYPE_SYSTEM_ANALYSIS.md)
    kuksa::SignalHandle<uint32_t> window_position_;        // Vehicle.Cabin.Door.Row1.DriverSide.Window.Position
    kuksa::SignalHandle<std::string> sunroof_switch_;      // Vehicle.Cabin.Sunroof.Switch

    // Custom signals (vss_extensions.json)
    kuksa::SignalHandle<bool> engine_start_stationary_;    // Vehicle.Private.Engine.IsStartWithoutIntentionToDrive
    kuksa::SignalHandle<float> min_battery_voltage_;       // Vehicle.Private.HVAC.MinimumBatteryVoltageForHVAC
    kuksa::SignalHandle<float> min_fuel_level_;            // Vehicle.Private.HVAC.MinimumFuelLevelForHVAC

    // Application state
    std::atomic<bool> running_;

    // Current sensor values
    float current_battery_voltage_ = 24.0f;
    float current_fuel_level_ = 100.0f;
    float current_coolant_temp_ = 20.0f;
    float current_ambient_temp_ = 20.0f;
    bool current_hvac_active_ = false;
    bool current_engine_running_ = false;

    // Configuration thresholds (24V system)
    float min_battery_voltage_threshold_ = 23.6f;   // Critical voltage
    float safe_battery_voltage_ = 24.8f;            // Safe/recovered voltage
    float min_fuel_level_threshold_ = 10.0f;        // Critical fuel level (%)

    // Engine management
    bool engine_started_by_us_ = false;
    std::chrono::steady_clock::time_point engine_start_time_;
    const std::chrono::minutes min_engine_runtime_{10};  // Minimum 10 minutes
};
