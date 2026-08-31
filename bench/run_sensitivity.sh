#!/usr/bin/env bash
# Run the registered sensitivity subset under a second allocator.
#
#   VALSEG_ALT_ALLOC=/path/to/libmimalloc.dylib \
#     bench/run_sensitivity.sh <campaign-id> [build-dir]
#
# The subset (registered-protocol.md, section 7) is W1, W5 and W11: the
# general case, the full-coverage extreme and the width sweep that joins
# them. The alternate allocator is injected with the platform preload
# variable, so neither the binary nor the build changes; the allocator path
# and its checksum land in the campaign's environment record. The second
# compiler needs no script: configure `release-verify-gcc` or
# `release-verify-clang` and run bench/run_confirmatory.sh against that
# build directory with its own campaign id.
set -euo pipefail

campaign="${1:-}"
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${2:-$root/build/release-verify}"
alloc="${VALSEG_ALT_ALLOC:-}"

if [[ -z "$campaign" || ! "$campaign" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]]; then
  echo "usage: VALSEG_ALT_ALLOC=/path/to/allocator $0 <campaign-id> [build-dir]" >&2
  exit 2
fi
if [[ -z "$alloc" || ! -f "$alloc" ]]; then
  echo "VALSEG_ALT_ALLOC must point at the alternate allocator library" >&2
  exit 2
fi

binary="$build/bench/valseg_bench"
[[ -x "$binary" ]] || { echo "missing $binary; build the release-verify preset first" >&2; exit 2; }

raw="$root/bench/results/campaigns/$campaign/raw"
mkdir -p "$raw"

case "$(uname -s)" in
Darwin) preload_var="DYLD_INSERT_LIBRARIES" ;;
Linux)  preload_var="LD_PRELOAD" ;;
*) echo "unsupported platform for allocator preload" >&2; exit 2 ;;
esac

for workload in W1 W5 W11; do
  tag="timing-$workload"
  bash "$root/bench/collect_environment.sh" > "$raw/system_$tag.txt"
  {
    printf 'alt_allocator=%s\n' "$alloc"
    printf 'alt_allocator_sha256=%s\n' "$(shasum -a 256 "$alloc" | cut -d' ' -f1)"
    printf 'preload_variable=%s\n' "$preload_var"
  } >> "$raw/system_$tag.txt"
  env "$preload_var=$alloc" "$binary" --mode batch --workload "$workload" \
    --out-dir "$raw" --tag "$tag" --trials "${VALSEG_SENSITIVITY_TRIALS:-20}" \
    --warmup 3 --warmup-seconds 15 --capped-trials 2 --batch-trials 0
done

printf 'sensitivity campaign complete: %s\n' "$campaign"
