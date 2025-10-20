# Climate Control Protection System

A comprehensive example demonstrating libkuksa-cpp API usage with state machines for vehicle climate control protection.

## Overview

This example implements a climate protection system that monitors vehicle battery and fuel levels during HVAC operation. When critical thresholds are reached, the system automatically starts the engine for battery charging or shuts down HVAC to prevent battery/fuel exhaustion.

## What This Example Demonstrates

### 1. **Wrapped State Machine Pattern**
- `ClimateProtectionStateMachine` - Type-safe wrapper for protection states
- `EngineManagementStateMachine` - Type-safe wrapper for engine control
- Similar to `ConnectionStateMachine` pattern used in libkuksa-cpp
- Provides type-safe methods instead of string-based triggers
- Encapsulates control actions (HVAC, engine) in state entry callbacks

### 2. **Signal Management**
- **Batch signal resolution** using `resolver->signals()` fluent API
- **Quality-aware subscriptions** handling all VSS signal quality states:
  - `VALID` - Use the value
  - `INVALID` - Enter safe mode (critical signals) or use last value (non-critical)
  - `NOT_AVAILABLE` - Enter safe mode (critical signals) or use last value (non-critical)
  - `STALE` - Continue with degraded mode warnings
- **Critical vs. non-critical signal handling**
- **Safe mode behavior** when critical signals are lost

### 3. **Modern API Usage**
- `get_values()` - Batch read with structured binding
  ```cpp
  auto [min_voltage, min_fuel] = client->get_values(
      min_battery_voltage_,
      min_fuel_level_
  ).value_or(std::tuple{23.6f, 10.0f});
  ```
- `get_value()` - Single value read with quality validation
- `set()` - Write values to actuators/attributes
- `subscribe()` - Register callbacks with `QualifiedValue<T>` containing quality metadata

