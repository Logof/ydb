
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(yq-libs-ydb)
target_compile_options(yq-libs-ydb PRIVATE
  -DUSE_CURRENT_UDF_ABI_VERSION
)
target_link_libraries(yq-libs-ydb PUBLIC
  contrib-libs-cxxsupp
  yutil
  cpp-actors-core
  library-cpp-retry
  ydb-core-base
  yq-libs-config
  yq-libs-events
  ydb-library-security
  cpp-client-ydb_coordination
  cpp-client-ydb_rate_limiter
  cpp-client-ydb_scheme
  cpp-client-ydb_table
  tools-enum_parser-enum_serialization_runtime
)
target_sources(yq-libs-ydb PRIVATE
  ${CMAKE_SOURCE_DIR}/ydb/core/yq/libs/ydb/schema.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/yq/libs/ydb/util.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/yq/libs/ydb/ydb.cpp
)
generate_enum_serilization(yq-libs-ydb
  ${CMAKE_SOURCE_DIR}/ydb/core/yq/libs/ydb/ydb.h
  INCLUDE_HEADERS
  ydb/core/yq/libs/ydb/ydb.h
)
