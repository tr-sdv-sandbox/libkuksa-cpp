/**
 * @file kuksa_logger.cpp
 * @brief Simple KUKSA signal logger - like candump for VSS signals
 *
 * Usage:
 *   kuksa_logger                              # Log all Vehicle.** signals
 *   kuksa_logger "Vehicle.Speed"              # Log specific signal
 *   kuksa_logger "Vehicle.Cabin.**"           # Log signals matching pattern
 *   kuksa_logger --address localhost:61234   # Use different address
 */

#include <kuksa_cpp/client.hpp>
#include <kuksa_cpp/resolver.hpp>
#include <vss/types/struct.hpp>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include <atomic>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <chrono>
#include <thread>

DEFINE_string(address, "localhost:55555", "KUKSA databroker address");
DEFINE_string(pattern, "Vehicle", "Signal branch to subscribe to (e.g., Vehicle, Vehicle.Speed, Vehicle.Cabin)");
DEFINE_bool(timestamp, true, "Show timestamps");
DEFINE_bool(quiet, false, "Suppress startup messages");
DEFINE_int32(ready_timeout, 30, "Timeout in seconds waiting for subscriptions to be ready");

std::atomic<bool> g_shutdown{false};

void signal_handler(int) {
    g_shutdown = true;
}

std::string format_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

template<typename T>
std::string format_vector(const std::vector<T>& vec) {
    std::stringstream ss;
    ss << "[";
    for (size_t i = 0; i < vec.size(); ++i) {
        if (i > 0) ss << ", ";
        if constexpr (std::is_same_v<T, std::string>) {
            ss << "\"" << vec[i] << "\"";
        } else if constexpr (std::is_same_v<T, bool>) {
            ss << (vec[i] ? "true" : "false");
        } else {
            ss << vec[i];
        }
        if (i >= 5 && vec.size() > 7) {
            ss << ", ... (" << (vec.size() - i - 1) << " more)";
            break;
        }
    }
    ss << "]";
    return ss.str();
}

std::string format_value(const vss::types::Value& value) {
    return std::visit([](auto&& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool>) {
            return v ? "true" : "false";
        } else if constexpr (std::is_same_v<T, std::string>) {
            return "\"" + v + "\"";
        } else if constexpr (std::is_same_v<T, std::monostate>) {
            return "<empty>";
        } else if constexpr (std::is_floating_point_v<T>) {
            std::stringstream ss;
            ss << std::fixed << std::setprecision(2) << v;
            return ss.str();
        } else if constexpr (std::is_same_v<T, std::shared_ptr<vss::types::StructValue>>) {
            return "<struct>";
        } else if constexpr (std::is_same_v<T, std::vector<std::shared_ptr<vss::types::StructValue>>>) {
            return "<struct[]>[" + std::to_string(v.size()) + "]";
        } else if constexpr (std::is_same_v<T, std::vector<bool>>) {
            return format_vector(v);
        } else if constexpr (std::is_same_v<T, std::vector<int8_t>>) {
            return format_vector(v);
        } else if constexpr (std::is_same_v<T, std::vector<int16_t>>) {
            return format_vector(v);
        } else if constexpr (std::is_same_v<T, std::vector<int32_t>>) {
            return format_vector(v);
        } else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
            return format_vector(v);
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            return format_vector(v);
        } else if constexpr (std::is_same_v<T, std::vector<uint16_t>>) {
            return format_vector(v);
        } else if constexpr (std::is_same_v<T, std::vector<uint32_t>>) {
            return format_vector(v);
        } else if constexpr (std::is_same_v<T, std::vector<uint64_t>>) {
            return format_vector(v);
        } else if constexpr (std::is_same_v<T, std::vector<float>>) {
            return format_vector(v);
        } else if constexpr (std::is_same_v<T, std::vector<double>>) {
            return format_vector(v);
        } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
            return format_vector(v);
        } else if constexpr (std::is_integral_v<T>) {
            return std::to_string(static_cast<int64_t>(v));
        } else {
            return "<unknown>";
        }
    }, value);
}

