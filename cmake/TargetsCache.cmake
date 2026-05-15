# cmake/TargetsCache.cmake — Cache library targets

add_library(bytetaper_cache STATIC
  src/cache/cache_invalidation_target_resolver.cpp
  src/cache/l1_cache.cpp
  src/cache/l2_rocksdb_cache.cpp
  src/cache/cache_entry_codec.cpp
  src/cache/cache_entry.cpp
  src/cache/cache_key.cpp
  src/cache/cache_ttl.cpp
  src/cache/cache_safety.cpp
)
set_source_files_properties(src/cache/l2_rocksdb_cache.cpp
  PROPERTIES COMPILE_OPTIONS "-fexceptions"
)
target_include_directories(bytetaper_cache PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bytetaper_cache PUBLIC bytetaper_hash bytetaper_prometheus_registry RocksDB::RocksDB)
