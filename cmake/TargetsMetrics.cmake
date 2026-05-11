# cmake/TargetsMetrics.cmake — Metrics library targets

add_library(bytetaper_hash STATIC
  src/hash/hash.cpp
)
target_include_directories(bytetaper_hash PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(bytetaper_prometheus_registry STATIC
  src/metrics/prometheus_registry.cpp
  src/metrics/cache_metrics.cpp
  src/metrics/pagination_metrics.cpp
  src/metrics/compression_metrics.cpp
  src/metrics/coalescing_metrics.cpp
  src/metrics/runtime_metrics.cpp
)
target_include_directories(bytetaper_prometheus_registry
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(bytetaper_prometheus_registry
  PUBLIC
    bytetaper_hash
)

add_library(bytetaper_metrics_http_server STATIC
  src/metrics/metrics_http_server.cpp
)
target_include_directories(bytetaper_metrics_http_server
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(bytetaper_metrics_http_server
  PRIVATE
    bytetaper_prometheus_registry
    Threads::Threads
)
