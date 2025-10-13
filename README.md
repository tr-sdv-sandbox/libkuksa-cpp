# libkuksa-cpp

C++ library for interacting with KUKSA.val databroker using the Vehicle Signal Specification (VSS).

## Features

- Type-safe signal handles with compile-time checking
- Single connection for all operations
- Support for all VSS data types including arrays
- Automatic RPC routing based on signal class
- Thread-safe operations

## Requirements

- C++17 or later
- CMake 3.16+
- gRPC and protobuf
- Google glog
- KUKSA.val databroker v2

## Quick Start

```cpp
#include <kuksa_cpp/kuksa.hpp>
#include <glog/logging.h>

using namespace kuksa;

int main() {
    // Step 1: Resolve signal handles
    auto resolver = std::move(*Resolver::create("localhost:55555"));
    auto speed = *resolver->get<float>("Vehicle.Speed");
    auto door = *resolver->get<bool>("Vehicle.Cabin.Door.Row1.Left.IsLocked");

    // Step 2: Create client
    auto client = std::move(*Client::create("localhost:55555"));

    // Step 3: Subscribe to updates
    client->subscribe(speed, [](std::optional<float> value) {
        if (value) LOG(INFO) << "Speed: " << *value << " km/h";
    });

    // Step 4: Start client for async operations
    client->start();
    client->wait_until_ready(std::chrono::seconds(5));

    // Step 5: Read and write values
    auto current = client->get(speed);
    client->set(door, true);  // Auto-routes to Actuate() for actuators
    client->publish(speed, 120.5f);  // PublishValue() for sensors

    return 0;
}
```

## API Overview

### Resolver

Query signal metadata and create typed handles:

```cpp
auto resolver = std::move(*Resolver::create("localhost:55555"));

// Get handles for any signal type
auto sensor = *resolver->get<float>("Vehicle.Speed");
auto actuator = *resolver->get<bool>("Vehicle.Door.IsLocked");
auto attribute = *resolver->get<std::string>("Vehicle.VIN");
```

### Client

Unified client for all operations:

```cpp
auto client = std::move(*Client::create("localhost:55555"));

// Synchronous operations (work immediately)
client->get(handle);           // Read current value
client->set(handle, value);    // Write (auto-routes to correct RPC)

// Asynchronous operations (require start())
client->subscribe(handle, callback);      // Receive updates
client->publish(handle, value);           // Publish sensor values
client->serve_actuator(handle, handler);  // Handle actuation requests

// Start async operations
client->start();
client->wait_until_ready(std::chrono::seconds(5));
```

### Signal Handles

All signals use `SignalHandle<T>`:

```cpp
// Same handle type for all signal classes
auto speed = resolver->get<float>("Vehicle.Speed");        // Sensor
auto door = resolver->get<bool>("Vehicle.Door.IsLocked");  // Actuator
auto vin = resolver->get<std::string>("Vehicle.VIN");      // Attribute

// Operation determines behavior
client->get(*speed);              // Read
client->set(*door, true);         // Command actuator
client->publish(*speed, 100.0f);  // Publish sensor
```

## Supported Data Types

**Scalars**: `bool`, `int32_t`, `uint32_t`, `int64_t`, `uint64_t`, `float`, `double`, `std::string`

**Arrays**: `std::vector<T>` for all scalar types

## Threading Model

### Resolver
- No internal threads
- Synchronous operations
- Thread-safe

### Client
- Internal threads for streaming operations
- Synchronous operations (`get`, `set`) work immediately
- Asynchronous operations (`subscribe`, `publish`, `serve_actuator`) require `start()`
- Thread-safe
- **Important**: Callbacks run on gRPC threads - do not block or call `publish()` directly

### Handling Long Operations

Queue work from callbacks to separate threads:

```cpp
std::queue<float> work_queue;
std::mutex mutex;

client->subscribe(sensor, [&](std::optional<float> value) {
    // Fast - just queue the work
    std::lock_guard lock(mutex);
    work_queue.push(*value);
});

// Process in separate thread
std::thread worker([&]() {
    while (running) {
        // Get work and process
        float value = get_from_queue();
        process_value(value);  // Safe to do long operations here
    }
});
```

## Actuator Pattern

Register handler and process in separate thread:

```cpp
auto client_shared = std::shared_ptr<Client>(std::move(*Client::create("localhost:55555")));

client_shared->serve_actuator(door, [client_weak = std::weak_ptr<Client>(client_shared)](
        bool target, const SignalHandle<bool>& handle) {
    // Queue work - DO NOT call publish() here (will cause gRPC errors)
    queue_work(handle, target);
});

// Worker thread publishes actual values
std::thread worker([client_shared]() {
    while (running) {
        auto [handle, value] = get_work();
        client_shared->publish(handle, value);  // Safe - different thread
    }
});

client_shared->start();
```

## Error Handling

All operations return `Result<T>` or `absl::Status`:

```cpp
// Connection failures
auto resolver = Resolver::create("invalid:12345");
if (!resolver.ok()) {
    LOG(ERROR) << "Failed: " << resolver.status();
}

// Signal resolution
auto handle = resolver->get<float>("Invalid.Signal");
if (!handle.ok()) {
    LOG(ERROR) << "Signal not found: " << handle.status();
}

// Read operations (two-layer)
auto result = client->get(handle);
if (!result.ok()) {
    LOG(ERROR) << "Get failed: " << result.status();
} else if (!result->has_value()) {
    LOG(INFO) << "Signal has no value (NONE)";
} else {
    LOG(INFO) << "Value: " << **result;
}
```

## Building

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get install -y build-essential cmake \
    libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc libgoogle-glog-dev

# Build
mkdir build && cd build
cmake .. -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON
make -j$(nproc)

# Run tests
ctest
```

## CMake Integration

```cmake
find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)
find_package(glog REQUIRED)

add_subdirectory(libkuksa-cpp)

target_link_libraries(your_app
    kuksa
    glog::glog
)
```

## Additional Components

### State Machine (`sdv::state_machine`)
Generic hierarchical state machine used internally for connection management. Can be used independently.

### Testing Library (`sdv::testing`)
YAML-based testing framework with Google Test integration. See `TESTING.md` for details.

## License

Apache License 2.0
