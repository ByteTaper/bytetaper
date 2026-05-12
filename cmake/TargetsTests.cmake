# cmake/TargetsTests.cmake — Unit tests and integration tests

if(BUILD_TESTING)
  add_test(
    NAME bytetaper_extproc_smoke
    COMMAND $<TARGET_FILE:bytetaper-extproc>
  )

  if(BYTETAPER_ENABLE_GTEST_TESTS)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(googletest)
    target_link_libraries(gtest_main PUBLIC bytetaper_hash)
    target_link_libraries(gtest PUBLIC bytetaper_hash)

    add_executable(apg_transform_context_test
      tests/apg_transform_context_test.cpp
    )
    target_include_directories(apg_transform_context_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(apg_transform_context_test
      PRIVATE
        gtest_main
    )

    add_test(
      NAME apg_transform_context_test
      COMMAND apg_transform_context_test
    )

    add_executable(trace_test
      tests/trace_test.cpp
    )
    target_include_directories(trace_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(trace_test
      PRIVATE
        gtest_main
        bytetaper_trace
    )

    add_test(
      NAME trace_test
      COMMAND trace_test
    )

    add_executable(apg_stage_signature_test
      tests/apg_stage_signature_test.cpp
    )
    target_include_directories(apg_stage_signature_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(apg_stage_signature_test
      PRIVATE
        gtest_main
    )

    add_test(
      NAME apg_stage_signature_test
      COMMAND apg_stage_signature_test
    )

    add_executable(apg_result_output_test
      tests/apg_result_output_test.cpp
    )
    target_include_directories(apg_result_output_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(apg_result_output_test
      PRIVATE
        gtest_main
    )

    add_test(
      NAME apg_result_output_test
      COMMAND apg_result_output_test
    )

    add_executable(hash_test
      tests/hash_test.cpp
    )
    target_include_directories(hash_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(hash_test
      PRIVATE
        gtest_main
        bytetaper_hash
    )
    add_test(
      NAME hash_test
      COMMAND hash_test
    )

    add_executable(apg_pipeline_runner_test
      tests/apg_pipeline_runner_test.cpp
    )
    target_include_directories(apg_pipeline_runner_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(apg_pipeline_runner_test
      PRIVATE
        gtest_main
        bytetaper_apg
    )

    add_test(
      NAME apg_pipeline_runner_test
      COMMAND apg_pipeline_runner_test
    )

    add_executable(field_selection_test
      tests/field_selection_test.cpp
    )
    target_include_directories(field_selection_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(field_selection_test
      PRIVATE
        gtest_main
        bytetaper_field_selection
        bytetaper_apg
    )

    add_test(
      NAME field_selection_test
      COMMAND field_selection_test
    )

    add_executable(apg_query_view_test
      tests/apg_query_view_test.cpp
    )
    target_include_directories(apg_query_view_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(apg_query_view_test
      PRIVATE
        gtest_main
        bytetaper_apg
    )

    add_test(
      NAME apg_query_view_test
      COMMAND apg_query_view_test
    )

    add_executable(l1_cache_test
      tests/l1_cache_test.cpp
    )
    target_include_directories(l1_cache_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(l1_cache_test
      PRIVATE
        gtest_main
        bytetaper_cache
        bytetaper_prometheus_registry
        RocksDB::RocksDB
    )

    add_test(
      NAME l1_cache_test
      COMMAND l1_cache_test
    )

    add_executable(runtime_worker_queue_test
      tests/runtime_worker_queue_test.cpp
    )
    target_include_directories(runtime_worker_queue_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(runtime_worker_queue_test
      PRIVATE
        gtest_main
        bytetaper_runtime
        bytetaper_cache
        bytetaper_coalescing
        bytetaper_prometheus_registry
        Threads::Threads
        RocksDB::RocksDB
    )
    add_test(
      NAME runtime_worker_queue_test
      COMMAND runtime_worker_queue_test
    )

    add_executable(l1_cache_lookup_stage_test
      tests/l1_cache_lookup_stage_test.cpp
    )
    target_include_directories(l1_cache_lookup_stage_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(l1_cache_lookup_stage_test
      PRIVATE
        gtest_main
        bytetaper_stages
    )

    add_test(
      NAME l1_cache_lookup_stage_test
      COMMAND l1_cache_lookup_stage_test
    )

    add_executable(l2_cache_lookup_stage_test
      tests/l2_cache_lookup_stage_test.cpp
    )
    target_include_directories(l2_cache_lookup_stage_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(l2_cache_lookup_stage_test
      PRIVATE
        gtest_main
        bytetaper_stages
    )

    add_test(
      NAME l2_cache_lookup_stage_test
      COMMAND l2_cache_lookup_stage_test
    )

    add_executable(cache_key_prepare_stage_test
      tests/cache_key_prepare_stage_test.cpp
    )
    target_include_directories(cache_key_prepare_stage_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(cache_key_prepare_stage_test
      PRIVATE
        gtest_main
        bytetaper_stages
    )
    add_test(NAME cache_key_prepare_stage_test COMMAND cache_key_prepare_stage_test)

    add_executable(field_variant_admission_stage_test
      tests/field_variant_admission_stage_test.cpp
    )
    target_include_directories(field_variant_admission_stage_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(field_variant_admission_stage_test
      PRIVATE
        gtest_main
        bytetaper_stages
    )
    add_test(NAME field_variant_admission_stage_test COMMAND field_variant_admission_stage_test)

    add_executable(l1_variant_stage_test
      tests/l1_variant_stage_test.cpp
    )
    target_include_directories(l1_variant_stage_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(l1_variant_stage_test
      PRIVATE
        gtest_main
        bytetaper_stages
    )
    add_test(NAME l1_variant_stage_test COMMAND l1_variant_stage_test)

    add_executable(l1_cache_store_stage_test
      tests/l1_cache_store_stage_test.cpp
    )
    target_include_directories(l1_cache_store_stage_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(l1_cache_store_stage_test
      PRIVATE
        gtest_main
        bytetaper_stages
    )

    add_test(
      NAME l1_cache_store_stage_test
      COMMAND l1_cache_store_stage_test
    )

    add_executable(l1_put_if_newer_test
      tests/l1_put_if_newer_test.cpp
    )
    target_include_directories(l1_put_if_newer_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(l1_put_if_newer_test
      PRIVATE
        gtest_main
        bytetaper_cache
        bytetaper_prometheus_registry
        RocksDB::RocksDB
    )
    add_test(
      NAME l1_put_if_newer_test
      COMMAND l1_put_if_newer_test
    )

    add_executable(l2_cache_store_stage_test
      tests/l2_cache_store_stage_test.cpp
    )
    target_include_directories(l2_cache_store_stage_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(l2_cache_store_stage_test
      PRIVATE
        gtest_main
        bytetaper_stages
    )

    add_test(
      NAME l2_cache_store_stage_test
      COMMAND l2_cache_store_stage_test
    )

    add_executable(l2_promotion_test
      tests/l2_promotion_test.cpp
    )
    target_include_directories(l2_promotion_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(l2_promotion_test
      PRIVATE
        gtest_main
        bytetaper_stages
        RocksDB::RocksDB
        bytetaper_prometheus_registry
    )

    add_test(
      NAME l2_promotion_test
      COMMAND l2_promotion_test
    )

    add_executable(cache_hot_path_regression_test
      tests/cache_hot_path_regression_test.cpp
    )
    target_link_libraries(cache_hot_path_regression_test
      PRIVATE
        gtest_main
        bytetaper_extproc_grpc_server
        RocksDB::RocksDB
        bytetaper_prometheus_registry
    )

    add_test(
      NAME cache_hot_path_regression_test
      COMMAND cache_hot_path_regression_test
    )

    add_executable(grpc_server_worker_lifecycle_test
      tests/grpc_server_worker_lifecycle_test.cpp
    )
    target_link_libraries(grpc_server_worker_lifecycle_test
      PRIVATE
        gtest_main
        bytetaper_extproc_grpc_server
        RocksDB::RocksDB
        bytetaper_prometheus_registry
    )
    add_test(NAME grpc_server_worker_lifecycle_test COMMAND grpc_server_worker_lifecycle_test)

    add_executable(server_main_args_test
      tests/server_main_args_test.cpp
    )
    target_link_libraries(server_main_args_test
      PRIVATE
        gtest_main
        bytetaper_extproc_grpc_server
        RocksDB::RocksDB
        bytetaper_prometheus_registry
    )
    add_test(NAME server_main_args_test COMMAND server_main_args_test)

    add_executable(extproc_header_view_test
      tests/extproc_header_view_test.cpp
      src/extproc/header_view.cpp
    )
    target_include_directories(extproc_header_view_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BYTETAPER_GENERATED_PROTO_DIR}
    )
    target_link_libraries(extproc_header_view_test
      PRIVATE
        gtest_main
        bytetaper_extproc_proto
        protobuf::libprotobuf
    )
    add_test(NAME extproc_header_view_test COMMAND extproc_header_view_test)


    add_executable(runtime_partitioned_queue_test
      tests/runtime_partitioned_queue_test.cpp
    )
    target_link_libraries(runtime_partitioned_queue_test
      PRIVATE
        gtest_main
        bytetaper_extproc_grpc_server
        RocksDB::RocksDB
        bytetaper_prometheus_registry
    )
    add_test(NAME runtime_partitioned_queue_test COMMAND runtime_partitioned_queue_test)

    add_executable(default_pipeline_order_test
      tests/default_pipeline_order_test.cpp
    )
    target_link_libraries(default_pipeline_order_test
      PRIVATE
        gtest_main
        bytetaper_extproc_grpc_server
        RocksDB::RocksDB
        bytetaper_prometheus_registry
    )
    add_test(NAME default_pipeline_order_test COMMAND default_pipeline_order_test)

    add_executable(compiled_route_runtime_test
      tests/compiled_route_runtime_test.cpp
    )
    target_link_libraries(compiled_route_runtime_test
      PRIVATE
        gtest_main
        bytetaper_extproc_grpc_server
        RocksDB::RocksDB
        bytetaper_prometheus_registry
    )
    add_test(NAME compiled_route_runtime_test COMMAND compiled_route_runtime_test)

    add_executable(cache_hot_path_tail_regression_test
      tests/cache_hot_path_tail_regression_test.cpp
    )
    target_link_libraries(cache_hot_path_tail_regression_test
      PRIVATE
        gtest_main
        bytetaper_extproc_grpc_server
        RocksDB::RocksDB
        bytetaper_prometheus_registry
    )
    add_test(NAME cache_hot_path_tail_regression_test COMMAND cache_hot_path_tail_regression_test)

    add_executable(json_transform_content_type_test
      tests/json_transform_content_type_test.cpp
    )
    target_include_directories(json_transform_content_type_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(json_transform_content_type_test
      PRIVATE
        gtest_main
        bytetaper_json_transform
    )

    add_test(
      NAME json_transform_content_type_test
      COMMAND json_transform_content_type_test
    )

    add_executable(json_transform_flat_json_test
      tests/json_transform_flat_json_test.cpp
    )
    target_include_directories(json_transform_flat_json_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(json_transform_flat_json_test
      PRIVATE
        gtest_main
        bytetaper_json_transform
    )

    add_test(
      NAME json_transform_flat_json_test
      COMMAND json_transform_flat_json_test
    )

    add_executable(json_transform_filter_flat_json_test
      tests/json_transform_filter_flat_json_test.cpp
    )
    target_include_directories(json_transform_filter_flat_json_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(json_transform_filter_flat_json_test
      PRIVATE
        gtest_main
        bytetaper_json_transform
    )

    add_test(
      NAME json_transform_filter_flat_json_test
      COMMAND json_transform_filter_flat_json_test
    )

    add_executable(policy_route_policy_test
      tests/policy_route_policy_test.cpp
    )
    target_include_directories(policy_route_policy_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(policy_route_policy_test
      PRIVATE
        gtest_main
        bytetaper_policy
    )

    add_test(
      NAME policy_route_policy_test
      COMMAND policy_route_policy_test
    )


    add_executable(policy_yaml_loader_test
      tests/policy_yaml_loader_test.cpp
    )
    target_include_directories(policy_yaml_loader_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(policy_yaml_loader_test
      PRIVATE
        policy_yaml_loader
        gtest_main
    )

    add_test(
      NAME policy_yaml_loader_test
      COMMAND policy_yaml_loader_test
    )

    add_executable(policy_route_matcher_test
      tests/policy_route_matcher_test.cpp
    )
    target_include_directories(policy_route_matcher_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(policy_route_matcher_test
      PRIVATE
        gtest_main
        bytetaper_hash
        bytetaper_prometheus_registry
    )

    add_test(
      NAME policy_route_matcher_test
      COMMAND policy_route_matcher_test
    )

    add_executable(policy_method_matcher_test
      tests/policy_method_matcher_test.cpp
    )
    target_include_directories(policy_method_matcher_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(policy_method_matcher_test
      PRIVATE
        gtest_main
    )

    add_test(
      NAME policy_method_matcher_test
      COMMAND policy_method_matcher_test
    )

    add_executable(policy_yaml_loader_method_test
      tests/policy_yaml_loader_method_test.cpp
    )
    target_include_directories(policy_yaml_loader_method_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(policy_yaml_loader_method_test
      PRIVATE
        policy_yaml_loader
        gtest_main
    )

    add_test(
      NAME policy_yaml_loader_method_test
      COMMAND policy_yaml_loader_method_test
    )

    add_executable(policy_field_filter_test
      tests/policy_field_filter_test.cpp
    )
    target_include_directories(policy_field_filter_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(policy_field_filter_test
      PRIVATE
        gtest_main
    )

    add_test(
      NAME policy_field_filter_test
      COMMAND policy_field_filter_test
    )

    add_executable(policy_yaml_loader_field_filter_test
      tests/policy_yaml_loader_field_filter_test.cpp
    )
    target_include_directories(policy_yaml_loader_field_filter_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(policy_yaml_loader_field_filter_test
      PRIVATE
        policy_yaml_loader
        gtest_main
    )

    add_test(
      NAME policy_yaml_loader_field_filter_test
      COMMAND policy_yaml_loader_field_filter_test
    )

    add_executable(policy_max_response_bytes_test
      tests/policy_max_response_bytes_test.cpp
    )
    target_include_directories(policy_max_response_bytes_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(policy_max_response_bytes_test
      PRIVATE
        gtest_main
    )

    add_test(
      NAME policy_max_response_bytes_test
      COMMAND policy_max_response_bytes_test
    )

    add_executable(policy_yaml_loader_max_bytes_test
      tests/policy_yaml_loader_max_bytes_test.cpp
    )
    target_include_directories(policy_yaml_loader_max_bytes_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(policy_yaml_loader_max_bytes_test
      PRIVATE
        policy_yaml_loader
        gtest_main
    )

    add_test(
      NAME policy_yaml_loader_max_bytes_test
      COMMAND policy_yaml_loader_max_bytes_test
    )

    add_executable(policy_cache_policy_test
      tests/policy_cache_policy_test.cpp
    )
    target_include_directories(policy_cache_policy_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(policy_cache_policy_test
      PRIVATE
        gtest_main
        bytetaper_policy
    )

    add_test(
      NAME policy_cache_policy_test
      COMMAND policy_cache_policy_test
    )

    add_executable(pagination_policy_test
      tests/pagination_policy_test.cpp
    )
    target_include_directories(pagination_policy_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(pagination_policy_test
      PRIVATE
        gtest_main
        bytetaper_policy
    )

    add_test(
      NAME pagination_policy_test
      COMMAND pagination_policy_test
    )

    add_executable(compression_policy_test
      tests/compression_policy_test.cpp
    )
    target_include_directories(compression_policy_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(compression_policy_test
      PRIVATE
        gtest_main
        bytetaper_policy
    )

    add_test(
      NAME compression_policy_test
      COMMAND compression_policy_test
    )

    add_executable(coalescing_key_test
      tests/coalescing_key_test.cpp
    )
    target_include_directories(coalescing_key_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(coalescing_key_test
      PRIVATE
        gtest_main
        bytetaper_coalescing
    )

    add_test(
      NAME coalescing_key_test
      COMMAND coalescing_key_test
    )

    add_executable(coalescing_get_only_test
      tests/coalescing_get_only_test.cpp
    )
    target_include_directories(coalescing_get_only_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(coalescing_get_only_test
      PRIVATE
        gtest_main
        bytetaper_coalescing
    )

    add_test(
      NAME coalescing_get_only_test
      COMMAND coalescing_get_only_test
    )

    add_executable(coalescing_auth_safety_test
      tests/coalescing_auth_safety_test.cpp
    )
    target_include_directories(coalescing_auth_safety_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(coalescing_auth_safety_test
      PRIVATE
        gtest_main
        bytetaper_coalescing
    )

    add_test(
      NAME coalescing_auth_safety_test
      COMMAND coalescing_auth_safety_test
    )

    add_executable(inflight_registry_test
      tests/inflight_registry_test.cpp
    )
    target_include_directories(inflight_registry_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(inflight_registry_test
      PRIVATE
        gtest_main
        bytetaper_coalescing
    )

    add_test(
      NAME inflight_registry_test
      COMMAND inflight_registry_test
    )

    add_executable(coalescing_decision_test
      tests/coalescing_decision_test.cpp
    )
    target_include_directories(coalescing_decision_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(coalescing_decision_test
      PRIVATE
        gtest_main
        bytetaper_coalescing
    )

    add_test(
      NAME coalescing_decision_test
      COMMAND coalescing_decision_test
    )

    add_executable(coalescing_timeout_test
      tests/coalescing_timeout_test.cpp
    )
    target_include_directories(coalescing_timeout_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(coalescing_timeout_test
      PRIVATE
        gtest_main
        bytetaper_coalescing
    )

    add_test(
      NAME coalescing_timeout_test
      COMMAND coalescing_timeout_test
    )

    add_executable(wait_window_test
      tests/wait_window_test.cpp
    )
    target_include_directories(wait_window_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(wait_window_test
      PRIVATE
        gtest_main
        bytetaper_coalescing
    )

    add_test(
      NAME wait_window_test
      COMMAND wait_window_test
    )

    add_executable(coalescing_follower_wait_test
      tests/coalescing_follower_wait_test.cpp
    )
    target_include_directories(coalescing_follower_wait_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(coalescing_follower_wait_test
      PRIVATE
        bytetaper_extproc_grpc_server
        RocksDB::RocksDB
        gtest_main
    )

    add_test(
      NAME coalescing_follower_wait_test
      COMMAND coalescing_follower_wait_test
    )

    add_executable(follower_wait_guardrail_test
      tests/follower_wait_guardrail_test.cpp
    )
    target_include_directories(follower_wait_guardrail_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(follower_wait_guardrail_test
      PRIVATE
        bytetaper_extproc_grpc_server
        policy_yaml_loader
        RocksDB::RocksDB
        gtest_main
    )

    add_test(
      NAME follower_wait_guardrail_test
      COMMAND follower_wait_guardrail_test
    )

    add_executable(coalescing_event_driven_wait_test
      tests/coalescing_event_driven_wait_test.cpp
    )
    target_include_directories(coalescing_event_driven_wait_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(coalescing_event_driven_wait_test
      PRIVATE
        bytetaper_extproc_grpc_server
        RocksDB::RocksDB
        gtest_main
    )

    add_test(
      NAME coalescing_event_driven_wait_test
      COMMAND coalescing_event_driven_wait_test
    )

    add_executable(coalescing_fallback_tightened_test
      tests/coalescing_fallback_tightened_test.cpp
    )
    target_include_directories(coalescing_fallback_tightened_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(coalescing_fallback_tightened_test
      PRIVATE
        bytetaper_extproc_grpc_server
        RocksDB::RocksDB
        gtest_main
    )

    add_test(
      NAME coalescing_fallback_tightened_test
      COMMAND coalescing_fallback_tightened_test
    )

    add_executable(l2_cache_async_lookup_enqueue_stage_test
      tests/l2_cache_async_lookup_enqueue_stage_test.cpp
    )
    target_include_directories(l2_cache_async_lookup_enqueue_stage_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(l2_cache_async_lookup_enqueue_stage_test
      PRIVATE
        gtest_main
        bytetaper_stages
        RocksDB::RocksDB
        Threads::Threads
    )
    add_test(
      NAME l2_cache_async_lookup_enqueue_stage_test
      COMMAND l2_cache_async_lookup_enqueue_stage_test
    )

    add_executable(l2_cache_async_store_enqueue_stage_test
      tests/l2_cache_async_store_enqueue_stage_test.cpp
    )
    target_include_directories(l2_cache_async_store_enqueue_stage_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(l2_cache_async_store_enqueue_stage_test
      PRIVATE
        gtest_main
        bytetaper_stages
        RocksDB::RocksDB
        Threads::Threads
    )
    add_test(
      NAME l2_cache_async_store_enqueue_stage_test
      COMMAND l2_cache_async_store_enqueue_stage_test
    )

    add_executable(coalescing_leader_completion_test
      tests/coalescing_leader_completion_test.cpp
    )
    target_include_directories(coalescing_leader_completion_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(coalescing_leader_completion_test
      PRIVATE
        gtest_main
        bytetaper_stages
    )

    add_test(
      NAME coalescing_leader_completion_test
      COMMAND coalescing_leader_completion_test
    )

    add_executable(runtime_metrics_test
      tests/runtime_metrics_test.cpp
    )
    target_include_directories(runtime_metrics_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(runtime_metrics_test
      PRIVATE
        gtest_main
        bytetaper_prometheus_registry
    )
    add_test(
      NAME runtime_metrics_test
      COMMAND runtime_metrics_test
    )

    add_executable(default_pipeline_boundaries_test
      tests/default_pipeline_boundaries_test.cpp
    )
    target_include_directories(default_pipeline_boundaries_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(default_pipeline_boundaries_test
      PRIVATE
        bytetaper_extproc_grpc_server
        gtest_main
        RocksDB::RocksDB
        Threads::Threads
    )
    add_test(
      NAME default_pipeline_boundaries_test
      COMMAND default_pipeline_boundaries_test
    )

    add_executable(coalescing_cache_integration_test
      tests/coalescing_cache_integration_test.cpp
    )
    target_include_directories(coalescing_cache_integration_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(coalescing_cache_integration_test
      PRIVATE
        bytetaper_extproc_grpc_server
        gtest_main
        RocksDB::RocksDB
        Threads::Threads
    )

    add_executable(l1_cache_concurrency_test
      tests/l1_cache_concurrency_test.cpp
    )
    target_include_directories(l1_cache_concurrency_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(l1_cache_concurrency_test
      PRIVATE
        bytetaper_extproc_grpc_server
        gtest_main
        Threads::Threads
    )


    add_test(
      NAME l1_cache_concurrency_test
      COMMAND l1_cache_concurrency_test
    )

    add_executable(runtime_worker_queue_concurrency_test
      tests/runtime_worker_queue_concurrency_test.cpp
    )
    target_include_directories(runtime_worker_queue_concurrency_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(runtime_worker_queue_concurrency_test
      PRIVATE
        bytetaper_extproc_grpc_server
        gtest_main
        RocksDB::RocksDB
        Threads::Threads
    )
    add_test(
      NAME runtime_worker_queue_concurrency_test
      COMMAND runtime_worker_queue_concurrency_test
    )

    add_executable(l2_async_worker_concurrency_test
      tests/l2_async_worker_concurrency_test.cpp
    )
    target_include_directories(l2_async_worker_concurrency_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(l2_async_worker_concurrency_test
      PRIVATE
        bytetaper_extproc_grpc_server
        gtest_main
        RocksDB::RocksDB
        Threads::Threads
    )
    add_test(
      NAME l2_async_worker_concurrency_test
      COMMAND l2_async_worker_concurrency_test
    )

    add_test(
      NAME coalescing_cache_integration_test
      COMMAND coalescing_cache_integration_test
    )

    add_executable(coalescing_metrics_test
      tests/coalescing_metrics_test.cpp
    )
    target_include_directories(coalescing_metrics_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(coalescing_metrics_test
      PRIVATE
        gtest_main
        bytetaper_prometheus_registry
    )

    add_test(
      NAME coalescing_metrics_test
      COMMAND coalescing_metrics_test
    )

    add_executable(coalescing_policy_validator_test
      tests/coalescing_policy_validator_test.cpp
    )
    target_include_directories(coalescing_policy_validator_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(coalescing_policy_validator_test
      PRIVATE
        gtest_main
        bytetaper_policy
    )

    add_test(
      NAME coalescing_policy_validator_test
      COMMAND coalescing_policy_validator_test
    )

    add_executable(accept_encoding_test
      tests/accept_encoding_test.cpp
    )
    target_include_directories(accept_encoding_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(accept_encoding_test
      PRIVATE
        gtest_main
        bytetaper_compression
    )

    add_test(
      NAME accept_encoding_test
      COMMAND accept_encoding_test
    )

    add_executable(content_encoding_test
      tests/content_encoding_test.cpp
    )
    target_include_directories(content_encoding_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(content_encoding_test
      PRIVATE
        gtest_main
        bytetaper_compression
    )

    add_test(
      NAME content_encoding_test
      COMMAND content_encoding_test
    )

    add_executable(content_type_eligibility_test
      tests/content_type_eligibility_test.cpp
    )
    target_include_directories(content_type_eligibility_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(content_type_eligibility_test
      PRIVATE
        gtest_main
        bytetaper_compression
    )

    add_test(
      NAME content_type_eligibility_test
      COMMAND content_type_eligibility_test
    )

    add_executable(compression_size_test
      tests/compression_size_test.cpp
    )
    target_include_directories(compression_size_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(compression_size_test
      PRIVATE
        gtest_main
        bytetaper_compression
    )

    add_test(
      NAME compression_size_test
      COMMAND compression_size_test
    )

    add_executable(compression_decision_test
      tests/compression_decision_test.cpp
    )
    target_include_directories(compression_decision_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(compression_decision_test
      PRIVATE
        gtest_main
        bytetaper_compression
    )

    add_test(
      NAME compression_decision_test
      COMMAND compression_decision_test
    )

    add_executable(compression_decision_stage_test
      tests/compression_decision_stage_test.cpp
    )
    target_include_directories(compression_decision_stage_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(compression_decision_stage_test
      PRIVATE
        gtest_main
        bytetaper_stages
    )

    add_test(
      NAME compression_decision_stage_test
      COMMAND compression_decision_stage_test
    )

    add_executable(pagination_policy_validator_test
      tests/pagination_policy_validator_test.cpp
    )
    target_include_directories(pagination_policy_validator_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(pagination_policy_validator_test
      PRIVATE
        gtest_main
        bytetaper_policy
    )

    add_test(
      NAME pagination_policy_validator_test
      COMMAND pagination_policy_validator_test
    )
 
    add_executable(cache_policy_validator_test
      tests/cache_policy_validator_test.cpp
    )
    target_include_directories(cache_policy_validator_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(cache_policy_validator_test
      PRIVATE
        gtest_main
        bytetaper_policy
    )
 
    add_test(
      NAME cache_policy_validator_test
      COMMAND cache_policy_validator_test
    )

    add_executable(compression_policy_validator_test
      tests/compression_policy_validator_test.cpp
    )
    target_include_directories(compression_policy_validator_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(compression_policy_validator_test
      PRIVATE
        gtest_main
        bytetaper_policy
    )

    add_test(
      NAME compression_policy_validator_test
      COMMAND compression_policy_validator_test
    )

    add_executable(policy_yaml_loader_cache_test
      tests/policy_yaml_loader_cache_test.cpp
    )
    target_include_directories(policy_yaml_loader_cache_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(policy_yaml_loader_cache_test
      PRIVATE
        policy_yaml_loader
        gtest_main
    )

    add_test(
      NAME policy_yaml_loader_cache_test
      COMMAND policy_yaml_loader_cache_test
    )

    add_executable(policy_validate_command_test
      tests/policy_validate_command_test.cpp
    )
    target_include_directories(policy_validate_command_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(policy_validate_command_test
      PRIVATE
        policy_yaml_loader
        gtest_main
    )

    add_test(
      NAME policy_validate_command_test
      COMMAND policy_validate_command_test
    )

    add_test(
      NAME policy_validate_command_example_smoke
      COMMAND bytetaper-validate-policy
              ${CMAKE_CURRENT_SOURCE_DIR}/examples/policy/bytetaper-policy.yaml
    )

    add_executable(policy_report_output_test
      tests/policy_report_output_test.cpp
    )
    target_include_directories(policy_report_output_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(policy_report_output_test
      PRIVATE
        gtest_main
    )

    add_test(
      NAME policy_report_output_test
      COMMAND policy_report_output_test
    )

    add_executable(metrics_prometheus_registry_test
      tests/metrics_prometheus_registry_test.cpp
    )
    target_include_directories(metrics_prometheus_registry_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(metrics_prometheus_registry_test
      PRIVATE
        gtest_main
        bytetaper_prometheus_registry
    )

    add_test(
      NAME metrics_prometheus_registry_test
      COMMAND metrics_prometheus_registry_test
    )
 
    add_executable(cache_metrics_test
      tests/cache_metrics_test.cpp
    )
    target_include_directories(cache_metrics_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(cache_metrics_test
      PRIVATE
        gtest_main
        bytetaper_prometheus_registry
    )
 
    add_test(
      NAME cache_metrics_test
      COMMAND cache_metrics_test
    )

    add_executable(pagination_metrics_test
      tests/pagination_metrics_test.cpp
    )
    target_include_directories(pagination_metrics_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(pagination_metrics_test
      PRIVATE
        gtest_main
        bytetaper_prometheus_registry
    )

    add_test(
      NAME pagination_metrics_test
      COMMAND pagination_metrics_test
    )

    add_executable(compression_metrics_test
      tests/compression_metrics_test.cpp
    )
    target_include_directories(compression_metrics_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(compression_metrics_test
      PRIVATE
        gtest_main
        bytetaper_prometheus_registry
    )

    add_test(
      NAME compression_metrics_test
      COMMAND compression_metrics_test
    )

    add_executable(pagination_signal_test
      tests/pagination_signal_test.cpp
      src/api_intelligence/pagination_signal.cpp
    )
    target_include_directories(pagination_signal_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(pagination_signal_test
      PRIVATE
        gtest_main
    )

    add_test(
      NAME pagination_signal_test
      COMMAND pagination_signal_test
    )

    add_executable(compression_signal_test
      tests/compression_signal_test.cpp
      src/api_intelligence/compression_signal.cpp
    )
    target_include_directories(compression_signal_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(compression_signal_test
      PRIVATE
        gtest_main
    )

    add_test(
      NAME compression_signal_test
      COMMAND compression_signal_test
    )

    add_executable(coalescing_signal_test
      tests/coalescing_signal_test.cpp
      src/api_intelligence/coalescing_signal.cpp
    )
    target_include_directories(coalescing_signal_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(coalescing_signal_test
      PRIVATE
        gtest_main
    )
    add_test(
      NAME coalescing_signal_test
      COMMAND coalescing_signal_test
    )

    add_executable(coalescing_policy_test
      tests/coalescing_policy_test.cpp
    )
    target_include_directories(coalescing_policy_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(coalescing_policy_test
      PRIVATE
        gtest_main
        bytetaper_policy
    )

    add_test(
      NAME coalescing_policy_test
      COMMAND coalescing_policy_test
    )

    add_executable(coalescing_budget_policy_test
      tests/coalescing_budget_policy_test.cpp
    )
    target_include_directories(coalescing_budget_policy_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(coalescing_budget_policy_test
      PRIVATE
        gtest_main
        bytetaper_policy
    )

    add_test(
      NAME coalescing_budget_policy_test
      COMMAND coalescing_budget_policy_test
    )

    add_executable(safety_fail_open_test
      tests/safety_fail_open_test.cpp
      src/safety/fail_open.cpp
    )
    target_include_directories(safety_fail_open_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(safety_fail_open_test
      PRIVATE
        gtest_main
    )

    add_test(
      NAME safety_fail_open_test
      COMMAND safety_fail_open_test
    )

    add_executable(cache_entry_test
      tests/cache_entry_test.cpp
    )
    target_include_directories(cache_entry_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(cache_entry_test
      PRIVATE
        gtest_main
        bytetaper_cache
    )

    add_test(
      NAME cache_entry_test
      COMMAND cache_entry_test
    )

    add_executable(cache_key_test
      tests/cache_key_test.cpp
    )
    target_include_directories(cache_key_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(cache_key_test
      PRIVATE
        gtest_main
        bytetaper_cache
    )

    add_test(
      NAME cache_key_test
      COMMAND cache_key_test
    )

    add_executable(cache_key_auth_scope_test
      tests/cache_key_auth_scope_test.cpp
    )
    target_include_directories(cache_key_auth_scope_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(cache_key_auth_scope_test
      PRIVATE
        gtest_main
        bytetaper_cache
    )

    add_test(
      NAME cache_key_auth_scope_test
      COMMAND cache_key_auth_scope_test
    )

    add_executable(l2_rocksdb_cache_test
      tests/l2_rocksdb_cache_test.cpp
    )
    target_include_directories(l2_rocksdb_cache_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(l2_rocksdb_cache_test
      PRIVATE
        gtest_main
        bytetaper_cache
        RocksDB::RocksDB
    )

    add_test(
      NAME l2_rocksdb_cache_test
      COMMAND l2_rocksdb_cache_test
    )

    add_executable(cache_ttl_test
      tests/cache_ttl_test.cpp
    )
    target_include_directories(cache_ttl_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(cache_ttl_test
      PRIVATE
        gtest_main
        bytetaper_cache
    )

    add_test(
      NAME cache_ttl_test
      COMMAND cache_ttl_test
    )

    add_executable(cache_auth_safety_test
      tests/cache_auth_safety_test.cpp
    )
    target_include_directories(cache_auth_safety_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(cache_auth_safety_test
      PRIVATE
        gtest_main
        bytetaper_cache
    )

    add_test(
      NAME cache_auth_safety_test
      COMMAND cache_auth_safety_test
    )

    add_executable(pagination_query_test
      tests/pagination_query_test.cpp
    )
    target_include_directories(pagination_query_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(pagination_query_test
      PRIVATE
        gtest_main
        bytetaper_pagination
        bytetaper_apg
    )

    add_test(
      NAME pagination_query_test
      COMMAND pagination_query_test
    )

    add_executable(missing_limit_decision_test
      tests/missing_limit_decision_test.cpp
    )
    target_include_directories(missing_limit_decision_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(missing_limit_decision_test
      PRIVATE
        gtest_main
        bytetaper_pagination
        bytetaper_policy
        bytetaper_apg
    )

    add_test(
      NAME missing_limit_decision_test
      COMMAND missing_limit_decision_test
    )

    add_executable(default_limit_mutation_test
      tests/default_limit_mutation_test.cpp
    )
    target_include_directories(default_limit_mutation_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(default_limit_mutation_test
      PRIVATE
        gtest_main
        bytetaper_pagination
        bytetaper_policy
        bytetaper_apg
    )

    add_test(
      NAME default_limit_mutation_test
      COMMAND default_limit_mutation_test
    )

    add_executable(max_limit_enforcement_test
      tests/max_limit_enforcement_test.cpp
    )
    target_include_directories(max_limit_enforcement_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(max_limit_enforcement_test
      PRIVATE
        gtest_main
        bytetaper_pagination
        bytetaper_policy
        bytetaper_apg
    )

    add_test(
      NAME max_limit_enforcement_test
      COMMAND max_limit_enforcement_test
    )

    add_executable(preserve_valid_limit_test
      tests/preserve_valid_limit_test.cpp
    )
    target_include_directories(preserve_valid_limit_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(preserve_valid_limit_test
      PRIVATE
        gtest_main
        bytetaper_pagination
        bytetaper_policy
        bytetaper_apg
    )

    add_test(
      NAME preserve_valid_limit_test
      COMMAND preserve_valid_limit_test
    )

    add_executable(offset_query_test
      tests/offset_query_test.cpp
    )
    target_include_directories(offset_query_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(offset_query_test
      PRIVATE
        gtest_main
        bytetaper_pagination
        bytetaper_apg
    )

    add_test(
      NAME offset_query_test
      COMMAND offset_query_test
    )

    add_executable(pagination_cursor_placeholder_test
      tests/pagination_cursor_placeholder_test.cpp
    )
    target_include_directories(pagination_cursor_placeholder_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(pagination_cursor_placeholder_test
      PRIVATE
        gtest_main
        bytetaper_policy
    )

    add_test(
      NAME pagination_cursor_placeholder_test
      COMMAND pagination_cursor_placeholder_test
    )

    add_executable(pagination_request_mutation_stage_test
      tests/pagination_request_mutation_stage_test.cpp
    )
    target_include_directories(pagination_request_mutation_stage_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(pagination_request_mutation_stage_test
      PRIVATE
        gtest_main
        bytetaper_stages
    )

    add_test(
      NAME pagination_request_mutation_stage_test
      COMMAND pagination_request_mutation_stage_test
    )

    add_executable(oversized_response_guard_test
      tests/oversized_response_guard_test.cpp
    )
    target_include_directories(oversized_response_guard_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(oversized_response_guard_test
      PRIVATE
        gtest_main
        bytetaper_pagination
    )

    add_test(
      NAME oversized_response_guard_test
      COMMAND oversized_response_guard_test
    )
  endif()

  if(BYTETAPER_ENABLE_INTEGRATION_TESTS)
    add_executable(extproc_server_smoke_test
      tests/integration/extproc/extproc_server_smoke_test.cpp
    )
    target_include_directories(extproc_server_smoke_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BYTETAPER_GENERATED_PROTO_DIR}
    )
    target_link_libraries(extproc_server_smoke_test
      PRIVATE
        bytetaper_extproc_grpc_server
        bytetaper_extproc_grpc
    )

    add_test(
      NAME extproc_server_smoke_test
      COMMAND extproc_server_smoke_test
    )

    add_executable(extproc_l2_sync_removed_integration_test
      tests/integration/extproc/extproc_l2_sync_removed_integration_test.cpp
    )
    target_include_directories(extproc_l2_sync_removed_integration_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BYTETAPER_GENERATED_PROTO_DIR}
    )
    target_link_libraries(extproc_l2_sync_removed_integration_test
      PRIVATE
        bytetaper_extproc_grpc_server
        bytetaper_extproc_grpc
    )
    add_test(
      NAME extproc_l2_sync_removed_integration_test
      COMMAND extproc_l2_sync_removed_integration_test
    )

    add_executable(extproc_externalprocessor_service_integration_test
      tests/integration/extproc/externalprocessor_service_integration_test.cpp
    )
    target_include_directories(extproc_externalprocessor_service_integration_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BYTETAPER_GENERATED_PROTO_DIR}
    )
    target_link_libraries(extproc_externalprocessor_service_integration_test
      PRIVATE
        bytetaper_extproc_grpc_server
        bytetaper_extproc_grpc
    )

    add_test(
      NAME extproc_externalprocessor_service_integration_test
      COMMAND extproc_externalprocessor_service_integration_test
    )

    add_executable(extproc_original_bytes_integration_test
      tests/integration/extproc/extproc_original_bytes_integration_test.cpp
    )
    target_include_directories(extproc_original_bytes_integration_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BYTETAPER_GENERATED_PROTO_DIR}
    )
    target_link_libraries(extproc_original_bytes_integration_test
      PRIVATE
        bytetaper_extproc_grpc_server
        bytetaper_extproc_grpc
    )

    add_test(
      NAME extproc_original_bytes_integration_test
      COMMAND extproc_original_bytes_integration_test
    )

    add_executable(extproc_optimized_bytes_integration_test
      tests/integration/extproc/extproc_optimized_bytes_integration_test.cpp
    )
    target_include_directories(extproc_optimized_bytes_integration_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BYTETAPER_GENERATED_PROTO_DIR}
    )
    target_link_libraries(extproc_optimized_bytes_integration_test
      PRIVATE
        bytetaper_extproc_grpc_server
        bytetaper_extproc_grpc
    )

    add_test(
      NAME extproc_optimized_bytes_integration_test
      COMMAND extproc_optimized_bytes_integration_test
    )

    add_executable(extproc_original_bytes_header_integration_test
      tests/integration/extproc/extproc_original_bytes_header_integration_test.cpp
    )
    target_include_directories(extproc_original_bytes_header_integration_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BYTETAPER_GENERATED_PROTO_DIR}
    )
    target_link_libraries(extproc_original_bytes_header_integration_test
      PRIVATE
        bytetaper_extproc_grpc_server
        bytetaper_extproc_grpc
    )

    add_test(
      NAME extproc_original_bytes_header_integration_test
      COMMAND extproc_original_bytes_header_integration_test
    )

    add_executable(extproc_optimized_bytes_header_integration_test
      tests/integration/extproc/extproc_optimized_bytes_header_integration_test.cpp
    )
    target_include_directories(extproc_optimized_bytes_header_integration_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BYTETAPER_GENERATED_PROTO_DIR}
    )
    target_link_libraries(extproc_optimized_bytes_header_integration_test
      PRIVATE
        bytetaper_extproc_grpc_server
        bytetaper_extproc_grpc
    )

    add_test(
      NAME extproc_optimized_bytes_header_integration_test
      COMMAND extproc_optimized_bytes_header_integration_test
    )

    add_executable(extproc_transform_applied_integration_test
      tests/integration/extproc/extproc_transform_applied_integration_test.cpp
    )
    target_include_directories(extproc_transform_applied_integration_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BYTETAPER_GENERATED_PROTO_DIR}
    )
    target_link_libraries(extproc_transform_applied_integration_test
      PRIVATE
        bytetaper_extproc_grpc_server
        bytetaper_extproc_grpc
    )

    add_test(
      NAME extproc_transform_applied_integration_test
      COMMAND extproc_transform_applied_integration_test
    )

    add_executable(extproc_route_policy_integration_test
      tests/integration/extproc/extproc_route_policy_integration_test.cpp
    )
    target_include_directories(extproc_route_policy_integration_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BYTETAPER_GENERATED_PROTO_DIR}
    )
    target_link_libraries(extproc_route_policy_integration_test
      PRIVATE
        bytetaper_extproc_grpc_server
        bytetaper_extproc_grpc
    )

    add_test(
      NAME extproc_route_policy_integration_test
      COMMAND extproc_route_policy_integration_test
    )

    add_executable(extproc_metrics_endpoint_integration_test
      tests/integration/extproc/extproc_metrics_endpoint_integration_test.cpp
    )
    target_include_directories(extproc_metrics_endpoint_integration_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BYTETAPER_GENERATED_PROTO_DIR}
    )
    target_link_libraries(extproc_metrics_endpoint_integration_test
      PRIVATE
        bytetaper_extproc_grpc_server
        bytetaper_extproc_grpc
        bytetaper_metrics_http_server
        bytetaper_prometheus_registry
    )

    add_test(
      NAME extproc_metrics_endpoint_integration_test
      COMMAND extproc_metrics_endpoint_integration_test
    )

    add_executable(extproc_fail_closed_integration_test
      tests/integration/extproc/extproc_fail_closed_integration_test.cpp
    )
    target_include_directories(extproc_fail_closed_integration_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BYTETAPER_GENERATED_PROTO_DIR}
    )
    target_link_libraries(extproc_fail_closed_integration_test
      PRIVATE
        bytetaper_extproc_grpc_server
        bytetaper_extproc_grpc
    )

    add_test(
      NAME extproc_fail_closed_integration_test
      COMMAND extproc_fail_closed_integration_test
    )

    add_executable(extproc_payload_too_large_integration_test
      tests/integration/extproc/extproc_payload_too_large_integration_test.cpp
    )
    target_include_directories(extproc_payload_too_large_integration_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BYTETAPER_GENERATED_PROTO_DIR}
    )
    target_link_libraries(extproc_payload_too_large_integration_test
      PRIVATE
        bytetaper_extproc_grpc_server
        bytetaper_extproc_grpc
    )

    add_test(
      NAME extproc_payload_too_large_integration_test
      COMMAND extproc_payload_too_large_integration_test
    )

    add_executable(extproc_non_2xx_integration_test
      tests/integration/extproc/extproc_non_2xx_integration_test.cpp
    )
    target_include_directories(extproc_non_2xx_integration_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BYTETAPER_GENERATED_PROTO_DIR}
    )
    target_link_libraries(extproc_non_2xx_integration_test
      PRIVATE
        bytetaper_extproc_grpc_server
        bytetaper_extproc_grpc
    )

    add_test(
      NAME extproc_non_2xx_integration_test
      COMMAND extproc_non_2xx_integration_test
    )

    add_executable(extproc_non_json_integration_test
      tests/integration/extproc/extproc_non_json_integration_test.cpp
    )
    target_include_directories(extproc_non_json_integration_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BYTETAPER_GENERATED_PROTO_DIR}
    )
    target_link_libraries(extproc_non_json_integration_test
      PRIVATE
        bytetaper_extproc_grpc_server
        bytetaper_extproc_grpc
    )

    add_test(
      NAME extproc_non_json_integration_test
      COMMAND extproc_non_json_integration_test
    )

    add_executable(extproc_malformed_input_integration_test
      tests/integration/extproc/extproc_malformed_input_integration_test.cpp
    )
    target_include_directories(extproc_malformed_input_integration_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BYTETAPER_GENERATED_PROTO_DIR}
    )
    target_link_libraries(extproc_malformed_input_integration_test
      PRIVATE
        bytetaper_extproc_grpc_server
        bytetaper_extproc_grpc
    )

    add_test(
      NAME extproc_malformed_input_integration_test
      COMMAND extproc_malformed_input_integration_test
    )

    add_executable(extproc_invalid_policy_integration_test
      tests/integration/extproc/extproc_invalid_policy_integration_test.cpp
    )
    target_include_directories(extproc_invalid_policy_integration_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BYTETAPER_GENERATED_PROTO_DIR}
    )
    target_link_libraries(extproc_invalid_policy_integration_test
      PRIVATE
        bytetaper_extproc_grpc_server
        bytetaper_extproc_grpc
    )

    add_test(
      NAME extproc_invalid_policy_integration_test
      COMMAND extproc_invalid_policy_integration_test
    )

    add_executable(extproc_observe_mode_integration_test
      tests/integration/extproc/extproc_observe_mode_integration_test.cpp
    )
    target_include_directories(extproc_observe_mode_integration_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BYTETAPER_GENERATED_PROTO_DIR}
    )
    target_link_libraries(extproc_observe_mode_integration_test
      PRIVATE
        bytetaper_extproc_grpc_server
        bytetaper_extproc_grpc
    )

    add_test(
      NAME extproc_observe_mode_integration_test
      COMMAND extproc_observe_mode_integration_test
    )

    add_executable(extproc_policy_not_found_integration_test
      tests/integration/extproc/extproc_policy_not_found_integration_test.cpp
    )
    target_include_directories(extproc_policy_not_found_integration_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BYTETAPER_GENERATED_PROTO_DIR}
    )
    target_link_libraries(extproc_policy_not_found_integration_test
      PRIVATE
        bytetaper_extproc_grpc_server
        bytetaper_extproc_grpc
    )

    add_test(
      NAME extproc_policy_not_found_integration_test
      COMMAND extproc_policy_not_found_integration_test
    )

    add_executable(extproc_pagination_headers_integration_test
      tests/integration/extproc/extproc_pagination_headers_integration_test.cpp
    )
    target_include_directories(extproc_pagination_headers_integration_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BYTETAPER_GENERATED_PROTO_DIR}
    )
    target_link_libraries(extproc_pagination_headers_integration_test
      PRIVATE
        bytetaper_extproc_grpc_server
        bytetaper_extproc_grpc
    )

    add_test(
      NAME extproc_pagination_headers_integration_test
      COMMAND extproc_pagination_headers_integration_test
    )

    add_executable(extproc_compression_candidate_header_integration_test
      tests/integration/extproc/extproc_compression_candidate_header_integration_test.cpp
    )
    target_include_directories(extproc_compression_candidate_header_integration_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BYTETAPER_GENERATED_PROTO_DIR}
    )
    target_link_libraries(extproc_compression_candidate_header_integration_test
      PRIVATE
        bytetaper_extproc_grpc_server
        bytetaper_extproc_grpc
    )

    add_test(
      NAME extproc_compression_candidate_header_integration_test
      COMMAND extproc_compression_candidate_header_integration_test
    )
    
    add_executable(extproc_worker_lifecycle_cleanup_test
      tests/integration/extproc/extproc_worker_lifecycle_cleanup_test.cpp
    )
    target_include_directories(extproc_worker_lifecycle_cleanup_test
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BYTETAPER_GENERATED_PROTO_DIR}
    )
    target_link_libraries(extproc_worker_lifecycle_cleanup_test
      PRIVATE
        bytetaper_extproc_grpc_server
        bytetaper_extproc_grpc
        gtest
    )
    add_test(
      NAME extproc_worker_lifecycle_cleanup_test
      COMMAND extproc_worker_lifecycle_cleanup_test
    )

    add_executable(json_filter_benchmark
      benchmarks/json_filter_benchmark.cpp
    )
    target_include_directories(json_filter_benchmark
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BYTETAPER_GENERATED_PROTO_DIR}
    )
    target_link_libraries(json_filter_benchmark
      PRIVATE
        bytetaper_extproc_grpc_server
        bytetaper_extproc_grpc
    )

    add_executable(taperquery_policy_ir_test
      tests/taperquery_policy_ir_test.cpp
    )
    target_include_directories(taperquery_policy_ir_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(taperquery_policy_ir_test PRIVATE gtest_main bytetaper_taperquery)
    add_test(NAME taperquery_policy_ir_test COMMAND taperquery_policy_ir_test)

    add_executable(taperquery_policy_ir_normalize_test
      tests/taperquery_policy_ir_normalize_test.cpp
    )
    target_include_directories(taperquery_policy_ir_normalize_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(taperquery_policy_ir_normalize_test PRIVATE gtest_main bytetaper_taperquery)
    add_test(NAME taperquery_policy_ir_normalize_test COMMAND taperquery_policy_ir_normalize_test)

    add_executable(taperquery_policy_ir_hash_test
      tests/taperquery_policy_ir_hash_test.cpp
    )
    target_include_directories(taperquery_policy_ir_hash_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(taperquery_policy_ir_hash_test PRIVATE gtest_main bytetaper_taperquery)
    add_test(NAME taperquery_policy_ir_hash_test COMMAND taperquery_policy_ir_hash_test)

    add_executable(taperquery_policy_parity_examples_test
      tests/taperquery_policy_parity_examples_test.cpp
    )
    target_include_directories(taperquery_policy_parity_examples_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(taperquery_policy_parity_examples_test PRIVATE gtest_main bytetaper_taperquery_loader)
    add_test(NAME taperquery_policy_parity_examples_test COMMAND taperquery_policy_parity_examples_test)
  endif()
endif()
