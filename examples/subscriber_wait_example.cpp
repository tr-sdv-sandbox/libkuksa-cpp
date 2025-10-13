/**
 * @file subscriber_wait_example.cpp
 * @brief Example showing how to wait for subscriber to be ready
 */

#include <kuksa_cpp/kuksa.hpp>
#include <kuksa_cpp/resolver.hpp>
#include <glog/logging.h>
#include <chrono>
#include <thread>

using namespace kuksa;
using namespace std::chrono_literals;

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;

    const std::string address = "localhost:55555";

    LOG(INFO) << "=== Client wait_until_ready() Example ===\n";

    // Example 1: Wait for ready before proceeding
    {
        LOG(INFO) << "Example 1: Synchronous startup pattern";
        LOG(INFO) << "========================================";

        auto subscriber = *Client::create(address);

        // Set up subscriptions (using test signal)
        auto resolver = Resolver::create(address);
        if (!resolver.ok()) {
            LOG(ERROR) << "Failed to create resolver: " << resolver.status();
            return 1;
        }

        auto speed_result = (*resolver)->get<float>("Vehicle.Speed");
        if (!speed_result.ok()) {
            LOG(ERROR) << "Failed to get speed handle: " << speed_result.status();
            return 1;
        }
        auto speed = *speed_result;

        subscriber->subscribe(speed, [](std::optional<float> value) {
            if (value) {
                LOG(INFO) << "Speed: " << *value << " km/h";
            }
        });

        // Start subscriber
        LOG(INFO) << "Starting subscriber...";
        subscriber->start();

        // Wait for it to be ready (with 5 second timeout)
        LOG(INFO) << "Waiting for subscriber to be ready (timeout: 5s)...";
        auto ready_status = subscriber->wait_until_ready(5000ms);

        if (ready_status.ok()) {
            LOG(INFO) << "✓ Subscriber is READY and streaming!";
            LOG(INFO) << "  Current status: " << subscriber->status();
        } else if (absl::IsDeadlineExceeded(ready_status)) {
            LOG(ERROR) << "✗ Timeout waiting for subscriber";
            LOG(ERROR) << "  Current status: " << subscriber->status();
            return 1;
        } else {
            LOG(ERROR) << "✗ Failed to connect: " << ready_status;
            return 1;
        }

        // Now we can be confident that subscription is active
        LOG(INFO) << "Application can now proceed knowing subscriber is operational";

        std::this_thread::sleep_for(2s);
        subscriber->stop();
    }

    // Example 2: Non-blocking startup with status polling
    {
        LOG(INFO) << "\nExample 2: Non-blocking startup pattern";
        LOG(INFO) << "========================================";

        auto subscriber = *Client::create(address);

        auto resolver = Resolver::create(address);
        auto speed_result = (*resolver)->get<float>("Vehicle.Speed");
        auto speed = *speed_result;

        subscriber->subscribe(speed, [](std::optional<float> value) {
            if (value) {
                LOG(INFO) << "Speed: " << *value << " km/h";
            }
        });

        subscriber->start();

        // Don't wait - just check status periodically
        LOG(INFO) << "Starting subscriber (non-blocking)";

        for (int i = 0; i < 10; ++i) {
            auto status = subscriber->status();

            if (status.ok()) {
                LOG(INFO) << "✓ Subscriber ready after " << i << " checks";
                break;
            } else {
                LOG(INFO) << "Status: " << status.message();
                std::this_thread::sleep_for(500ms);
            }
        }

        subscriber->stop();
    }

    // Example 3: Handle connection failure gracefully
    {
        LOG(INFO) << "\nExample 3: Connection failure handling";
        LOG(INFO) << "=======================================";

        auto subscriber = *Client::create("invalid.address:99999");

        auto resolver = Resolver::create(address);
        auto speed_result = (*resolver)->get<float>("Vehicle.Speed");
        auto speed = *speed_result;

        subscriber->subscribe(speed, [](std::optional<float> value) {});

        subscriber->start();

        // This should timeout
        LOG(INFO) << "Waiting for connection to invalid address (1s timeout)...";
        auto ready_status = subscriber->wait_until_ready(1000ms);

        if (!ready_status.ok()) {
            LOG(INFO) << "✓ Expected failure: " << ready_status;
            LOG(INFO) << "  Application can handle this gracefully";
        }

        subscriber->stop();
    }

    LOG(INFO) << "\n=== All examples completed ===";
    google::ShutdownGoogleLogging();
    return 0;
}
