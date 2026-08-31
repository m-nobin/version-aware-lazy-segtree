#!/usr/bin/env bash
# Execute one phase of a confirmatory campaign under the registered
# fresh-process protocol: one process per (cell, structure, trial), balanced
# order from the registered schedule seed, environment captured before every
# process, resumable by tag.
#
#   bench/run_confirmatory.sh <campaign-id> timing      # batch mode, valseg_bench
#   bench/run_confirmatory.sh <campaign-id> alloc       # interleaved, counting binary
#   bench/run_confirmatory.sh <campaign-id> latency     # sampled clock pairs, subset
#   bench/run_confirmatory.sh <campaign-id> structural  # counts only, monolithic
#   bench/run_confirmatory.sh <campaign-id> trace       # external WT distribution
#
# VALSEG_DRY_RUN=1 switches to the dry-run seeds (excluded from confirmation),
# two trials per cell and no warm-up load; the campaign id must then contain
# "dryrun" so dry-run data can never be mistaken for confirmatory data.
# VALSEG_PIN=1 wraps every process in bench/env/pin_<os>.sh run --.
# VALSEG_BUILD_DIR overrides the default release-verify build directory.
set -euo pipefail

campaign="${1:-}"
phase="${2:-}"
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${VALSEG_BUILD_DIR:-$root/build/release-verify}"

if [[ -z "$campaign" || ! "$campaign" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ || -z "$phase" ]]; then
  echo "usage: $0 <campaign-id> [timing|alloc|latency|structural|trace]" >&2
  exit 2
fi

# Registered protocol constants (docs/research/registered-protocol.md).
confirm_seed=20270214
dryrun_seed=20260901
schedule_seed=20270214
warmup_trials=1
warmup_seconds=3
latency_workloads="W1,W3,W9"
sample_every=64

seed="$confirm_seed"
dry=""
if [[ "${VALSEG_DRY_RUN:-0}" == "1" ]]; then
  if [[ "$campaign" != *dryrun* ]]; then
    echo "dry runs must use a campaign id containing 'dryrun'" >&2
    exit 2
  fi
  seed="$dryrun_seed"
  warmup_seconds=0
  dry="yes"
fi

raw="$root/bench/results/campaigns/$campaign/raw"
meta="$root/bench/results/campaigns/$campaign"
mkdir -p "$raw"

pin=()
if [[ "${VALSEG_PIN:-0}" == "1" ]]; then
  case "$(uname -s)" in
  Darwin) pin=("$root/bench/env/pin_macos.sh" run --) ;;
  Linux)  pin=("$root/bench/env/pin_linux.sh" run --) ;;
  esac
fi

timing_binary="$build/bench/valseg_bench"
alloc_binary="$build/bench/valseg_bench_alloc"
[[ -x "$timing_binary" ]] || { echo "missing $timing_binary; build release-verify first" >&2; exit 2; }

record_meta() {
  local file="$meta/campaign.txt"
  if [[ ! -e "$file" ]]; then
    {
      printf 'campaign=%s\n' "$campaign"
      printf 'dry_run=%s\n' "${dry:-no}"
      printf 'seed_base=%s\n' "$seed"
      printf 'schedule_seed=%s\n' "$schedule_seed"
      printf 'git_commit=%s\n' "$(git -C "$root" rev-parse HEAD 2>/dev/null || echo untracked)"
      printf 'git_dirty=%s\n' "$(test -n "$(git -C "$root" status --porcelain 2>/dev/null)" && echo yes || echo no)"
      printf 'build_dir=%s\n' "$build"
      printf 'binary_sha256=%s\n' "$(shasum -a 256 "$timing_binary" | cut -d' ' -f1)"
    } > "$file"
  fi
}

