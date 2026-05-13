# cmake/TargetsExtproc.cmake — External processor and integration server targets

add_executable(bytetaper-extproc
  src/main.cpp
)
target_include_directories(bytetaper-extproc
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(bytetaper-extproc
  PRIVATE
    RocksDB::RocksDB
)

if(BYTETAPER_ENABLE_INTEGRATION_TESTS)
  set(BYTETAPER_PROTO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/proto")
  set(BYTETAPER_GENERATED_PROTO_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/extproc")
  set(BYTETAPER_EXTPROC_SERVICE_PROTO "envoy/service/ext_proc/v3/external_processor.proto")

  file(GLOB_RECURSE BYTETAPER_VENDORED_PROTO_RELATIVE
    RELATIVE "${BYTETAPER_PROTO_ROOT}"
    CONFIGURE_DEPENDS
    "${BYTETAPER_PROTO_ROOT}/*.proto"
  )
  set(BYTETAPER_VENDORED_PROTOS_ABSOLUTE "")
  set(BYTETAPER_GENERATED_PROTO_SOURCES "")
  set(BYTETAPER_GENERATED_PROTO_HEADERS "")
  foreach(BYTETAPER_PROTO_FILE IN LISTS BYTETAPER_VENDORED_PROTO_RELATIVE)
    list(APPEND BYTETAPER_VENDORED_PROTOS_ABSOLUTE
      "${BYTETAPER_PROTO_ROOT}/${BYTETAPER_PROTO_FILE}"
    )
    string(REPLACE ".proto" ".pb.cc" BYTETAPER_PROTO_CC "${BYTETAPER_PROTO_FILE}")
    string(REPLACE ".proto" ".pb.h" BYTETAPER_PROTO_H "${BYTETAPER_PROTO_FILE}")
    list(APPEND BYTETAPER_GENERATED_PROTO_SOURCES
      "${BYTETAPER_GENERATED_PROTO_DIR}/${BYTETAPER_PROTO_CC}"
    )
    list(APPEND BYTETAPER_GENERATED_PROTO_HEADERS
      "${BYTETAPER_GENERATED_PROTO_DIR}/${BYTETAPER_PROTO_H}"
    )
  endforeach()

  add_custom_command(
    OUTPUT ${BYTETAPER_GENERATED_PROTO_SOURCES} ${BYTETAPER_GENERATED_PROTO_HEADERS}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS
      --cpp_out=${BYTETAPER_GENERATED_PROTO_DIR}
      -I ${BYTETAPER_PROTO_ROOT}
      ${BYTETAPER_VENDORED_PROTO_RELATIVE}
    WORKING_DIRECTORY ${BYTETAPER_PROTO_ROOT}
    DEPENDS ${BYTETAPER_VENDORED_PROTOS_ABSOLUTE}
    COMMENT "Generating C++ protobuf sources for Envoy ext_proc snapshot"
    VERBATIM
  )

  add_custom_target(bytetaper_extproc_proto_codegen
    DEPENDS ${BYTETAPER_GENERATED_PROTO_SOURCES} ${BYTETAPER_GENERATED_PROTO_HEADERS}
  )

  string(REPLACE ".proto" ".grpc.pb.cc" BYTETAPER_EXTPROC_GRPC_CC "${BYTETAPER_EXTPROC_SERVICE_PROTO}")
  string(REPLACE ".proto" ".grpc.pb.h" BYTETAPER_EXTPROC_GRPC_H "${BYTETAPER_EXTPROC_SERVICE_PROTO}")
  set(BYTETAPER_GENERATED_GRPC_SOURCE "${BYTETAPER_GENERATED_PROTO_DIR}/${BYTETAPER_EXTPROC_GRPC_CC}")
  set(BYTETAPER_GENERATED_GRPC_HEADER "${BYTETAPER_GENERATED_PROTO_DIR}/${BYTETAPER_EXTPROC_GRPC_H}")

  add_custom_command(
    OUTPUT ${BYTETAPER_GENERATED_GRPC_SOURCE} ${BYTETAPER_GENERATED_GRPC_HEADER}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS
      --grpc_out=${BYTETAPER_GENERATED_PROTO_DIR}
      --plugin=protoc-gen-grpc=${gRPC_CPP_PLUGIN_EXECUTABLE}
      -I ${BYTETAPER_PROTO_ROOT}
      ${BYTETAPER_EXTPROC_SERVICE_PROTO}
    WORKING_DIRECTORY ${BYTETAPER_PROTO_ROOT}
    DEPENDS "${BYTETAPER_PROTO_ROOT}/${BYTETAPER_EXTPROC_SERVICE_PROTO}"
    COMMENT "Generating C++ gRPC sources for Envoy ext_proc service"
    VERBATIM
  )

  add_custom_target(bytetaper_extproc_grpc_codegen
    DEPENDS ${BYTETAPER_GENERATED_GRPC_SOURCE} ${BYTETAPER_GENERATED_GRPC_HEADER}
  )

  add_library(bytetaper_extproc_proto STATIC
    ${BYTETAPER_GENERATED_PROTO_SOURCES}
  )
  add_dependencies(bytetaper_extproc_proto bytetaper_extproc_proto_codegen)
  target_include_directories(bytetaper_extproc_proto
    PUBLIC
      ${BYTETAPER_GENERATED_PROTO_DIR}
  )
  target_link_libraries(bytetaper_extproc_proto
    PUBLIC
      protobuf::libprotobuf
  )

  add_library(bytetaper_extproc_grpc STATIC
    ${BYTETAPER_GENERATED_GRPC_SOURCE}
  )
  add_dependencies(bytetaper_extproc_grpc bytetaper_extproc_grpc_codegen bytetaper_extproc_proto_codegen)
  target_include_directories(bytetaper_extproc_grpc
    PUBLIC
      ${BYTETAPER_GENERATED_PROTO_DIR}
  )
  target_link_libraries(bytetaper_extproc_grpc
    PUBLIC
      bytetaper_extproc_proto
      gRPC::grpc++
      protobuf::libprotobuf
  )

  add_library(bytetaper_extproc_proto_boundary STATIC
    src/extproc/proto_boundary.cpp
  )
  target_include_directories(bytetaper_extproc_proto_boundary
    PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
      ${BYTETAPER_GENERATED_PROTO_DIR}
  )
  target_link_libraries(bytetaper_extproc_proto_boundary
    PRIVATE
      bytetaper_extproc_proto
  )

  add_library(bytetaper_logger STATIC
    src/observability/logger.cpp
  )
  set_source_files_properties(src/observability/logger.cpp
    PROPERTIES COMPILE_OPTIONS "-fexceptions"
  )
  target_include_directories(bytetaper_logger PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(bytetaper_logger PUBLIC quill::quill)

  add_library(bytetaper_trace STATIC
    src/observability/trace.cpp
  )
  target_include_directories(bytetaper_trace PUBLIC include)
  target_link_libraries(bytetaper_trace PRIVATE bytetaper_logger)

  add_library(bytetaper_extproc_adapter STATIC
    src/extproc/request_runtime.cpp
    src/extproc/bytetaper_to_envoy.cpp
    src/extproc/default_pipelines.cpp
    src/extproc/header_view.cpp
  )
  target_include_directories(bytetaper_extproc_adapter
    PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
      ${BYTETAPER_GENERATED_PROTO_DIR}
  )
  target_link_libraries(bytetaper_extproc_adapter PUBLIC
    bytetaper_stages
    bytetaper_apg
    bytetaper_cache
    bytetaper_runtime
    bytetaper_coalescing
    bytetaper_pagination
    bytetaper_compression
    bytetaper_policy
    bytetaper_field_selection
    bytetaper_json_transform
    bytetaper_safety
    bytetaper_extproc_proto_boundary
    bytetaper_logger
    bytetaper_trace
  )

  add_library(bytetaper_extproc_grpc_server STATIC
    src/extproc/grpc_server.cpp
  )
  target_include_directories(bytetaper_extproc_grpc_server
    PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
      ${BYTETAPER_GENERATED_PROTO_DIR}
  )
  target_link_libraries(bytetaper_extproc_grpc_server
    PUBLIC
      bytetaper_extproc_adapter
      bytetaper_stages
      bytetaper_cache
      bytetaper_runtime
      bytetaper_coalescing
      bytetaper_prometheus_registry
      bytetaper_trace
    PRIVATE
      bytetaper_extproc_grpc
      RocksDB::RocksDB
      Threads::Threads
      bytetaper_hash
  )

  add_executable(bytetaper-extproc-server
    src/extproc/server_main.cpp
  )
  target_include_directories(bytetaper-extproc-server
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${BYTETAPER_GENERATED_PROTO_DIR}
  )
  target_link_libraries(bytetaper-extproc-server
    PRIVATE
      bytetaper_extproc_grpc_server
      bytetaper_extproc_grpc
      policy_yaml_loader
      bytetaper_metrics_http_server
      bytetaper_prometheus_registry
      bytetaper_admin
      bytetaper_logger
      ${YAML_CPP_LIBRARIES}
  )
endif()
