#!/usr/bin/env bash
# Regression for the cross-structure warm-up leak: warming every structure
# before applying the --structure filter (bench_main.cpp) inflated a
# fresh-process trial's peak RSS with unrelated structures' allocations. lazy
# is the non-persistent control with no tree nodes at all, so warming only
# itself should leave peak_rss close to its own unwarmed figure; warming any
# tree-building structure alongside it would not.
set -euo pipefail

binary="${1:?usage: $0 /path/to/valseg_bench}"
scratch="$(mktemp -d -t valseg-rss-isolation.XXXXXX)"
trap 'rm -rf -- "$scratch"' EXIT

peak_rss() {
  local tag="$1"
  awk -F, 'NR==1{for(i=1;i<=NF;i++) c[$i]=i} NR==2{print $c["peak_rss_bytes"]}' "$scratch/runs_$tag.csv"
}

"$binary" --mode batch --workload W1 --n 100000 --variant 0.000000 --structure lazy \
  --trials 1 --trial-index 0 --warmup 0 --warmup-seconds 0 \
  --seed 1 --capped-trials 1 --batch-trials 0 --out-dir "$scratch" --tag cold >/dev/null
"$binary" --mode batch --workload W1 --n 100000 --variant 0.000000 --structure lazy \
  --trials 1 --trial-index 0 --warmup 0 --warmup-seconds 2 \
  --seed 1 --capped-trials 1 --batch-trials 0 --out-dir "$scratch" --tag warmed >/dev/null

cold_rss="$(peak_rss cold)"
warmed_rss="$(peak_rss warmed)"

if [[ -z "$cold_rss" || -z "$warmed_rss" || "$cold_rss" -le 0 ]]; then
  echo "could not read peak_rss_bytes from either run" >&2
  exit 1
fi

# A generous bound: lazy warming only itself should not multiply the
# process's high-water mark. The old bug also warmed up to six tree-building
# structures first, which allocate far more than lazy's flat array.
limit=$((cold_rss * 4))
if [[ "$warmed_rss" -gt "$limit" ]]; then
  echo "warm-up inflated lazy's peak RSS beyond the isolation bound:" >&2
  echo "  cold=$cold_rss warmed=$warmed_rss limit=$limit" >&2
  echo "  warmUp() is warming a structure other than the one being measured" >&2
  exit 1
fi

echo "peak RSS isolation holds: cold=$cold_rss warmed=$warmed_rss (limit $limit)"
