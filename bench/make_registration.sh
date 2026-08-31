#!/usr/bin/env bash
# Assemble the immutable-registration manifest: the SHA-256 of every file the
# registered protocol freezes, plus the repository state. The manifest is
# what gets deposited (OSF or Zenodo) with the protocol document; the deposit
# timestamp is the registration time, and no confirmatory seed may run before
# it.
#
#   bench/make_registration.sh            # writes docs/research/registration-manifest.txt
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/docs/research/registration-manifest.txt"

frozen=(
  docs/research/registered-protocol.md
  docs/research/cost-model.md
  bench/analysis/blind.py
  bench/analysis/confirm.py
  bench/analysis/cost_model.py
  bench/analysis/data.py
  bench/analysis/split.py
  bench/analysis/pyproject.toml
  bench/analysis/uv.lock
  bench/bench_main.cpp
  bench/workloads.cpp
  bench/workloads.hpp
  bench/adapters.hpp
  bench/structural_counts.hpp
  bench/copy_on_push_segment_tree.hpp
  bench/external/thealgorithms_persistent_lazy.hpp
  bench/external/thealgorithms_persistent_seg_tree_lazy_prop.cpp
  bench/external/PROVENANCE.md
  bench/confirm_schedule.py
  bench/run_confirmatory.sh
  bench/run_sensitivity.sh
  bench/collect_environment.sh
  bench/env/pin_linux.sh
  bench/env/pin_macos.sh
  bench/primary_cells.csv
  bench/capped_cells.csv
  bench/traces/make_external_distribution.py
)

{
  printf '# Registration manifest\n'
  printf '# generated_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf '# git_commit=%s\n' "$(git -C "$root" rev-parse HEAD)"
  printf '# git_dirty=%s\n' "$(test -n "$(git -C "$root" status --porcelain)" && echo yes || echo no)"
  for file in "${frozen[@]}"; do
    if [[ ! -e "$root/$file" ]]; then
      echo "missing frozen file: $file" >&2
      exit 2
    fi
    (cd "$root" && shasum -a 256 "$file")
  done
} > "$out"

printf 'manifest -> %s\n' "$out"
shasum -a 256 "$out"
