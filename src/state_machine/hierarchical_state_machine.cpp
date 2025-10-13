/**
 * @file hierarchical_state_machine.cpp
 * @brief Implementation of non-template methods for hierarchical state machine
 */

#include <kuksa_cpp/state_machine/hierarchical_state_machine.hpp>
#include <glog/logging.h>

namespace sdv {

// Any non-template implementations would go here
// Most of the hierarchical state machine is header-only due to templates

void log_hierarchical_features() {
    LOG(INFO) << "Hierarchical State Machine features:";
    LOG(INFO) << " - Composite states with substates";
    LOG(INFO) << " - Automatic parent state entry/exit";
    LOG(INFO) << " - State depth tracking";
    LOG(INFO) << " - Enhanced VSS introspection for state hierarchy";
}

} // namespace kuksa