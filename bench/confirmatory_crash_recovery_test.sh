#!/usr/bin/env bash
# Regression for crash recovery in run_confirmatory.sh. An unclean shutdown
# leaves zero-length output files behind: ext4 journals the file metadata but
# the data never flushes. A resume that tested only for existence skipped
# those cells forever and the phase audit counted them as present, so the
# campaign was marked complete with silent holes in it.
#
# Both halves of the rule are checked here: a cell whose runs file is empty is
# measured again, and the audit refuses to mark a phase complete while an
# empty file is standing in for a real one.
#
# Like the resume-guard regression this stubs valseg_bench, and it writes its
# own two-row schedule so the check stays cheap: the registered timing
# schedule is 892 cells and none of them are needed to prove the rule.
set -euo pipefail

runner="${1:?usage: $0 /path/to/run_confirmatory.sh}"

scratch="$(mktemp -d -t valseg-crash-recovery.XXXXXX)"
build_dir="$scratch/build"
mkdir -p "$build_dir/bench"
fake_binary="$build_dir/bench/valseg_bench"
cat > "$fake_binary" <<'FAKE'
#!/usr/bin/env bash
set -euo pipefail
out_dir="" tag=""
while [[ $# -gt 0 ]]; do
  case "$1" in
  --out-dir) out_dir="$2"; shift 2 ;;
  --tag) tag="$2"; shift 2 ;;
  *) shift ;;
  esac
done
if [[ -e "$out_dir/.die" ]]; then
  : > "$out_dir/runs_${tag}.csv"
  exit 0
fi
echo "structure,ns" > "$out_dir/runs_${tag}.csv"
echo "measured" >> "$out_dir/runs_${tag}.csv"
echo "peak_rss_bytes" > "$out_dir/memory_${tag}.csv"
echo "core_placement=stub" > "$out_dir/environment_${tag}.txt"
FAKE
chmod +x "$fake_binary"
cp "$fake_binary" "$build_dir/bench/valseg_bench_alloc"

campaign="dryrun-crash-recovery"
repo_root="$(cd "$(dirname "$runner")/.." && pwd)"
meta="$repo_root/bench/results/campaigns/$campaign"
raw="$meta/raw"
trap 'rm -rf -- "$meta" "$scratch"' EXIT
rm -rf -- "$meta"
mkdir -p "$raw"

survivor="timing-W1-n1000-v0-lazy-t00"
crashed="timing-W1-n1000-v0-persistent-t00"

# A schedule the runner will reuse instead of generating the registered one.
printf '%s\n' \
  "$(printf 'W1\t1000\t0\tlazy\t0\t2\t0\t0\t%s' "$survivor")" \
  "$(printf 'W1\t1000\t0\tpersistent\t0\t2\t1\t1\t%s' "$crashed")" \
  > "$meta/schedule_timing.tsv"

# One cell completed before the crash; the other is what a power loss leaves.
printf 'structure,ns\nmeasured\n' > "$raw/runs_$survivor.csv"
: > "$raw/runs_$crashed.csv"
: > "$raw/memory_$crashed.csv"
: > "$raw/environment_$crashed.txt"
: > "$raw/system_$crashed.txt"

run() { VALSEG_DRY_RUN=1 VALSEG_BUILD_DIR="$build_dir" bash "$runner" "$campaign" timing; }

if ! run >"$scratch/resume.log" 2>&1; then
  echo "resuming after a simulated crash was refused" >&2
  cat "$scratch/resume.log" >&2
  exit 1
fi

if [[ ! -s "$raw/runs_$crashed.csv" ]]; then
  echo "the crashed cell was skipped instead of being measured again" >&2
  cat "$scratch/resume.log" >&2
  exit 1
fi
if ! grep -Fq measured "$raw/runs_$crashed.csv"; then
  echo "the crashed cell's runs file has no measured row" >&2
  exit 1
fi
if [[ ! -f "$meta/complete_timing" ]]; then
  echo "expected a completion marker once every cell held real data" >&2
  cat "$scratch/resume.log" >&2
  exit 1
fi

# The survivor must not have been measured a second time: re-running a cell
# that already holds data would silently replace it.
if [[ "$(wc -l <"$raw/runs_$survivor.csv")" -ne 2 ]]; then
  echo "an already-complete cell was measured again on resume" >&2
  exit 1
fi

# Second half: an empty file must never satisfy the phase audit. A process
# that dies before writing leaves the same zero-length file the power loss
# did, so the stub is told to die rather than being replaced: swapping the
# binary would change its hash and the resume guard would refuse first.
rm -f "$meta/complete_timing"
: > "$raw/runs_$crashed.csv"
: > "$raw/.die"

if run >"$scratch/audit.log" 2>&1; then
  echo "the phase audit accepted a zero-length file as a produced output" >&2
  cat "$scratch/audit.log" >&2
  exit 1
fi
if ! grep -Fq 'refusing to mark timing complete' "$scratch/audit.log"; then
  echo "the audit failed for some reason other than the missing output" >&2
  cat "$scratch/audit.log" >&2
  exit 1
fi

echo "crash recovery re-measures emptied cells and the audit rejects them"
