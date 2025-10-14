# VSS Types Refactoring - libkuksa-cpp

## Overview

Refactor libkuksa-cpp to use **libvss-types** as the canonical VSS type system, enabling deep integration with libvssdag and establishing a shared type foundation across all VSS C++ implementations.

## Goal

**Deep integration with vss::types, using QualifiedValue throughout the API.**

Accept that quality information is lost when writing to KUKSA (protobuf limitation), but preserve it everywhere else in the application. When reading from KUKSA, infer conservative quality values.

## Current State

### libkuksa-cpp (include/kuksa_cpp/types.hpp)
```cpp
using Value = std::variant<
    // Scalars
    bool, int32_t, uint32_t, int64_t, uint64_t, float, double, std::string,
    // Arrays
    std::vector<bool>, std::vector<int32_t>, ..., std::vector<std::string>
>;

enum class ValueType { STRING = 1, BOOL = 2, ..., DOUBLE_ARRAY = 31 };
```

**Missing:**
- ❌ `std::monostate` for empty values
- ❌ Struct support (VSS 4.0)
- ❌ Signal quality indicators
- ❌ QualifiedValue (value + quality + timestamp)

### libvss-types (from libvssdag dependency)
```cpp
namespace vss::types {

using Value = std::variant<
    std::monostate,  // Empty/uninitialized
    bool, int32_t, uint32_t, int64_t, uint64_t, float, double, std::string,
    std::vector<bool>, ..., std::vector<std::string>,
    std::shared_ptr<StructValue>,              // VSS 4.0
    std::vector<std::shared_ptr<StructValue>>
>;

enum class SignalQuality {
    UNKNOWN, VALID, INVALID, NOT_AVAILABLE, STALE, OUT_OF_RANGE
};

template<typename T>
struct QualifiedValue {
    std::optional<T> value;
    SignalQuality quality;
    std::chrono::system_clock::time_point timestamp;

    bool is_valid() const;
    bool is_invalid() const;
    bool is_not_available() const;
};

} // namespace vss::types
```

## Design Principle: Deep Integration

**Use vss::types directly throughout the API - no aliases or re-exports.**

Users should include both headers:
```cpp
#include <vss/types/types.hpp>    // For Value, QualifiedValue, SignalQuality
#include <kuksa_cpp/client.hpp>   // For Client, SignalHandle
```

This makes it clear that:
- `vss::types` is the shared VSS type system
- `kuksa` provides KUKSA-specific communication layer

## Quality Mapping Strategy

### Writing to KUKSA (Quality Loss is Acceptable)

**Rule:** Only publish values with `SignalQuality::VALID`. Everything else becomes empty.

| QualifiedValue | Sent to KUKSA | Subscribers Receive | Rationale |
|---|---|---|---|
| `{120.5, VALID}` | `float = 120.5` | `{120.5, VALID}` | ✅ Valid data |
| `{120.5, INVALID}` | *(empty)* | `{nullopt, NOT_AVAILABLE}` | ⚠️ Cannot send invalid as valid |
| `{nullopt, INVALID}` | *(empty)* | `{nullopt, NOT_AVAILABLE}` | ❌ No valid value |
| `{nullopt, NOT_AVAILABLE}` | *(empty)* | `{nullopt, NOT_AVAILABLE}` | ❌ Unavailable |
| `{120.5, STALE}` | *(empty)* | `{nullopt, NOT_AVAILABLE}` | ⚠️ Too old to trust |
| `{120.5, OUT_OF_RANGE}` | *(empty)* | `{nullopt, NOT_AVAILABLE}` | ⚠️ Out of spec |

**Critical Safety Rule:** Never send a value with quality ≠ VALID, because KUKSA subscribers cannot distinguish it from valid data. Sending invalid data as if it's valid is dangerous.

### Reading from KUKSA (Conservative Inference)

| KUKSA Protobuf | QualifiedValue | Rationale |
|---|---|---|
| Has value | `{value, VALID}` | Assume valid if present |
| No value / empty | `{nullopt, NOT_AVAILABLE}` | Conservative default |
| Failure set | `{nullopt, INVALID}` | Explicit error |

## Refactored API

### Client Interface

