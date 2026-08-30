#!/usr/bin/env bash
# Run the full campaign one workload at a time.
#
# One process per workload rather than one for the whole matrix: the run stays
# restartable, partial results are usable, and the machine's state is recorded
# again before each workload so a change part-way through the campaign (power
# source, load, thermal state) is visible in the record rather than invisible
# in the numbers.
#
# usage: bench/run_campaign.sh [timing|alloc] [trials] <campaign-id>
set -euo pipefail

mode="${1:-timing}"
trials="${2:-11}"
campaign="${3:-}"
root="$(cd "$(dirname "$0")/.." && pwd)"

if [[ -z "$campaign" || ! "$campaign" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]]; then
  echo "usage: $0 [timing|alloc] [trials] <campaign-id>" >&2
  echo "campaign-id must use only letters, digits, dot, underscore and hyphen" >&2
  exit 2
fi

raw="$root/bench/results/campaigns/$campaign/raw"
mkdir -p "$raw"

case "$mode" in
timing) binary="$root/build/release-verify/bench/valseg_bench"; extra=(--batch-trials 2) ;;
alloc)  binary="$root/build/release-verify/bench/valseg_bench_alloc"; extra=(--batch-trials 0) ;;
*) echo "usage: $0 [timing|alloc] [trials] <campaign-id>" >&2; exit 2 ;;
esac

for workload in W1 W2 W3 W4 W5 W6 W7 W8 W9 W10 W11 W12; do
  tag="$mode-$workload"
  for path in \
    "$raw/system_$tag.txt" \
    "$raw/runs_$tag.csv" \
    "$raw/memory_$tag.csv" \
    "$raw/environment_$tag.txt"; do
    if [[ -e "$path" ]]; then
      echo "refusing to overwrite $path; resume with a new campaign id" >&2
      exit 2
    fi
  done
  printf '=== %s %s ===\n' "$mode" "$workload"
  bash "$root/bench/collect_environment.sh" > "$raw/system_$tag.txt"
  start=$(date +%s)
  "$binary" --workload "$workload" --out-dir "$raw" --tag "$tag" \
            --trials "$trials" --warmup 3 --warmup-seconds 15 --capped-trials 2 "${extra[@]}"
  printf 'elapsed_seconds=%s\n' "$(( $(date +%s) - start ))" >> "$raw/environment_$tag.txt"
done

printf 'campaign complete: %s (%s)\n' "$mode" "$campaign"
