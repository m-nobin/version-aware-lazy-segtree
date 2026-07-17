# tests/CMakeLists.txt
#
# Integrates GoogleTest via CMake FetchContent (pinned release).
# Each test executable links against valseg::valseg and GTest::gtest_main.
# Tests are registered with CTest and run via:
#   ctest --preset verify --verbose

include(FetchContent)

# ---------------------------------------------------------------------------
# GoogleTest — pinned to a stable release tag.
# Set FETCHCONTENT_UPDATES_DISCONNECTED=ON in CI for repeatability.
# ---------------------------------------------------------------------------

FetchContent_Declare(
  googletest
  URL      https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
  URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
)

# Build only the GoogleTest targets this project uses.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)

# Match the parent project's shared MSVC runtime setting on Windows.
if(MSVC)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
endif()

FetchContent_MakeAvailable(googletest)
include(GoogleTest)

# ---------------------------------------------------------------------------
# Helper function — avoids repeating the same boilerplate for every target.
# ---------------------------------------------------------------------------

function(add_valseg_test target source)
  add_executable(${target} ${source})

  target_link_libraries(${target} PRIVATE valseg::valseg GTest::gtest_main)

  target_compile_features(${target} PRIVATE cxx_std_17)

  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    )
  endif()

  if(VALSEG_WARNINGS_AS_ERRORS)
    if(MSVC)
      target_compile_options(${target} PRIVATE /WX)
    else()
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()

  # Discover and register individual GoogleTest cases with CTest.
  gtest_discover_tests(${target})
endfunction()

# ---------------------------------------------------------------------------
# Test targets
# ---------------------------------------------------------------------------

add_valseg_test(brute_force_array_test brute_force_array_test.cpp)
add_valseg_test(lazy_segment_tree_test lazy_segment_tree_test.cpp)
