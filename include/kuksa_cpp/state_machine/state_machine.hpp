#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <optional>
#include <any>
#include <iomanip>
#include <glog/logging.h>

#ifdef SDV_WITH_PROMETHEUS
#include <prometheus/counter.h>
#include <prometheus/histogram.h>
#include <prometheus/info.h>
#include <prometheus/registry.h>
#endif

namespace sdv {

// Forward declarations
template<typename StateT>
class StateDefinition;

template<typename StateT>
class Transition;

// Type aliases
using Context = std::unordered_map<std::string, std::any>;

template<typename StateT>
using ConditionFunc = std::function<bool(const Context&)>;

template<typename StateT>
using ConditionAsyncFunc = std::function<std::future<bool>(const Context&)>;

template<typename StateT>
using ActionFunc = std::function<void(const Context&)>;

template<typename StateT>
using ActionAsyncFunc = std::function<std::future<void>(const Context&)>;

template<typename StateT>
using StateNameFunc = std::function<std::string(StateT)>;


/**
 * @brief Thread-safe state machine with Prometheus metrics and structured logging
 * 
 * Provides full observability through structured log output:
 * - [SM:name] TRANSITION: from -> to | trigger=event
 * - [SM:name] STATE: current=state
 * - [SM:name] BLOCKED: trigger='event' from=X to=Y reason=...
 * 
 * @tparam StateT Enum class defining the states
 */
template<typename StateT>
class StateMachine {
    static_assert(std::is_enum_v<StateT>, "StateT must be an enum type");
    
public:
    /**
     * @brief Construct a new State Machine
     * 
     * @param name Name of the state machine (used for metrics and logging)
     * @param initial_state Initial state
     */
    explicit StateMachine(std::string name, StateT initial_state)
        : name_(std::move(name))
        , current_state_(initial_state)
        , state_entry_time_(std::chrono::system_clock::now()) {
        
        init_metrics();
    }
    
    /**
     * @brief Set custom state name function for logging
     */
    void set_state_name_function(StateNameFunc<StateT> func) {
        state_name_func_ = std::move(func);
        
        // Log initial state with proper name
        LOG(INFO) << "[SM:" << name_ << "] INIT: state=" << state_name(current_state_);
    }
    
    /**
     * @brief Add a transition between states
     */
    void add_transition(StateT from_state,
                       StateT to_state,
                       std::string trigger,
                       ConditionFunc<StateT> condition = {},
                       ActionFunc<StateT> action = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto key = make_transition_key(from_state, trigger);
        transitions_[key].push_back(
            std::make_unique<Transition<StateT>>(
                from_state, to_state, std::move(trigger),
                std::move(condition), std::move(action)
            )
        );
    }
    
    /**
     * @brief Define a state with entry/exit actions
     */
    StateDefinition<StateT>& define_state(StateT state) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto [it, inserted] = state_definitions_.emplace(
            state,
            std::make_unique<StateDefinition<StateT>>(state_name(state))
        );
        
        return *it->second;
    }
    
    /**
     * @brief Trigger a state transition
     * 
     * @param event The trigger event
     * @param context Optional context data
     * @return true if transition occurred, false otherwise
     */
    bool trigger(const std::string& event, const Context& context = {}) {
        auto future = trigger_async(event, context);
        return future.get();
    }
    
    /**
     * @brief Trigger a state transition asynchronously
     */
    std::future<bool> trigger_async(const std::string& event, 
                                   const Context& context = {}) {
        return std::async(std::launch::async, [this, event, context]() {
            return execute_transition(event, context);
        });
    }
    
    /**
     * @brief Get current state
     */
    StateT current_state() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_state_;
    }
    
    /**
     * @brief Get current state name as string
     */
    std::string current_state_name() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_name(current_state_);
    }
    
    /**
     * @brief Get available triggers from current state
     */
    std::vector<std::string> available_triggers() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<std::string> triggers;
        auto prefix = std::to_string(static_cast<int>(current_state_.load())) + ":";
        
        for (const auto& [key, _] : transitions_) {
            if (key.find(prefix) == 0) {
                triggers.push_back(key.substr(prefix.length()));
            }
        }
        
        return triggers;
    }
    
    
    
#ifdef SDV_WITH_PROMETHEUS
    /**
     * @brief Get Prometheus metrics registry
     */
    prometheus::Registry& metrics_registry() {
        return *metrics_registry_;
    }
#endif
    
protected:
    std::string name_;
    mutable std::mutex mutex_;
    std::atomic<StateT> current_state_;
    StateNameFunc<StateT> state_name_func_;
    
    // State management
    std::unordered_map<StateT, std::unique_ptr<StateDefinition<StateT>>> state_definitions_;
    std::unordered_map<std::string, std::vector<std::unique_ptr<Transition<StateT>>>> transitions_;
    std::chrono::system_clock::time_point state_entry_time_;
    
#ifdef SDV_WITH_PROMETHEUS
    // Prometheus metrics
    std::shared_ptr<prometheus::Registry> metrics_registry_;
    prometheus::Gauge* state_gauge_;
    prometheus::Histogram* state_duration_;
    prometheus::Counter* transition_counter_;
    prometheus::Histogram* transition_latency_;
    prometheus::Info* state_info_;
