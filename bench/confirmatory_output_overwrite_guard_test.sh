#!/usr/bin/env bash
set -euo pipefail

runner="${1:?usage: $0 /path/to/run_confirmatory.sh}"
scratch="$(mktemp -d -t valseg-confirmatory-guard.XXXXXX)"

cleanup() {
  rm -rf -- "$scratch"
}
trap cleanup EXIT

system_path="$scratch/system_tag.txt"
runs_path="$scratch/runs_tag.csv"
touch "$system_path"

if bash "$runner" --check-output-paths "$system_path" "$runs_path" \
    >"$scratch/refusal.log" 2>&1; then
  echo "existing system metadata was unexpectedly accepted" >&2
  exit 1
fi

if ! grep -Fq "refusing to overwrite $system_path" "$scratch/refusal.log"; then
  echo "metadata refusal did not identify the existing path" >&2
  exit 1
fi

bash "$runner" --check-output-paths \
  "$scratch/missing_system.txt" "$scratch/missing_runs.csv"
