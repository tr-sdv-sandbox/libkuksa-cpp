#pragma once

#include <string>
#include <functional>
#include <any>
#include <unordered_map>
#include <glog/logging.h>

namespace sdv {

// Forward declarations
template<typename StateT>
class StateMachine;

/**
 * @brief Context passed to conditions and actions
 */
using Context = std::unordered_map<std::string, std::any>;

/**
 * @brief Transition between two states
 * 
 * @tparam StateT State enum type
 */
template<typename StateT>
class Transition {
public:
    StateT from_state;
    StateT to_state;
    std::string trigger;
    std::function<bool(const Context&)> condition;
    std::function<void(const Context&)> action;
    
    Transition(StateT from, StateT to, std::string trig)
        : from_state(from), to_state(to), trigger(std::move(trig)) {
        VLOG(3) << "Created transition: " << trigger;
    }
    
    /**
     * @brief Set the condition for this transition
     */
    Transition& when(std::function<bool(const Context&)> cond) {
        condition = std::move(cond);
        return *this;
    }
    
    /**
     * @brief Set the action for this transition
     */
    Transition& then(std::function<void(const Context&)> act) {
        action = std::move(act);
        return *this;
    }
    
    /**
     * @brief Check if this transition can be taken
     */
    bool can_transition(const Context& ctx) const {
        if (!condition) {
            return true;  // No condition means always allowed
        }
        
        try {
            bool result = condition(ctx);
            VLOG(2) << "Condition result for " << trigger << ": " << result;
            return result;
        } catch (const std::exception& e) {
            LOG(ERROR) << "Condition evaluation failed: " << e.what();
            return false;
        }
    }
    
    /**
     * @brief Execute the transition action
     */
    void execute(const Context& ctx) const {
        if (action) {
            try {
                action(ctx);
                VLOG(2) << "Executed action for " << trigger;
            } catch (const std::exception& e) {
                LOG(ERROR) << "Action execution failed: " << e.what();
                throw;  // Re-throw to let state machine handle
            }
        }
    }
};

} // namespace kuksa