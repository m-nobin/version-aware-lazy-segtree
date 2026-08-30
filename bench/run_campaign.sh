#!/usr/bin/env bash
# Run the full campaign one workload at a time.
#
# One process per workload rather than one for the whole matrix: the run stays
# restartable, partial results are usable, and the machine's state is recorded
# again before each workload so a change part-way through the campaign (power
# source, load, thermal state) is visible in the record rather than invisible
# in the numbers.
#
# usage: bench/run_campaign.sh [timing|alloc] [trials]
set -euo pipefail

mode="${1:-timing}"
trials="${2:-11}"
root="$(cd "$(dirname "$0")/.." && pwd)"
raw="$root/bench/results/raw"
mkdir -p "$raw"

case "$mode" in
timing) binary="$root/build/release-verify/bench/valseg_bench"; extra=(--batch-trials 2) ;;
alloc)  binary="$root/build/release-verify/bench/valseg_bench_alloc"; extra=(--batch-trials 0) ;;
*) echo "usage: $0 [timing|alloc] [trials]" >&2; exit 2 ;;
esac

for workload in W1 W2 W3 W4 W5 W6 W7 W8 W9 W10 W11 W12; do
  tag="$mode-$workload"
  printf '=== %s %s ===\n' "$mode" "$workload"
  bash "$root/bench/collect_environment.sh" > "$raw/system_$tag.txt"
  start=$(date +%s)
  "$binary" --workload "$workload" --out-dir "$raw" --tag "$tag" \
            --trials "$trials" --warmup 3 --warmup-seconds 15 --capped-trials 2 "${extra[@]}"
  printf 'elapsed_seconds=%s\n' "$(( $(date +%s) - start ))" >> "$raw/environment_$tag.txt"
done

printf 'campaign complete: %s\n' "$mode"
