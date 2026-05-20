# cmake/TargetsAdmin.cmake — TaperQuery admin http server target

add_library(bytetaper_admin STATIC
  src/admin/taperquery_admin_http_server.cpp
  src/admin/control_plane_admin_http_server.cpp
)
target_include_directories(bytetaper_admin
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(bytetaper_admin
  PRIVATE
    bytetaper_control_plane_service
    bytetaper_taperquery_apply
    bytetaper_policy
    bytetaper_runtime_policy
    Threads::Threads
)