run_schedule() {
  local mode="$1" binary="$2" extra_flags=("${@:3}")
  local schedule="$meta/schedule_$mode.tsv"
  local trials=20 primary_trials=40
  if [[ "$mode" == "alloc" ]]; then trials=3 primary_trials=3; fi
  if [[ -n "$dry" ]]; then trials=2 primary_trials=2; fi
  if [[ ! -e "$schedule" ]]; then
    local workloads="all"
    [[ "$mode" == "latency" ]] && workloads="$latency_workloads"
    python3 "$root/bench/confirm_schedule.py" --binary "$timing_binary" --mode "$mode" \
      --trials "$trials" --primary-trials "$primary_trials" \
      --primary-cells "$root/bench/primary_cells.csv" \
      --capped-cells "$root/bench/capped_cells.csv" \
      --schedule-seed "$schedule_seed" --workloads "$workloads" --out "$schedule"
  fi
  local total done_count
  total=$(grep -vc '^#' "$schedule")
  done_count=0
  while IFS=$'\t' read -r workload n variant structure trial cell_trials tag; do
    [[ "$workload" == \#* ]] && continue
    done_count=$((done_count + 1))
    if [[ -e "$raw/runs_$tag.csv" ]]; then
      continue
    fi
    printf '=== %s [%d/%d] ===\n' "$tag" "$done_count" "$total"
    bash "$root/bench/collect_environment.sh" > "$raw/system_$tag.txt"
    "${pin[@]}" "$binary" --mode "${extra_flags[@]}" \
      --workload "$workload" --n "$n" --variant "$variant" --structure "$structure" \
      --trials "$cell_trials" --trial-index "$trial" \
      --warmup "$warmup_trials" --warmup-seconds "$warmup_seconds" \
      --seed "$seed" --capped-trials 1 --batch-trials 0 \
      --out-dir "$raw" --tag "$tag"
  done < "$schedule"
}

record_meta
case "$phase" in
timing)
  run_schedule timing "$timing_binary" batch
  ;;
alloc)
  run_schedule alloc "$alloc_binary" interleaved
  ;;
latency)
  run_schedule latency "$timing_binary" latency --sample-every "$sample_every"
  ;;
structural)
  # Nothing is timed and no structure is built, so this stays monolithic. 40
  # recorded seeds cover every cell's trial count.
  for workload in W1 W2 W3 W4 W5 W6 W7 W8 W9 W10 W11 W12; do
    tag="timing-$workload"
    if [[ -e "$raw/structural_$tag-$workload.csv" ]]; then
      continue
    fi
    "$timing_binary" --structural --workload "$workload" --out-dir "$raw" \
      --tag "$tag" --seed "$seed" --trials 40 --warmup "$warmup_trials"
  done
  ;;
trace)
  trace_file="$meta/external.trace"
  if [[ ! -e "$trace_file" ]]; then
    python3 "$root/bench/traces/make_external_distribution.py" --out "$trace_file"
  fi
  trials=20
  [[ -n "$dry" ]] && trials=2
  for structure in lazy persistent copy-on-push full-copy point-only checkpointing buffered fat-node external; do
    for ((trial = 0; trial < trials; ++trial)); do
      tag="trace-WT-$structure-t$(printf '%02d' "$trial")"
      if [[ -e "$raw/runs_$tag.csv" ]]; then
        continue
      fi
      bash "$root/bench/collect_environment.sh" > "$raw/system_$tag.txt"
      "${pin[@]}" "$timing_binary" --mode batch --trace "$trace_file" --workload WT \
        --structure "$structure" --trials "$trials" --trial-index "$trial" \
        --warmup "$warmup_trials" --warmup-seconds "$warmup_seconds" \
        --seed "$seed" --capped-trials 1 --batch-trials 0 \
        --out-dir "$raw" --tag "$tag"
    done
  done
  ;;
*)
  echo "usage: $0 <campaign-id> [timing|alloc|latency|structural|trace]" >&2
  exit 2
  ;;
esac

printf 'phase complete: %s (%s)\n' "$phase" "$campaign"
