
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(yq-libs-events)
target_compile_options(yq-libs-events PRIVATE
  -DUSE_CURRENT_UDF_ABI_VERSION
)
target_link_libraries(yq-libs-events PUBLIC
  contrib-libs-cxxsupp
  yutil
  tools-enum_parser-enum_serialization_runtime
  cpp-actors-core
  libs-graph_params-proto
  yq-libs-protos
  yql-core-facade
  yql-public-issue
  api-protos
  public-lib-yq
  cpp-client-ydb_table
  providers-common-db_id_async_resolver
  providers-dq-provider
)
generate_enum_serilization(yq-libs-events
  ${CMAKE_SOURCE_DIR}/ydb/core/yq/libs/events/events.h
  INCLUDE_HEADERS
  ydb/core/yq/libs/events/events.h
)
generate_enum_serilization(yq-libs-events
  ${CMAKE_SOURCE_DIR}/ydb/core/yq/libs/events/event_ids.h
  INCLUDE_HEADERS
  ydb/core/yq/libs/events/event_ids.h
)
