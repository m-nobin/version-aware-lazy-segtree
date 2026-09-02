#!/usr/bin/env bash
# The two registered sensitivity arms differ only in what they vary: the
# compiler arm points at a second build directory and leaves the allocator
# alone, the allocator arm preloads a library into the registered build. Both
# run the same sixteen-cell schedule, so run_sensitivity.sh has to accept an
# absent VALSEG_ALT_ALLOC. Before this was true the compiler arm was
# documented to run through run_confirmatory.sh, whose forty-trial primary
# cells the registered twenty-paired-trial check rejects.
#
# A stub binary stands in for valseg_bench: what is under test is the
# script's wiring, not a measurement.
set -euo pipefail

script="${1:?usage: $0 /path/to/run_sensitivity.sh /path/to/valseg_bench}"
real_binary="${2:?usage: $0 /path/to/run_sensitivity.sh /path/to/valseg_bench}"
scratch="$(mktemp -d -t valseg-sensitivity-arms.XXXXXX)"
trap 'rm -rf -- "$scratch"' EXIT

stub="$scratch/build/bench"
mkdir -p "$stub"
export VALSEG_TEST_REAL_BINARY="$real_binary"
cat > "$stub/valseg_bench" <<'STUB'
#!/usr/bin/env bash
set -euo pipefail
# The cell list comes from the real binary, so the stub runs the registered
# schedule rather than one invented here.
if [[ "${1:-}" == "--list-cells" ]]; then exec "$VALSEG_TEST_REAL_BINARY" --list-cells; fi
out=""; tag=""
while [[ "$#" -gt 0 ]]; do
  case "$1" in
    --out-dir) out="$2"; shift 2 ;;
    --tag) tag="$2"; shift 2 ;;
    *) shift ;;
  esac
done
printf 'workload\n' > "$out/runs_$tag.csv"
STUB
chmod +x "$stub/valseg_bench"

# The preload reaches the loader, so it has to be a real library: a file that
# only looks like one aborts the process on macOS and is ignored with noise on
# Linux. This one does nothing, which is all the test needs -- whether an
# allocator actually took over is answered by each process's recorded
# malloc_provider, not here.
printf 'int valseg_stand_in_allocator(void) { return 0; }\n' > "$scratch/stand_in.c"
case "$(uname -s)" in
Darwin) stand_in_allocator="$scratch/libstandin.dylib"; shared=(-dynamiclib) ;;
Linux)  stand_in_allocator="$scratch/libstandin.so";    shared=(-shared -fPIC) ;;
*) echo "unsupported platform" >&2; exit 1 ;;
esac
if ! "${CC:-cc}" "${shared[@]}" -o "$stand_in_allocator" "$scratch/stand_in.c" 2>/dev/null; then
  echo "cannot build the stand-in library; skipping" >&2
  exit 0
fi

run_arm() {
  local campaign="$1"
  shift
  # VALSEG_DRY_RUN keeps the registration gate out of the way: what is under
  # test is the arm wiring, and the gate has its own fixture.
  # run_sensitivity.sh writes under the repository it lives in, so the
  # campaign ids are scratch-only and removed at the end.
  (cd "$scratch" && env VALSEG_DRY_RUN=1 "$@" bash "$script" "$campaign" "$scratch/build" >/dev/null)
}

root="$(cd "$(dirname "$script")/.." && pwd)"
compiler_campaign="sensitivity-arms-test-dryrun-compiler-$$"
allocator_campaign="sensitivity-arms-test-dryrun-allocator-$$"
compiler_raw="$root/bench/results/campaigns/$compiler_campaign/raw"
allocator_raw="$root/bench/results/campaigns/$allocator_campaign/raw"
trap 'rm -rf -- "$scratch" "$root/bench/results/campaigns/$compiler_campaign" \
      "$root/bench/results/campaigns/$allocator_campaign"' EXIT

run_arm "$compiler_campaign" VALSEG_SENSITIVITY_TRIALS=1
run_arm "$allocator_campaign" VALSEG_SENSITIVITY_TRIALS=1 "VALSEG_ALT_ALLOC=$stand_in_allocator"

expected=16
for raw in "$compiler_raw" "$allocator_raw"; do
  count="$(find "$raw" -name 'runs_*.csv' | wc -l | tr -d ' ')"
  if [[ "$count" -ne $((expected * 2)) ]]; then
    echo "expected $((expected * 2)) sensitivity processes, found $count in $raw" >&2
    exit 1
  fi
done

# What each arm preloaded is not asserted here: macOS strips DYLD_* before a
# protected interpreter, so a shell stub cannot see it. The library that
# actually answered malloc is recorded per process as malloc_provider, and the
# registered compiler and allocator stages refuse a campaign that matches the
# primary one on it.
if ! grep -qx 'alt_allocator=none' "$(find "$compiler_raw" -name 'system_*.txt' | head -1)"; then
  echo "the compiler arm did not record that it left the allocator alone" >&2
  exit 1
fi
system_file="$(find "$allocator_raw" -name 'system_*.txt' | head -1)"
if ! grep -q "^alt_allocator=$stand_in_allocator$" "$system_file" \
   || ! grep -qE '^preload_variable=(DYLD_INSERT_LIBRARIES|LD_PRELOAD)$' "$system_file"; then
  echo "the allocator arm did not record the preloaded library" >&2
  exit 1
fi

if (cd "$scratch" && VALSEG_DRY_RUN=1 VALSEG_ALT_ALLOC="$scratch/absent.so" bash "$script" \
      "sensitivity-arms-test-dryrun-missing-$$" "$scratch/build" >/dev/null 2>&1); then
  echo "an unreadable VALSEG_ALT_ALLOC was accepted" >&2
  exit 1
fi

# A confirmatory id has to reach the registration gate rather than measure.
if (cd "$scratch" && bash "$script" "sensitivity-arms-test-real-$$" "$scratch/build" >/dev/null 2>&1); then
  echo "a confirmatory sensitivity campaign ran without passing the registration gate" >&2
  rm -rf -- "$root/bench/results/campaigns/sensitivity-arms-test-real-$$"
  exit 1
fi
rm -rf -- "$root/bench/results/campaigns/sensitivity-arms-test-real-$$"

echo "both sensitivity arms run the registered schedule"
