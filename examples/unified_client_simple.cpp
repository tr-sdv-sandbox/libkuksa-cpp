/**
 * @file unified_client_simple.cpp
 * @brief Simple demonstration of unified Client
 *
 * Shows how to use Client for:
 * - Providing actuators (with handle storage)
 * - Subscribing to signals
 * - Publishing sensor values (no registration needed!)
 * - Batch publishing (efficient!)
 */

#include <kuksa_cpp/client.hpp>
#include <kuksa_cpp/resolver.hpp>
#include <glog/logging.h>
#include <thread>
#include <chrono>

using namespace kuksa;
using namespace std::chrono_literals;

int main(int argc, char** argv) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;

    LOG(INFO) << "=== Unified Client Example ===\n";

    const std::string address = "localhost:55555";

    // Create resolver and client
    auto resolver_result = Resolver::create(address);
    if (!resolver_result.ok()) {
        LOG(ERROR) << "Failed to create resolver: " << resolver_result.status();
        return 1;
    }
    auto resolver = std::move(*resolver_result);

    auto client = *Client::create(address);

    // ========================================================================
    // 1. Provide an actuator (resolve handle BEFORE registration!)
    // ========================================================================

    // Resolve the actuator handle externally
    auto door_lock_result = resolver->get<bool>("Vehicle.Cabin.Door.Row1.DriverSide.IsLocked");
    if (!door_lock_result.ok()) {
        LOG(ERROR) << "Failed to get door lock handle: " << door_lock_result.status();
        return 1;
    }
    auto door_lock_handle = *door_lock_result;

    // Register actuator with the handle
    auto serve_status = client->serve_actuator(door_lock_handle, [](bool target, const SignalHandle<bool>& handle) {
        LOG(INFO) << "Actuation request for " << handle.path() << ": " << target;

        // In real code: queue to state machine/hardware controller
        // Don't block here - this runs on gRPC thread!
        // Handle is available if needed (e.g., for logging, type info)
    });
    if (!serve_status.ok()) {
        LOG(ERROR) << "Failed to register actuator: " << serve_status;
        return 1;
    }

    // ========================================================================
    // 2. Subscribe to a signal
    // ========================================================================
    auto speed_result = resolver->get<float>("Vehicle.Speed");
    if (!speed_result.ok()) {
        LOG(ERROR) << "Failed to get speed handle";
        return 1;
    }
    auto speed = *speed_result;

    client->subscribe(speed, [](vss::types::QualifiedValue<float> qvalue) {
        if (qvalue.is_valid()) {
            LOG(INFO) << "Speed update: " << *qvalue.value << " km/h";
        }
    });

    // ========================================================================
    // 3. Start and wait for ready
    // ========================================================================
    auto start_status = client->start();
    if (!start_status.ok()) {
        LOG(ERROR) << "Failed to start client: " << start_status;
        return 1;
    }

    auto ready_status = client->wait_until_ready(5000ms);
    if (!ready_status.ok()) {
        LOG(ERROR) << "Client not ready: " << ready_status;
        return 1;
    }

    LOG(INFO) << "✓ Client ready!\n";

    // ========================================================================
    // 4. Publish sensor values (no registration needed!)
    // ========================================================================
    LOG(INFO) << "Publishing sensor values:";

    // Get RW handles for sensors we want to publish
    auto rpm_result = resolver->get<uint32_t>("Vehicle.Powertrain.CombustionEngine.Speed");
    auto temp_result = resolver->get<float>("Vehicle.Cabin.Temperature");
    auto speed_rw_result = resolver->get<float>("Vehicle.Speed");

    if (rpm_result.ok() && temp_result.ok() && speed_rw_result.ok()) {
        auto rpm_handle = *rpm_result;
        auto temp_handle = *temp_result;
        auto speed_rw_handle = *speed_rw_result;

        // Single publish (handles are values, not pointers)
        auto rpm_status = client->publish(rpm_handle, uint32_t(3000));
        if (rpm_status.ok()) {
            LOG(INFO) << "  Published RPM: 3000";
        } else {
            LOG(ERROR) << "  Failed to publish RPM: " << rpm_status;
        }

        auto temp_status = client->publish(temp_handle, 22.5f);
        if (temp_status.ok()) {
            LOG(INFO) << "  Published Temperature: 22.5°C";
        } else {
            LOG(ERROR) << "  Failed to publish Temperature: " << temp_status;
        }

        // Batch publish (efficient! - type-safe without explicit Value{})
        LOG(INFO) << "\nBatch publishing 3 sensor values:";
        auto batch_status = client->publish_batch(
            {
                {rpm_handle, uint32_t(3500)},
                {temp_handle, 23.0f},
                {speed_rw_handle, 120.5f}
            },
            [](const std::map<int32_t, absl::Status>& errors) {
                if (errors.empty()) {
                    LOG(INFO) << "✓ Batch publish succeeded!";
                } else {
                    for (const auto& [id, status] : errors) {
                        LOG(ERROR) << "Signal " << id << " failed: " << status;
                    }
                }
            }
        );
        if (!batch_status.ok()) {
            LOG(ERROR) << "Batch publish failed: " << batch_status;
        }
    }

    // ========================================================================
    // 5. Publish actuator actual value (using same handle from registration!)
    // ========================================================================
    std::this_thread::sleep_for(1s);

    LOG(INFO) << "\nPublishing actuator actual value:";
    auto status = client->publish(door_lock_handle, true);
    if (status.ok()) {
        LOG(INFO) << "✓ Published door lock actual: true";
    } else {
        LOG(ERROR) << "Failed to publish: " << status;
    }

    std::this_thread::sleep_for(2s);

    // ========================================================================
    // Cleanup
    // ========================================================================
    client->stop();

    LOG(INFO) << "\n=== Key Takeaways ===";
    LOG(INFO) << "1. Actuators: Register with serve_actuator() BEFORE start() (claims ownership)";
    LOG(INFO) << "2. Sensors: NO registration needed! Just publish() anytime after start()";
    LOG(INFO) << "3. Publishing: Works for both actuators and sensors (no duplication!)";
    LOG(INFO) << "4. Batch publish: Efficient for multiple sensors";
    LOG(INFO) << "5. Single connection: One TCP connection for everything";

    return 0;
}