### 4. **VSS Signal Types**
- Standard VSS 5.1 signals (float, bool, string)
- Transparent type mapping for logical types (uint8 → uint32)
- Custom signals via vss_extensions.json

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                   ClimateProtectionSystem                        │
│                                                                   │
│  ┌────────────────────────┐    ┌──────────────────────────┐    │
│  │ ClimateProtection      │    │ EngineManagement         │    │
│  │ StateMachine           │◄───┤ StateMachine             │    │
│  │                        │    │                          │    │
│  │ States:                │    │ States:                  │    │
│  │ • MONITORING           │    │ • STOPPED                │    │
│  │ • BATTERY_LOW          │    │ • STARTING               │    │
│  │ • ENGINE_CHARGING      │    │ • RUNNING_FOR_CHARGE     │    │
│  │ • FUEL_LOW_SHUTDOWN    │    │ • STOPPING               │    │
│  │ • EMERGENCY_SHUTDOWN   │    │                          │    │
│  └────────────────────────┘    └──────────────────────────┘    │
│                                                                   │
│  Signal Subscriptions:         Control Actions:                 │
│  • Battery voltage (critical)  • HVAC on/off                    │
│  • Fuel level (critical)       • Engine start/stop              │
│  • HVAC state                                                    │
│  • Engine state                                                  │
│  • Coolant temp                                                  │
│  • Ambient temp                                                  │
└─────────────────────────────────────────────────────────────────┘
```

## Protection Logic

### Battery Protection
1. **Battery Low (< 23.6V)** → Start engine for charging (if fuel available)
2. **Engine Charging** → Keep engine running minimum 10 minutes
3. **Battery Recovered (> 24.8V)** → Stop engine after minimum runtime
4. **Battery + Fuel Critical** → Emergency shutdown

### Fuel Protection
1. **Fuel Low (< 10%)** → Shut down HVAC immediately
2. **Fuel Low + Engine Running** → Stop engine to conserve fuel
3. **Fuel Recovered (> 15%)** → Resume normal monitoring

### Signal Loss Handling
- **Critical signals lost** (battery voltage, fuel level) → Enter safe mode:
  - Shut down HVAC
  - Stop engine if we started it
  - Log warnings about degraded operation
- **Non-critical signals** → Continue with last known values

## VSS Signals Used

### Standard VSS 5.1 Signals
| Signal Path | Type | Direction | Purpose |
|-------------|------|-----------|---------|
| `Vehicle.LowVoltageBattery.CurrentVoltage` | float | Read | Monitor 24V battery |
| `Vehicle.OBD.FuelLevel` | float | Read | Monitor fuel percentage |
| `Vehicle.Cabin.HVAC.IsAirConditioningActive` | bool | Read/Write | HVAC state and control |
| `Vehicle.Powertrain.CombustionEngine.IsRunning` | bool | Read | Engine state |
| `Vehicle.OBD.CoolantTemperature` | float | Read | Engine temp (info) |
| `Vehicle.Cabin.HVAC.AmbientAirTemperature` | float | Read | Outside temp (info) |
| `Vehicle.Cabin.HVAC.Station.Row1.Driver.Temperature` | float | Read | Cabin temp (info) |

### Custom Signals (vss_extensions.json)
| Signal Path | Type | Direction | Purpose |
|-------------|------|-----------|---------|
| `Vehicle.Private.Engine.IsStartWithoutIntentionToDrive` | bool | Write | Start engine for stationary charging |
| `Vehicle.Private.HVAC.MinimumBatteryVoltageForHVAC` | float | Read | Configuration attribute |
| `Vehicle.Private.HVAC.MinimumFuelLevelForHVAC` | float | Read | Configuration attribute |

## State Machines

### ClimateProtectionStateMachine

**States:**
- `MONITORING` - Normal operation, monitoring thresholds
- `BATTERY_LOW_ENGINE_START` - Battery critical, requesting engine start
- `ENGINE_CHARGING` - Engine running for battery charging
- `FUEL_LOW_HVAC_SHUTDOWN` - Fuel critical, HVAC shut down
- `EMERGENCY_SHUTDOWN` - Both battery and fuel critical

**Type-safe triggers:**
- `trigger_battery_critical()` - Battery below threshold
- `trigger_battery_recovered()` - Battery above safe level
- `trigger_engine_started()` - Engine confirmed running
- `trigger_fuel_critical()` - Fuel below threshold
- `trigger_fuel_recovered()` - Fuel recovered

### EngineManagementStateMachine

**States:**
- `STOPPED` - Engine not running
- `STARTING` - Engine start command sent
- `RUNNING_FOR_CHARGE` - Engine running, tracking runtime
- `STOPPING` - Engine stop command sent

**Type-safe triggers:**
- `trigger_start_for_charge()` - Request engine start
- `trigger_engine_running()` - Engine confirmed running
- `trigger_stop_charging()` - Request engine stop
- `trigger_engine_stopped()` - Engine confirmed stopped

**Features:**
- Minimum runtime enforcement (10 minutes)
- Runtime tracking and query methods
- Force stop for emergency situations

## Building and Running

```bash
# Build
cd libkuksa-cpp/build
cmake --build . --target climate_control

# Run (requires KUKSA Databroker)
./examples/climate_control
```

## Code Structure

```
climate_control/
├── climate_control.hpp                      # Main system class
├── climate_control.cpp                      # Implementation
├── climate_protection_state_machine.hpp     # Protection state machine wrapper
├── engine_management_state_machine.hpp      # Engine state machine wrapper
├── main.cpp                                 # Entry point
└── README.md                                # This file
```

## Key Takeaways

1. **State machines provide structure** - Clear states and transitions make complex logic manageable
2. **Wrappers add safety** - Type-safe methods prevent string typos and encapsulate actions
3. **Quality matters** - Always handle signal quality in safety-critical systems
4. **Batch operations** - Use fluent APIs for efficient multi-signal operations
5. **Observability** - State machines with logging provide clear system visibility
6. **Separation of concerns** - State logic, signal handling, and control actions are clearly separated
