
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.


add_subdirectory(ut)

add_library(providers-s3-range_helpers)
target_compile_options(providers-s3-range_helpers PRIVATE
  -DUSE_CURRENT_UDF_ABI_VERSION
)
target_link_libraries(providers-s3-range_helpers PUBLIC
  contrib-libs-cxxsupp
  yutil
  providers-common-provider
  providers-s3-proto
  library-yql-utils
  cpp-protobuf-util
)
target_sources(providers-s3-range_helpers PRIVATE
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/providers/s3/range_helpers/file_tree_builder.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/providers/s3/range_helpers/path_list_reader.cpp
)
