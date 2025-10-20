/**
 * @file climate_control.cpp
 * @brief Climate Protection System - Observer/Protector Pattern
 *
 * Monitors and protects vehicle from battery/fuel exhaustion during climate control operation.
 * Uses VSS 5.1 signals and libkuksa-cpp SDK.
 */

#include "climate_control.hpp"

ClimateProtectionSystem::ClimateProtectionSystem(const std::string& kuksa_url)
        : kuksa_url_(kuksa_url),
          running_(true) {
    // State machines will be initialized in setup_state_machines() after client is created
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

    // Setup state machines with client callbacks
    setup_state_machines();

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

    // Read configuration values (using get_values() for batch read with defaults)
    auto [min_voltage, min_fuel] = client_->get_values(
        min_battery_voltage_,
        min_fuel_level_
    ).value_or(std::tuple{
        min_battery_voltage_threshold_,  // Default: 23.6V
        min_fuel_level_threshold_         // Default: 10.0%
    });

    min_battery_voltage_threshold_ = min_voltage;
    min_fuel_level_threshold_ = min_fuel;

    LOG(INFO) << "Configuration:";
    LOG(INFO) << "  Minimum battery voltage threshold: " << min_battery_voltage_threshold_ << "V";
    LOG(INFO) << "  Minimum fuel level threshold: " << min_fuel_level_threshold_ << "%";
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
    LOG(INFO) << "  - Min engine runtime: 10 minutes";

    // Main monitoring loop
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Periodic protection checks
        check_battery_protection();
        check_fuel_protection();
    }

    LOG(INFO) << "Climate protection system stopped";
}

void ClimateProtectionSystem::stop() {
    running_ = false;
}

void ClimateProtectionSystem::setup_state_machines() {
    // Create HVAC controller callback
    auto hvac_controller = [this](bool active) {
        auto status = client_->set(hvac_is_active_, active);
        if (!status.ok()) {
            LOG(ERROR) << "Failed to control HVAC: " << status;
        }
    };

    // Create engine starter callback (triggers engine state machine)
    auto engine_starter = [this]() {
        engine_sm_->trigger_start_for_charge();
    };

    // Create engine controller callback
    auto engine_controller = [this](bool start) {
        auto status = client_->set(engine_start_stationary_, start);
        if (!status.ok()) {
            LOG(ERROR) << "Failed to control engine: " << status;
        }
    };

    // Initialize state machines with callbacks
    protection_sm_ = std::make_unique<ClimateProtectionStateMachine>(
        hvac_controller,
        engine_starter
    );

    engine_sm_ = std::make_unique<EngineManagementStateMachine>(
        engine_controller,
        std::chrono::minutes(10)  // Minimum 10 minutes runtime
    );
}

