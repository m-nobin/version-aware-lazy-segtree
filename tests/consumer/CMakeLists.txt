# A project that consumes valseg the way a downstream user would. CTest builds
# it twice through consumer_check.cmake: once against an installed prefix with
# find_package(), once with add_subdirectory(), which must yield the library
# alone and no bench or GoogleTest targets.
cmake_minimum_required(VERSION 3.25)
project(valseg_consumer CXX)

if(VALSEG_CONSUMER_MODE STREQUAL "add_subdirectory")
  add_subdirectory(${VALSEG_SOURCE_DIR} valseg)
  if(TARGET valseg_bench OR TARGET gtest OR TARGET brute_force_array_test)
    message(FATAL_ERROR "add_subdirectory(valseg) produced bench or test targets")
  endif()
else()
  find_package(valseg 0.1 CONFIG REQUIRED)
endif()

add_executable(valseg_consumer main.cpp)
target_link_libraries(valseg_consumer PRIVATE valseg::valseg)
# One output path for every generator, so the check script can run the binary.
set_target_properties(valseg_consumer PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/bin>")
