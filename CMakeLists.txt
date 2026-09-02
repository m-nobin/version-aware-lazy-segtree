# CMakeLists.txt: version-aware-lazy-segtree
#
# Supported platforms : Linux, Windows, macOS
# Supported toolchains: GCC, Clang, Apple Clang, MSVC
# Minimum CMake version chosen to support:
#   - CMakePresets.json schema version 6 (requires CMake >= 3.25)
#   - CTest --output-junit / outputJUnitFile in test presets (CMake 3.21+)
#   - gtest_discover_tests (CMake 3.10+)
cmake_minimum_required(VERSION 3.25)

project(
  valseg
  VERSION 0.1.0
  DESCRIPTION "Version-aware lazy segment tree: correctness and baseline components"
  LANGUAGES CXX
)

# Optional developer tooling

option(VALSEG_ENABLE_CLANG_TIDY "Run Clang-Tidy while compiling project targets" OFF)

if(VALSEG_ENABLE_CLANG_TIDY)
  find_program(
    VALSEG_CLANG_TIDY_EXECUTABLE
    NAMES clang-tidy
    DOC "Path to the Clang-Tidy executable"
  )

  if(NOT VALSEG_CLANG_TIDY_EXECUTABLE)
    message(FATAL_ERROR
      "VALSEG_ENABLE_CLANG_TIDY requires clang-tidy. "
      "Install LLVM or set VALSEG_CLANG_TIDY_EXECUTABLE explicitly."
    )
  endif()

  set(VALSEG_CLANG_TIDY_COMMAND
    "${VALSEG_CLANG_TIDY_EXECUTABLE};--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy"
  )
endif()

# Library target

add_library(valseg
  src/brute_force_array.cpp
  src/lazy_segment_tree.cpp
  src/persistent_lazy_segment_tree.cpp
  src/full_copy_persistent_segment_tree.cpp
  src/point_only_persistent_segment_tree.cpp
  src/checkpointing_segment_tree.cpp
  src/buffered_path_copying_segment_tree.cpp
  src/fat_node_persistent_segment_tree.cpp
)

# Canonical alias used by consumers and by CTest targets in this project.
add_library(valseg::valseg ALIAS valseg)

if(VALSEG_ENABLE_CLANG_TIDY)
  set_property(TARGET valseg PROPERTY CXX_CLANG_TIDY "${VALSEG_CLANG_TIDY_COMMAND}")
endif()

# C++17 is required; propagate the requirement to downstream targets.
target_compile_features(valseg PUBLIC cxx_std_17)

# Include directories: build-tree paths for in-source builds, install-tree
# paths for installed usage.  Generator expressions keep this portable.
include(GNUInstallDirs)
target_include_directories(valseg
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Install and export
#
# Consumers use either add_subdirectory() with the valseg::valseg alias or an
# installed tree with find_package(valseg CONFIG). The library has no
# dependencies, so the exported targets file doubles as the package config.
include(CMakePackageConfigHelpers)

install(TARGETS valseg EXPORT valsegTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp")
install(EXPORT valsegTargets
  NAMESPACE valseg::
  FILE valsegConfig.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/valseg
)
write_basic_package_version_file(
  ${PROJECT_BINARY_DIR}/valsegConfigVersion.cmake
  COMPATIBILITY SameMinorVersion
)
install(FILES ${PROJECT_BINARY_DIR}/valsegConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/valseg
)

# Compiler warnings, private so downstream consumers do not inherit them

if(MSVC)
  target_compile_options(valseg PRIVATE /W4 /permissive-)
else()
  target_compile_options(valseg PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow
  )
endif()

# Optional: promote warnings to errors for CI.
# Enable with -DVALSEG_WARNINGS_AS_ERRORS=ON.
option(VALSEG_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
if(VALSEG_WARNINGS_AS_ERRORS)
  if(MSVC)
    target_compile_options(valseg PRIVATE /WX)
  else()
    target_compile_options(valseg PRIVATE -Werror)
  endif()
endif()

# Tests

# CTest, the tests and the benchmarks exist only when this is the top-level
# project, so a consumer that add_subdirectory()s the library gets the library
# alone. BUILD_TESTING is the standard CTest cache variable; VALSEG_BUILD_BENCH
# can be set ON explicitly from a parent project.
if(PROJECT_IS_TOP_LEVEL)
  include(CTest)
  if(BUILD_TESTING)
    add_subdirectory(tests)
  endif()
endif()

# Benchmarks

option(VALSEG_BUILD_BENCH "Build the benchmark runner" ${PROJECT_IS_TOP_LEVEL})

if(VALSEG_BUILD_BENCH)
  add_subdirectory(bench)
endif()
