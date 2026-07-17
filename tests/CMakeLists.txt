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
  URL      https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
  URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
)

# Do not install GoogleTest alongside our project.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

# Keep GoogleTest's own warning flags out of our targets.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# ---------------------------------------------------------------------------
# Helper macro — avoids repeating the same boilerplate for every target.
# ---------------------------------------------------------------------------

macro(add_valseg_test target source)
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
  include(GoogleTest)
  gtest_discover_tests(${target})
endmacro()

# ---------------------------------------------------------------------------
# Test targets
# ---------------------------------------------------------------------------

add_valseg_test(brute_force_array_test brute_force_array_test.cpp)
add_valseg_test(lazy_segment_tree_test lazy_segment_tree_test.cpp)
