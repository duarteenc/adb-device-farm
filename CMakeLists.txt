cmake_minimum_required(VERSION 3.19 FATAL_ERROR)
project(all)

add_subdirectory(QtScrcpy)

option(FARM_BUILD_TESTS "Build the farm unit tests" ON)
if(FARM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
