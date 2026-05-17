# cmake/TargetsPolicy.cmake — Policy library and tool targets

add_library(bytetaper_policy STATIC
  src/policy/pagination_policy.cpp
  src/policy/compression_policy.cpp
  src/policy/cache_policy.cpp
  src/policy/coalescing_policy.cpp
  src/policy/pagination_policy_validator.cpp
  src/policy/compression_policy_validator.cpp
  src/policy/cache_policy_validator.cpp
  src/policy/coalescing_policy_validator.cpp
  src/policy/policy_semantic_validator.cpp
  src/policy/policy_identity.cpp
)
target_include_directories(bytetaper_policy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(policy_yaml_loader STATIC
  src/policy/yaml_loader.cpp
)
set_source_files_properties(src/policy/yaml_loader.cpp
  PROPERTIES COMPILE_OPTIONS "-fexceptions"
)
target_include_directories(policy_yaml_loader
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${YAML_CPP_INCLUDE_DIRS}
)
target_link_directories(policy_yaml_loader
  PUBLIC
    ${YAML_CPP_LIBRARY_DIRS}
)
target_link_libraries(policy_yaml_loader
  PUBLIC
    bytetaper_policy
    ${YAML_CPP_LIBRARIES}
)

add_executable(bytetaper-validate-policy
  src/policy/validate_policy_main.cpp
)
target_include_directories(bytetaper-validate-policy
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(bytetaper-validate-policy
  PRIVATE
    policy_yaml_loader
)
