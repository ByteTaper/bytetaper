# cmake/TargetsControlPlane.cmake — Control plane policy state library

add_library(bytetaper_control_plane STATIC
  src/control_plane/policy_state_key.cpp
  src/control_plane/policy_state_record.cpp
  src/control_plane/rocksdb_policy_state_store.cpp
)
set_source_files_properties(src/control_plane/rocksdb_policy_state_store.cpp
  PROPERTIES COMPILE_OPTIONS "-fexceptions"
)
target_include_directories(bytetaper_control_plane PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bytetaper_control_plane PUBLIC RocksDB::RocksDB)

add_library(bytetaper_control_plane_service STATIC
  src/control_plane/policy_apply_status.cpp
  src/control_plane/policy_apply_api.cpp
  src/control_plane/policy_apply_contract.cpp
  src/control_plane/control_plane_service.cpp
  src/control_plane/policy_update_job.cpp
  src/control_plane/policy_update_job_record.cpp
  src/control_plane/policy_update_shard.cpp
  src/control_plane/policy_update_queue.cpp
  src/control_plane/policy_update_worker.cpp
  src/control_plane/policy_apply_transaction.cpp
  src/control_plane/runtime_status_report.cpp
  src/control_plane/runtime_convergence_status.cpp
  src/control_plane/fleet_status_service.cpp
  src/control_plane/manual_resolution_api.cpp
  src/control_plane/manual_resolution_audit.cpp
  src/control_plane/policy_generation_commit.cpp
  src/control_plane/policy_rollback_operation.cpp
  src/control_plane/policy_adopt_operation.cpp
  src/control_plane/policy_repair_operation.cpp
  src/control_plane/manual_resolution_service.cpp
)
target_include_directories(bytetaper_control_plane_service PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bytetaper_control_plane_service PUBLIC
  bytetaper_control_plane
  bytetaper_operational
  bytetaper_taperquery_loader
  bytetaper_taperquery
  bytetaper_taperquery_apply
  bytetaper_runtime
  Threads::Threads
)