int main(int argc, char** argv) {
    google::InitGoogleLogging(argv[0]);
    gflags::SetUsageMessage(
        "KUKSA signal logger - like candump for VSS signals\n"
        "Usage: kuksa_logger [--address=HOST:PORT] [--pattern=PATTERN]"
    );
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // Use positional arg as pattern if provided
    if (argc > 1) {
        FLAGS_pattern = argv[1];
    }

    FLAGS_logtostderr = true;
    FLAGS_minloglevel = FLAGS_quiet ? 2 : 0;  // Suppress INFO if quiet

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    if (!FLAGS_quiet) {
        std::cerr << "KUKSA Signal Logger" << std::endl;
        std::cerr << "  Address: " << FLAGS_address << std::endl;
        std::cerr << "  Pattern: " << FLAGS_pattern << std::endl;
        std::cerr << "  Press Ctrl+C to stop" << std::endl;
        std::cerr << "==========================================" << std::endl;
    }

    // Create resolver
    auto resolver_result = kuksa::Resolver::create(FLAGS_address);
    if (!resolver_result.ok()) {
        std::cerr << "Failed to connect to KUKSA at " << FLAGS_address << std::endl;
        std::cerr << "Error: " << resolver_result.status() << std::endl;
        return 1;
    }
    auto resolver = std::move(*resolver_result);

    // Create client
    auto client_result = kuksa::Client::create(FLAGS_address);
    if (!client_result.ok()) {
        std::cerr << "Failed to create client: " << client_result.status() << std::endl;
        return 1;
    }
    auto client = std::move(*client_result);

    // List signals matching the pattern
    auto signals_result = resolver->list_signals(FLAGS_pattern);
    if (!signals_result.ok()) {
        std::cerr << "Failed to list signals for pattern '" << FLAGS_pattern << "': "
                  << signals_result.status() << std::endl;
        return 1;
    }

    auto& handles = *signals_result;
    if (handles.empty()) {
        std::cerr << "No signals found matching pattern: " << FLAGS_pattern << std::endl;
        return 1;
    }

    if (!FLAGS_quiet) {
        std::cerr << "Subscribing to " << handles.size() << " signals" << std::endl;
        std::cerr << "==========================================" << std::endl;
    }

    // Subscribe to all matching signals
    for (const auto& handle : handles) {
        std::string signal_path = handle->path();

        client->subscribe(*handle, [signal_path](const vss::types::DynamicQualifiedValue& qv) {
            std::string line;

            if (FLAGS_timestamp) {
                line += format_timestamp() + "  ";
            }

            line += signal_path;

            if (!vss::types::is_empty(qv.value)) {
                line += " = " + format_value(qv.value);
            } else {
                line += " = <no value>";
            }

            // Quality indicator
            switch (qv.quality) {
                case vss::types::SignalQuality::VALID:
                    break;  // No indicator for valid
                case vss::types::SignalQuality::NOT_AVAILABLE:
                    line += " [N/A]";
                    break;
                case vss::types::SignalQuality::INVALID:
                    line += " [INVALID]";
                    break;
                case vss::types::SignalQuality::UNKNOWN:
                    line += " [UNKNOWN]";
                    break;
            }

            std::cout << line << std::endl;
        });
    }

    // Start client
    auto start_status = client->start();
    if (!start_status.ok()) {
        std::cerr << "Failed to start client: " << start_status << std::endl;
        return 1;
    }

    // Wait for ready (may take a while with many signals)
    if (!FLAGS_quiet) {
        std::cerr << "Waiting up to " << FLAGS_ready_timeout << "s for subscriptions..." << std::endl;
    }
    auto ready_status = client->wait_until_ready(std::chrono::seconds(FLAGS_ready_timeout));
    if (!ready_status.ok()) {
        std::cerr << "Client failed to become ready: " << ready_status << std::endl;
        return 1;
    }

    // Main loop - wait for shutdown
    while (!g_shutdown) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!FLAGS_quiet) {
        std::cerr << "\nShutting down..." << std::endl;
    }

    client->stop();
    return 0;
}
