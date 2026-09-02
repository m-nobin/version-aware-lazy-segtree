# bench/CMakeLists.txt
#
# One executable, valseg_bench, that replays the frozen W1-W12 operation
# streams against every structure and writes raw CSV. It carries the same
# warning, warnings-as-errors and clang-tidy settings as the library and the
# tests, so a benchmark that would not survive review does not compile.

# Two binaries from the same sources. valseg_bench measures time against the
# untouched system allocator. valseg_bench_alloc links a counting operator new
# that records requested and peak bytes, which is the only way to see the
# std::vector doubling that a payload figure hides — and which adds a header to
# every allocation, so its timings are not reported.
set(VALSEG_BENCH_SOURCES bench_main.cpp workloads.cpp)

# The optimizer settings the measurements were taken under, recorded verbatim
# in every environment file. A benchmark that does not state its flags is not
# reproducible, and "Release" alone does not name them.
string(TOUPPER "${CMAKE_BUILD_TYPE}" VALSEG_BENCH_CONFIG)
string(STRIP "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${VALSEG_BENCH_CONFIG}}" VALSEG_BENCH_FLAG_TEXT)

add_executable(valseg_bench ${VALSEG_BENCH_SOURCES} allocation_counter_off.cpp)
add_executable(valseg_bench_alloc ${VALSEG_BENCH_SOURCES} allocation_counter_on.cpp)

foreach(target valseg_bench valseg_bench_alloc)
  target_link_libraries(${target} PRIVATE valseg::valseg)
  target_compile_features(${target} PRIVATE cxx_std_17)
endforeach()

# Recorded in the environment file next to every result set, so a CSV can
# always be traced back to how the binary that produced it was built.
foreach(target valseg_bench valseg_bench_alloc)
  target_compile_definitions(${target} PRIVATE
    VALSEG_BENCH_BUILD_TYPE="$<IF:$<BOOL:${CMAKE_BUILD_TYPE}>,${CMAKE_BUILD_TYPE},multi-config>"
    VALSEG_BENCH_COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
    VALSEG_BENCH_FLAGS="${VALSEG_BENCH_FLAG_TEXT}"
  )
endforeach()

foreach(target valseg_bench valseg_bench_alloc)
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
endforeach()