void ClimateProtectionSystem::subscribe_to_signals() {
    LOG(INFO) << "Subscribing to VSS signals...";

    auto self = this;  // Capture 'this' for callbacks

    // Subscribe to all signals (errors reported at start())
    // CRITICAL: Battery voltage - system cannot operate safely without this
    client_->subscribe(battery_voltage_, [self](vss::types::QualifiedValue<float> qv) {
        using vss::types::SignalQuality;
        switch (qv.quality) {
            case SignalQuality::VALID:
                self->battery_voltage_available_ = true;
                self->handle_battery_voltage_change(*qv.value);
                break;
            case SignalQuality::NOT_AVAILABLE:
                LOG(ERROR) << "Battery voltage signal lost - entering safe mode";
                self->handle_battery_voltage_loss();
                break;
            case SignalQuality::INVALID:
                LOG(ERROR) << "Battery voltage signal invalid - cannot trust data";
                self->handle_battery_voltage_loss();
                break;
            case SignalQuality::STALE:
                LOG(WARNING) << "Battery voltage data stale, continuing with last value: "
                            << self->current_battery_voltage_ << "V (DEGRADED)";
                self->system_degraded_ = true;
                break;
        }
    });

    // CRITICAL: Fuel level - needed for engine start decisions
    client_->subscribe(fuel_level_, [self](vss::types::QualifiedValue<float> qv) {
        using vss::types::SignalQuality;
        switch (qv.quality) {
            case SignalQuality::VALID:
                self->fuel_level_available_ = true;
                self->handle_fuel_level_change(*qv.value);
                break;
            case SignalQuality::NOT_AVAILABLE:
                LOG(ERROR) << "Fuel level signal lost - assuming low fuel";
                self->handle_fuel_level_loss();
                break;
            case SignalQuality::INVALID:
                LOG(ERROR) << "Fuel level signal invalid - assuming low fuel";
                self->handle_fuel_level_loss();
                break;
            case SignalQuality::STALE:
                LOG(WARNING) << "Fuel level data stale, using last value: "
                            << self->current_fuel_level_ << "% (DEGRADED)";
                self->system_degraded_ = true;
                break;
        }
    });

    // Important: HVAC state (can continue with stale data briefly)
    client_->subscribe(hvac_is_active_, [self](vss::types::QualifiedValue<bool> qv) {
        using vss::types::SignalQuality;
        if (qv.quality == SignalQuality::VALID) {
            self->handle_hvac_state_change(*qv.value);
        } else if (qv.quality == SignalQuality::STALE) {
            LOG(WARNING) << "HVAC state data stale, using last known: "
                        << (self->current_hvac_active_ ? "active" : "inactive");
        } else {
            LOG(WARNING) << "HVAC state unavailable/invalid";
        }
    });

    // Important: Engine state (can continue with stale data briefly)
    client_->subscribe(engine_is_running_, [self](vss::types::QualifiedValue<bool> qv) {
        using vss::types::SignalQuality;
        if (qv.quality == SignalQuality::VALID) {
            self->handle_engine_state_change(*qv.value);
        } else if (qv.quality == SignalQuality::STALE) {
            LOG(WARNING) << "Engine state data stale, using last known: "
                        << (self->current_engine_running_ ? "running" : "stopped");
        } else {
            LOG(WARNING) << "Engine state unavailable/invalid";
        }
    });

    // Nice-to-have: Coolant temp (can continue with stale/old data)
    client_->subscribe(coolant_temp_, [self](vss::types::QualifiedValue<float> qv) {
        using vss::types::SignalQuality;
        if (qv.quality == SignalQuality::VALID) {
            self->handle_coolant_temp_change(*qv.value);
        } else {
            VLOG(1) << "Coolant temp unavailable, using last value: "
                    << self->current_coolant_temp_ << "°C";
        }
    });

    // Nice-to-have: Ambient temp (can continue with stale/old data)
    client_->subscribe(ambient_temp_, [self](vss::types::QualifiedValue<float> qv) {
        using vss::types::SignalQuality;
        if (qv.quality == SignalQuality::VALID) {
            self->handle_ambient_temp_change(*qv.value);
        } else {
            VLOG(1) << "Ambient temp unavailable, using last value: "
                    << self->current_ambient_temp_ << "°C";
        }
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
        if (engine_sm_->current_state() == EngineState::STARTING) {
            engine_sm_->trigger_engine_running();
            // Also transition protection state machine
            if (protection_sm_->current_state() == ProtectionState::BATTERY_LOW_ENGINE_START) {
                protection_sm_->trigger_engine_started();
            }
        }
    } else if (!is_running && was_running) {
        LOG(INFO) << "Engine stopped";
        // Transition engine state machine
        if (engine_sm_->current_state() == EngineState::STOPPING) {
            engine_sm_->trigger_engine_stopped();
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

// ========== Signal Health Management ==========

void ClimateProtectionSystem::handle_battery_voltage_loss() {
    LOG(ERROR) << "CRITICAL SIGNAL LOSS: Battery voltage";
    battery_voltage_available_ = false;
    enter_safe_mode();
}

void ClimateProtectionSystem::handle_fuel_level_loss() {
    LOG(ERROR) << "CRITICAL SIGNAL LOSS: Fuel level";
    fuel_level_available_ = false;
    enter_safe_mode();
}

void ClimateProtectionSystem::enter_safe_mode() {
    system_degraded_ = true;

    LOG(ERROR) << "=================================================================";
    LOG(ERROR) << "ENTERING SAFE MODE - Critical signal(s) unavailable";
    LOG(ERROR) << "  Battery voltage available: " << (battery_voltage_available_ ? "YES" : "NO");
    LOG(ERROR) << "  Fuel level available: " << (fuel_level_available_ ? "YES" : "NO");
    LOG(ERROR) << "=================================================================";

    // Conservative safe actions:
    // 1. Shut down HVAC to conserve battery (can't verify battery state)
    if (current_hvac_active_) {
        LOG(WARNING) << "Safe mode: Shutting down HVAC";
        auto status = client_->set(hvac_is_active_, false);
        if (!status.ok()) {
            LOG(ERROR) << "Failed to shut down HVAC in safe mode: " << status;
        }
    }

    // 2. If engine is running for charging and we started it, stop it
    //    (can't verify fuel level or charging effectiveness)
    if (engine_sm_->started_by_us() && current_engine_running_) {
        LOG(WARNING) << "Safe mode: Stopping engine (cannot verify state)";
        engine_sm_->force_stop();
    }

    // 3. Log current state
    auto current_state = protection_sm_->current_state();
    if (current_state != ProtectionState::EMERGENCY_SHUTDOWN) {
        LOG(WARNING) << "Safe mode: Current protection state: " << protection_state_name(current_state);
    }

    LOG(ERROR) << "Safe mode active - system will not perform automatic protection";
    LOG(ERROR) << "Manual intervention may be required";
}

// ========== Protection Logic ==========

void ClimateProtectionSystem::check_battery_protection() {
    // Skip protection checks if battery signal unavailable (safe mode active)
    if (!battery_voltage_available_) {
        return;
    }

    auto current_state = protection_sm_->current_state();

    // Check if battery is critically low
    if (current_battery_voltage_ < min_battery_voltage_threshold_) {
        if (current_state == ProtectionState::MONITORING) {
            // Battery critical and we have fuel -> start engine
            if (current_fuel_level_ > min_fuel_level_threshold_) {
                LOG(WARNING) << "Battery critical (" << current_battery_voltage_
                             << "V < " << min_battery_voltage_threshold_ << "V), starting engine";
                protection_sm_->trigger_battery_critical();
            } else {
                // Battery critical and no fuel -> emergency shutdown
                LOG(ERROR) << "Battery and fuel both critical!";
                protection_sm_->trigger_battery_critical();
            }
        } else if (current_state == ProtectionState::FUEL_LOW_HVAC_SHUTDOWN) {
            // Already in fuel low state, battery also critical -> emergency
            LOG(ERROR) << "Battery critical while fuel already low!";
            protection_sm_->trigger_battery_critical();
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
                protection_sm_->trigger_battery_recovered();
            }
        }
    }
}

void ClimateProtectionSystem::check_fuel_protection() {
    // Skip protection checks if fuel signal unavailable (safe mode active)
    if (!fuel_level_available_) {
        return;
    }

    auto current_state = protection_sm_->current_state();

    // Check if fuel is critically low
    if (current_fuel_level_ < min_fuel_level_threshold_) {
        if (current_state == ProtectionState::MONITORING ||
            current_state == ProtectionState::ENGINE_CHARGING) {
            LOG(WARNING) << "Fuel critical (" << current_fuel_level_
                         << "% < " << min_fuel_level_threshold_ << "%), shutting down HVAC";
            protection_sm_->trigger_fuel_critical();

            // If engine is running for charging, stop it
            if (engine_sm_->current_state() == EngineState::RUNNING_FOR_CHARGE) {
                LOG(WARNING) << "Stopping engine due to low fuel";
                engine_sm_->trigger_stop_charging();
            }
        }
    }

    // Check if fuel has recovered
    if (current_fuel_level_ > min_fuel_level_threshold_ + 5.0f) {  // +5% hysteresis
        if (current_state == ProtectionState::FUEL_LOW_HVAC_SHUTDOWN) {
            LOG(INFO) << "Fuel recovered (" << current_fuel_level_ << "%)";
            protection_sm_->trigger_fuel_recovered();
        }
    }
}

// ========== Engine Management ==========

void ClimateProtectionSystem::start_engine_for_charging() {
    LOG(INFO) << "Requesting engine start for battery charging";
    engine_sm_->trigger_start_for_charge();
}

void ClimateProtectionSystem::stop_engine_after_charging() {
    LOG(INFO) << "Requesting engine stop after charging";
    engine_sm_->trigger_stop_charging();
}

bool ClimateProtectionSystem::should_stop_engine() {
    // Only stop if we started it and minimum runtime is met
    if (!engine_sm_->started_by_us()) {
        return false;
    }

    // Check minimum runtime
    if (!engine_sm_->has_met_minimum_runtime()) {
        auto remaining = engine_sm_->remaining_runtime();
        VLOG(1) << "Engine minimum runtime not met, " << remaining.count() << "s remaining";
        return false;
    }

    return true;
}
