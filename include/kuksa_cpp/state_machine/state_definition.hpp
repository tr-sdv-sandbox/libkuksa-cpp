#pragma once

#include <string>
#include <functional>
#include <glog/logging.h>

namespace sdv {

/**
 * @brief Definition of a state with entry/exit actions
 * 
 * @tparam StateT State enum type
 */
template<typename StateT>
class StateDefinition {
public:
    std::string name;
    std::function<void()> entry_action;
    std::function<void()> exit_action;
    
    explicit StateDefinition(std::string state_name) 
        : name(std::move(state_name)) {
        VLOG(3) << "Created state definition for: " << name;
    }
    
    /**
     * @brief Set the entry action for this state
     */
    StateDefinition& on_entry(std::function<void()> action) {
        entry_action = std::move(action);
        VLOG(2) << "Added entry action to state: " << name;
        return *this;
    }
    
    /**
     * @brief Set the exit action for this state
     */
    StateDefinition& on_exit(std::function<void()> action) {
        exit_action = std::move(action);
        VLOG(2) << "Added exit action to state: " << name;
        return *this;
    }
    
    /**
     * @brief Execute entry action if defined
     */
    void enter() const {
        if (entry_action) {
            try {
                entry_action();
                VLOG(1) << "Executed entry action for state: " << name;
            } catch (const std::exception& e) {
                LOG(ERROR) << "Entry action failed for " << name << ": " << e.what();
                throw;
            }
        }
    }
    
    /**
     * @brief Execute exit action if defined
     */
    void exit() const {
        if (exit_action) {
            try {
                exit_action();
                VLOG(1) << "Executed exit action for state: " << name;
            } catch (const std::exception& e) {
                LOG(ERROR) << "Exit action failed for " << name << ": " << e.what();
                throw;
            }
        }
    }
};

} // namespace kuksa