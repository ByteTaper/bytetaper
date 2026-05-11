# cmake/TargetsCoalescing.cmake — Coalescing library targets

add_library(bytetaper_coalescing STATIC
  src/coalescing/coalescing_key.cpp
  src/coalescing/coalescing_eligibility.cpp
  src/coalescing/coalescing_safety.cpp
  src/coalescing/inflight_registry.cpp
  src/coalescing/coalescing_decision.cpp
  src/coalescing/wait_window.cpp
  src/coalescing/coalescing_timeout.cpp
  src/coalescing/coalescing_completion_handoff.cpp
)
target_include_directories(bytetaper_coalescing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bytetaper_coalescing PUBLIC bytetaper_cache bytetaper_prometheus_registry bytetaper_hash)
