#!/usr/bin/env bash
# Assemble the immutable-registration manifest: the SHA-256 of every file the
# registered protocol freezes, plus the repository state. The commit it names
# is tagged and pushed; the tag's push time is the registration time, and no
# confirmatory seed may run before it. The manifest is published with the
# paper.
#
#   bench/make_registration.sh             # write after PR6/PR7 are merged
#   bench/make_registration.sh --verify    # verify every recorded checksum
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/docs/research/registration-manifest.txt"

verify_manifest() {
  local manifest="$1"
  [[ -f "$manifest" ]] || { echo "missing registration manifest: $manifest" >&2; return 2; }
  grep -qx '# git_dirty=no' "$manifest" || {
    echo "registration manifest was not generated from a clean worktree" >&2
    return 2
  }
  (cd "$root" && shasum -a 256 -c "$manifest")
}

if [[ "${1:-}" == "--verify" ]]; then
  verify_manifest "${2:-$out}"
  exit 0
fi
if [[ "$#" -gt 1 ]]; then
  echo "usage: $0 [OUTPUT] | $0 --verify [MANIFEST]" >&2
  exit 2
fi
out="${1:-$out}"

if [[ -n "$(git -C "$root" status --porcelain)" ]]; then
  echo "refusing registration: git worktree must be clean before manifest generation" >&2
  exit 2
fi

frozen=(
  docs/research/registered-protocol.md
  docs/research/cost-model.md
  docs/research/statistical-review.md
  bench/analysis/blind.py
  bench/analysis/confirm.py
  bench/analysis/cost_model.py
  bench/analysis/data.py
  bench/analysis/split.py
  bench/analysis/pyproject.toml
  bench/analysis/uv.lock
  bench/analysis/tests/test_analysis.py
  bench/analysis/tests/test_confirm.py
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
  bench/campaign_gate.sh
  bench/run_confirmatory.sh
  bench/run_registered_analysis.sh
  bench/run_sensitivity.sh
  bench/collect_environment.sh
  bench/env/pin_linux.sh
  bench/env/pin_macos.sh
  bench/primary_cells.csv
  bench/h4_cells.csv
  bench/h5_trace_draws.csv
  bench/sensitivity_cells.csv
  bench/capped_cells.csv
  bench/traces/make_external_distribution.py
)

temporary="$(mktemp "${TMPDIR:-/tmp}/valseg-registration.XXXXXX")"
trap 'rm -f "$temporary"' EXIT

{
  printf '# Registration manifest\n'
  printf '# generated_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf '# git_commit=%s\n' "$(git -C "$root" rev-parse HEAD)"
  printf '# git_dirty=no\n'
  for file in "${frozen[@]}"; do
    if [[ ! -e "$root/$file" ]]; then
      echo "missing frozen file: $file" >&2
      exit 2
    fi
    (cd "$root" && shasum -a 256 "$file")
  done
} > "$temporary"

verify_manifest "$temporary"
mkdir -p "$(dirname "$out")"
mv "$temporary" "$out"
trap - EXIT

printf 'manifest -> %s\n' "$out"
shasum -a 256 "$out"
cat <<NEXT
next: commit this manifest, tag the commit it names, push the tag, then record
the registration in docs/research/registration-record.md (not a frozen file):
  git tag -a registered-$(date -u +%Y%m%d) $(git -C "$root" rev-parse HEAD) -m "registered protocol"
NEXT