```cpp
namespace kuksa {

template<typename T>
class SignalHandle {
public:
    // Callback receives QualifiedValue
    using Callback = std::function<void(vss::types::QualifiedValue<T>)>;

    const std::string& path() const;
    int32_t id() const;
    vss::types::ValueType type() const;
    SignalClass signal_class() const;  // KUKSA-specific
};

class Client {
public:
    // === Read Operations ===
    // Always returns QualifiedValue
    Result<vss::types::QualifiedValue<T>> get(const SignalHandle<T>& handle);

    // === Write Operations ===
    // Primary: Accept QualifiedValue (quality validated)
    Status set(const SignalHandle<T>& handle, const vss::types::QualifiedValue<T>& value);

    // Convenience: Plain value (assumes VALID quality)
    Status set(const SignalHandle<T>& handle, const T& value) {
        return set(handle, vss::types::QualifiedValue<T>{value, vss::types::SignalQuality::VALID});
    }

    // === Subscribe (Async) ===
    // Callback receives QualifiedValue
    Status subscribe(
        const SignalHandle<T>& handle,
        std::function<void(vss::types::QualifiedValue<T>)> callback
    );

    // === Publish (Provider Stream) ===
    // Primary: Accept QualifiedValue (quality handled per rules)
    Status publish(const SignalHandle<T>& handle, const vss::types::QualifiedValue<T>& value);

    // Convenience: Plain value
    Status publish(const SignalHandle<T>& handle, const T& value);
};

} // namespace kuksa
```

### Implementation: Quality-Aware Publishing

```cpp
template<typename T>
Status Client::publish(const SignalHandle<T>& handle, const vss::types::QualifiedValue<T>& qvalue) {
    // Only publish if quality is VALID and value exists
    if (qvalue.quality == vss::types::SignalQuality::VALID && qvalue.value.has_value()) {
        return publish_impl(handle.id(), *qvalue.value);
    }

    // Any other quality: send empty datapoint to notify subscribers
    LOG(WARNING) << "Publishing empty for " << handle.path()
                 << " (quality=" << vss::types::signal_quality_to_string(qvalue.quality) << ")";

    return publish_empty_impl(handle.id());
}

template<typename T>
Status Client::set(const SignalHandle<T>& handle, const vss::types::QualifiedValue<T>& qvalue) {
    // Synchronous set - reject non-VALID quality
    if (qvalue.quality != vss::types::SignalQuality::VALID || !qvalue.value.has_value()) {
        return Status(absl::StatusCode::kInvalidArgument,
                      "Cannot set value with quality != VALID");
    }

    return set_impl(handle.id(), *qvalue.value);
}
```

### Implementation: Quality Inference on Read

```cpp
template<typename T>
Result<vss::types::QualifiedValue<T>> Client::get(const SignalHandle<T>& handle) {
    auto result = get_impl(handle.id());
    if (!result.ok()) {
        return result.status();
    }

    // Infer quality from presence of value
    if (result->has_value()) {
        return vss::types::QualifiedValue<T>{
            **result,
            vss::types::SignalQuality::VALID,
            std::chrono::system_clock::now()
        };
    } else {
        return vss::types::QualifiedValue<T>{
            std::nullopt,
            vss::types::SignalQuality::NOT_AVAILABLE,
            std::chrono::system_clock::now()
        };
    }
}
```

## Usage Examples

### Example 1: libvssdag → libkuksa-cpp Integration

```cpp
#include <vss/types/types.hpp>
#include <kuksa_cpp/client.hpp>
#include <vssdag/signal_processor.h>

using namespace vss::types;

// libvssdag produces QualifiedValue from CAN
vssdag::SignalProcessorDAG dag;
auto can_updates = can_source.get_updates();
auto vss_signals = dag.process_signal_updates(can_updates);

// libkuksa-cpp consumes QualifiedValue naturally
auto client = kuksa::Client::create("localhost:55555");
auto resolver = kuksa::Resolver::create("localhost:55555");
auto speed_handle = resolver->get<float>("Vehicle.Speed");

for (const auto& signal : vss_signals) {
    if (signal.path == "Vehicle.Speed") {
        // Direct integration - no conversion!
        client->publish(*speed_handle, signal);  // signal is QualifiedValue<float>

        // Quality is preserved in application, lost only at KUKSA wire boundary
        if (signal.quality == SignalQuality::INVALID) {
            LOG(WARNING) << "CAN speed sensor failed - publishing empty to KUKSA";
        }
    }
}
```

### Example 2: Sensor Failure Handling

