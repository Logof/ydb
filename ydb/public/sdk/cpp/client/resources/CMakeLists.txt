
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(cpp-client-resources)
target_link_libraries(cpp-client-resources PUBLIC
  contrib-libs-cxxsupp
  yutil
  library-cpp-resource
)
target_sources(cpp-client-resources PRIVATE
  ${CMAKE_SOURCE_DIR}/ydb/public/sdk/cpp/client/resources/ydb_resources.cpp
  ${CMAKE_SOURCE_DIR}/ydb/public/sdk/cpp/client/resources/ydb_ca.cpp
)

add_global_library_for(cpp-client-resources.global cpp-client-resources)
target_link_libraries(cpp-client-resources.global PUBLIC
  contrib-libs-cxxsupp
  yutil
  library-cpp-resource
)
target_sources(cpp-client-resources.global PRIVATE
  ${CMAKE_BINARY_DIR}/ydb/public/sdk/cpp/client/resources/6ed212bf45019efe2a5e72b6d5ed50fb.cpp
)
resources(cpp-client-resources.global
  ${CMAKE_BINARY_DIR}/ydb/public/sdk/cpp/client/resources/6ed212bf45019efe2a5e72b6d5ed50fb.cpp
  INPUTS
  ${CMAKE_SOURCE_DIR}/ydb/public/sdk/cpp/client/resources/ydb_sdk_version.txt
  ${CMAKE_SOURCE_DIR}/ydb/public/sdk/cpp/client/resources/ydb_root_ca.pem
  KEYS
  ydb_sdk_version.txt
  ydb_root_ca.pem
)
