#!/usr/bin/env bash
# valseg_bench --structural --trace must write the replayed trace's own
# machine-independent counts under the draw label, one row per recorded
# seed, so the H5 transfer can join predictors to the trace phase's runs.
set -euo pipefail

binary="${1:?usage: $0 /path/to/valseg_bench /path/to/trace}"
trace="${2:?usage: $0 /path/to/valseg_bench /path/to/trace}"

scratch="$(mktemp -d -t valseg-structural-trace.XXXXXX)"
trap 'rm -rf -- "$scratch"' EXIT

updates=$(grep -c '^u,' "$trace")
queries=$(grep -c '^q,' "$trace")

"$binary" --structural --trace "$trace" --trace-id WT01 --workload WT --seed 7 --trials 2 \
  --warmup 1 --out-dir "$scratch" --tag trace-WT01 >/dev/null

csv="$scratch/structural_trace-WT01-WT01.csv"
[[ -f "$csv" ]] || { echo "missing $csv" >&2; exit 1; }
rows="$(awk -F, 'NR == 1 { for (i = 1; i <= NF; ++i) col[$i] = i }
  NR > 1 { print $col["workload"], $col["seed"], $col["updates"], $col["queries"] }' "$csv")"
expected=$'WT01 8 '"$updates $queries"$'\nWT01 9 '"$updates $queries"
if [[ "$rows" != "$expected" ]]; then
  printf 'structural trace rows were:\n%s\nexpected:\n%s\n' "$rows" "$expected" >&2
  exit 1
fi
echo "structural trace counts recorded for seeds 8 and 9"
