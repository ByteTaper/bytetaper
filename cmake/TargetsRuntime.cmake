# cmake/TargetsRuntime.cmake — Runtime pipeline library targets

add_library(bytetaper_apg STATIC
  src/apg/pipeline.cpp
  src/apg/query_view.cpp
)
target_include_directories(bytetaper_apg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(bytetaper_runtime STATIC
  src/runtime/worker_queue.cpp
  src/runtime/policy_snapshot.cpp
)
target_include_directories(bytetaper_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bytetaper_runtime PUBLIC bytetaper_cache bytetaper_coalescing bytetaper_prometheus_registry Threads::Threads bytetaper_policy bytetaper_taperquery)

add_library(bytetaper_pagination STATIC
  src/pagination/pagination_decision.cpp
  src/pagination/pagination_mutation.cpp
  src/pagination/pagination_query.cpp
  src/pagination/oversized_response_guard.cpp
)
target_include_directories(bytetaper_pagination PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bytetaper_pagination PUBLIC bytetaper_apg)

add_library(bytetaper_compression STATIC
  src/compression/accept_encoding.cpp
  src/compression/content_encoding.cpp
  src/compression/compression_eligibility.cpp
  src/compression/compression_size.cpp
  src/compression/compression_decision.cpp
)
target_include_directories(bytetaper_compression PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(bytetaper_safety STATIC
  src/safety/fail_open.cpp
)
target_include_directories(bytetaper_safety PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(bytetaper_field_selection STATIC
  src/field_selection/request_target.cpp
)
target_include_directories(bytetaper_field_selection PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(bytetaper_json_transform STATIC
  src/json_transform/content_type.cpp
  src/json_transform/flat_json.cpp
  src/json_transform/filter_flat_json.cpp
)
target_include_directories(bytetaper_json_transform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(bytetaper_stages STATIC
  src/stages/l1_cache_lookup_stage.cpp
  src/stages/l1_cache_store_stage.cpp
  src/stages/l2_cache_lookup_stage.cpp
  src/stages/l2_cache_store_stage.cpp
  src/stages/pagination_request_mutation_stage.cpp
  src/stages/l2_cache_async_lookup_enqueue_stage.cpp
  src/stages/l2_cache_async_store_enqueue_stage.cpp
  src/stages/coalescing_follower_wait_stage.cpp
  src/stages/coalescing_leader_completion_stage.cpp
  src/stages/coalescing_decision_stage.cpp
  src/stages/compression_decision_stage.cpp
  src/stages/cache_key_prepare_stage.cpp
  src/stages/field_variant_admission_stage.cpp
  src/stages/l1_variant_lookup_stage.cpp
  src/stages/l1_variant_store_stage.cpp
)
target_include_directories(bytetaper_stages PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bytetaper_stages PUBLIC
  bytetaper_apg
  bytetaper_cache
  bytetaper_runtime
  bytetaper_coalescing
  bytetaper_pagination
  bytetaper_compression
  bytetaper_policy
  bytetaper_prometheus_registry
)
