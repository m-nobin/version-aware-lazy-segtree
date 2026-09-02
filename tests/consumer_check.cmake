# Build and run tests/consumer against this project. Run by CTest as
# cmake -DMODE=<find_package|add_subdirectory> ... -P consumer_check.cmake.
# find_package mode first installs the project build tree into WORK_DIR/prefix.
cmake_minimum_required(VERSION 3.25)

set(prefix ${WORK_DIR}/prefix)
set(build ${WORK_DIR}/build)
file(REMOVE_RECURSE ${WORK_DIR})

if(MODE STREQUAL "find_package")
  execute_process(
    COMMAND ${CMAKE_COMMAND} --install ${BINARY_DIR} --prefix ${prefix} --config ${CONFIG}
    COMMAND_ERROR_IS_FATAL ANY)
endif()

set(platform)
if(PLATFORM)
  set(platform -A ${PLATFORM})
endif()
execute_process(
  COMMAND ${CMAKE_COMMAND} -S ${SOURCE_DIR}/tests/consumer -B ${build}
          -G ${GENERATOR} ${platform}
          -DCMAKE_CXX_COMPILER=${CXX_COMPILER}
          "-DCMAKE_CXX_FLAGS=${CXX_FLAGS}"
          "-DCMAKE_EXE_LINKER_FLAGS=${LINKER_FLAGS}"
          -DCMAKE_BUILD_TYPE=${CONFIG}
          -DCMAKE_PREFIX_PATH=${prefix}
          -DVALSEG_CONSUMER_MODE=${MODE}
          -DVALSEG_SOURCE_DIR=${SOURCE_DIR}
  COMMAND_ERROR_IS_FATAL ANY)
execute_process(
  COMMAND ${CMAKE_COMMAND} --build ${build} --config ${CONFIG}
  COMMAND_ERROR_IS_FATAL ANY)
execute_process(
  COMMAND ${build}/bin/valseg_consumer${EXE_SUFFIX}
  COMMAND_ERROR_IS_FATAL ANY)
