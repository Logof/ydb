
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.


add_subdirectory(ut)

add_library(tx-long_tx_service-public)
target_compile_options(tx-long_tx_service-public PRIVATE
  -DUSE_CURRENT_UDF_ABI_VERSION
)
target_link_libraries(tx-long_tx_service-public PUBLIC
  contrib-libs-cxxsupp
  yutil
  library-cpp-cgiparam
  library-cpp-uri
  ydb-core-base
  ydb-core-protos
  ydb-core-util
  yql-public-issue
)
target_sources(tx-long_tx_service-public PRIVATE
  ${CMAKE_SOURCE_DIR}/ydb/core/tx/long_tx_service/public/events.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/tx/long_tx_service/public/lock_handle.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/tx/long_tx_service/public/types.cpp
)
