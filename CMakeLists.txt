# CMakeLists.txt — version-aware-lazy-segtree
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
  DESCRIPTION "Version-aware lazy segment tree — correctness and baseline components"
  LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Optional developer tooling
# ---------------------------------------------------------------------------

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

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------

add_library(valseg
  src/brute_force_array.cpp
  src/lazy_segment_tree.cpp
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
target_include_directories(valseg
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# ---------------------------------------------------------------------------
# Compiler warnings (private — not inherited by downstream consumers)
# ---------------------------------------------------------------------------

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

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

# BUILD_TESTING is a standard CMake cache variable (ON by default).
# Downstream consumers who add_subdirectory this project can set it OFF.
include(CTest)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
