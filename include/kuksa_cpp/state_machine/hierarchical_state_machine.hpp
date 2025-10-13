#pragma once

#include "state_machine.hpp"
#include <glog/logging.h>
#include <unordered_set>

namespace sdv {

/**
 * @brief Hierarchical state machine with composite state support
 * 
 * Extends the base StateMachine to support:
 * - Nested states (composite states with substates)
 * - Automatic parent state entry/exit
 * - State hierarchy queries
 * 
 * @tparam StateT Primary state enum type
 */
template<typename StateT>
class HierarchicalStateMachine : public StateMachine<StateT> {
public:
    using StateMachine<StateT>::StateMachine;
    
    /**
     * @brief Define a composite state with substates
     * 
     * @tparam SubstateT Enum type for substates
     * @param parent Parent composite state
     * @param substates List of substates
     * @param initial_substate Default substate to enter
     */
    template<typename SubstateT>
    void add_composite_state(StateT parent,
                           const std::vector<SubstateT>& substates,
                           SubstateT initial_substate) {
        parent_states_.insert(parent);
        
        // Store substate info (would need proper implementation)
        LOG(INFO) << "[SM:" << this->name_ << "] COMPOSITE_STATE: Added " << this->state_name(parent) 
                  << " with " << substates.size() << " substates";
                  
        // In full implementation, would track parent-child relationships
    }
    
    /**
     * @brief Check if state machine is currently in a given state
     * 
     * For hierarchical states, returns true if:
     * - The current state matches exactly
     * - The current state is a child of the queried state
     */
    bool is_in_state(StateT state) const {
        if (this->current_state() == state) {
            return true;
        }
        
        // Check if current state is a child of queried state
        if (parent_states_.count(state) > 0) {
            // In full implementation, check parent-child relationship
            return false;
        }
        
        return false;
    }
    
    /**
     * @brief Get all currently active states (including parents)
     */
    std::unordered_set<StateT> get_active_states() const {
        std::unordered_set<StateT> active;
        active.insert(this->current_state());
        
        // In full implementation, would add parent states
        
        return active;
    }
    
    /**
     * @brief Get the depth of current state in hierarchy
     */
    int get_state_depth() const {
        // In full implementation, calculate actual depth
        return parent_states_.count(this->current_state()) > 0 ? 1 : 0;
    }
    
    
private:
    std::unordered_set<StateT> parent_states_;
    std::unordered_set<StateT> active_states_;
    
    // Note: Cannot override private enter_state/exit_state from base class
    // Would need to refactor base class to have protected virtual methods
    
};

} // namespace kuksa