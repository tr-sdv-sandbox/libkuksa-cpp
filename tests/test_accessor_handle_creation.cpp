/**
 * @file test_accessor_handle_creation.cpp
 * @brief Compilation and linking test for TestResolver handle creation methods
 *
 * This test verifies that all explicit template instantiations for handle creation
 * methods exist and can be linked.
 *
 * The test calls each method with all supported scalar types to ensure:
 * 1. Templates compile correctly
 * 2. Explicit instantiations exist in the library
 * 3. Symbols can be resolved at link time
 */

#include <kuksa_cpp/testing/test_utils.hpp>
#include <gtest/gtest.h>
#include <glog/logging.h>

using namespace kuksa;

// ============================================================================
// Handle Creation Tests - Signal Handles
// ============================================================================

TEST(AccessorHandleCreation, GetSensorBool) {
    auto result = TestResolver::signal<bool>("Vehicle.Test.Bool");
    EXPECT_EQ(result.path(), "Vehicle.Test.Bool");
    EXPECT_EQ(result.id(), 1);
}

TEST(AccessorHandleCreation, GetSensorInt32) {
    auto result = TestResolver::signal<int32_t>("Vehicle.Test.Int32");
    EXPECT_EQ(result.path(), "Vehicle.Test.Int32");
}

TEST(AccessorHandleCreation, GetSensorUint32) {
    auto result = TestResolver::signal<uint32_t>("Vehicle.Test.Uint32");
    EXPECT_EQ(result.path(), "Vehicle.Test.Uint32");
}

TEST(AccessorHandleCreation, GetSensorInt64) {
    auto result = TestResolver::signal<int64_t>("Vehicle.Test.Int64");
    EXPECT_EQ(result.path(), "Vehicle.Test.Int64");
}

TEST(AccessorHandleCreation, GetSensorUint64) {
    auto result = TestResolver::signal<uint64_t>("Vehicle.Test.Uint64");
    EXPECT_EQ(result.path(), "Vehicle.Test.Uint64");
}

TEST(AccessorHandleCreation, GetSensorFloat) {
    auto result = TestResolver::signal<float>("Vehicle.Test.Float");
    EXPECT_EQ(result.path(), "Vehicle.Test.Float");
}

TEST(AccessorHandleCreation, GetSensorDouble) {
    auto result = TestResolver::signal<double>("Vehicle.Test.Double");
    EXPECT_EQ(result.path(), "Vehicle.Test.Double");
}

TEST(AccessorHandleCreation, GetSensorString) {
    auto result = TestResolver::signal<std::string>("Vehicle.Test.String");
    EXPECT_EQ(result.path(), "Vehicle.Test.String");
}

// ============================================================================
// Handle Creation Tests - Actuator Handles (SignalHandleRW)
// ============================================================================

TEST(AccessorHandleCreation, GetActuatorBool) {
    auto result = TestResolver::signal<bool>("Vehicle.Test.Actuator.Bool");
    EXPECT_EQ(result.path(), "Vehicle.Test.Actuator.Bool");
}

TEST(AccessorHandleCreation, GetActuatorInt32) {
    auto result = TestResolver::signal<int32_t>("Vehicle.Test.Actuator.Int32");
    EXPECT_EQ(result.path(), "Vehicle.Test.Actuator.Int32");
}

TEST(AccessorHandleCreation, GetActuatorUint32) {
    auto result = TestResolver::signal<uint32_t>("Vehicle.Test.Actuator.Uint32");
    EXPECT_EQ(result.path(), "Vehicle.Test.Actuator.Uint32");
}

TEST(AccessorHandleCreation, GetActuatorInt64) {
    auto result = TestResolver::signal<int64_t>("Vehicle.Test.Actuator.Int64");
    EXPECT_EQ(result.path(), "Vehicle.Test.Actuator.Int64");
}

TEST(AccessorHandleCreation, GetActuatorUint64) {
    auto result = TestResolver::signal<uint64_t>("Vehicle.Test.Actuator.Uint64");
    EXPECT_EQ(result.path(), "Vehicle.Test.Actuator.Uint64");
}

