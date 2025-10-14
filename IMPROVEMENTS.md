# libkuksa-cpp Improvement Backlog

## Critical Issues

### 1. Race condition in Resolver::create()
**Location**: `src/vss/resolver.cpp:46-60`
**Issue**: Multiple threads can race on channel creation - mutex not held during channel wait
**Impact**: Connection failures or crashes
**Fix**: Hold mutex during channel creation and wait
**Status**: ✅ **NOT AN ISSUE** - Mutex IS held during entire connect() operation including WaitForConnected()

### 2. Use-after-free in callbacks
**Location**: `include/sdv/vss/vss.hpp:89`
**Issue**: Example shows capturing `client_ptr` in callback - if client destroyed while callback pending, undefined behavior
**Impact**: Crashes in production
**Fix**: Document lifetime requirements clearly, consider weak_ptr pattern
**Status**: ✅ **FIXED** - Added comprehensive callback lifetime safety documentation in vss.hpp:132-165 with safe patterns (weak_ptr) and anti-patterns. Fixed example to use weak_ptr.

### 3. Unprotected stub_ access
**Location**: `src/vss/vss_client.cpp:297, 365`
**Issue**: Multiple threads call sync operations (get, publish) without synchronization
**Mitigation**: gRPC stubs are thread-safe (but undocumented in code)
**Fix**: Add comment explaining thread-safety relies on gRPC guarantees
**Status**: ✅ **FIXED** - Same documentation added in #7 covers this (vss_client.cpp:293-305)

## High Priority Issues

### 4. Inconsistent factory methods
**Issue**:
- `Resolver::create()` returns `Result<unique_ptr<Resolver>>`
- `Client::create()` returns `unique_ptr<Client>` (never fails)
**Impact**: Inconsistent error handling patterns, confusion
**Fix**: Make `Client::create()` return `Result<>` for consistency
**Status**: ✅ **FIXED** - Client::create() now returns `Result<unique_ptr<Client>>` for consistency with Resolver

### 5. Mixed use of Result<T> and absl::Status
**Issue**: Some methods return `Result<T>`, others return `absl::Status` without clear pattern
**Impact**: Inconsistent error handling
**Fix**: Establish clear convention:
- Methods returning values: use `Result<T>`
- Methods without return values: use `Status`
- Added type aliases: `using Status = absl::Status` and `using Result<T> = absl::StatusOr<T>`
**Status**: ✅ **FIXED** - Aliased both types for consistency, updated all code to use `kuksa::Status` and `kuksa::Result<T>`

### 6. subscribe() and serve_actuator() async semantics
**Issue**: `subscribe()` and `serve_actuator()` return void/Status but errors only discoverable after `start()`
**Impact**: Silent failures, difficult debugging
**Fix**: Documented clearly that these are async configuration operations - actual connection and validation happens in `start()`. Errors are reported via `start()` or `wait_until_ready()`.
**Status**: ✅ **FIXED** - Added comprehensive documentation explaining async semantics

### 7. Document thread-safety assumptions
**Issue**: Code relies on gRPC stub thread-safety without documentation
**Impact**: Future maintainers may add incorrect synchronization
**Fix**: Add comments explaining thread-safety model
**Status**: ✅ **FIXED** - Added comprehensive documentation in vss_client.cpp:293-305 explaining thread-safety relies on gRPC guarantees

## Medium Priority Issues

### 8. No validation of signal paths
**Issue**: Resolver accepts any string, failure only at gRPC level
**Impact**: Poor error messages
**Fix**: Add path validation (regex for VSS format)
**Status**: Open

### 9. Vector iteration without lock
**Location**: `src/vss/vss_client.cpp:641-654`
**Issue**: `actuator_handlers_` iterated without lock (safe if immutable after start())
**Fix**: Add assertion that `!running_` in `serve_actuator_impl()`
**Status**: ✅ **ALREADY SAFE** - serve_actuator_impl() checks `running_` at line 244-245, preventing modifications after start(). Iteration is safe because vector is immutable after start().

### 10. fetch_initial_values() can block
**Location**: `src/vss/vss_client.cpp:749-767`
**Issue**: Synchronous RPC calls in subscriber thread startup
**Impact**: Slow startup with many subscriptions or high latency
**Fix**: Add timeout or make async
**Status**: Open

### 11. Subscription callback in critical path
**Location**: `src/vss/vss_client.cpp:783`
**Issue**: User callback executes with `subscriptions_mutex_` held
**Impact**: Slow callback blocks all subscription updates
**Fix**: Copy callback outside lock, invoke after unlock
**Status**: ✅ **ALREADY FIXED** - handle_subscription_update() at lines 779-798 copies callback outside lock (line 786), releases lock (line 788), then invokes callback (line 793). No blocking issue.

