# KUKSA Communication Integration Tests

This directory contains comprehensive integration tests for the C++ SDK's KUKSA v2 communication layer.

## Overview

These tests verify the interaction between the SDK and KUKSA Databroker v2 (version 0.6.0), including:

1. **Basic Connectivity** - Resolver and Client connections
2. **Actuator Provider Pattern** - Receiving and handling actuation commands via Client
3. **Signal Subscriptions** - Real-time updates from VSS signals via Client
4. **Reading/Writing Values** - Using Client with auto-routing to Actuate() or PublishValue() RPCs
5. **Error Handling** - Invalid paths, connection failures
6. **Concurrent Operations** - Multiple publishers/subscribers

## Key Features Tested

The integration tests verify:

- ✅ **Unified Client Pattern**: Client handles subscriptions, actuators, and publishing in one client
- ✅ **Smart RPC Routing**: `client->set()` auto-routes to either `Actuate()` or `PublishValue()` based on signal class
- ✅ **Unified Handle Type**: `SignalHandle<T>` supports all operations (read, write, subscribe, publish)
- ✅ **Independent Publishing**: Clients can publish actual values using cached `SignalHandle<T>` handles

## Running the Tests

### Prerequisites

- Docker and Docker Compose installed
- C++ build environment (for local debugging)

### Quick Start

```bash
# Run all tests with Docker
./run_tests.sh

# Or manually with docker-compose
docker-compose up --build --abort-on-container-exit
```

### Local Development

For debugging with your IDE:

```bash
# Build the tests
cd /path/to/libkuksa-cpp
mkdir -p build && cd build
cmake .. -DBUILD_TESTS=ON -DWITH_VSS=ON
make test_kuksa_communication

# Start KUKSA databroker
cd ../cpp/tests/integration
docker-compose up kuksa-databroker

# Run tests locally (in another terminal)
export KUKSA_ADDRESS=localhost:55555
./build/tests/integration/test_kuksa_communication
```

## Test Cases

### 1. BasicConnectivity
Verifies that clients can connect to and disconnect from KUKSA.

### 2. ProviderConnectivity
Verifies that providers can establish connections.

### 3. ActuatorProviderPattern
Tests the complete actuator provider flow:
- Provider claims actuator ownership
- Client sends actuation command
- Provider receives actuation request

### 4. StandalonePublishing
Tests publishing values using the standalone `PublishValue()` RPC (recommended approach).

### 5. SensorSubscription
Tests subscribing to and receiving sensor updates.

### 6. MultipleSubscriptions
Tests multiple simultaneous subscriptions to different signals.

### 7. ActuatorActualValueFlow
Tests the complete actuator flow from target to actual value:
- Actuation command → Provider → Simulated delay → Publish actual → Subscription update

### 8. ProviderStreamPublishingBug
Demonstrates the current bug where provider stream publishing fails without proper signal claiming.

### 9. InvalidSignalPaths
Tests error handling for non-existent VSS paths.

### 10. ConnectionResilience
Tests connection failure handling and reconnection.

### 11. ConcurrentOperations
Tests multiple publishers updating the same signal concurrently.

## Architecture

```
┌──────────────┐     ┌─────────────────┐     ┌──────────────────┐
│  Client   │────▶│ KUKSA Databroker│◀────│    Client     │
│ (Commander)  │     │    (v0.6.0)     │     │  (Actuator Owner)│
└──────────────┘     └─────────────────┘     └──────────────────┘
      │                      │                        │
      │ get()               │                        │
      │ set() ──────┬─────▶ │ Actuate()             │
      │             └─────▶ │ PublishValue()        │
      │ subscribe()         │                        │
      │                     │ OpenProviderStream()◀──│
      │                     │ BatchActuate() ───────▶│
      │                     │                        │
      │                     │ publish() ◀────────────│
      └─────────────────────┴────────────────────────┘
```

## Recommended Patterns

### For Actuator Owners

```cpp
// 1. Create resolver to get handles
auto resolver_result = Resolver::create("localhost:55555");
auto resolver = std::move(*resolver_result);

auto door_result = resolver->get<bool>("Vehicle.Cabin.Door.IsLocked");
auto door = *door_result;

// 2. Create unified client
auto client = Client::create("localhost:55555");
Client* client_ptr = client.get();

// 3. Register actuator handler
client->serve_actuator(door, [client_ptr](bool target, const SignalHandle<bool>& handle) {
    // Process actuation (don't block - runs on gRPC thread!)
    bool actual = control_hardware(target);

    // Publish actual value from worker thread
    auto status = client_ptr->publish(handle, actual);
    if (!status.ok()) {
        LOG(ERROR) << "Failed to publish actual: " << status;
    }
});

client->start();
auto status = client->wait_until_ready(5000ms);
if (!status.ok()) {
    LOG(ERROR) << "Client not ready: " << status;
    return;
}
```

### For Reading and Writing Values

Use `Client` with smart RPC routing:
```cpp
// Create client
auto client = Client::create("localhost:55555");

// Create resolver to get handles
auto resolver_result = Resolver::create("localhost:55555");
auto resolver = std::move(*resolver_result);

// Get handle for any operation
auto speed = resolver->get<float>("Vehicle.Speed");
if (speed.ok()) {
    // Read value
    auto value_result = client->get(*speed);
    if (value_result.ok() && value_result->has_value()) {
        LOG(INFO) << "Speed: " << **value_result;
    }

    // Write value - auto-routes to PublishValue() RPC for sensors
    auto status = client->set(*speed, 65.5f);
    if (!status.ok()) {
        LOG(ERROR) << "Failed to set: " << status;
    }
}

// For actuators, set() auto-routes to Actuate() RPC
auto door = resolver->get<bool>("Vehicle.Cabin.Door.IsLocked");
if (door.ok()) {
    // Auto-routes to Actuate() RPC for actuators
    client->set(*door, true);
}
```

## Next Steps

1. Add support for high-frequency sensor streaming
2. Implement connection retry logic with exponential backoff
3. Add performance benchmarks for concurrent operations
4. Create more complex multi-component scenarios (e.g., full vehicle simulation)
5. Add tests for error recovery and reconnection scenarios