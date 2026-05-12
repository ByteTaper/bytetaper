# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

add_library(bytetaper_taperquery STATIC
  src/taperquery/policy_ir.cpp
  src/taperquery/policy_ir_normalize.cpp
  src/taperquery/policy_ir_hash.cpp
  src/taperquery/policy_ir_printer.cpp
  src/taperquery/policy_ir_compare.cpp
)

target_include_directories(bytetaper_taperquery PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bytetaper_taperquery PUBLIC bytetaper_hash bytetaper_policy)

add_library(bytetaper_taperquery_loader STATIC
  src/taperquery/policy_ir_from_yaml.cpp
)
target_include_directories(bytetaper_taperquery_loader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bytetaper_taperquery_loader PUBLIC
  bytetaper_taperquery
  policy_yaml_loader
)