### 12. Sequential batch publishing
**Location**: `src/vss/vss_client.cpp:403-408`
**Issue**: `publish_batch()` implemented as loop of individual publishes, not true batching
**Impact**: No performance benefit, misleading API
**Fix**: Use PublishValuesRequest (single RPC for multiple values)
**Status**: Open

### 13. Callback constraints not enforced
**Issue**: Documentation warns about blocking callbacks, but no runtime checks
**Impact**: Deadlocks or poor performance
**Fix**: Add timeout or warning log for slow callbacks (>100ms)
**Status**: Open

### 14. No way to query subscription status
**Issue**: Can't check if subscribe() succeeded until after start()
**Impact**: Difficult to debug
**Fix**: Add `get_subscription_status(handle)` or return Status from subscribe()
**Status**: Open

## Low Priority Issues

### 15. Handle cache never invalidates
**Location**: `src/vss/resolver.cpp:160`
**Issue**: If VSS schema reloads, cached handles have stale IDs
**Impact**: Operations fail with "signal not found"
**Fix**: Add TTL or cache invalidation method
**Status**: Open

### 16. publish_batch() error handling
**Location**: `src/vss/vss_client.cpp:396-416`
**Issue**: Returns generic "some failed" instead of detailed status
**Impact**: Difficult to debug batch publish failures
**Fix**: Return Status with structured error info
**Status**: Open

### 17. fetch_initial_values() is O(n) RPCs
**Location**: `src/vss/vss_client.cpp:752-760`
**Issue**: One GetValue() call per subscription
**Impact**: Startup time grows linearly
**Fix**: Batch GetValue() calls (lower priority - resolver perf not critical per design)
**Status**: Open

### 18. No connection pooling
**Issue**: Each Client creates new gRPC channel
**Impact**: Resource waste if multiple clients to same broker
**Fix**: Shared channel pool or factory
**Status**: Open

### 19. String copies in type conversions
**Location**: `src/vss/vss_client.cpp:102, 159`
**Issue**: String arrays copied during proto conversions
**Impact**: High memory allocation for string arrays
**Fix**: Move semantics or reserve capacity
**Status**: Open

### 20. Confusing handle ownership semantics
**Issue**: SignalHandle<T> wraps shared_ptr but acts like value type
**Impact**: Unclear lifetime
**Fix**: Document ownership model clearly
**Status**: Open

### 21. publish() vs set() confusion
**Issue**: Both can publish sensor values, unclear which to use
**Impact**: API confusion
**Fix**: Clarify in documentation or deprecate one
**Status**: Open

## Missing Features (Future)

### 22. No authentication/authorization
**Issue**: Always uses InsecureChannelCredentials
**Impact**: Cannot use in production with secure brokers
**Fix**: Add TLS/token support
**Status**: Backlog

### 23. No streaming publish
**Issue**: publish_batch() not using true streaming
**Impact**: Performance limitation for high-frequency sensor feeders
**Fix**: Implement streaming publish API
**Status**: Backlog

### 24. No query capabilities
**Issue**: Can't list available signals or browse VSS tree
**Impact**: Poor discoverability
**Fix**: Add ListMetadata() API
**Status**: Backlog

### 25. Limited observability
**Issue**: No metrics (pub/sub counts, latency, errors)
**Impact**: Difficult to monitor in production
**Fix**: Add metrics interface
**Status**: Backlog

### 26. No graceful degradation
**Issue**: Connection failure stops all operations
**Impact**: Poor resilience
**Fix**: Automatic reconnection for sync operations
**Status**: Backlog

## Architectural Refactoring (Long-term)

### 27. Split VSSClientImpl
**Issue**: Single class handles Provider, Subscriber, Accessor concerns (886 lines)
**Fix**: Separate into focused classes, Client becomes facade
**Status**: Backlog

### 28. Separate read-only/read-write handles
**Issue**: Single handle type, permissions enforced at runtime
**Fix**: Compile-time enforcement via separate types
**Status**: Backlog

### 29. Callback interface improvements
**Issue**: No cancellation or context support
**Fix**: Add context parameter to callbacks
**Status**: Backlog

## Assumptions / Design Decisions

- **Resolver performance**: Apps call resolve once per signal at startup, list is small → caching/batching not critical
- **Async subscribe()**: subscribe() is configuration only, nothing happens until start() → no Result needed
- **Thread safety**: Rely on gRPC stub thread-safety guarantees
