
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(public-lib-yq)
target_link_libraries(public-lib-yq PUBLIC
  contrib-libs-cxxsupp
  yutil
  library-cpp-json
  api-grpc-draft
  cpp-client-ydb_table
)
target_sources(public-lib-yq PRIVATE
  ${CMAKE_SOURCE_DIR}/ydb/public/lib/yq/yq.cpp
  ${CMAKE_SOURCE_DIR}/ydb/public/lib/yq/scope.cpp
)
