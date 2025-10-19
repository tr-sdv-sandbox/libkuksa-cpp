/**
 * @file climate_control.cpp
 * @brief Climate Protection System - Observer/Protector Pattern
 *
 * Monitors and protects vehicle from battery/fuel exhaustion during climate control operation.
 * Uses VSS 5.1 signals and libkuksa-cpp SDK.
 */

#include "climate_control.hpp"

// State name functions for observability
std::string protection_state_name(ProtectionState state) {
    switch (state) {
        case ProtectionState::MONITORING:                return "MONITORING";
        case ProtectionState::BATTERY_LOW_ENGINE_START:  return "BATTERY_LOW_ENGINE_START";
        case ProtectionState::ENGINE_CHARGING:           return "ENGINE_CHARGING";
        case ProtectionState::FUEL_LOW_HVAC_SHUTDOWN:   return "FUEL_LOW_HVAC_SHUTDOWN";
        case ProtectionState::EMERGENCY_SHUTDOWN:        return "EMERGENCY_SHUTDOWN";
        default:                                         return "UNKNOWN";
    }
}

std::string engine_state_name(EngineState state) {
    switch (state) {
        case EngineState::STOPPED:              return "STOPPED";
        case EngineState::STARTING:             return "STARTING";
        case EngineState::RUNNING_FOR_CHARGE:   return "RUNNING_FOR_CHARGE";
        case EngineState::STOPPING:             return "STOPPING";
        default:                                return "UNKNOWN";
    }
}

ClimateProtectionSystem::ClimateProtectionSystem(const std::string& kuksa_url)
        : protection_sm_("ClimateProtection", ProtectionState::MONITORING),
          engine_sm_("EngineManagement", EngineState::STOPPED),
          kuksa_url_(kuksa_url),
          running_(true) {

    // Set state name functions for observability
    protection_sm_.set_state_name_function(protection_state_name);
    engine_sm_.set_state_name_function(engine_state_name);

    setup_states();
    setup_transitions();
}

bool ClimateProtectionSystem::connect() {
    LOG(INFO) << "=== Climate Protection System ===";
    LOG(INFO) << "Connecting to KUKSA databroker at " << kuksa_url_;

    // Create resolver to get signal handles
    auto resolver_result = kuksa::Resolver::create(kuksa_url_);
    if (!resolver_result.ok()) {
        LOG(ERROR) << "Failed to create resolver: " << resolver_result.status();
        return false;
    }
    resolver_ = std::move(*resolver_result);
    LOG(INFO) << "Resolver created successfully";

    // Batch resolve all signal handles using fluent API
    auto status = resolver_->signals()
        // Monitoring inputs (VSS 5.1)
        .add(battery_voltage_, "Vehicle.LowVoltageBattery.CurrentVoltage")
        .add(fuel_level_, "Vehicle.OBD.FuelLevel")
        .add(hvac_is_active_, "Vehicle.Cabin.HVAC.IsAirConditioningActive")
        .add(engine_is_running_, "Vehicle.Powertrain.CombustionEngine.IsRunning")
        .add(coolant_temp_, "Vehicle.OBD.CoolantTemperature")
        .add(ambient_temp_, "Vehicle.Cabin.HVAC.AmbientAirTemperature")
        .add(cabin_temp_, "Vehicle.Cabin.HVAC.Station.Row1.Driver.Temperature")
        // Protection action outputs (VSS 5.1)
        .add(window_position_, "Vehicle.Cabin.Door.Row1.DriverSide.Window.Position")  // uint8 with transparent type mapping!
        .add(sunroof_switch_, "Vehicle.Cabin.Sunroof.Switch")
        // Custom signals (vss_extensions.json)
        .add(engine_start_stationary_, "Vehicle.Private.Engine.IsStartWithoutIntentionToDrive")
        .add(min_battery_voltage_, "Vehicle.Private.HVAC.MinimumBatteryVoltageForHVAC")
        .add(min_fuel_level_, "Vehicle.Private.HVAC.MinimumFuelLevelForHVAC")
        .resolve();

    if (!status.ok()) {
        LOG(ERROR) << "Failed to resolve signals:\n" << status;
        return false;
    }
    LOG(INFO) << "All signal handles resolved successfully";

    // Create client
    auto client_result = kuksa::Client::create(kuksa_url_);
    if (!client_result.ok()) {
        LOG(ERROR) << "Failed to create client: " << client_result.status();
        return false;
    }
    client_ = std::shared_ptr<kuksa::Client>(std::move(*client_result));
    LOG(INFO) << "Client created successfully";

    // Subscribe to signals BEFORE starting client
    subscribe_to_signals();

    // Start client (starts background threads for streams)
    auto start_status = client_->start();
    if (!start_status.ok()) {
        LOG(ERROR) << "Failed to start client: " << start_status;
        return false;
    }

    // Wait for client to be ready
    auto ready_status = client_->wait_until_ready(std::chrono::milliseconds(5000));
    if (!ready_status.ok()) {
        LOG(ERROR) << "Client not ready: " << ready_status;
        return false;
    }
    LOG(INFO) << "Client is ready";

    // Read configuration attributes
    read_configuration();

    return true;
}