#endif
    
    void init_metrics() {
#ifdef SDV_WITH_PROMETHEUS
        metrics_registry_ = std::make_shared<prometheus::Registry>();
        
        auto metric_name = name_;
        std::transform(metric_name.begin(), metric_name.end(), metric_name.begin(), ::tolower);
        
        // Current state gauge
        auto& state_gauge_family = prometheus::BuildGauge()
            .Name(metric_name + "_state")
            .Help("Current state of " + name_)
            .Register(*metrics_registry_);
        state_gauge_ = &state_gauge_family.Add({});
        
        // State duration histogram
        auto& state_duration_family = prometheus::BuildHistogram()
            .Name(metric_name + "_state_duration_seconds")
            .Help("Time spent in each state")
            .Register(*metrics_registry_);
        state_duration_ = &state_duration_family.Add({{"state", ""}}, 
            prometheus::Histogram::BucketBoundaries{0.1, 0.5, 1, 5, 10, 30, 60, 120, 300, 600});
        
        // Transition counter
        auto& transition_counter_family = prometheus::BuildCounter()
            .Name(metric_name + "_transitions_total")
            .Help("Total state transitions")
            .Register(*metrics_registry_);
        transition_counter_ = &transition_counter_family.Add({{"from_state", ""}, {"to_state", ""}, {"trigger", ""}});
        
        // Transition latency histogram
        auto& transition_latency_family = prometheus::BuildHistogram()
            .Name(metric_name + "_transition_latency_seconds")
            .Help("Latency of state transitions")
            .Register(*metrics_registry_);
        transition_latency_ = &transition_latency_family.Add({{"from_state", ""}, {"to_state", ""}},
            prometheus::Histogram::BucketBoundaries{0.001, 0.005, 0.01, 0.05, 0.1, 0.5, 1});
#endif
    }
    
    
    std::string make_transition_key(StateT state, const std::string& trigger) const {
        return std::to_string(static_cast<int>(state)) + ":" + trigger;
    }
    
protected:
    std::string state_name(StateT state) const {
        if (state_name_func_) {
            return state_name_func_(state);
        }
        // Fallback to numeric representation
        return "State_" + std::to_string(static_cast<int>(state));
    }
    
private:
    bool execute_transition(const std::string& event, const Context& context) {
        auto start_time = std::chrono::steady_clock::now();
        
        
        std::unique_lock<std::mutex> lock(mutex_);
        
        auto key = make_transition_key(current_state_, event);
        auto it = transitions_.find(key);
        
        if (it == transitions_.end()) {
            VLOG(1) << "[SM:" << name_ << "] IGNORED: trigger='" << event << "' state=" << state_name(current_state_) << " reason=no_transition";
            return false;
        }
        
        // Find valid transition
        for (const auto& transition : it->second) {
            // Check condition
            if (transition->condition && !transition->condition(context)) {
                VLOG(1) << "[SM:" << name_ << "] BLOCKED: trigger='" << event << "' from=" << state_name(current_state_) << " to=" << state_name(transition->to_state) << " reason=condition_failed";
                continue;
            }
            
            // Valid transition found
            auto old_state = current_state_.load();
            
            LOG(INFO) << "[SM:" << name_ << "] TRANSITION: " 
                      << state_name(old_state) << " -> " << state_name(transition->to_state) 
                      << " | trigger=" << event;
            
            // Exit current state
            exit_state(old_state);
            
            // Execute transition action
            if (transition->action) {
                lock.unlock();
                transition->action(context);
                lock.lock();
            }
            
            // Enter new state
            enter_state(transition->to_state);
            current_state_ = transition->to_state;
            
            // Record metrics
            record_metrics(start_time);
            
            // Log current state for test framework
            LOG(INFO) << "[SM:" << name_ << "] STATE: current=" << state_name(current_state_);
            
            return true;
        }
        
        return false;
    }
    
    void enter_state(StateT state) {
        state_entry_time_ = std::chrono::system_clock::now();
        
        // Execute entry action
        auto it = state_definitions_.find(state);
        if (it != state_definitions_.end() && it->second->entry_action) {
            it->second->entry_action();
        }
        
        // Update metrics
#ifdef SDV_WITH_PROMETHEUS
        state_gauge_->Set(static_cast<double>(state));
#endif
        
    }
    
    void exit_state(StateT state) {
        // Record time in state
        auto duration = std::chrono::duration<double>(
            std::chrono::system_clock::now() - state_entry_time_
        ).count();
        
#ifdef SDV_WITH_PROMETHEUS
        state_duration_->Observe(duration);
#endif
        
        // Execute exit action
        auto it = state_definitions_.find(state);
        if (it != state_definitions_.end() && it->second->exit_action) {
            it->second->exit_action();
        }
    }
    
    void record_metrics(std::chrono::steady_clock::time_point start_time) {
#ifdef SDV_WITH_PROMETHEUS
        auto latency = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start_time
        ).count();
        
        transition_counter_->Increment();
        transition_latency_->Observe(latency);
#endif
    }
    
};

// State definition builder
template<typename StateT>
class StateDefinition {
public:
    explicit StateDefinition(std::string name) : name_(std::move(name)) {}
    
    StateDefinition& on_entry(std::function<void()> action) {
        entry_action = std::move(action);
        return *this;
    }
    
    StateDefinition& on_exit(std::function<void()> action) {
        exit_action = std::move(action);
        return *this;
    }
    
    std::string name_;
    std::function<void()> entry_action;
    std::function<void()> exit_action;
};

// Transition definition
template<typename StateT>
class Transition {
public:
    StateT from_state;
    StateT to_state;
    std::string trigger;
    ConditionFunc<StateT> condition;
    ActionFunc<StateT> action;
    
    Transition(StateT from, StateT to, std::string trig,
              ConditionFunc<StateT> cond = {},
              ActionFunc<StateT> act = {})
        : from_state(from)
        , to_state(to)
        , trigger(std::move(trig))
        , condition(std::move(cond))
        , action(std::move(act)) {}
};

} // namespace kuksa