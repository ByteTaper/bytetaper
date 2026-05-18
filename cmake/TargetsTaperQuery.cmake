# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

add_library(bytetaper_taperquery STATIC
  src/taperquery/policy_ir_version.cpp
  src/taperquery/policy_ir_normalize.cpp
  src/taperquery/policy_ir_identity.cpp
  src/taperquery/policy_ir_printer.cpp
  src/taperquery/policy_ir_compare.cpp
  src/taperquery/route_analysis.cpp
  src/taperquery/policy_ir_validator.cpp
  src/taperquery/tq_lexer.cpp
  src/taperquery/tq_parser.cpp
  src/taperquery/tq_compiler.cpp
  src/taperquery/tq_diagnostic.cpp
  src/taperquery/tq_plan.cpp
  src/taperquery/tq_dry_run_reporter.cpp
  src/taperquery/policy_ir_yaml_emitter.cpp
)

target_include_directories(bytetaper_taperquery PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bytetaper_taperquery PUBLIC bytetaper_hash bytetaper_policy)

add_library(bytetaper_taperquery_apply STATIC
  src/taperquery/tq_apply_service.cpp
  src/taperquery/tq_apply_audit.cpp
  src/taperquery/policy_persistence.cpp
  src/taperquery/policy_ir_yaml_roundtrip.cpp
  src/taperquery/tq_cache_namespace_versioning.cpp
)
target_include_directories(bytetaper_taperquery_apply PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bytetaper_taperquery_apply PUBLIC
  bytetaper_taperquery
  bytetaper_taperquery_loader
  bytetaper_runtime
)

add_library(bytetaper_taperquery_loader STATIC
  src/taperquery/policy_ir_from_yaml.cpp
)
set_source_files_properties(src/taperquery/policy_ir_from_yaml.cpp
  PROPERTIES COMPILE_OPTIONS "-fexceptions"
)
target_include_directories(bytetaper_taperquery_loader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bytetaper_taperquery_loader PUBLIC
  bytetaper_taperquery
  policy_yaml_loader
)
