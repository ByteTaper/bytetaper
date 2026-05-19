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
)
target_include_directories(bytetaper_control_plane_service PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bytetaper_control_plane_service PUBLIC
  bytetaper_control_plane
  bytetaper_taperquery_loader
  bytetaper_taperquery
)
