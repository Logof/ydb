
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(restricted-boost-spirit INTERFACE)
target_include_directories(restricted-boost-spirit INTERFACE
  ${CMAKE_SOURCE_DIR}/contrib/restricted/boost/spirit/include
)
target_link_libraries(restricted-boost-spirit INTERFACE
  contrib-libs-cxxsupp
  yutil
  restricted-boost-algorithm
  restricted-boost-array
  restricted-boost-assert
  restricted-boost-concept_check
  restricted-boost-config
  restricted-boost-core
  restricted-boost-endian
  restricted-boost-filesystem
  restricted-boost-foreach
  restricted-boost-function
  restricted-boost-function_types
  restricted-boost-fusion
  restricted-boost-integer
  restricted-boost-io
  restricted-boost-iostreams
  restricted-boost-iterator
  restricted-boost-lexical_cast
  restricted-boost-locale
  restricted-boost-math
  restricted-boost-move
  restricted-boost-mpl
  restricted-boost-optional
  restricted-boost-phoenix
  restricted-boost-pool
  restricted-boost-predef
  restricted-boost-preprocessor
  restricted-boost-proto
  restricted-boost-range
  restricted-boost-regex
  restricted-boost-smart_ptr
  restricted-boost-static_assert
  restricted-boost-thread
  restricted-boost-throw_exception
  restricted-boost-tti
  restricted-boost-type_traits
  restricted-boost-typeof
  restricted-boost-unordered
  restricted-boost-utility
  restricted-boost-variant
)
