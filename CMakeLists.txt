cmake_minimum_required(VERSION 3.22)
project(ByteTaper LANGUAGES CXX)

include(CTest)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(BYTETAPER_ENABLE_GTEST_TESTS "Enable GoogleTest-based unit tests" OFF)
option(BYTETAPER_ENABLE_INTEGRATION_TESTS "Enable integration-style extproc adapter tests" OFF)
option(BYTETAPER_ENABLE_TSAN "Enable ThreadSanitizer for concurrency tests" OFF)

include(cmake/CompilerOptions.cmake)
include(cmake/Dependencies.cmake)
include(cmake/TargetsPolicy.cmake)
include(cmake/TargetsMetrics.cmake)
include(cmake/TargetsCache.cmake)
include(cmake/TargetsCoalescing.cmake)
include(cmake/TargetsRuntime.cmake)
include(cmake/TargetsExtproc.cmake)

if(BYTETAPER_ENABLE_GTEST_TESTS OR BYTETAPER_ENABLE_INTEGRATION_TESTS)
    include(cmake/TargetsTests.cmake)
endif()
