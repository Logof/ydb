
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(public-lib-scheme_types)
target_link_libraries(public-lib-scheme_types PUBLIC
  contrib-libs-cxxsupp
  yutil
  yql-public-types
)
target_sources(public-lib-scheme_types PRIVATE
  ${CMAKE_SOURCE_DIR}/ydb/public/lib/scheme_types/scheme_type_id.cpp
)
