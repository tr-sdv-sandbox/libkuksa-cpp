# Standalone Test Runner Example
#
# This example shows how to build a standalone YAML test runner
# using the SDV testing library.
#
# Requirements:
# - WITH_TESTING=ON (builds sdv::testing library)
# - WITH_VSS=ON (required by testing library)

cmake_minimum_required(VERSION 3.16)

# Only build if testing library is enabled
if(NOT WITH_TESTING)
    message(STATUS "Skipping standalone_test_runner example (WITH_TESTING=OFF)")
    return()
endif()

# Build the standalone test runner executable
add_executable(standalone_test_runner main.cpp)

# Link against the testing library
target_link_libraries(standalone_test_runner
    PRIVATE
        sdv::testing
        glog::glog
)

# Installation (optional)
install(TARGETS standalone_test_runner
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/examples
)
