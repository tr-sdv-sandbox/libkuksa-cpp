# libkuksa-cpp Usage Guide

Complete API reference and usage patterns for libkuksa-cpp.

## Table of Contents

- [Core API](#core-api)
  - [Resolver](#resolver)
  - [Client](#client)
  - [Signal Handles](#signal-handles)
- [Signal Quality](#signal-quality)
- [Data Types](#data-types)
- [Threading Model](#threading-model)
- [Error Handling](#error-handling)
- [Common Patterns](#common-patterns)
- [State Machines](#state-machines)

## Core API

### Resolver

The Resolver queries databroker metadata to create typed signal handles. It validates signal paths and types at runtime before any data operations.

#### Creating a Resolver

```cpp
#include <kuksa_cpp/kuksa.hpp>

// Create resolver (connects to databroker metadata service)
auto resolver_result = kuksa::Resolver::create("localhost:55555");
if (!resolver_result.ok()) {
    LOG(ERROR) << "Failed to create resolver: " << resolver_result.status();
    return;
}
auto resolver = std::move(*resolver_result);
```

#### Single Signal Resolution

```cpp
// Resolve individual signals
auto speed = resolver->get<float>("Vehicle.Speed");
if (!speed.ok()) {
    LOG(ERROR) << "Signal not found: " << speed.status();
    return;
}

// Use the handle
kuksa::SignalHandle<float> speed_handle = *speed;
```

#### Batch Signal Resolution

Resolve multiple signals at once with aggregated error reporting:

```cpp
kuksa::SignalHandle<float> battery_voltage;
kuksa::SignalHandle<float> fuel_level;
kuksa::SignalHandle<bool> hvac_active;
kuksa::SignalHandle<bool> engine_running;

auto status = resolver->signals()
    .add(battery_voltage, "Vehicle.LowVoltageBattery.CurrentVoltage")
    .add(fuel_level, "Vehicle.OBD.FuelLevel")
    .add(hvac_active, "Vehicle.Cabin.HVAC.IsAirConditioningActive")
    .add(engine_running, "Vehicle.Powertrain.CombustionEngine.IsRunning")
    .resolve();

if (!status.ok()) {
    // Error message shows all failed signals with individual reasons:
    // "Failed to resolve 2 signal(s):
    //   - Vehicle.OBD.FuelLevel: NOT_FOUND
    //   - Vehicle.Powertrain.CombustionEngine.IsRunning: TYPE_MISMATCH"
    LOG(ERROR) << "Failed to resolve signals:\n" << status;
    return;
}

// All handles are now valid and ready to use
```

### Client

The Client provides a unified interface for all signal operations. One client handles get/set/subscribe/publish/actuate.

#### Creating a Client

```cpp
// Create client
auto client_result = kuksa::Client::create("localhost:55555");
if (!client_result.ok()) {
    LOG(ERROR) << "Failed to create client: " << client_result.status();
    return;
}

// Use as unique_ptr or shared_ptr depending on ownership needs
auto client_unique = std::move(*client_result);
// or
auto client_shared = std::make_shared<kuksa::Client>(std::move(*client_result));
```

#### Synchronous Operations

These work immediately without calling `start()`:

```cpp
// Read current value with quality metadata
auto result = client->get(speed_handle);
if (result.ok()) {
    const auto& qv = *result;
    if (qv.quality == vss::types::SignalQuality::VALID && qv.value.has_value()) {
        LOG(INFO) << "Speed: " << *qv.value << " km/h";
    }
}

// Read single unwrapped value (quality-checked)
auto speed_value = client->get_value(speed_handle);
if (speed_value.ok()) {
    LOG(INFO) << "Speed: " << *speed_value << " km/h";  // Guaranteed VALID
}

// Read multiple values with structured binding
auto result = client->get_values(battery_voltage, fuel_level);
if (result.ok()) {
    auto [voltage, fuel] = *result;
    LOG(INFO) << "Battery: " << voltage << "V, Fuel: " << fuel << "%";
}

// With defaults if unavailable
auto [voltage, fuel] = client->get_values(battery_voltage, fuel_level)
    .value_or(std::tuple{24.0f, 50.0f});

// Write value (auto-routes to Actuate/Publish/Set based on signal class)
auto status = client->set(door_locked, true);
if (!status.ok()) {
    LOG(ERROR) << "Failed to set door lock: " << status;
}
```

#### Asynchronous Operations

These require calling `start()` and run on background threads:

```cpp
// 1. Register subscriptions/publishers/actuators BEFORE start()
client->subscribe(speed_handle, [](vss::types::QualifiedValue<float> qv) {
    if (qv.quality == vss::types::SignalQuality::VALID) {
        LOG(INFO) << "Speed update: " << *qv.value << " km/h";
    }
});

client->serve_actuator(door_locked,
    [](bool target, const kuksa::SignalHandle<bool>& handle) {
        LOG(INFO) << "Actuation request: " << target;
        // Queue work for separate thread (don't call publish() here!)
    });

// 2. Start the client (starts background threads)
auto start_status = client->start();
if (!start_status.ok()) {
    LOG(ERROR) << "Failed to start client: " << start_status;
    return;
}

// 3. Wait for streams to be ready
auto ready_status = client->wait_until_ready(std::chrono::seconds(5));
if (!ready_status.ok()) {
    LOG(ERROR) << "Client not ready: " << ready_status;
    return;
}

// 4. Now async operations are active
// Callbacks fire as updates arrive
// You can publish sensor values:
client->publish(speed_handle, 120.5f);
```

### Signal Handles

`SignalHandle<T>` is a lightweight, copyable handle representing a VSS signal. The same type is used for all signal classes (sensor, actuator, attribute).

```cpp
// All signals use the same handle type
kuksa::SignalHandle<float> speed;        // Sensor
kuksa::SignalHandle<bool> door;          // Actuator
kuksa::SignalHandle<std::string> vin;    // Attribute

// Handles are copyable
kuksa::SignalHandle<float> speed_copy = speed;

// Handles are comparable
if (speed == speed_copy) {
    LOG(INFO) << "Same signal";
}

// Query signal information
LOG(INFO) << "Signal path: " << speed.path();
LOG(INFO) << "Signal ID: " << speed.id();
```

## Signal Quality

All subscriptions deliver `QualifiedValue<T>` containing:
- `quality` - Signal quality state
- `value` - Optional value (may be absent)
- `timestamp` - When value was captured

### Quality States

```cpp
enum class SignalQuality {
    VALID,           // Normal operation - value is trustworthy
    INVALID,         // Sensor malfunction - do not use value
    NOT_AVAILABLE,   // Signal source disconnected/unavailable
    STALE            // Value not updating (old data)
};
```

### Handling Quality in Subscriptions

**Safety-critical systems must handle all quality states:**

```cpp
client->subscribe(battery_voltage, [](vss::types::QualifiedValue<float> qv) {
    using vss::types::SignalQuality;

    switch (qv.quality) {
        case SignalQuality::VALID:
            // Normal operation - use the value
            if (qv.value.has_value()) {
                process_battery_voltage(*qv.value);
            }
            break;

        case SignalQuality::INVALID:
            // Sensor malfunction - enter safe mode
            LOG(ERROR) << "Battery sensor invalid - cannot trust data";
            enter_safe_mode();
            break;

        case SignalQuality::NOT_AVAILABLE:
            // Signal lost connection
            LOG(ERROR) << "Battery signal unavailable";
            enter_safe_mode();
            break;
    }
});
```

### Quality-Checked Reads

```cpp
// get_value() returns error if quality is not VALID
auto voltage = client->get_value(battery_voltage);
if (!voltage.ok()) {
    // Either communication error OR quality not VALID
    LOG(ERROR) << voltage.status();
} else {
    // Guaranteed VALID quality
    LOG(INFO) << "Battery: " << *voltage << "V";
}

// get() returns quality metadata - you decide how to handle it
auto result = client->get(battery_voltage);
if (result.ok()) {
    if (result->quality == SignalQuality::VALID && result->value.has_value()) {
        LOG(INFO) << "Battery: " << *result->value << "V";
    } else {
        LOG(ERROR) << "Invalid quality: "
                  << vss::types::signal_quality_to_string(result->quality);
    }
}
```

## Data Types

### Scalar Types

All VSS scalar types with transparent mapping for logical types:

| VSS Type | C++ Type | Physical Type | Notes |
|----------|----------|---------------|-------|
| `int8` | `int8_t` | `int32` | Transparent conversion |
| `int16` | `int16_t` | `int32` | Transparent conversion |
| `int32` | `int32_t` | `int32` | Direct |
| `int64` | `int64_t` | `int64` | Direct |
| `uint8` | `uint8_t` | `uint32` | Transparent conversion |
| `uint16` | `uint16_t` | `uint32` | Transparent conversion |
| `uint32` | `uint32_t` | `uint32` | Direct |
| `uint64` | `uint64_t` | `uint64` | Direct |
| `float` | `float` | `float` | Direct |
| `double` | `double` | `double` | Direct |
| `bool` | `bool` | `bool` | Direct |
| `string` | `std::string` | `string` | Direct |

**Example with uint8:**

```cpp
// VSS 5.1: Window position is uint8 (0-100%)
kuksa::SignalHandle<uint8_t> window_position;

resolver->signals()
    .add(window_position, "Vehicle.Cabin.Door.Row1.DriverSide.Window.Position")
    .resolve();

// Use as uint8_t - library handles protobuf int32↔uint8 conversion
client->set(window_position, static_cast<uint8_t>(50));  // 50% open

auto pos = client->get_value(window_position);
if (pos.ok()) {
    uint8_t position = *pos;  // Returns uint8_t
    LOG(INFO) << "Window position: " << static_cast<int>(position) << "%";
}
```

### Array Types

Use `std::vector<T>` for any scalar type:

```cpp
// Arrays of any scalar type
kuksa::SignalHandle<std::vector<float>> sensor_readings;
kuksa::SignalHandle<std::vector<uint8_t>> binary_data;
kuksa::SignalHandle<std::vector<std::string>> error_messages;

// Read array
auto readings = client->get_value(sensor_readings);
if (readings.ok()) {
    for (float value : *readings) {
        LOG(INFO) << "Reading: " << value;
    }
}

// Write array
std::vector<float> new_readings = {1.2f, 3.4f, 5.6f};
client->set(sensor_readings, new_readings);
```

## Threading Model

### Resolver
- **No internal threads**
- All operations are synchronous
- Thread-safe - can be called from multiple threads
- Typically used during initialization

### Client

#### Thread Architecture
- **Internal gRPC threads** - Handle streaming operations (subscribe, publish, serve_actuator)
- **User threads** - Call synchronous operations (get, set) from any thread

#### Operation Categories

**Synchronous (work immediately, any thread):**
- `get()`, `get_value()`, `get_values()` - Read operations
- `set()` - Write operations

**Asynchronous (require start(), callbacks on gRPC threads):**
- `subscribe()` - Register callback
- `publish()` - Send value on stream
- `serve_actuator()` - Register actuation handler

### Callback Guidelines

Subscription and actuator callbacks run on internal gRPC threads:

**DO:**
- Keep callbacks fast (< 1ms)
- Queue work to separate threads for processing
- Use signal quality to make decisions
- Log important events
- Update atomic variables or lock-protected state

**DON'T:**
- Block or sleep in callbacks (blocks gRPC thread pool)
- Call `publish()` from subscription callbacks (gRPC deadlock)
- Call `publish()` from actuator handlers (gRPC deadlock)
- Throw exceptions (will crash - gRPC doesn't catch)
- Do heavy computation

### Pattern: Queue-Based Processing

```cpp
// Shared state
std::queue<float> work_queue;
std::mutex queue_mutex;
std::condition_variable cv;
std::atomic<bool> running{true};

// Fast callback - just queue work
client->subscribe(sensor, [&](vss::types::QualifiedValue<float> qv) {
    if (qv.quality == vss::types::SignalQuality::VALID) {
        std::lock_guard lock(queue_mutex);
        work_queue.push(*qv.value);
        cv.notify_one();
    }
});

// Worker thread processes queue
std::thread worker([&]() {
    while (running) {
        std::unique_lock lock(queue_mutex);
        cv.wait(lock, [&] { return !work_queue.empty() || !running; });

        if (!running) break;

        float value = work_queue.front();
        work_queue.pop();
        lock.unlock();

        // Safe to do long operations here
        process_complex_computation(value);
    }
});

client->start();

// Cleanup
running = false;
cv.notify_all();
worker.join();
```

### Pattern: Actuator with Worker Thread

Actuator handlers must NOT call `publish()` directly (gRPC deadlock). Use a worker thread:

```cpp
auto client_shared = std::make_shared<kuksa::Client>(...);

// Work queue for actuation requests
struct ActuationWork {
    kuksa::SignalHandle<bool> handle;
    bool target_value;
};

std::queue<ActuationWork> work_queue;
std::mutex queue_mutex;
std::condition_variable cv;

// Register handler (runs on gRPC thread - just queue)
client_shared->serve_actuator(door_lock,
    [&](bool target, const kuksa::SignalHandle<bool>& handle) {
        std::lock_guard lock(queue_mutex);
        work_queue.push({handle, target});
        cv.notify_one();
    });

// Worker thread performs actuation and publishes actual state
std::thread worker([&, client_shared]() {
    while (running) {
        std::unique_lock lock(queue_mutex);
        cv.wait(lock, [&] { return !work_queue.empty() || !running; });

        if (!running) break;

        auto work = work_queue.front();
        work_queue.pop();
        lock.unlock();

        // Perform hardware actuation
        bool actual_state = hardware_set_door_lock(work.target_value);

        // Publish actual state (safe - different thread)
        auto status = client_shared->publish(work.handle, actual_state);
        if (!status.ok()) {
            LOG(ERROR) << "Failed to publish: " << status;
        }
    }
});

client_shared->start();
```

## Error Handling

### Return Types

- `Result<T>` - Alias for `absl::StatusOr<T>`, contains either value or error
- `absl::Status` - Contains error code and message

### Checking Errors

```cpp
// Result<T> - check ok() before dereferencing
auto result = client->get_value(speed);
if (!result.ok()) {
    LOG(ERROR) << "Error: " << result.status();
} else {
    float speed = *result;  // Safe to dereference
}

// absl::Status - check ok()
auto status = client->set(door, true);
if (!status.ok()) {
    LOG(ERROR) << "Set failed: " << status;
}
```

### Error Categories

**Connection errors:**
```cpp
auto resolver = kuksa::Resolver::create("invalid-host:99999");
if (!resolver.ok()) {
    // absl::UnavailableError or absl::DeadlineExceededError
    LOG(ERROR) << resolver.status();
}
```

**Resolution errors:**
```cpp
auto handle = resolver->get<float>("Invalid.Signal.Path");
if (!handle.ok()) {
    // absl::NotFoundError
    LOG(ERROR) << handle.status();
}

// Batch resolution shows all failures
auto status = resolver->signals()
    .add(sig1, "Invalid.Path.1")
    .add(sig2, "Invalid.Path.2")
    .resolve();

if (!status.ok()) {
    // absl::FailedPreconditionError with detailed message:
    // "Failed to resolve 2 signal(s):
    //   - Invalid.Path.1: NOT_FOUND
    //   - Invalid.Path.2: TYPE_MISMATCH"
    LOG(ERROR) << status;
}
```

**Read/write errors:**
```cpp
auto result = client->get(speed);
if (!result.ok()) {
    // Communication error (absl::UnavailableError, etc.)
    LOG(ERROR) << result.status();
} else {
    // Got data, but check quality
    if (result->quality != vss::types::SignalQuality::VALID) {
        LOG(WARNING) << "Quality: "
                    << vss::types::signal_quality_to_string(result->quality);
    }
}
```

**Quality vs error:**
- `get()` returns error only for communication failures
- Signal quality issues are reported in `QualifiedValue.quality`
- `get_value()` returns error for BOTH communication failures AND non-VALID quality

## Common Patterns

### Initialization Pattern

```cpp
class MyVehicleApp {
public:
    MyVehicleApp(const std::string& databroker_url)
        : databroker_url_(databroker_url) {}

    bool initialize() {
        // 1. Create resolver
        auto resolver_result = kuksa::Resolver::create(databroker_url_);
        if (!resolver_result.ok()) {
            LOG(ERROR) << "Resolver failed: " << resolver_result.status();
            return false;
        }
        resolver_ = std::move(*resolver_result);

        // 2. Batch resolve all signals
        auto status = resolver_->signals()
            .add(speed_, "Vehicle.Speed")
            .add(battery_, "Vehicle.LowVoltageBattery.CurrentVoltage")
            .add(door_lock_, "Vehicle.Cabin.Door.Row1.Left.IsLocked")
            .resolve();

        if (!status.ok()) {
            LOG(ERROR) << "Signal resolution failed:\n" << status;
            return false;
        }

        // 3. Create client
        auto client_result = kuksa::Client::create(databroker_url_);
        if (!client_result.ok()) {
            LOG(ERROR) << "Client creation failed: " << client_result.status();
            return false;
        }
        client_ = std::make_shared<kuksa::Client>(std::move(*client_result));

        // 4. Register subscriptions BEFORE start()
        subscribe_to_signals();

        // 5. Start client
        auto start_status = client_->start();
        if (!start_status.ok()) {
            LOG(ERROR) << "Client start failed: " << start_status;
            return false;
        }

        // 6. Wait for ready
        auto ready_status = client_->wait_until_ready(std::chrono::seconds(5));
        if (!ready_status.ok()) {
            LOG(ERROR) << "Client not ready: " << ready_status;
            return false;
        }

        // 7. Read initial configuration
        read_configuration();

        return true;
    }

private:
    void subscribe_to_signals() {
        client_->subscribe(speed_, [this](vss::types::QualifiedValue<float> qv) {
            handle_speed_update(qv);
        });

        client_->subscribe(battery_, [this](vss::types::QualifiedValue<float> qv) {
            handle_battery_update(qv);
        });
    }

    void read_configuration() {
        auto [max_speed, min_battery] = client_->get_values(
            max_speed_config_,
            min_battery_config_
        ).value_or(std::tuple{120.0f, 22.0f});

        LOG(INFO) << "Config: max_speed=" << max_speed
                 << ", min_battery=" << min_battery;
    }

    std::string databroker_url_;
    std::shared_ptr<kuksa::Resolver> resolver_;
    std::shared_ptr<kuksa::Client> client_;

    kuksa::SignalHandle<float> speed_;
    kuksa::SignalHandle<float> battery_;
    kuksa::SignalHandle<bool> door_lock_;
};
```

### Configuration Read Pattern

```cpp
// Read multiple configuration attributes with defaults
kuksa::SignalHandle<float> min_voltage_config;
kuksa::SignalHandle<float> max_temp_config;
kuksa::SignalHandle<int32_t> timeout_config;

resolver->signals()
    .add(min_voltage_config, "Vehicle.Config.MinVoltage")
    .add(max_temp_config, "Vehicle.Config.MaxTemp")
    .add(timeout_config, "Vehicle.Config.Timeout")
    .resolve();

// Read with structured binding and defaults
auto [min_voltage, max_temp, timeout] = client->get_values(
    min_voltage_config,
    max_temp_config,
    timeout_config
).value_or(std::tuple{
    23.6f,    // Default min voltage
    85.0f,    // Default max temp
    5000      // Default timeout ms
});

LOG(INFO) << "Configuration loaded: "
          << "min_voltage=" << min_voltage << "V, "
          << "max_temp=" << max_temp << "°C, "
          << "timeout=" << timeout << "ms";
```

## State Machines

libkuksa-cpp includes `sdv::StateMachine<StateEnum>` for structured state management.

### Basic State Machine

```cpp
#include <kuksa_cpp/state_machine/state_machine.hpp>

enum class ConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    ERROR
};

sdv::StateMachine<ConnectionState> sm("Connection", ConnectionState::DISCONNECTED);

// Set state name function for logging
sm.set_state_name_function([](ConnectionState state) {
    switch (state) {
        case ConnectionState::DISCONNECTED: return "DISCONNECTED";
        case ConnectionState::CONNECTING:   return "CONNECTING";
        case ConnectionState::CONNECTED:    return "CONNECTED";
        case ConnectionState::ERROR:        return "ERROR";
        default:                           return "UNKNOWN";
    }
});

// Define state entry/exit actions
sm.define_state(ConnectionState::CONNECTING)
    .on_entry([]() {
        LOG(INFO) << "Attempting connection...";
    })
    .on_exit([]() {
        LOG(INFO) << "Connection attempt finished";
    });

sm.define_state(ConnectionState::CONNECTED)
    .on_entry([]() {
        LOG(INFO) << "Connected successfully!";
    });

// Define transitions
sm.add_transition(ConnectionState::DISCONNECTED, ConnectionState::CONNECTING, "connect");
sm.add_transition(ConnectionState::CONNECTING, ConnectionState::CONNECTED, "success");
sm.add_transition(ConnectionState::CONNECTING, ConnectionState::ERROR, "failure");

// Trigger transitions
sm.trigger("connect");   // DISCONNECTED → CONNECTING (entry action runs)
sm.trigger("success");   // CONNECTING → CONNECTED (exit + entry actions run)

// Query state
if (sm.current_state() == ConnectionState::CONNECTED) {
    LOG(INFO) << "Ready to communicate";
}
```

### Wrapped State Machine Pattern

For production code, wrap state machines to provide type-safe methods:

```cpp
class ConnectionStateMachine {
public:
    ConnectionStateMachine() {
        sm_ = std::make_unique<sdv::StateMachine<ConnectionState>>(
            "Connection", ConnectionState::DISCONNECTED
        );
        setup_states();
        setup_transitions();
    }

    // Type-safe triggers (no string typos!)
    void trigger_connect() { sm_->trigger("connect"); }
    void trigger_success() { sm_->trigger("success"); }
    void trigger_failure() { sm_->trigger("failure"); }

    // Type-safe queries
    bool is_connected() const {
        return sm_->current_state() == ConnectionState::CONNECTED;
    }

    ConnectionState current_state() const {
        return sm_->current_state();
    }

private:
    void setup_states() {
        sm_->define_state(ConnectionState::CONNECTING)
            .on_entry([this]() {
                // Can access class members
                attempt_connection();
            });
    }

    void setup_transitions() {
        sm_->add_transition(ConnectionState::DISCONNECTED,
                          ConnectionState::CONNECTING,
                          "connect");
        // ...
    }

    std::unique_ptr<sdv::StateMachine<ConnectionState>> sm_;
};
```

See `examples/climate_control/` for complete examples of wrapped state machines with callbacks.

## Best Practices

1. **Always handle signal quality** - Don't ignore non-VALID states in safety-critical code
2. **Use batch operations** - `resolver->signals()` and `get_values()` for efficiency
3. **Keep callbacks fast** - Queue work to separate threads
4. **Use shared_ptr for Client** - When passing to callbacks or multiple owners
5. **Register before start()** - Call `subscribe()` before `client->start()`
6. **Wrap state machines** - Provide type-safe methods for production code
7. **Check all errors** - Connection, resolution, and operation errors
8. **Use structured bindings** - With `get_values()` for readable multi-value reads
9. **Thread safety** - Don't call `publish()` from gRPC callbacks
10. **Configuration pattern** - Read attributes once at startup with defaults

## Examples

See the `examples/` directory for complete working examples:

- **climate_control/** - Full-featured protection system with:
  - Wrapped state machines
  - Quality-aware subscriptions
  - Batch operations
  - Automatic protection logic