```cpp
// CAN sensor detects 0xFF (invalid pattern)
QualifiedValue<float> battery{std::nullopt, SignalQuality::INVALID};

// Publish to KUKSA - sends empty datapoint (NOT skipped!)
client->publish(battery_handle, battery);
// Logs: "Publishing empty for Vehicle.Battery.Voltage (quality=INVALID)"

// All subscribers are notified
client->subscribe(battery_handle, [](QualifiedValue<float> bat) {
    if (bat.is_valid()) {
        LOG(INFO) << "Battery: " << *bat.value << "V";
        use_battery_value(*bat.value);
    } else {
        LOG(ERROR) << "Battery sensor failed (quality="
                   << signal_quality_to_string(bat.quality) << ")";
        enter_safe_mode();  // Critical - subscribers MUST be notified
    }
});
```

### Example 3: Convenience Methods

```cpp
// Power users: Explicit quality control
client->publish(handle, QualifiedValue{120.5f, SignalQuality::VALID});

// Simple users: Assume valid
client->publish(handle, 120.5f);  // Equivalent to VALID

// Reading always returns QualifiedValue
auto result = client->get(handle);
if (result.ok() && result->is_valid()) {
    LOG(INFO) << "Value: " << *result->value;
}
```

## Struct Support (VSS 4.0) - Blocked

### What We Can Do Now

```cpp
// Accept struct types in resolver (metadata only)
auto pos_handle = resolver->get<vss::types::StructValue>("Vehicle.Cabin.Infotainment.Navigation.Position");

// Returns error with clear message
if (!pos_handle.ok()) {
    LOG(ERROR) << pos_handle.status();
    // "Struct type 'Position' found in VSS but not supported by KUKSA databroker v2"
}
```

### When KUKSA Adds Struct Support

```cpp
// This will "just work" when KUKSA protobuf is extended
vss::types::StructValue pos{"Position"};
pos.set_field("Latitude", 37.7749);
pos.set_field("Longitude", -122.4194);

auto qvalue = QualifiedValue{pos, SignalQuality::VALID};
client->set(*pos_handle, qvalue);  // Converts to new KUKSA struct protobuf
```

## Implementation Plan

### Phase 1: Add libvss-types Dependency (1 day)

**Files to modify:**
- `CMakeLists.txt` - Add vss::types dependency
- `.github/workflows/ci.yml` - Already installs libvss-types v0.0.1 ✅

**Testing:**
- Build succeeds with libvss-types linked
- No API changes yet

### Phase 2: Refactor Core Types (2 days)

**Files to modify:**
- `include/kuksa_cpp/types.hpp` - Use vss::types directly, remove duplicates
- `src/vss/vss_types.cpp` - Remove redundant implementations
- `include/kuksa_cpp/client.hpp` - Update signatures to use QualifiedValue
- `src/vss/vss_client.cpp` - Implement quality-aware logic

**Changes:**
```cpp
// OLD
Result<std::optional<T>> get(const SignalHandle<T>& handle);
Status set(const SignalHandle<T>& handle, const T& value);
Status subscribe(const SignalHandle<T>&, std::function<void(std::optional<T>)>);

// NEW
Result<vss::types::QualifiedValue<T>> get(const SignalHandle<T>& handle);
Status set(const SignalHandle<T>& handle, const vss::types::QualifiedValue<T>& value);
Status set(const SignalHandle<T>& handle, const T& value);  // Convenience
Status subscribe(const SignalHandle<T>&, std::function<void(vss::types::QualifiedValue<T>)>);
```

**Testing:**
- All existing tests updated to use QualifiedValue
- New tests for quality mapping rules
- Test empty datapoint publishing
- Test quality inference on read

### Phase 3: Update Examples and Documentation (1 day)

**Files to modify:**
- `README.md` - Document quality mapping, struct limitations
- `examples/*.cpp` - Update to use QualifiedValue
- `TESTING.md` - Add quality-aware test scenarios

**New documentation:**
- Quality mapping rules (write/read)
- Safety rationale for empty on non-VALID
- Struct support roadmap

### Phase 4: Protobuf Helper Functions (1 day)

**Files to modify:**
- `src/vss/vss_types.cpp` - Add quality-aware conversions

**New functions:**
```cpp
// Convert QualifiedValue to protobuf (with quality rules)
Datapoint to_proto_datapoint(const vss::types::QualifiedValue<T>& qvalue);

// Convert protobuf to QualifiedValue (with inference)
template<typename T>
vss::types::QualifiedValue<T> from_proto_datapoint(const Datapoint& dp);

// Publish empty datapoint (for non-VALID quality)
Status publish_empty_impl(int32_t signal_id);
```

## KUKSA Protobuf Extensions Needed

### 1. Signal Quality Support 🔴 CRITICAL

**Current State:** No quality field in `Datapoint`