void ClimateProtectionSystem::read_configuration() {
    LOG(INFO) << "Reading configuration attributes...";

    // Read minimum battery voltage threshold
    auto min_voltage = client_->get(min_battery_voltage_);
    if (min_voltage.ok() && min_voltage->is_valid()) {
        min_battery_voltage_threshold_ = min_voltage->value.value();
        LOG(INFO) << "Minimum battery voltage threshold: " << min_battery_voltage_threshold_ << "V";
    } else {
        LOG(WARNING) << "Could not read MinimumBatteryVoltageForHVAC, using default: "
                     << min_battery_voltage_threshold_ << "V";
    }

    // Read minimum fuel level threshold
    auto min_fuel = client_->get(min_fuel_level_);
    if (min_fuel.ok() && min_fuel->is_valid()) {
        min_fuel_level_threshold_ = min_fuel->value.value();
        LOG(INFO) << "Minimum fuel level threshold: " << min_fuel_level_threshold_ << "%";
    } else {
        LOG(WARNING) << "Could not read MinimumFuelLevelForHVAC, using default: "
                     << min_fuel_level_threshold_ << "%";
    }
}

void ClimateProtectionSystem::run() {
    if (!connect()) {
        LOG(ERROR) << "Failed to connect to KUKSA";
        return;
    }

    LOG(INFO) << "Starting climate protection monitoring...";
    LOG(INFO) << "Configuration:";
    LOG(INFO) << "  - Battery critical: < " << min_battery_voltage_threshold_ << "V";
    LOG(INFO) << "  - Battery safe: > " << safe_battery_voltage_ << "V";
    LOG(INFO) << "  - Fuel critical: < " << min_fuel_level_threshold_ << "%";
    LOG(INFO) << "  - Min engine runtime: " << min_engine_runtime_.count() << " minutes";

    // Main monitoring loop
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Periodic protection checks
        check_battery_protection();
        check_fuel_protection();
        check_smart_ventilation();
    }

    LOG(INFO) << "Climate protection system stopped";
}

void ClimateProtectionSystem::stop() {
    running_ = false;
}

