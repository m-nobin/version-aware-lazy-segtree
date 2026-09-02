#!/usr/bin/env bash
# Regression for the trace replay under --trace-id: the workload id is
# relabeled to the draw id for CSV grouping, and the replay loop once picked
# the trace stream by id, so every relabeled draw replayed an empty stream
# and recorded zero operations with status ok. The runs row must carry the
# trace's own operation counts and a non-zero answer checksum.
set -euo pipefail

binary="${1:?usage: $0 /path/to/valseg_bench /path/to/trace}"
trace="${2:?usage: $0 /path/to/valseg_bench /path/to/trace}"

scratch="$(mktemp -d -t valseg-trace-replay.XXXXXX)"
trap 'rm -rf -- "$scratch"' EXIT

updates=$(grep -c '^u,' "$trace")
queries=$(grep -c '^q,' "$trace")

"$binary" --mode batch --trace "$trace" --workload WT --trace-id WT01 --structure persistent \
  --trials 1 --warmup 0 --warmup-seconds 0 --batch-trials 0 --cap-mib 64 \
  --seed 7 --out-dir "$scratch" --tag replay >/dev/null

# One data row: workload, updates, queries, checksum, status.
row="$(awk -F, 'NR == 1 { for (i = 1; i <= NF; ++i) col[$i] = i }
  NR == 2 { print $col["workload"], $col["updates"], $col["queries"], $col["checksum"], $col["status"] }' \
  "$scratch/runs_replay.csv")"
read -r workload got_updates got_queries checksum status <<<"$row"
if [[ "$workload" != "WT01" || "$got_updates" != "$updates" || "$got_queries" != "$queries" \
      || "$checksum" == "0" || "$status" != "ok" ]]; then
  echo "trace replay recorded '$row'; expected WT01 $updates $queries <non-zero> ok" >&2
  exit 1
fi
echo "trace replay recorded $updates updates and $queries queries for WT01"