**Needed:**
```protobuf
enum SignalQuality {
  SIGNAL_QUALITY_UNSPECIFIED = 0;
  SIGNAL_QUALITY_VALID = 1;
  SIGNAL_QUALITY_INVALID = 2;
  SIGNAL_QUALITY_NOT_AVAILABLE = 3;
  SIGNAL_QUALITY_STALE = 4;
  SIGNAL_QUALITY_OUT_OF_RANGE = 5;
}

message Datapoint {
  google.protobuf.Timestamp timestamp = 1;
  SignalQuality quality = 2;  // NEW
  oneof value {
    Failure failure_value = 10;
    string string_value = 11;
    // ... other types
  }
}
```

**Use Cases:**
- CAN signal invalid/unavailable detection (libvssdag 0xFF patterns)
- Sensor failures, out-of-range values
- Stale data detection
- Automotive standards compliance (AUTOSAR, DDS QoS)

**Impact:**
- libkuksa-cpp can stop inferring quality and use actual values
- libvssdag → KUKSA → subscribers preserves full signal state
- Safety-critical applications get reliable quality information

### 2. Struct Type Support 🔴 CRITICAL for VSS 4.0

**Current State:** No struct support in DataType enum or Datapoint

**Needed:**
```protobuf
enum DataType {
  // ... existing primitive types
  DATA_TYPE_STRUCT = 40;
  DATA_TYPE_STRUCT_ARRAY = 41;
}

message StructField {
  string name = 1;
  Datapoint value = 2;  // Recursive - field can be any type
}

message StructValue {
  string type_name = 1;  // e.g., "Position", "DeliveryInfo"
  repeated StructField fields = 2;
}

message Datapoint {
  // ... existing fields
  oneof value {
    // ... existing types
    StructValue struct_value = 50;
    StructArray struct_array_value = 51;
  }
}

message StructArray {
  repeated StructValue values = 1;
}
```

**VSS 4.0 Examples:**
```yaml
# VSS 4.0 specification
Vehicle.Cabin.Infotainment.Navigation.DestinationSet.Destination:
  type: branch
  aggregate: true  # This is a struct!

# Fields:
Vehicle.Cabin.Infotainment.Navigation.DestinationSet.Destination.Latitude:
  datatype: double

Vehicle.Cabin.Infotainment.Navigation.DestinationSet.Destination.Longitude:
  datatype: double
```

**Impact:**
- Cannot represent VSS 4.0 aggregate types without this
- Forces unnatural flattening of logical data groups
- Loses semantic meaning and type safety

### 3. Enhanced Metadata (Medium Priority)

**Current:** Limited metadata in `GetMetadataResponse`

**Needed:**
```protobuf
message Metadata {
  DataType data_type = 1;
  EntryType entry_type = 2;
  string description = 3;

  // NEW
  string unit = 4;              // "km/h", "°C", "bar"
  optional double min_value = 5;
  optional double max_value = 6;
  repeated string allowed_values = 7;  // For enums

  // For struct types
  optional string struct_type_name = 10;
  repeated FieldMetadata struct_fields = 11;
}

message FieldMetadata {
  string name = 1;
  Metadata metadata = 2;  // Recursive for nested structs
}
```

**Benefits:**
- Runtime validation of values against VSS spec
- Better error messages ("value 200 exceeds max 150")
- Type-safe enum handling

## Testing Strategy

### Unit Tests

```cpp
TEST(QualityMapping, PublishValid) {
    QualifiedValue<float> qv{120.5f, SignalQuality::VALID};
    auto status = client->publish(handle, qv);
    EXPECT_TRUE(status.ok());
    // Verify protobuf has value
}

TEST(QualityMapping, PublishInvalid) {
    QualifiedValue<float> qv{120.5f, SignalQuality::INVALID};
    auto status = client->publish(handle, qv);
    EXPECT_TRUE(status.ok());
    // Verify protobuf is EMPTY (not skipped!)
}

TEST(QualityMapping, PublishNotAvailable) {
    QualifiedValue<float> qv{std::nullopt, SignalQuality::NOT_AVAILABLE};
    auto status = client->publish(handle, qv);
    EXPECT_TRUE(status.ok());
    // Verify empty datapoint sent
}

TEST(QualityMapping, ReadWithValue) {
    // KUKSA returns value
    auto result = client->get(handle);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result->quality, SignalQuality::VALID);
    EXPECT_TRUE(result->value.has_value());
}

TEST(QualityMapping, ReadNoValue) {
    // KUKSA returns empty
    auto result = client->get(handle);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result->quality, SignalQuality::NOT_AVAILABLE);
    EXPECT_FALSE(result->value.has_value());
}
```