TEST(AccessorHandleCreation, GetActuatorFloat) {
    auto result = TestResolver::signal<float>("Vehicle.Test.Actuator.Float");
    EXPECT_EQ(result.path(), "Vehicle.Test.Actuator.Float");
}

TEST(AccessorHandleCreation, GetActuatorDouble) {
    auto result = TestResolver::signal<double>("Vehicle.Test.Actuator.Double");
    EXPECT_EQ(result.path(), "Vehicle.Test.Actuator.Double");
}

TEST(AccessorHandleCreation, GetActuatorString) {
    auto result = TestResolver::signal<std::string>("Vehicle.Test.Actuator.String");
    EXPECT_EQ(result.path(), "Vehicle.Test.Actuator.String");
}

// ============================================================================
// Handle Creation Tests - Signal Handles (Attributes)
// ============================================================================

TEST(AccessorHandleCreation, GetAttributeBool) {
    auto result = TestResolver::signal<bool>("Vehicle.Test.Attribute.Bool");
    EXPECT_EQ(result.path(), "Vehicle.Test.Attribute.Bool");
}

TEST(AccessorHandleCreation, GetAttributeInt32) {
    auto result = TestResolver::signal<int32_t>("Vehicle.Test.Attribute.Int32");
    EXPECT_EQ(result.path(), "Vehicle.Test.Attribute.Int32");
}

TEST(AccessorHandleCreation, GetAttributeUint32) {
    auto result = TestResolver::signal<uint32_t>("Vehicle.Test.Attribute.Uint32");
    EXPECT_EQ(result.path(), "Vehicle.Test.Attribute.Uint32");
}

TEST(AccessorHandleCreation, GetAttributeInt64) {
    auto result = TestResolver::signal<int64_t>("Vehicle.Test.Attribute.Int64");
    EXPECT_EQ(result.path(), "Vehicle.Test.Attribute.Int64");
}

TEST(AccessorHandleCreation, GetAttributeUint64) {
    auto result = TestResolver::signal<uint64_t>("Vehicle.Test.Attribute.Uint64");
    EXPECT_EQ(result.path(), "Vehicle.Test.Attribute.Uint64");
}

TEST(AccessorHandleCreation, GetAttributeFloat) {
    auto result = TestResolver::signal<float>("Vehicle.Test.Attribute.Float");
    EXPECT_EQ(result.path(), "Vehicle.Test.Attribute.Float");
}

TEST(AccessorHandleCreation, GetAttributeDouble) {
    auto result = TestResolver::signal<double>("Vehicle.Test.Attribute.Double");
    EXPECT_EQ(result.path(), "Vehicle.Test.Attribute.Double");
}

TEST(AccessorHandleCreation, GetAttributeString) {
    auto result = TestResolver::signal<std::string>("Vehicle.Test.Attribute.String");
    EXPECT_EQ(result.path(), "Vehicle.Test.Attribute.String");
}

// ============================================================================
// Comprehensive Test - All Types Together
// ============================================================================

TEST(AccessorHandleCreation, AllTypesCompileAndLink) {
    // This test ensures all template instantiations can be called in a single test
    // If any explicit instantiation is missing, this test will fail to link

    // Signals (sensors + attributes)
    TestResolver::signal<bool>("test");
    TestResolver::signal<int32_t>("test");
    TestResolver::signal<uint32_t>("test");
    TestResolver::signal<int64_t>("test");
    TestResolver::signal<uint64_t>("test");
    TestResolver::signal<float>("test");
    TestResolver::signal<double>("test");
    TestResolver::signal<std::string>("test");

    // Actuators (SignalHandleRW)
    TestResolver::signal<bool>("test");
    TestResolver::signal<int32_t>("test");
    TestResolver::signal<uint32_t>("test");
    TestResolver::signal<int64_t>("test");
    TestResolver::signal<uint64_t>("test");
    TestResolver::signal<float>("test");
    TestResolver::signal<double>("test");
    TestResolver::signal<std::string>("test");

    SUCCEED() << "All handle creation methods compiled and linked successfully";
}
