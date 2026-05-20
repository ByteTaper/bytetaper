# cmake/TargetsOperational.cmake — Operational activation barrier

add_library(bytetaper_operational STATIC
  src/operational/policy_activation_result.cpp
  src/operational/policy_operational_diff.cpp
  src/operational/cache_namespace_sync.cpp
  src/operational/route_epoch_sync.cpp
  src/operational/materialized_variant_sync.cpp
  src/operational/policy_cleanup_sync.cpp
  src/operational/policy_activation_barrier.cpp
  src/runtime_policy/runtime_policy_metrics.cpp
  src/runtime_policy/runtime_policy_log_events.cpp
)

target_include_directories(bytetaper_operational PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bytetaper_operational PUBLIC
  bytetaper_control_plane
  bytetaper_taperquery_apply
  bytetaper_runtime
  bytetaper_cache
  bytetaper_logger
)
