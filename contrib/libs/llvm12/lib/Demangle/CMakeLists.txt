
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(llvm12-lib-Demangle)
target_compile_options(llvm12-lib-Demangle PRIVATE
  -Wno-everything
)
target_include_directories(llvm12-lib-Demangle PRIVATE
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Demangle
)
target_link_libraries(llvm12-lib-Demangle PUBLIC
  contrib-libs-cxxsupp
  contrib-libs-llvm12
)
target_sources(llvm12-lib-Demangle PRIVATE
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Demangle/Demangle.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Demangle/ItaniumDemangle.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Demangle/MicrosoftDemangle.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Demangle/MicrosoftDemangleNodes.cpp
)
