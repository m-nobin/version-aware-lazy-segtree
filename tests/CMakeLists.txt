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

  if(VALSEG_ENABLE_CLANG_TIDY)
    set_property(TARGET ${target} PROPERTY CXX_CLANG_TIDY "${VALSEG_CLANG_TIDY_COMMAND}")
  endif()

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
add_valseg_test(persistent_lazy_segment_tree_test persistent_lazy_segment_tree_test.cpp)
add_valseg_test(differential_validation_test differential_validation_test.cpp)
add_valseg_test(full_copy_persistent_segment_tree_test full_copy_persistent_segment_tree_test.cpp)
add_valseg_test(point_only_persistent_segment_tree_test point_only_persistent_segment_tree_test.cpp)
add_valseg_test(baseline_differential_test baseline_differential_test.cpp)
add_valseg_test(checkpointing_segment_tree_test checkpointing_segment_tree_test.cpp)
add_valseg_test(buffered_path_copying_segment_tree_test buffered_path_copying_segment_tree_test.cpp)
add_valseg_test(fat_node_persistent_segment_tree_test fat_node_persistent_segment_tree_test.cpp)
add_valseg_test(copy_on_push_segment_tree_test copy_on_push_segment_tree_test.cpp)
add_valseg_test(policy_test policy_test.cpp)
add_valseg_test(policy_trees_test policy_trees_test.cpp)
add_valseg_test(frontier_test frontier_test.cpp)
add_valseg_test(record_layout_test record_layout_test.cpp)
add_valseg_test(structural_counts_test structural_counts_test.cpp)
target_include_directories(structural_counts_test PRIVATE ${PROJECT_SOURCE_DIR}/bench)
add_valseg_test(numeric_contract_test numeric_contract_test.cpp)
add_valseg_test(brute_force_allocation_failure_test brute_force_allocation_failure_test.cpp)

# ---------------------------------------------------------------------------
# Compile-fail test: the tag-retaining subject must reject a policy whose
# induced actions do not commute. The target is excluded from the default
# build; the CTest case builds it on demand and passes only when the compiler
# output names the static_assert, so an accidental successful build fails.
# ---------------------------------------------------------------------------

add_executable(retained_tag_rejects_affine EXCLUDE_FROM_ALL
  compile_fail/retained_tag_rejects_affine.cpp)
target_link_libraries(retained_tag_rejects_affine PRIVATE valseg::valseg)
target_compile_features(retained_tag_rejects_affine PRIVATE cxx_std_17)

add_test(NAME retained_tag_rejects_affine_compile_fail
         COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
                 --target retained_tag_rejects_affine --config $<CONFIG>)
set_tests_properties(retained_tag_rejects_affine_compile_fail PROPERTIES
  PASS_REGULAR_EXPRESSION "kInducedActionsCommute"
  LABELS "compile-fail")
