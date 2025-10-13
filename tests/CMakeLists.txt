# Tests for SDV SDK

# Find Google Test
find_package(GTest REQUIRED)

# ============================================================================
# State Machine Tests
# ============================================================================

add_executable(state_machine_tests
    test_state_machine.cpp
    test_transitions.cpp
    test_hierarchical.cpp
)

target_link_libraries(state_machine_tests
    PRIVATE
        sdv::state_machine
        GTest::gtest
        GTest::gtest_main
)

# ============================================================================
# Testing Framework Tests
# ============================================================================

if(WITH_TESTING)
    add_executable(testing_framework_tests
        test_testing_framework.cpp
    )

    target_link_libraries(testing_framework_tests
        PRIVATE
            sdv::testing
            kuksa
            GTest::gtest
            GTest::gtest_main
    )

    gtest_discover_tests(testing_framework_tests)
endif()

# ============================================================================
# VSS Types Unit Tests (No Docker Required)
# ============================================================================

# Test all VSS data types
add_executable(all_vss_types_tests
    test_all_vss_types.cpp
)

target_link_libraries(all_vss_types_tests
    PRIVATE
        kuksa
        GTest::gtest
        GTest::gtest_main
        glog::glog
)

gtest_discover_tests(all_vss_types_tests)

# Test VSSClient handle creation methods (template instantiation test)
add_executable(accessor_handle_creation_tests
    test_accessor_handle_creation.cpp
)

target_link_libraries(accessor_handle_creation_tests
    PRIVATE
        kuksa
        GTest::gtest
        GTest::gtest_main
        glog::glog
)

gtest_discover_tests(accessor_handle_creation_tests)

# ============================================================================
# KUKSA Communication Integration Tests (Requires Docker + KUKSA Databroker)
# ============================================================================

# Create integration subdirectory
add_subdirectory(integration)

# ============================================================================
# Register Tests
# ============================================================================

include(GoogleTest)
gtest_discover_tests(state_machine_tests)

enable_testing()