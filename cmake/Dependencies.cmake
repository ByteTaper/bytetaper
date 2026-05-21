# cmake/Dependencies.cmake — External dependencies

include(FetchContent)

set(QUILL_NO_EXCEPTIONS ON CACHE BOOL "" FORCE)
set(QUILL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(QUILL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(QUILL_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(QUILL_DISABLE_NON_PREFIXED_MACROS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    quill
    GIT_REPOSITORY https://github.com/odygrd/quill.git
    GIT_TAG        v11.1.0
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
    GIT_SUBMODULES ""
)
FetchContent_MakeAvailable(quill)

find_package(PkgConfig REQUIRED)
pkg_check_modules(YAML_CPP REQUIRED yaml-cpp)

find_package(RocksDB REQUIRED)
find_package(Threads REQUIRED)

if(BYTETAPER_ENABLE_INTEGRATION_TESTS)
  find_package(Protobuf REQUIRED)
  find_package(gRPC CONFIG REQUIRED)
  if(NOT gRPC_CPP_PLUGIN_EXECUTABLE)
    find_program(gRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin REQUIRED)
  endif()
endif()