void ClimateProtectionSystem::setup_states() {
    // Protection state machine states
    protection_sm_.define_state(ProtectionState::MONITORING)
        .on_entry([this]() {
            LOG(INFO) << "Protection: Normal monitoring mode";
        });

    protection_sm_.define_state(ProtectionState::BATTERY_LOW_ENGINE_START)
        .on_entry([this]() {
            LOG(WARNING) << "Protection: Battery low, attempting to start engine";
            start_engine_for_charging();
        });

    protection_sm_.define_state(ProtectionState::ENGINE_CHARGING)
        .on_entry([this]() {
            LOG(INFO) << "Protection: Engine running for battery charging";
        });

    protection_sm_.define_state(ProtectionState::FUEL_LOW_HVAC_SHUTDOWN)
        .on_entry([this]() {
            LOG(WARNING) << "Protection: Fuel critically low, shutting down HVAC";
            // Emergency shutoff HVAC
            auto status = client_->set(hvac_is_active_, false);
            if (!status.ok()) {
                LOG(ERROR) << "Failed to shut down HVAC: " << status;
            }
        });

    protection_sm_.define_state(ProtectionState::EMERGENCY_SHUTDOWN)
        .on_entry([this]() {
            LOG(ERROR) << "Protection: EMERGENCY - Both battery and fuel critical!";
            // Emergency shutoff everything
            auto status = client_->set(hvac_is_active_, false);
            if (!status.ok()) {
                LOG(ERROR) << "Failed to emergency shutdown HVAC: " << status;
            }
        });

    // Engine state machine states
    engine_sm_.define_state(EngineState::STOPPED)
        .on_entry([this]() {
            LOG(INFO) << "Engine: Stopped";
            engine_started_by_us_ = false;
        });

    engine_sm_.define_state(EngineState::STARTING)
        .on_entry([this]() {
            LOG(INFO) << "Engine: Starting without intention to drive...";
            auto status = client_->set(engine_start_stationary_, true);
            if (!status.ok()) {
                LOG(ERROR) << "Failed to send engine start command: " << status;
            }
        });

    engine_sm_.define_state(EngineState::RUNNING_FOR_CHARGE)
        .on_entry([this]() {
            LOG(INFO) << "Engine: Running for battery charging";
            engine_started_by_us_ = true;
            engine_start_time_ = std::chrono::steady_clock::now();
        });

    engine_sm_.define_state(EngineState::STOPPING)
        .on_entry([this]() {
            LOG(INFO) << "Engine: Stopping stationary engine...";
            auto status = client_->set(engine_start_stationary_, false);
            if (!status.ok()) {
                LOG(ERROR) << "Failed to send engine stop command: " << status;
            }
        });
}

void ClimateProtectionSystem::setup_transitions() {
    // ========== Protection State Machine Transitions ==========

    // Normal monitoring -> Battery low
    protection_sm_.add_transition(
        ProtectionState::MONITORING,
        ProtectionState::BATTERY_LOW_ENGINE_START,
        "battery_critical"
    );

    // Battery low -> Engine charging (when engine starts)
    protection_sm_.add_transition(
        ProtectionState::BATTERY_LOW_ENGINE_START,
        ProtectionState::ENGINE_CHARGING,
        "engine_started"
    );

    // Engine charging -> Normal monitoring (when battery recovered)
    protection_sm_.add_transition(
        ProtectionState::ENGINE_CHARGING,
        ProtectionState::MONITORING,
        "battery_recovered"
    );

    // Any state -> Fuel low shutdown
    protection_sm_.add_transition(
        ProtectionState::MONITORING,
        ProtectionState::FUEL_LOW_HVAC_SHUTDOWN,
        "fuel_critical"
    );

    protection_sm_.add_transition(
        ProtectionState::ENGINE_CHARGING,
        ProtectionState::FUEL_LOW_HVAC_SHUTDOWN,
        "fuel_critical"
    );

    // Fuel low -> Emergency (if battery also critical)
    protection_sm_.add_transition(
        ProtectionState::FUEL_LOW_HVAC_SHUTDOWN,
        ProtectionState::EMERGENCY_SHUTDOWN,
        "battery_critical"
    );

    // Recovery from fuel low
    protection_sm_.add_transition(
        ProtectionState::FUEL_LOW_HVAC_SHUTDOWN,
        ProtectionState::MONITORING,
        "fuel_recovered"
    );

    // ========== Engine State Machine Transitions ==========

    // Stopped -> Starting
    engine_sm_.add_transition(
        EngineState::STOPPED,
        EngineState::STARTING,
        "start_for_charge"
    );

    // Starting -> Running (when engine reports running)
    engine_sm_.add_transition(
        EngineState::STARTING,
        EngineState::RUNNING_FOR_CHARGE,
        "engine_running"
    );

    // Running -> Stopping (when conditions met)
    engine_sm_.add_transition(
        EngineState::RUNNING_FOR_CHARGE,
        EngineState::STOPPING,
        "stop_charging"
    );

    // Stopping -> Stopped (when engine reports stopped)
    engine_sm_.add_transition(
        EngineState::STOPPING,
        EngineState::STOPPED,
        "engine_stopped"
    );
}

