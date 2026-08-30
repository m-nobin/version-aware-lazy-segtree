#!/usr/bin/env bash
# Verify the preserved exploratory pilot and rebuild every derived artifact.
# This script never writes beneath bench/results/raw.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
results="$root/bench/results"
manifest="$results/raw.sha256"

for command in shasum uv latexmk rg; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "missing required command: $command" >&2
    exit 2
  fi
done

if [[ ! -f "$manifest" ]]; then
  echo "missing pilot manifest: $manifest" >&2
  exit 2
fi

manifest_entries="$(wc -l < "$manifest" | tr -d ' ')"
if [[ "$manifest_entries" != "96" ]]; then
  echo "pilot manifest has $manifest_entries entries; expected 96" >&2
  exit 2
fi

(
  cd "$results"
  shasum -a 256 --status -c raw.sha256
)

uv run --frozen --project "$root/bench/analysis" \
  python -m unittest discover -s "$root/bench/analysis/tests" -v

uv run --frozen --project "$root/bench/analysis" "$root/bench/analysis/report.py"

for artifact in \
  "$results/summary/cells.csv" \
  "$results/summary/comparisons.csv" \
  "$results/tables/facts.tex" \
  "$results/figures/scaling-update.pdf"; do
  if [[ ! -s "$artifact" ]]; then
    echo "analysis did not produce $artifact" >&2
    exit 2
  fi
done

cell_rows="$(($(wc -l < "$results/summary/cells.csv") - 1))"
if [[ "$cell_rows" != "357" ]]; then
  echo "analysis produced $cell_rows summary cells; expected 357" >&2
  exit 2
fi

if ! rg -q "Exploratory pilot" "$root/docs/benchmarking/benchmarking.tex"; then
  echo "pilot report is missing its exploratory label" >&2
  exit 2
fi

latexmk -pdf -cd "$root/docs/benchmarking/benchmarking.tex"
if [[ ! -s "$root/docs/benchmarking/benchmarking.pdf" ]]; then
  echo "pilot report PDF was not produced" >&2
  exit 2
fi

printf 'pilot verified: 96 raw files, 357 summary cells, report rebuilt\n'
