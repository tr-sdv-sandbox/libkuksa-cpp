# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

libkuksa-cpp is a modern C++17 library for interacting with KUKSA.val databroker using the Vehicle Signal Specification (VSS). It provides type-safe signal handling with quality awareness for vehicle signal management.

## Build Commands

```bash
# Configure and build
mkdir build && cd build
cmake .. -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON -DWITH_TESTING=ON
cmake --build . -j$(nproc)

# Run all tests
ctest --output-on-failure

# Run specific unit tests
./tests/state_machine_tests
./tests/all_vss_types_tests
./tests/accessor_handle_creation_tests

# Run specific integration tests (requires KUKSA databroker)
./tests/integration/test_kuksa_communication
./tests/integration/test_unified_client_integration

# Install system-wide
sudo make install && sudo ldconfig
```

## Architecture

```
User Application
    │
    ├─ Resolver ──→ SignalHandle<T> ──→ Client (gRPC)
    │                                      │
    └────────────────────────────────→ KUKSA.val Databroker
```

### Core Components

**Resolver** (`include/kuksa_cpp/resolver.hpp`, `src/vss/resolver.cpp`)
- Queries KUKSA metadata to create typed signal handles
- Thread-safe, synchronous, no internal threads
- Fluent API: `resolver->signals().add(handle, "Vehicle.Speed").resolve()`
- `list_signals(pattern)` - discover signals from broker schema matching pattern

**Client** (`include/kuksa_cpp/client.hpp`, `src/vss/vss_client.cpp`)
- Single gRPC channel with two logical streams (OpenProviderStream, SubscribeById)
- Sync operations (get, set) work without `start()`
- Async operations (subscribe, publish, serve_actuator) require `start()`
- Auto-routes operations based on signal class

**SignalHandle<T>** (`include/kuksa_cpp/types.hpp`)
- Lightweight, copyable, default-constructible typed handle
- Works for sensors, actuators, and attributes

**State Machines** (`include/kuksa_cpp/state_machine/`)
- Generic `StateMachine<StateEnum>` for application logic
- `ConnectionStateMachine` for databroker connections
- Thread-safe with mutex protection

### Threading Model

- Resolver: synchronous, thread-safe, use during initialization
- Client sync ops: work from any thread
- Client async callbacks: run on gRPC threads - keep fast (<1ms), queue heavy work
- Never call `publish()` from within subscription/actuator callbacks (gRPC deadlock)
- Use `weak_ptr` for callback lifetime safety

### Type Mapping

VSS types map to C++ types with automatic conversion:
- int8/int16 → int8_t/int16_t (via int32 wire type)
- uint8/uint16 → uint8_t/uint16_t (via uint32 wire type)
- float/double/bool/string → direct mapping
- Arrays → std::vector<T>

All operations return `QualifiedValue<T>` with quality (VALID, INVALID, NOT_AVAILABLE, STALE), value, and timestamp.

## Key Files

- `include/kuksa_cpp/kuksa.hpp` - Main include combining all APIs
- `include/kuksa_cpp/types.hpp` - SignalHandle, SignalClass, error types
- `include/kuksa_cpp/error.hpp` - Status, Result<T>, VSSError
- `src/vss/vss_client.cpp` - Core gRPC implementation
- `src/vss/vss_types.cpp` - Type conversions (VSS ↔ protobuf)
- `protos/kuksa/val/v2/` - Protocol buffer definitions

## Dependencies

Required: libvss-types (0.0.3+) from https://github.com/tr-sdv-sandbox/libvss-types.git

System packages (Ubuntu): `build-essential cmake libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc libgoogle-glog-dev libabsl-dev libyaml-cpp-dev nlohmann-json3-dev libgtest-dev libgflags-dev`

## Testing

- Unit tests: Google Test in `tests/`
- Integration tests: `tests/integration/` (require Docker KUKSA databroker)
- Testing library: YAML-based declarative testing in `include/kuksa_cpp/testing/`
- CI runs on Ubuntu 24.04 via GitHub Actions

## Examples

- `examples/climate_control/` - Full-featured example with state machines
- `examples/door_example.cpp` - Simple actuator control
- `examples/unified_client_simple.cpp` - Basic API usage

## Utilities

- `utils/kuksa_logger` - Real-time signal logger (like candump for VSS)
  ```bash
  kuksa_logger --address=localhost:55555 --pattern="Vehicle.Speed"
  ```