### Integration Tests

```cpp
TEST(Integration, SensorFailureNotification) {
    // Publish invalid value
    QualifiedValue<float> invalid{std::nullopt, SignalQuality::INVALID};
    client->publish(speed_handle, invalid);

    // Subscriber receives notification (not skipped!)
    bool callback_invoked = false;
    client->subscribe(speed_handle, [&](QualifiedValue<float> qv) {
        callback_invoked = true;
        EXPECT_FALSE(qv.is_valid());
        EXPECT_EQ(qv.quality, SignalQuality::NOT_AVAILABLE);
    });

    client->start();
    wait_for_condition([&] { return callback_invoked; }, 5s);
    EXPECT_TRUE(callback_invoked);
}
```

## Documentation

### README.md - New Section

```markdown
## Signal Quality

libkuksa-cpp uses `vss::types::QualifiedValue<T>` which includes:
- `value`: The actual data (`std::optional<T>`)
- `quality`: Signal state (VALID/INVALID/NOT_AVAILABLE/STALE/OUT_OF_RANGE)
- `timestamp`: When the value was captured

### Quality Mapping

⚠️ **KUKSA databroker v2 does not support signal quality.**

**When publishing (write):**
- Only `VALID` quality values are sent to KUKSA
- Any other quality (INVALID/NOT_AVAILABLE/STALE) → empty datapoint is sent
- Subscribers are **always notified** (never skipped) to ensure safety

**When reading (read):**
- KUKSA has value → `{value, VALID}`
- KUKSA no value → `{nullopt, NOT_AVAILABLE}`

### Safety Principle

**Never send invalid data as if it's valid.** Since KUKSA cannot represent quality, we send empty datapoints for non-VALID quality to prevent subscribers from using unreliable data.

```cpp
// Sensor failure detected by libvssdag
QualifiedValue<float> speed{std::nullopt, SignalQuality::INVALID};

// Empty datapoint sent to KUKSA - subscribers notified
client->publish(speed_handle, speed);

// Subscriber can take safe action
client->subscribe(speed_handle, [](QualifiedValue<float> s) {
    if (!s.is_valid()) {
        disable_cruise_control();  // Safe fallback
    }
});
```
```

## Migration for Existing Users

### Breaking Changes

1. **Callback signatures changed:**
```cpp
// OLD
client->subscribe(handle, [](std::optional<T> value) { ... });

// NEW
client->subscribe(handle, [](vss::types::QualifiedValue<T> qvalue) { ... });
```

2. **Get returns QualifiedValue:**
```cpp
// OLD
Result<std::optional<T>> result = client->get(handle);

// NEW
Result<vss::types::QualifiedValue<T>> result = client->get(handle);
```

### Migration Guide

```cpp
// OLD CODE
auto result = client->get(handle);
if (result.ok() && result->has_value()) {
    use_value(**result);
}

// NEW CODE
auto result = client->get(handle);
if (result.ok() && result->is_valid()) {
    use_value(*result->value);
}

// Or even simpler
if (result.ok() && result->value.has_value()) {
    use_value(*result->value);
}
```

## Benefits

### For Users
✅ **Type Safety:** Quality is always explicit
✅ **Safety:** Invalid data cannot masquerade as valid
✅ **Integration:** Seamless with libvssdag
✅ **Future-Proof:** Ready for KUKSA quality support

### For Developers
✅ **Less Code:** Remove duplicate type implementations
✅ **Shared Types:** Same VSS model as libvssdag
✅ **Clear Semantics:** Quality mapping rules are explicit

### For Ecosystem
✅ **Standardization:** Single VSS type system for C++
✅ **Interoperability:** libkuksa-cpp + libvssdag aligned
✅ **Path Forward:** Clear requirements for KUKSA

## Timeline

| Phase | Duration | Status |
|---|---|---|
| Phase 1: Add dependency | 1 day | Ready to start |
| Phase 2: Refactor types | 2 days | Ready to start |
| Phase 3: Update docs/examples | 1 day | Ready to start |
| Phase 4: Protobuf helpers | 1 day | Ready to start |
| **Total** | **~1 week** | |

## Next Steps

1. ✅ Get approval for deep integration approach
2. Start Phase 1: Add libvss-types dependency
3. File KUKSA issues for quality + struct support
4. Implement Phases 2-4
5. Update all examples and documentation
