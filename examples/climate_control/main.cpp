#include "climate_control.hpp"
#include <glog/logging.h>
#include <memory>
#include <cstdlib>
#include <csignal>

std::unique_ptr<ClimateProtectionSystem> g_system;

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        LOG(INFO) << "Received shutdown signal";
        if (g_system) {
            g_system->stop();
        }
    }
}

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::string kuksa_url = "localhost:55555";
    if (const char* env_addr = std::getenv("KUKSA_ADDRESS")) {
        if (const char* env_port = std::getenv("KUKSA_PORT")) {
            kuksa_url = std::string(env_addr) + ":" + std::string(env_port);
        }
    }

    LOG(INFO) << "=== Climate Protection System ===";
    LOG(INFO) << "Connecting to KUKSA at: " << kuksa_url;

    g_system = std::make_unique<ClimateProtectionSystem>(kuksa_url);
    g_system->run();

    LOG(INFO) << "Climate protection system exited";
    return 0;
}
