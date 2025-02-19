
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(restricted-boost-regex)
target_compile_options(restricted-boost-regex PRIVATE
  -DBOOST_HAS_ICU
  -DBOOST_NO_CXX98_BINDERS
)
target_include_directories(restricted-boost-regex PUBLIC
  ${CMAKE_SOURCE_DIR}/contrib/restricted/boost/regex/include
)
target_link_libraries(restricted-boost-regex PUBLIC
  contrib-libs-cxxsupp
  yutil
  contrib-libs-icu
  restricted-boost-assert
  restricted-boost-concept_check
  restricted-boost-config
  restricted-boost-container_hash
  restricted-boost-core
  restricted-boost-integer
  restricted-boost-iterator
  restricted-boost-mpl
  restricted-boost-predef
  restricted-boost-smart_ptr
  restricted-boost-static_assert
  restricted-boost-throw_exception
  restricted-boost-type_traits
)
target_sources(restricted-boost-regex PRIVATE
  ${CMAKE_SOURCE_DIR}/contrib/restricted/boost/regex/src/c_regex_traits.cpp
  ${CMAKE_SOURCE_DIR}/contrib/restricted/boost/regex/src/cpp_regex_traits.cpp
  ${CMAKE_SOURCE_DIR}/contrib/restricted/boost/regex/src/cregex.cpp
  ${CMAKE_SOURCE_DIR}/contrib/restricted/boost/regex/src/fileiter.cpp
  ${CMAKE_SOURCE_DIR}/contrib/restricted/boost/regex/src/icu.cpp
  ${CMAKE_SOURCE_DIR}/contrib/restricted/boost/regex/src/instances.cpp
  ${CMAKE_SOURCE_DIR}/contrib/restricted/boost/regex/src/posix_api.cpp
  ${CMAKE_SOURCE_DIR}/contrib/restricted/boost/regex/src/regex.cpp
  ${CMAKE_SOURCE_DIR}/contrib/restricted/boost/regex/src/regex_debug.cpp
  ${CMAKE_SOURCE_DIR}/contrib/restricted/boost/regex/src/regex_raw_buffer.cpp
  ${CMAKE_SOURCE_DIR}/contrib/restricted/boost/regex/src/regex_traits_defaults.cpp
  ${CMAKE_SOURCE_DIR}/contrib/restricted/boost/regex/src/static_mutex.cpp
  ${CMAKE_SOURCE_DIR}/contrib/restricted/boost/regex/src/usinstances.cpp
  ${CMAKE_SOURCE_DIR}/contrib/restricted/boost/regex/src/w32_regex_traits.cpp
  ${CMAKE_SOURCE_DIR}/contrib/restricted/boost/regex/src/wc_regex_traits.cpp
  ${CMAKE_SOURCE_DIR}/contrib/restricted/boost/regex/src/wide_posix_api.cpp
  ${CMAKE_SOURCE_DIR}/contrib/restricted/boost/regex/src/winstances.cpp
)
