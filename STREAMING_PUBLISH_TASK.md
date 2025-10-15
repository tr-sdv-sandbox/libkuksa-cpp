# Streaming Publish Implementation Task

## Problem

Currently `publish()` and `publish_batch()` use synchronous blocking RPCs, which defeats their purpose for high-frequency sensor gateways/feeders.

```cpp
// Current implementation - BLOCKS on each call!
Status publish_impl(int32_t signal_id, const Value& value) {
    // ... uses stub_->PublishValue() - synchronous blocking RPC
}

Status publish_batch_impl(...) {
    // Just loops calling publish_impl() - no actual batching!
    for (const auto& [signal_id, value] : values) {
        auto status = publish_impl(signal_id, value);  // Sequential blocking calls
    }
}
```

## Impact

**Sensor gateways** publishing at 10Hz or 100Hz are **blocked** on each publish, causing:
- Poor throughput
- High latency
- Wasted CPU (blocking threads)
- Defeats the purpose of having `publish()` separate from `set()`

## Intended Design vs Current Implementation

### Intended:
- **`set()`** - Synchronous one-shot writes/commands (current actuator, occasional sensor updates)
- **`publish()`** - Asynchronous streaming publish for high-frequency telemetry (sensor gateways)
- **`publish_batch()`** - Efficient batched publish in single RPC

### Current Reality:
- **`set()`** - Synchronous ✅ (correct)
- **`publish()`** - Synchronous ❌ (should be async/streaming)
- **`publish_batch()`** - Just a loop ❌ (should be true batching)

## Solution Approach

### 1. Use Provider Stream for Async Publish

The provider stream (`OpenProviderStream`) already exists and is bidirectional. We can use it to send `PublishValuesRequest` messages asynchronously.

```cpp
// Provider stream already handles:
// - OpenProviderStreamRequest with ProvideActuationRequest (registration)
// - OpenProviderStreamRequest with PublishValuesRequest (publish values)

Status publish_impl(int32_t signal_id, const Value& value) {
    // Queue to provider stream (async, non-blocking)
    if (!provider_stream_) {
        return absl::FailedPreconditionError("Provider stream not started");
    }

    // Add to pending queue
    {
        std::lock_guard<std::mutex> lock(publish_queue_mutex_);
        publish_queue_[signal_id] = value;
    }

    // Signal provider thread to flush
    publish_cv_.notify_one();

    return absl::OkStatus();  // Returns immediately
}
```

### 2. Batch Publishing in Provider Thread

```cpp
void provider_thread() {
    while (running_) {
        // Wait for publish requests or batching timeout
        std::map<int32_t, Value> to_publish;
        {
            std::unique_lock<std::mutex> lock(publish_queue_mutex_);
            publish_cv_.wait_for(lock, 100ms, [&]{ return !publish_queue_.empty() || !running_; });

            if (!publish_queue_.empty()) {
                to_publish = std::move(publish_queue_);
                publish_queue_.clear();
            }
        }

        if (!to_publish.empty()) {
            send_publish_values_request(to_publish);  // Single batched RPC
        }

        // Handle incoming actuation requests...
    }
}

void send_publish_values_request(const std::map<int32_t, Value>& values) {
    OpenProviderStreamRequest request;
    auto* publish_req = request.mutable_publish_values_request();

    for (const auto& [signal_id, value] : values) {
        auto* entry = publish_req->add_datapoints();
        entry->mutable_signal_id()->set_id(signal_id);
        *entry->mutable_datapoint()->mutable_value() = to_proto_value(value);
    }

    stream_->Write(request);  // Single RPC for all values
}
```

### 3. True Batch Publishing

```cpp
Status publish_batch_impl(
    const std::map<int32_t, Value>& values,
    std::function<void(const std::map<int32_t, Status>&)> callback) {

    // Queue entire batch atomically
    {
        std::lock_guard<std::mutex> lock(publish_queue_mutex_);
        for (const auto& [signal_id, value] : values) {
            publish_queue_[signal_id] = value;
        }

        if (callback) {
            batch_callbacks_.push_back(callback);
        }
    }

    publish_cv_.notify_one();
    return absl::OkStatus();
}
```

## Implementation Steps

### Phase 1: Basic Async Publish
1. Add `publish_queue_` and `publish_queue_mutex_` to VSSClientImpl
2. Modify `publish_impl()` to queue instead of blocking
3. Modify provider thread to flush queue periodically (e.g., every 100ms or when queue reaches threshold)
4. Send `PublishValuesRequest` via provider stream

### Phase 2: Optimize Batching
1. Add configurable flush interval (e.g., `set_publish_interval(ms)`)
2. Add queue size threshold for immediate flush
3. Implement true `publish_batch()` that groups values into single message

### Phase 3: Error Handling & Callbacks
1. Handle `PublishValuesResponse` from provider stream
2. Map errors back to signal IDs
3. Invoke batch callbacks with error map
4. Add timeout/retry logic for failed publishes

## Testing Considerations

1. **High-frequency publishing**: Verify 100Hz+ publish rate works without blocking
2. **Batching efficiency**: Confirm multiple publishes within flush interval are batched
3. **Error propagation**: Ensure publish errors are reported via callbacks
4. **Stream restart**: Verify publish queue survives stream reconnection
5. **Memory bounds**: Ensure queue doesn't grow unbounded if stream is slow

## Related Issues

- **#12**: Sequential batch publishing - publish_batch() not batching
- **#23**: No streaming publish - missing feature
- **#21**: publish() vs set() confusion - once async, distinction is clear

## API Impact

**None!** The public API stays the same:
```cpp
Status publish(handle, value);  // Just becomes async internally
Status publish_batch(entries, callback);  // Just becomes true batching
```

Users don't need to change code - they just get better performance.

## Performance Benefits

**Before (synchronous)**:
- 10 values @ 100Hz = 1000 blocking RPCs/sec
- Each RPC ~1-5ms latency
- Thread blocked 1-5 seconds/sec = unusable

**After (async batched)**:
- 10 values @ 100Hz queued
- Flushed in batches every 100ms = 10 RPCs/sec with 10 values each
- No blocking - returns immediately
- ~100x reduction in RPC overhead

## Considerations

1. **Sync vs Async Trade-off**
   - Async publish means no immediate error feedback
   - Must use callbacks for error handling
   - Document clearly that publish() returns before confirmation

2. **Provider Stream Requirement**
   - Async publish requires provider stream to be running
   - If stream not started, fall back to sync RPC or return error?

3. **Flush Policy**
   - Time-based (every 100ms)?
   - Size-based (every 100 values)?
   - Hybrid (whichever comes first)?
   - Make configurable?

4. **Backward Compatibility**
   - Should `set()` stay synchronous? Yes - for one-shot operations
   - Should we offer both sync and async publish? Or just async?

## Decision Needed

Should `publish()` require `start()` to have been called?
- **Option A**: Yes - async requires provider stream running
- **Option B**: No - fall back to sync RPC if stream not available

Recommendation: **Option A** - clearer semantics, encourages proper usage pattern
