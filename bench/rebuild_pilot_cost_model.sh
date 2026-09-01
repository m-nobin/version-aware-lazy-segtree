#!/usr/bin/env bash
# Recompute the exploratory pilot's derived structural counts with the current
# source, fit the frozen pilot model, then evaluate its pilot holdout.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
binary="$root/build/release-verify/bench/valseg_bench"
raw="$root/bench/results/raw"
summary="$root/bench/results/summary"
scratch="$(mktemp -d -t valseg-pilot-structural.XXXXXX)"

cleanup() {
  rm -rf -- "$scratch"
}
trap cleanup EXIT

if [[ ! -x "$binary" ]]; then
  echo "missing $binary; configure and build the release-verify preset first" >&2
  exit 2
fi

uv run --frozen --project "$root/bench/analysis" \
  python -m unittest discover -s "$root/bench/analysis/tests" -v

for workload in W1 W2 W3 W4 W5 W6 W7 W8 W9 W10 W11 W12; do
  "$binary" --structural --workload "$workload" --out-dir "$scratch" \
    --tag "timing-$workload" --seed 20260818 --trials 11 --warmup 3
done

model="$summary/cost_model_pilot_fit.json"
partitions="$scratch/model-inputs"
uv run --frozen --project "$root/bench/analysis" "$root/bench/analysis/cost_model.py" \
  --raw "$raw" --structural "$scratch" --summary "$summary" --stage prepare \
  --partition-directory "$partitions"
uv run --frozen --project "$root/bench/analysis" "$root/bench/analysis/cost_model.py" \
  --summary "$summary" --stage fit \
  --partition-manifest "$partitions/cost_model_partitions.json" \
  --model-artifact "$model"
uv run --frozen --project "$root/bench/analysis" "$root/bench/analysis/cost_model.py" \
  --summary "$summary" --stage evaluate \
  --partition-manifest "$partitions/cost_model_partitions.json" \
  --model-artifact "$model" --output-stem cost_model_pilot \
  --analysis-label "exploratory pilot, one machine; not the registered holdout evaluation"
