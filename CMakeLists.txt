cmake_minimum_required(VERSION 3.22)
project(ByteTaper LANGUAGES CXX)

set(BYTETAPER_VERSION "$ENV{BYTETAPER_VERSION}" CACHE STRING "ByteTaper version string")
set(BYTETAPER_GIT_SHA "$ENV{BYTETAPER_GIT_SHA}" CACHE STRING "ByteTaper git SHA")
set(BYTETAPER_BUILD_DATE "$ENV{BYTETAPER_BUILD_DATE}" CACHE STRING "ByteTaper build date")
set(BYTETAPER_BUILD_TYPE "${CMAKE_BUILD_TYPE}")

configure_file(
    src/runtime/build_info.h.in
    ${CMAKE_BINARY_DIR}/generated/bytetaper_build_info.h
    @ONLY
)

include(CTest)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(BYTETAPER_ENABLE_GTEST_TESTS "Enable GoogleTest-based unit tests" OFF)
option(BYTETAPER_ENABLE_INTEGRATION_TESTS "Enable integration-style extproc adapter tests" OFF)
option(BYTETAPER_ENABLE_TSAN "Enable ThreadSanitizer for concurrency tests" OFF)
option(BYTETAPER_ENABLE_ASAN  "Enable AddressSanitizer (mutually exclusive with TSAN)" OFF)
option(BYTETAPER_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer (combinable with ASAN)" OFF)

if(BYTETAPER_ENABLE_TSAN AND BYTETAPER_ENABLE_ASAN)
    message(FATAL_ERROR "BYTETAPER_ENABLE_TSAN and BYTETAPER_ENABLE_ASAN are mutually exclusive.")
endif()

include(cmake/CompilerOptions.cmake)
include(cmake/Dependencies.cmake)
include(cmake/TargetsPolicy.cmake)
include(cmake/TargetsTaperQuery.cmake)
include(cmake/TargetsMetrics.cmake)
include(cmake/TargetsCache.cmake)
include(cmake/TargetsCoalescing.cmake)
include(cmake/TargetsRuntime.cmake)
include(cmake/TargetsAdmin.cmake)
include(cmake/TargetsExtproc.cmake)

if(BYTETAPER_ENABLE_GTEST_TESTS OR BYTETAPER_ENABLE_INTEGRATION_TESTS)
    include(cmake/TargetsTests.cmake)
endif()
