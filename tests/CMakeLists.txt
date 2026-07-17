# tests/CMakeLists.txt
#
# Each test executable links against the valseg library.
# Tests are registered with CTest so they run via:
#   ctest --preset verify --verbose

# ---------------------------------------------------------------------------
# brute_force_array_test
# ---------------------------------------------------------------------------

add_executable(brute_force_array_test
  brute_force_array_test.cpp
)

target_link_libraries(brute_force_array_test PRIVATE valseg::valseg)

target_compile_features(brute_force_array_test PRIVATE cxx_std_17)

if(MSVC)
  target_compile_options(brute_force_array_test PRIVATE /W4 /permissive-)
else()
  target_compile_options(brute_force_array_test PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow
  )
endif()

if(VALSEG_WARNINGS_AS_ERRORS)
  if(MSVC)
    target_compile_options(brute_force_array_test PRIVATE /WX)
  else()
    target_compile_options(brute_force_array_test PRIVATE -Werror)
  endif()
endif()

add_test(NAME brute_force_array_test COMMAND brute_force_array_test)

# ---------------------------------------------------------------------------
# lazy_segment_tree_test
# ---------------------------------------------------------------------------

add_executable(lazy_segment_tree_test
  lazy_segment_tree_test.cpp
)

target_link_libraries(lazy_segment_tree_test PRIVATE valseg::valseg)

target_compile_features(lazy_segment_tree_test PRIVATE cxx_std_17)

if(MSVC)
  target_compile_options(lazy_segment_tree_test PRIVATE /W4 /permissive-)
else()
  target_compile_options(lazy_segment_tree_test PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow
  )
endif()

if(VALSEG_WARNINGS_AS_ERRORS)
  if(MSVC)
    target_compile_options(lazy_segment_tree_test PRIVATE /WX)
  else()
    target_compile_options(lazy_segment_tree_test PRIVATE -Werror)
  endif()
endif()

add_test(NAME lazy_segment_tree_test COMMAND lazy_segment_tree_test)
