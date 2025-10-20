# libkuksa-cpp

Modern C++ library for interacting with KUKSA.val databroker using the Vehicle Signal Specification (VSS).

## Features

- **Type-safe signal handles** with compile-time type checking
- **Signal quality awareness** - Handle VALID, INVALID, NOT_AVAILABLE, STALE states
- **VSS 5.1 support** with transparent type mapping (int8/16, uint8/16)
- **Unified client** - Single connection for all operations
- **Batch operations** - Fluent API for resolving and reading multiple signals
- **State machine library** - Observable state machines for application logic
- **Thread-safe** - Safe concurrent access from multiple threads

## Quick Start

```cpp
#include <kuksa_cpp/kuksa.hpp>

int main() {
    // 1. Resolve signals
    auto resolver = std::move(*kuksa::Resolver::create("localhost:55555"));

    kuksa::SignalHandle<float> speed;
    kuksa::SignalHandle<bool> door_locked;

    resolver->signals()
        .add(speed, "Vehicle.Speed")
        .add(door_locked, "Vehicle.Cabin.Door.Row1.Left.IsLocked")
        .resolve();

    // 2. Create client
    auto client = std::make_shared<kuksa::Client>(
        std::move(*kuksa::Client::create("localhost:55555"))
    );

    // 3. Subscribe with quality handling
    client->subscribe(speed, [](vss::types::QualifiedValue<float> qv) {
        if (qv.quality == vss::types::SignalQuality::VALID) {
            LOG(INFO) << "Speed: " << *qv.value << " km/h";
        }
    });

    // 4. Start and read/write
    client->start();
    client->wait_until_ready(std::chrono::seconds(5));

    auto current_speed = client->get_value(speed);
    client->set(door_locked, true);

    return 0;
}
```

## Documentation

- **[USAGE.md](USAGE.md)** - Complete API reference and usage guide
- **[examples/climate_control/](examples/climate_control/)** - Comprehensive example with state machines
- **[TESTING.md](TESTING.md)** - Testing library documentation

## Key Concepts

### Signal Quality

All subscriptions include quality metadata - critical for safety systems:

```cpp
client->subscribe(battery_voltage, [](vss::types::QualifiedValue<float> qv) {
    switch (qv.quality) {
        case SignalQuality::VALID:      // Use value
        case SignalQuality::INVALID:    // Sensor malfunction
        case SignalQuality::NOT_AVAILABLE:  // Signal lost
        case SignalQuality::STALE:      // Data not updating
    }
});
```

### Batch Operations

```cpp
// Resolve multiple signals with error aggregation
auto status = resolver->signals()
    .add(battery, "Vehicle.LowVoltageBattery.CurrentVoltage")
    .add(fuel, "Vehicle.OBD.FuelLevel")
    .add(hvac, "Vehicle.Cabin.HVAC.IsAirConditioningActive")
    .resolve();

// Read multiple values with structured binding
auto [voltage, fuel_level] = client->get_values(battery, fuel)
    .value_or(std::tuple{24.0f, 50.0f});
```

### VSS 5.1 Type Mapping

Transparent mapping for VSS logical types:

| VSS Type | C++ Type | Notes |
|----------|----------|-------|
| `int8`, `int16` | `int8_t`, `int16_t` | Maps to int32 in protobuf |
| `uint8`, `uint16` | `uint8_t`, `uint16_t` | Maps to uint32 in protobuf |
| `float`, `double`, `bool`, `string` | Direct mapping | |
| Arrays | `std::vector<T>` | For any scalar type |

## Building

### Dependencies

**libvss-types** (required):
```bash
# Clone and install libvss-types
git clone https://github.com/tr-sdv-sandbox/libvss-types.git
cd libvss-types
mkdir build && cd build
cmake .. && make -j$(nproc)
sudo make install
cd ../..
```

Or place libvss-types in parent directory for development:
```
tr-sdv-sandbox/
├── libvss-types/
└── libkuksa-cpp/
```

**Other dependencies**:
```bash
# Ubuntu/Debian
sudo apt-get install -y build-essential cmake \
    libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc \
    libgoogle-glog-dev libabsl-dev libyaml-cpp-dev
```

### Compile

```bash
mkdir build && cd build
cmake .. -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON
cmake --build . -j$(nproc)
ctest --output-on-failure
```

## Examples

### Climate Control Protection System

Full-featured example demonstrating:
- Wrapped state machines for protection and engine management
- Signal quality handling for safety-critical systems
- Batch operations and configuration reads
- Automatic protection logic (battery charging, HVAC shutdown)

```bash
cd build
./examples/climate_control
```

See [examples/climate_control/README.md](examples/climate_control/README.md) for details.

## CMake Integration

```cmake
find_package(absl REQUIRED)
find_package(gRPC REQUIRED)
find_package(glog REQUIRED)

add_subdirectory(libkuksa-cpp)

target_link_libraries(your_app
    kuksa_cpp
    glog::glog
    absl::status
)
```

## Architecture

```
┌─────────────────────────────────────────────────┐
│              Your Application                    │
│                                                  │
│  Resolver ──▶ SignalHandle<T> ──▶ Client       │
│                                    │             │
│                                    │ gRPC        │
└────────────────────────────────────┼─────────────┘
                                     ▼
                        ┌────────────────────────┐
                        │  KUKSA.val Databroker  │
                        │  (VSS metadata + data) │
                        └────────────────────────┘
```

## Additional Components

### State Machine Library
Generic hierarchical state machines with observability. Used internally for connection management, available for application logic. See wrapped state machine pattern in climate control example.

### Testing Library
YAML-based declarative testing framework with Google Test integration. See [TESTING.md](TESTING.md).

### VSS Types Library
Standalone library (`libvss-types`) providing `QualifiedValue<T>`, `SignalQuality`, and data type enumerations.

## License

Apache License 2.0

## Resources

- **KUKSA.val**: https://github.com/eclipse-kuksa/kuksa-databroker
- **VSS Specification**: https://covesa.github.io/vehicle_signal_specification/