# The smoke run is the harness's own test: it replays all twelve workloads at
# n = 256 against all eight structures and fails if the seven persistent ones
# disagree on any answer. It runs in about a second, so it belongs in the same
# CTest invocation as everything else rather than in a job of its own.
if(BUILD_TESTING)
  # No warm-up: the smoke run checks that the structures agree, not how fast
  # they are, and a duration-based warm-up would add fifteen seconds to CI for
  # a property it does not measure.
  add_test(NAME bench_smoke
           COMMAND valseg_bench --smoke --trials 1 --warmup 0 --warmup-seconds 0
                   --batch-trials 0 --cap-mib 64)
  set_tests_properties(bench_smoke PROPERTIES LABELS "bench")

  add_test(NAME bench_smoke_alloc
           COMMAND valseg_bench_alloc --smoke --trials 1 --warmup 0 --warmup-seconds 0
                   --batch-trials 0 --cap-mib 64 --tag alloc --workload W1)
  set_tests_properties(bench_smoke_alloc PROPERTIES LABELS "bench")

  # The registered primary and secondary timing modes replay the same streams
  # as the interleaved smoke run and must produce the same per-version
  # answers, so each mode's smoke run is also a cross-mode semantics check
  # through the shared per-cell checksum table.
  add_test(NAME bench_smoke_batch
           COMMAND valseg_bench --smoke --mode batch --trials 1 --warmup 0 --warmup-seconds 0
                   --batch-trials 0 --cap-mib 64 --tag batch)
  set_tests_properties(bench_smoke_batch PROPERTIES LABELS "bench")

  add_test(NAME bench_smoke_latency
           COMMAND valseg_bench --smoke --mode latency --sample-every 16 --trials 1 --warmup 0
                   --warmup-seconds 0 --batch-trials 0 --cap-mib 64 --tag latency --workload W1)
  set_tests_properties(bench_smoke_latency PROPERTIES LABELS "bench")

  # One fresh-process cell: the orchestrated confirmatory invocation shape.
  add_test(NAME bench_smoke_single_cell
           COMMAND valseg_bench --smoke --mode batch --workload W1 --n 256 --variant 0.000000
                   --structure persistent --trials 2 --trial-index 1 --warmup 0
                   --warmup-seconds 0 --batch-trials 0 --cap-mib 64 --tag cell)
  set_tests_properties(bench_smoke_single_cell PROPERTIES LABELS "bench")

  if(UNIX)
    add_test(NAME confirmatory_output_overwrite_guard
             COMMAND bash
                     ${CMAKE_CURRENT_SOURCE_DIR}/confirmatory_output_overwrite_guard_test.sh
                     ${CMAKE_CURRENT_SOURCE_DIR}/run_confirmatory.sh)
    set_tests_properties(confirmatory_output_overwrite_guard PROPERTIES LABELS "bench")

    add_test(NAME bench_rss_isolation_regression
             COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/rss_isolation_regression_test.sh
                     $<TARGET_FILE:valseg_bench>)
    set_tests_properties(bench_rss_isolation_regression PROPERTIES LABELS "bench")

    add_test(NAME confirmatory_resume_guard
             COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/confirmatory_resume_guard_test.sh
                     ${CMAKE_CURRENT_SOURCE_DIR}/run_confirmatory.sh)
    set_tests_properties(confirmatory_resume_guard PROPERTIES LABELS "bench")

    add_test(NAME bench_trace_replay
             COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/trace_replay_test.sh
                     $<TARGET_FILE:valseg_bench> ${CMAKE_CURRENT_SOURCE_DIR}/testdata/valid_small.trace)
    set_tests_properties(bench_trace_replay PROPERTIES LABELS "bench")
  endif()

  # Malformed CLI input: checked parsing (bench_main.cpp) must fail the
  # process before a campaign starts, not silently wrap or truncate.
  add_test(NAME bench_cli_rejects_negative_size
           COMMAND valseg_bench --smoke --mode batch --workload W1 --n -5 --structure persistent
                   --trials 1 --warmup 0 --warmup-seconds 0 --batch-trials 0 --cap-mib 64
                   --tag cli-bad-n)
  set_tests_properties(bench_cli_rejects_negative_size PROPERTIES LABELS "bench" WILL_FAIL TRUE)

  add_test(NAME bench_cli_rejects_trailing_garbage
           COMMAND valseg_bench --smoke --mode batch --workload W1 --n 256 --structure persistent
                   --trials 1abc --warmup 0 --warmup-seconds 0 --batch-trials 0 --cap-mib 64
                   --tag cli-bad-trials)
  set_tests_properties(bench_cli_rejects_trailing_garbage PROPERTIES LABELS "bench" WILL_FAIL TRUE)

  add_test(NAME bench_cli_rejects_overflowing_seed
           COMMAND valseg_bench --smoke --mode batch --workload W1 --n 256 --structure persistent
                   --trials 1 --seed 99999999999999999999999 --warmup 0 --warmup-seconds 0
                   --batch-trials 0 --cap-mib 64 --tag cli-bad-seed)
  set_tests_properties(bench_cli_rejects_overflowing_seed PROPERTIES LABELS "bench" WILL_FAIL TRUE)

  # Malformed trace input: bench_main.cpp's loadTrace must fail before any
  # timing starts, so a campaign never begins on a trace it cannot fully read.
  foreach(fixture malformed_size malformed_op malformed_negative_field malformed_negative_left
          malformed_range malformed_version malformed_ops_before_n)
    add_test(NAME bench_trace_rejects_${fixture}
             COMMAND valseg_bench --mode batch --workload WT
                     --trace ${CMAKE_CURRENT_SOURCE_DIR}/testdata/${fixture}.trace
                     --structure persistent --trials 1 --warmup 0 --warmup-seconds 0
                     --batch-trials 0 --out-dir ${CMAKE_CURRENT_BINARY_DIR}
                     --tag trace-${fixture})
    set_tests_properties(bench_trace_rejects_${fixture} PROPERTIES LABELS "bench" WILL_FAIL TRUE)
  endforeach()
endif()
