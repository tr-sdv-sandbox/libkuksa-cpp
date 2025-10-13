/**
 * @file state_machine.cpp
 * @brief Implementation of non-template methods for state machine
 */

#include <kuksa_cpp/state_machine/state_machine.hpp>
#include <glog/logging.h>

namespace sdv {

// Any non-template implementations would go here
// Most of the state machine is header-only due to templates

void log_state_machine_version() {
    LOG(INFO) << "SDV State Machine SDK v0.1.0";
    LOG(INFO) << "Using Google glog for logging";
}

} // namespace kuksa