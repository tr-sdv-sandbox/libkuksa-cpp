# VSS Types Refactoring - Status

## Completed ✅

### Phase 1: Add libvss-types Dependency
- ✅ Updated CMakeLists.txt to fetch libvss-types v0.0.1
- ✅ Linked vss::types to kuksa_cpp library
- ✅ Build succeeds with libvss-types

### Phase 2: Core Types Refactoring (In Progress)
- ✅ Refactored `include/kuksa_cpp/types.hpp`:
  - Removed duplicate Value variant (now uses `vss::types::Value`)
  - Removed duplicate ValueType enum (now uses `vss::types::ValueType`)
  - Removed duplicate utilities (get_value_type, value_type_to_string, are_types_compatible)
  - Updated SignalHandle<T>::Callback to use `vss::types::QualifiedValue<T>`
  - Kept only KUKSA-specific types (SignalClass, SignalHandle, DynamicSignalHandle)

- ✅ Updated `src/vss/vss_types.cpp`:
  - Removed duplicate implementations
  - File now placeholder for future protobuf conversion functions

## Remaining Work 🚧

### Phase 2: Client API Refactoring
Need to update:

1. **`include/kuksa_cpp/client.hpp`** - Update signatures:
   ```cpp
   // OLD
   Result<std::optional<T>> get(const SignalHandle<T>&);
   Status set(const SignalHandle<T>&, const T&);
   Status subscribe(const SignalHandle<T>&, std::function<void(std::optional<T>)>);

   // NEW
   Result<vss::types::QualifiedValue<T>> get(const SignalHandle<T>&);
   Status set(const SignalHandle<T>&, const vss::types::QualifiedValue<T>&);
   Status set(const SignalHandle<T>&, const T&);  // Convenience
   Status subscribe(const SignalHandle<T>&, std::function<void(vss::types::QualifiedValue<T>)>);
   Status publish(const SignalHandle<T>&, const vss::types::QualifiedValue<T>&);
   Status publish(const SignalHandle<T>&, const T&);  // Convenience
   ```

2. **`src/vss/vss_client.cpp`** - Implement quality mapping:
   - `get()` - Infer quality from KUKSA response (has value → VALID, no value → NOT_AVAILABLE)
   - `set()` - Reject non-VALID quality
   - `publish()` - Send empty datapoint if quality != VALID
   - `subscribe()` - Convert KUKSA updates to QualifiedValue

3. **`src/vss/vss_client.cpp`** - Add protobuf conversion:
   ```cpp
   // Convert QualifiedValue to protobuf (with quality rules)
   Datapoint to_proto_datapoint(const vss::types::QualifiedValue<T>& qvalue);

   // Convert protobuf to QualifiedValue (with inference)
   template<typename T>
   vss::types::QualifiedValue<T> from_proto_datapoint(const Datapoint& dp);

   // Publish empty datapoint (for non-VALID quality)
   Status publish_empty_impl(int32_t signal_id);
   ```

4. **`include/kuksa_cpp/resolver.hpp`** - Update to use `vss::types::ValueType`

5. **`src/vss/resolver.cpp`** - Update protobuf type conversions

### Phase 2: Tests
All tests need updates:

- `tests/test_all_vss_types.cpp` - Update to use vss::types
- `tests/test_accessor_handle_creation.cpp` - Update to use vss::types
- `tests/integration/*.cpp` - Update callbacks to receive QualifiedValue
- Add new tests for quality mapping rules

### Phase 3: Examples
All examples need updates:

- `examples/unified_client_simple.cpp`
- `examples/door_example.cpp`
- `examples/vehicle_example.cpp`
- `examples/subscriber_wait_example.cpp`
- Update to use `vss::types::QualifiedValue<T>` in callbacks
- Add quality handling examples

### Phase 3: Documentation
- ✅ Created VSS_TYPES_REFACTORING.md (design doc)
- ⏳ Update README.md with:
  - Quality mapping rules
  - Usage examples with QualifiedValue
  - Struct support status
  - Migration guide

## Breaking Changes

### API Changes
1. **Callback signatures changed:**
   ```cpp
   // OLD
   client->subscribe(handle, [](std::optional<T> value) { ... });

   // NEW
   client->subscribe(handle, [](vss::types::QualifiedValue<T> qvalue) {
       if (qvalue.is_valid()) {
           use_value(*qvalue.value);
       }
   });
   ```

2. **Get returns QualifiedValue:**
   ```cpp
   // OLD
   Result<std::optional<T>> result = client->get(handle);

   // NEW
   Result<vss::types::QualifiedValue<T>> result = client->get(handle);
   ```

3. **Removed functions (use vss::types instead):**
   - `kuksa::get_value_type()` → `vss::types::get_value_type()`
   - `kuksa::value_type_to_string()` → `vss::types::value_type_to_string()`
   - `kuksa::value_type_from_string()` → `vss::types::value_type_from_string()`
   - `kuksa::are_types_compatible()` → `vss::types::are_types_compatible()`

### Migration Guide

#### Before
```cpp
#include <kuksa_cpp/kuksa.hpp>

auto result = client->get(handle);
if (result.ok() && result->has_value()) {
    use_value(**result);
}

client->subscribe(handle, [](std::optional<float> value) {
    if (value) {
        LOG(INFO) << "Value: " << *value;
    }
});
```

#### After
```cpp
#include <vss/types/types.hpp>
#include <kuksa_cpp/kuksa.hpp>

using namespace vss::types;

auto result = client->get(handle);
if (result.ok() && result->is_valid()) {
    use_value(*result->value);
}

client->subscribe(handle, [](QualifiedValue<float> qvalue) {
    if (qvalue.is_valid()) {
        LOG(INFO) << "Value: " << *qvalue.value;
    } else {
        LOG(WARNING) << "Value unavailable: " << signal_quality_to_string(qvalue.quality);
    }
});
```

## Build Status

- ✅ Phase 1 builds successfully
- ⚠️ Phase 2 will not build until client.hpp and implementations are updated
- ⚠️ Tests will fail until updated to new API

## Next Steps

1. Update `include/kuksa_cpp/client.hpp` with new signatures
2. Update `src/vss/vss_client.cpp` implementation
3. Update `include/kuksa_cpp/resolver.hpp` and `src/vss/resolver.cpp`
4. Fix all compilation errors
5. Update all tests
6. Update all examples
7. Update documentation
8. Full build and test

## Timeline Estimate

- Client API update: 2-3 hours
- Implementation: 3-4 hours
- Tests: 2-3 hours
- Examples: 1-2 hours
- Documentation: 1 hour

**Total**: ~1-2 days of work

## Notes

- This is a breaking change but worth it for deep integration with libvssdag
- Quality mapping rules are well-defined and documented
- When KUKSA adds quality support, we just update the mapping - API stays the same
