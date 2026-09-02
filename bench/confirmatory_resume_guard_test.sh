#!/usr/bin/env bash
# Regression for the campaign-metadata resume guard added to
# run_confirmatory.sh: a resumed campaign must refuse to continue once its
# recorded binaries, scripts, seed or commit no longer match what is on
# disk, and a completed phase must leave a completion marker whose expected
# count matches what was actually produced.
#
# This exercises run_confirmatory.sh's own shell logic, not the timing
# binary's correctness (covered elsewhere), so it stubs valseg_bench with a
# fast fake that only implements the --structural CLI shape. The real
# structural dry run scans all twelve workloads at their full registered
# sizes and is not meant to be fast; a stub keeps this regression cheap
# enough to run on every push.
set -euo pipefail

runner="${1:?usage: $0 /path/to/run_confirmatory.sh}"

scratch="$(mktemp -d -t valseg-resume-guard.XXXXXX)"
build_dir="$scratch/build"
mkdir -p "$build_dir/bench"
fake_binary="$build_dir/bench/valseg_bench"
cat > "$fake_binary" <<'FAKE'
#!/usr/bin/env bash
set -euo pipefail
out_dir="" tag="" workload=""
while [[ $# -gt 0 ]]; do
  case "$1" in
  --out-dir) out_dir="$2"; shift 2 ;;
  --tag) tag="$2"; shift 2 ;;
  --workload) workload="$2"; shift 2 ;;
  *) shift ;;
  esac
done
echo "workload,structure,n" > "$out_dir/structural_${tag}-${workload}.csv"
FAKE
chmod +x "$fake_binary"
cp "$fake_binary" "$build_dir/bench/valseg_bench_alloc"

campaign="dryrun-resume-guard"
run() { VALSEG_DRY_RUN=1 VALSEG_BUILD_DIR="$build_dir" bash "$runner" "$campaign" structural; }

# run_confirmatory.sh resolves campaign output relative to its own script
# location, always under the real repository's bench/results tree, so this
# test operates there directly and removes it afterward.
repo_root="$(cd "$(dirname "$runner")/.." && pwd)"
meta="$repo_root/bench/results/campaigns/$campaign"
trap 'rm -rf -- "$meta" "$scratch"' EXIT
rm -rf -- "$meta"

run >"$scratch/first.log" 2>&1
if [[ ! -f "$meta/complete_structural" ]]; then
  echo "expected a completion marker after a clean structural dry run" >&2
  cat "$scratch/first.log" >&2
  exit 1
fi
if ! grep -Fq 'expected_files=12' "$meta/complete_structural"; then
  echo "completion marker does not record the expected file count" >&2
  cat "$meta/complete_structural" >&2
  exit 1
fi

# Resuming with unchanged metadata is a no-op success.
run >"$scratch/second.log" 2>&1

# Tamper with the recorded metadata and confirm the resume guard refuses.
sed -i.bak 's/^seed_base=.*/seed_base=1/' "$meta/campaign.txt"
if run >"$scratch/third.log" 2>&1; then
  echo "resuming with mismatched campaign metadata was unexpectedly accepted" >&2
  exit 1
fi
if ! grep -Fq 'refusing to resume' "$scratch/third.log"; then
  echo "metadata mismatch was not refused with the expected message" >&2
  cat "$scratch/third.log" >&2
  exit 1
fi

echo "resume guard and completion marker behave as registered"
