# Examples for SDV State Machine SDK

# Door control example
add_executable(door_example door_example.cpp)
target_link_libraries(door_example PRIVATE sdv::state_machine)

# Vehicle state example
add_executable(vehicle_example vehicle_example.cpp)
target_link_libraries(vehicle_example PRIVATE sdv::state_machine)

# Subscriber wait example (VSS)
add_executable(subscriber_wait_example subscriber_wait_example.cpp)
target_link_libraries(subscriber_wait_example
    PRIVATE
        kuksa
        glog::glog
)

# Unified client simple example (VSS)
add_executable(unified_client_simple unified_client_simple.cpp)
target_link_libraries(unified_client_simple
    PRIVATE
        kuksa
        glog::glog
)

# Climate control example (VSS 5.1)
add_executable(climate_control
    climate_control/main.cpp
    climate_control/climate_control.cpp
)
target_link_libraries(climate_control
    PRIVATE
        kuksa
        sdv::state_machine
        glog::glog
)

# Standalone test runner example (requires testing library)
if(WITH_TESTING)
    add_subdirectory(standalone_test_runner)
endif()