void ClimateProtectionSystem::subscribe_to_signals() {
    LOG(INFO) << "Subscribing to VSS signals...";

    auto self = this;  // Capture 'this' for callbacks

    // Batch subscribe using fluent API (no error handling needed!)
    client_->subscriptions()
        .subscribe(battery_voltage_, [self](vss::types::QualifiedValue<float> qv) {
            if (!qv.is_valid()) return;
            self->handle_battery_voltage_change(*qv.value);
        })
        .subscribe(fuel_level_, [self](vss::types::QualifiedValue<float> qv) {
            if (!qv.is_valid()) return;
            self->handle_fuel_level_change(*qv.value);
        })
        .subscribe(hvac_is_active_, [self](vss::types::QualifiedValue<bool> qv) {
            if (!qv.is_valid()) return;
            self->handle_hvac_state_change(*qv.value);
        })
        .subscribe(engine_is_running_, [self](vss::types::QualifiedValue<bool> qv) {
            if (!qv.is_valid()) return;
            self->handle_engine_state_change(*qv.value);
        })
        .subscribe(coolant_temp_, [self](vss::types::QualifiedValue<float> qv) {
            if (!qv.is_valid()) return;
            self->handle_coolant_temp_change(*qv.value);
        })
        .subscribe(ambient_temp_, [self](vss::types::QualifiedValue<float> qv) {
            if (!qv.is_valid()) return;
            self->handle_ambient_temp_change(*qv.value);
        });

    LOG(INFO) << "Subscribed to all signals";
}

// ========== Signal Handlers ==========

void ClimateProtectionSystem::handle_battery_voltage_change(float voltage) {
    current_battery_voltage_ = voltage;
    LOG(INFO) << "Battery voltage: " << voltage << "V";
}

void ClimateProtectionSystem::handle_fuel_level_change(float level) {
    current_fuel_level_ = level;
    LOG(INFO) << "Fuel level: " << level << "%";
}

void ClimateProtectionSystem::handle_hvac_state_change(bool is_active) {
    current_hvac_active_ = is_active;
    LOG(INFO) << "HVAC state: " << (is_active ? "ACTIVE" : "INACTIVE");
}

void ClimateProtectionSystem::handle_engine_state_change(bool is_running) {
    bool was_running = current_engine_running_;
    current_engine_running_ = is_running;

    if (is_running && !was_running) {
        LOG(INFO) << "Engine started";
        // Transition engine state machine
        if (engine_sm_.current_state() == EngineState::STARTING) {
            engine_sm_.trigger("engine_running");
            // Also transition protection state machine
            if (protection_sm_.current_state() == ProtectionState::BATTERY_LOW_ENGINE_START) {
                protection_sm_.trigger("engine_started");
            }
        }
    } else if (!is_running && was_running) {
        LOG(INFO) << "Engine stopped";
        // Transition engine state machine
        if (engine_sm_.current_state() == EngineState::STOPPING) {
            engine_sm_.trigger("engine_stopped");
        }
    }
}

void ClimateProtectionSystem::handle_coolant_temp_change(float temp) {
    current_coolant_temp_ = temp;
    VLOG(1) << "Coolant temperature: " << temp << "°C";
}

void ClimateProtectionSystem::handle_ambient_temp_change(float temp) {
    current_ambient_temp_ = temp;
    VLOG(1) << "Ambient temperature: " << temp << "°C";
}

// ========== Protection Logic ==========

