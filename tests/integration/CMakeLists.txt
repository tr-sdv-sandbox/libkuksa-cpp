# Integration Tests for KUKSA Communication

add_executable(test_kuksa_communication
    test_kuksa_communication.cpp
)

target_include_directories(test_kuksa_communication
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(test_kuksa_communication
    PRIVATE
        kuksa
        GTest::gtest
        glog::glog
)

# Tests automatically start KUKSA in Docker
# Run directly: ./test_kuksa_communication
# Or with CTest: ctest -R test_kuksa_communication -V
gtest_discover_tests(test_kuksa_communication)

# All types integration test
add_executable(test_all_types_integration
    test_all_types_integration.cpp
)

target_include_directories(test_all_types_integration
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(test_all_types_integration
    PRIVATE
        kuksa
        GTest::gtest
        glog::glog
)

gtest_discover_tests(test_all_types_integration)

# Accessor pattern test
add_executable(test_accessor_pattern
    test_accessor_pattern.cpp
)

target_include_directories(test_accessor_pattern
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(test_accessor_pattern
    PRIVATE
        kuksa
        GTest::gtest
        glog::glog
)

gtest_discover_tests(test_accessor_pattern)

# ActuatorOwnerHandle integration test
add_executable(test_actuator_owner_handle_integration
    test_actuator_owner_handle_integration.cpp
)

target_include_directories(test_actuator_owner_handle_integration
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(test_actuator_owner_handle_integration
    PRIVATE
        kuksa
        GTest::gtest
        glog::glog
)

gtest_discover_tests(test_actuator_owner_handle_integration)

# Unified VSSClient integration test
add_executable(test_unified_client_integration
    test_unified_client_integration.cpp
)

target_include_directories(test_unified_client_integration
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(test_unified_client_integration
    PRIVATE
        kuksa
        GTest::gtest
        glog::glog
)

gtest_discover_tests(test_unified_client_integration)

# Streaming batch publish integration test
add_executable(test_streaming_batch_publish
    test_streaming_batch_publish.cpp
)

target_include_directories(test_streaming_batch_publish
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(test_streaming_batch_publish
    PRIVATE
        kuksa
        GTest::gtest
        glog::glog
)

gtest_discover_tests(test_streaming_batch_publish)

# Testing library integration test
if(WITH_TESTING)
    add_executable(test_testing_library_integration
        test_testing_library_integration.cpp
    )

    target_include_directories(test_testing_library_integration
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(test_testing_library_integration
        PRIVATE
            sdv::testing
            kuksa
            GTest::gtest
            glog::glog
    )

    gtest_discover_tests(test_testing_library_integration)
endif()