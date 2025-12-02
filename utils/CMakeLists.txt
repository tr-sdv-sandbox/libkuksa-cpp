# Utils - KUKSA command line tools

find_package(gflags REQUIRED)

# kuksa_logger - signal logger like candump
add_executable(kuksa_logger kuksa_logger.cpp)
target_link_libraries(kuksa_logger
    PRIVATE
        kuksa
        gflags
        glog::glog
)

install(TARGETS kuksa_logger
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