void ClimateProtectionSystem::check_battery_protection() {
    auto current_state = protection_sm_.current_state();

    // Check if battery is critically low
    if (current_battery_voltage_ < min_battery_voltage_threshold_) {
        if (current_state == ProtectionState::MONITORING) {
            // Battery critical and we have fuel -> start engine
            if (current_fuel_level_ > min_fuel_level_threshold_) {
                LOG(WARNING) << "Battery critical (" << current_battery_voltage_
                             << "V < " << min_battery_voltage_threshold_ << "V), starting engine";
                protection_sm_.trigger("battery_critical");
                engine_sm_.trigger("start_for_charge");
            } else {
                // Battery critical and no fuel -> emergency shutdown
                LOG(ERROR) << "Battery and fuel both critical!";
                protection_sm_.trigger("battery_critical");
            }
        } else if (current_state == ProtectionState::FUEL_LOW_HVAC_SHUTDOWN) {
            // Already in fuel low state, battery also critical -> emergency
            LOG(ERROR) << "Battery critical while fuel already low!";
            protection_sm_.trigger("battery_critical");
        }
    }

    // Check if battery has recovered
    if (current_battery_voltage_ > safe_battery_voltage_) {
        if (current_state == ProtectionState::ENGINE_CHARGING) {
            // Battery recovered, check if we should stop engine
            if (should_stop_engine()) {
                LOG(INFO) << "Battery recovered (" << current_battery_voltage_
                         << "V > " << safe_battery_voltage_ << "V), stopping engine";
                stop_engine_after_charging();
                protection_sm_.trigger("battery_recovered");
            }
        }
    }
}

void ClimateProtectionSystem::check_fuel_protection() {
    auto current_state = protection_sm_.current_state();

    // Check if fuel is critically low
    if (current_fuel_level_ < min_fuel_level_threshold_) {
        if (current_state == ProtectionState::MONITORING ||
            current_state == ProtectionState::ENGINE_CHARGING) {
            LOG(WARNING) << "Fuel critical (" << current_fuel_level_
                         << "% < " << min_fuel_level_threshold_ << "%), shutting down HVAC";
            protection_sm_.trigger("fuel_critical");

            // If engine is running for charging, stop it
            if (engine_sm_.current_state() == EngineState::RUNNING_FOR_CHARGE) {
                LOG(WARNING) << "Stopping engine due to low fuel";
                engine_sm_.trigger("stop_charging");
            }
        }
    }

    // Check if fuel has recovered
    if (current_fuel_level_ > min_fuel_level_threshold_ + 5.0f) {  // +5% hysteresis
        if (current_state == ProtectionState::FUEL_LOW_HVAC_SHUTDOWN) {
            LOG(INFO) << "Fuel recovered (" << current_fuel_level_ << "%)";
            protection_sm_.trigger("fuel_recovered");
        }
    }
}

void ClimateProtectionSystem::check_smart_ventilation() {
    // TODO: Implement smart ventilation logic
    // - If ambient temp < cabin temp and HVAC cooling -> suggest opening windows
    // - If ambient temp > cabin temp and HVAC heating -> suggest closing windows
}

// ========== Engine Management ==========

void ClimateProtectionSystem::start_engine_for_charging() {
    LOG(INFO) << "Requesting engine start for battery charging";
    engine_sm_.trigger("start_for_charge");
}

void ClimateProtectionSystem::stop_engine_after_charging() {
    LOG(INFO) << "Requesting engine stop after charging";
    engine_sm_.trigger("stop_charging");
}

bool ClimateProtectionSystem::should_stop_engine() {
    // Only stop if we started it
    if (!engine_started_by_us_) {
        return false;
    }

    // Check minimum runtime
    auto runtime = std::chrono::steady_clock::now() - engine_start_time_;
    if (runtime < min_engine_runtime_) {
        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(min_engine_runtime_ - runtime);
        VLOG(1) << "Engine minimum runtime not met, " << remaining.count() << "s remaining";
        return false;
    }

    return true;
}
