/**
 * @file test_utils.cpp
 * @brief Implementation of test utilities for VSS unit tests
 */

#include <kuksa_cpp/testing/test_utils.hpp>

namespace sdv::vss {

// ============================================================================
// TestResolver Template Instantiations
// ============================================================================

// SignalHandle<T> (read-only) instantiations
template SignalHandle<bool> TestResolver::signal(const std::string&, int32_t, SignalClass);
template SignalHandle<int32_t> TestResolver::signal(const std::string&, int32_t, SignalClass);
template SignalHandle<uint32_t> TestResolver::signal(const std::string&, int32_t, SignalClass);
template SignalHandle<int64_t> TestResolver::signal(const std::string&, int32_t, SignalClass);
template SignalHandle<uint64_t> TestResolver::signal(const std::string&, int32_t, SignalClass);
template SignalHandle<float> TestResolver::signal(const std::string&, int32_t, SignalClass);
template SignalHandle<double> TestResolver::signal(const std::string&, int32_t, SignalClass);
template SignalHandle<std::string> TestResolver::signal(const std::string&, int32_t, SignalClass);

// SignalHandle<T> array instantiations
template SignalHandle<std::vector<bool>> TestResolver::signal(const std::string&, int32_t, SignalClass);
template SignalHandle<std::vector<int32_t>> TestResolver::signal(const std::string&, int32_t, SignalClass);
template SignalHandle<std::vector<uint32_t>> TestResolver::signal(const std::string&, int32_t, SignalClass);
template SignalHandle<std::vector<int64_t>> TestResolver::signal(const std::string&, int32_t, SignalClass);
template SignalHandle<std::vector<uint64_t>> TestResolver::signal(const std::string&, int32_t, SignalClass);
template SignalHandle<std::vector<float>> TestResolver::signal(const std::string&, int32_t, SignalClass);
template SignalHandle<std::vector<double>> TestResolver::signal(const std::string&, int32_t, SignalClass);
template SignalHandle<std::vector<std::string>> TestResolver::signal(const std::string&, int32_t, SignalClass);

// SignalHandleRW<T> (read-write) instantiations
template SignalHandleRW<bool> TestResolver::signal_rw(const std::string&, int32_t, SignalClass);
template SignalHandleRW<int32_t> TestResolver::signal_rw(const std::string&, int32_t, SignalClass);
template SignalHandleRW<uint32_t> TestResolver::signal_rw(const std::string&, int32_t, SignalClass);
template SignalHandleRW<int64_t> TestResolver::signal_rw(const std::string&, int32_t, SignalClass);
template SignalHandleRW<uint64_t> TestResolver::signal_rw(const std::string&, int32_t, SignalClass);
template SignalHandleRW<float> TestResolver::signal_rw(const std::string&, int32_t, SignalClass);
template SignalHandleRW<double> TestResolver::signal_rw(const std::string&, int32_t, SignalClass);
template SignalHandleRW<std::string> TestResolver::signal_rw(const std::string&, int32_t, SignalClass);

// SignalHandleRW<T> array instantiations
template SignalHandleRW<std::vector<bool>> TestResolver::signal_rw(const std::string&, int32_t, SignalClass);
template SignalHandleRW<std::vector<int32_t>> TestResolver::signal_rw(const std::string&, int32_t, SignalClass);
template SignalHandleRW<std::vector<uint32_t>> TestResolver::signal_rw(const std::string&, int32_t, SignalClass);
template SignalHandleRW<std::vector<int64_t>> TestResolver::signal_rw(const std::string&, int32_t, SignalClass);
template SignalHandleRW<std::vector<uint64_t>> TestResolver::signal_rw(const std::string&, int32_t, SignalClass);
template SignalHandleRW<std::vector<float>> TestResolver::signal_rw(const std::string&, int32_t, SignalClass);
template SignalHandleRW<std::vector<double>> TestResolver::signal_rw(const std::string&, int32_t, SignalClass);
template SignalHandleRW<std::vector<std::string>> TestResolver::signal_rw(const std::string&, int32_t, SignalClass);

} // namespace sdv::vss
