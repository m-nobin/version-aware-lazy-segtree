#!/usr/bin/env bash
# Run the registered sensitivity subset (protocol section 7) under the same
# balanced fresh-process scheduler as the primary campaign, with the
# registered seed, warm-up and trial counts -- an alternate allocator or
# compiler is a sensitivity dimension, not a reason to relax the protocol.
#
#   # allocator arm: registered build, second allocator preloaded
#   VALSEG_ALT_ALLOC=/path/to/libmimalloc.dylib \
#     bench/run_sensitivity.sh <campaign-id>-alloc
#
#   # compiler arm: second-compiler build, default allocator
#   bench/run_sensitivity.sh <campaign-id>-gcc build/release-verify-gcc
#
# The subset is exactly the sixteen rows of bench/sensitivity_cells.csv: all
# recorded cells of W1, W5 and W11, subject persistent versus baseline
# copy-on-push, twenty trials each. Both arms run through this script rather
# than through run_confirmatory.sh, because the registered check requires
# exactly twenty paired trials per cell and the primary schedule gives forty
# to the two W11 cells it shares with the primary family.
set -euo pipefail

campaign="${1:-}"
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${2:-$root/build/release-verify}"
alloc="${VALSEG_ALT_ALLOC:-}"

if [[ -z "$campaign" || ! "$campaign" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]]; then
  echo "usage: [VALSEG_ALT_ALLOC=/path/to/allocator] $0 <campaign-id> [build-dir]" >&2
  exit 2
fi
if [[ -n "$alloc" && ! -f "$alloc" ]]; then
  echo "VALSEG_ALT_ALLOC does not name a readable library: $alloc" >&2
  exit 2
fi

binary="$build/bench/valseg_bench"
[[ -x "$binary" ]] || { echo "missing $binary; build the release-verify preset first" >&2; exit 2; }

raw="$root/bench/results/campaigns/$campaign/raw"
meta="$root/bench/results/campaigns/$campaign"
mkdir -p "$raw"

preload=()
if [[ -n "$alloc" ]]; then
  case "$(uname -s)" in
  Darwin) preload_var="DYLD_INSERT_LIBRARIES" ;;
  Linux)  preload_var="LD_PRELOAD" ;;
  *) echo "unsupported platform for allocator preload" >&2; exit 2 ;;
  esac
  preload=("$preload_var=$alloc")
fi

# Registered protocol constants, identical to run_confirmatory.sh.
confirm_seed=20270214
schedule_seed=20270214
warmup_trials=1
warmup_seconds=3
trials="${VALSEG_SENSITIVITY_TRIALS:-20}"

schedule="$meta/schedule_sensitivity.tsv"
if [[ ! -e "$schedule" ]]; then
  python3 "$root/bench/confirm_schedule.py" --binary "$binary" --mode timing \
    --trials "$trials" --primary-trials "$trials" \
    --primary-cells "$root/bench/primary_cells.csv" \
    --capped-cells "$root/bench/capped_cells.csv" \
    --schedule-seed "$schedule_seed" --workloads W1,W5,W11 \
    --structures persistent,copy-on-push --out "$schedule"
fi
total=$(grep -vc '^#' "$schedule")
done_count=0
while IFS=$'\t' read -r workload n variant structure trial cell_trials exec_order process_seq tag; do
  [[ "$workload" == \#* ]] && continue
  done_count=$((done_count + 1))
  if [[ -e "$raw/runs_$tag.csv" ]]; then
    continue
  fi
  printf '=== %s [%d/%d] ===\n' "$tag" "$done_count" "$total"
  bash "$root/bench/collect_environment.sh" > "$raw/system_$tag.txt"
  {
    printf 'sensitivity_build_dir=%s\n' "$build"
    printf 'binary_sha256=%s\n' "$(shasum -a 256 "$binary" | cut -d' ' -f1)"
    if [[ -n "$alloc" ]]; then
      printf 'alt_allocator=%s\n' "$alloc"
      printf 'alt_allocator_sha256=%s\n' "$(shasum -a 256 "$alloc" | cut -d' ' -f1)"
      printf 'preload_variable=%s\n' "$preload_var"
    else
      printf 'alt_allocator=none\n'
    fi
  } >> "$raw/system_$tag.txt"
  # The preload is a request; the environment file records the library that
  # actually answered malloc, and the registered analysis checks it.
  env ${preload[@]+"${preload[@]}"} "$binary" --mode batch \
    --workload "$workload" --n "$n" --variant "$variant" --structure "$structure" \
    --trials "$cell_trials" --trial-index "$trial" \
    --warmup "$warmup_trials" --warmup-seconds "$warmup_seconds" \
    --seed "$confirm_seed" --capped-trials 1 --batch-trials 0 \
    --exec-order "$exec_order" --process-seq "$process_seq" \
    --out-dir "$raw" --tag "$tag"
done < "$schedule"

printf 'sensitivity campaign complete: %s\n' "$campaign"
