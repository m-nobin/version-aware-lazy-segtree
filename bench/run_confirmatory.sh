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
# "dryrun" so dry-run data can never be mistaken for confirmatory data, and a
# real campaign id must contain neither "dryrun" nor "pilot". A dry run may
# start from any commit. A real run refuses a dirty worktree, a commit that
# does not descend from the registered commit, a frozen file whose checksum
# no longer matches the manifest, and a registration record without a
# registered tag -- so no real phase runs before registration.
# VALSEG_PIN=1 wraps every process in bench/env/pin_<os>.sh run --.
# VALSEG_BUILD_DIR overrides the default release-verify build directory.
set -euo pipefail

refuse_existing_output() {
  local path
  for path in "$@"; do
    if [[ -e "$path" ]]; then
      echo "refusing to overwrite $path; choose a new campaign id" >&2
      return 2
    fi
  done
}

# A cell is finished only when its runs file has content. An unclean shutdown
# (power loss, panic) leaves zero-length outputs behind: the file metadata
# reaches the journal but the data never flushes. Treating those as finished
# would leave silent holes that the phase audit then counts as present, so the
# cell's partial files are cleared and it is measured again.
reuse_or_clear_cell() {
  local tag="$1"
  if [[ -s "$raw/runs_$tag.csv" ]]; then
    return 0
  fi
  rm -f "$raw/runs_$tag.csv" "$raw/memory_$tag.csv" \
    "$raw/environment_$tag.txt" "$raw/system_$tag.txt"
  return 1
}

# Internal test hook for the shell-level metadata guard. It runs before any
# campaign setup or binary lookup, so CTest can exercise the refusal path
# without creating campaign output.
if [[ "${1:-}" == "--check-output-paths" ]]; then
  shift
  refuse_existing_output "$@"
  exit 0
fi

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
# A dry run measures two trials per cell, which is enough to prove the harness
# and too few to classify a cell: the registered decisions need four shared
# pairs, and leave-one-out needs five. VALSEG_DRY_RUN_TRIALS raises the count
# so a validation dry run exercises every registered decision on real data. It
# is read only on a dry run; a confirmatory campaign always uses 20/40.
dry_trials="${VALSEG_DRY_RUN_TRIALS:-2}"
if [[ "${VALSEG_DRY_RUN:-0}" == "1" ]]; then
  if [[ ! "$dry_trials" =~ ^[1-9][0-9]*$ ]]; then
    echo "VALSEG_DRY_RUN_TRIALS must be a positive integer" >&2
    exit 2
  fi
  seed="$dryrun_seed"
  warmup_seconds=0
  dry="yes"
fi
# shellcheck source=bench/campaign_gate.sh
source "$root/bench/campaign_gate.sh"
campaign_gate "$root" "$campaign" "$dry"

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

hash_or() {
  local path="$1" fallback="$2"
  [[ -e "$path" ]] && shasum -a 256 "$path" | cut -d' ' -f1 || echo "$fallback"
}

# Hash one generated campaign artifact (a schedule or a trace file) the first
# time it is produced, and refuse a later mismatch: a schedule or trace that
# was regenerated with different content is exactly the "changed schedule"
# the registered protocol says a resumed campaign must never mix in.
hash_or_verify_artifact() {
  local path="$1"
  local sidecar="$path.sha256" digest
  digest="$(shasum -a 256 "$path" | cut -d' ' -f1)"
  if [[ -e "$sidecar" ]]; then
    if [[ "$(cat "$sidecar")" != "$digest" ]]; then
      echo "refusing: $path no longer matches its recorded checksum ($sidecar)" >&2
      exit 2
    fi
  else
    printf '%s\n' "$digest" > "$sidecar"
  fi
}

record_meta() {
  local commit dirty registered_commit
  campaign_gate "$root" "$campaign" "$dry"
  commit="$campaign_gate_commit"
  dirty="$campaign_gate_dirty"
  registered_commit="$campaign_gate_registered_commit"

  local expected file="$meta/campaign.txt"
  expected="$(
    printf 'campaign=%s\n' "$campaign"
    printf 'dry_run=%s\n' "${dry:-no}"
    printf 'seed_base=%s\n' "$seed"
    printf 'schedule_seed=%s\n' "$schedule_seed"
    printf 'git_commit=%s\n' "$commit"
    printf 'git_dirty=%s\n' "$dirty"
    printf 'registered_commit=%s\n' "$registered_commit"
    printf 'build_dir=%s\n' "$build"
    printf 'timing_binary_sha256=%s\n' "$(hash_or "$timing_binary" missing)"
    printf 'alloc_binary_sha256=%s\n' "$(hash_or "$alloc_binary" not_built)"
    printf 'schedule_script_sha256=%s\n' "$(hash_or "$root/bench/confirm_schedule.py" missing)"
    printf 'runner_script_sha256=%s\n' "$(hash_or "$root/bench/run_confirmatory.sh" missing)"
    printf 'environment_script_sha256=%s\n' "$(hash_or "$root/bench/collect_environment.sh" missing)"
  )"
  if [[ -e "$file" ]]; then
    if [[ "$expected" != "$(cat "$file")" ]]; then
      echo "refusing to resume $campaign: recorded metadata does not match the current binaries, scripts, seed or commit" >&2
      diff <(printf '%s\n' "$expected") "$file" >&2 || true
      exit 2
    fi
  else
    printf '%s\n' "$expected" > "$file"
  fi
}

# Write a completion marker for one phase only after every file its schedule
# promised is actually present, so a marker is trustworthy evidence rather
# than "the loop reached the end". Idempotent: a second call with the same
# expected count is a no-op, a different one refuses (the resume-mismatch
# case belongs to record_meta, not here).
audit_and_mark_phase() {
  local phase_name="$1" expected="$2" glob="$3"
  local marker="$meta/complete_$phase_name" produced
  produced=$(find "$raw" -maxdepth 1 -name "$glob" -size +0 | wc -l | tr -d ' ')
  if [[ "$produced" -lt "$expected" ]]; then
    echo "refusing to mark $phase_name complete: expected $expected output files, found $produced" >&2
    exit 1
  fi
  if [[ -e "$marker" ]]; then
    return 0
  fi
  {
    printf 'phase=%s\n' "$phase_name"
    printf 'expected_files=%s\n' "$expected"
    printf 'present_files=%s\n' "$produced"
    printf 'completed_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  } > "$marker"
}

run_schedule() {
  local mode="$1" binary="$2" extra_flags=("${@:3}")
  local schedule="$meta/schedule_$mode.tsv"
  local trials=20 primary_trials=40
  if [[ "$mode" == "alloc" ]]; then trials=3 primary_trials=3; fi
  # A dry run lowers the trial count; it never raises it above the campaign it
  # rehearses, so alloc stays at its registered three however high dry_trials is.
  if [[ -n "$dry" ]]; then
    [[ "$dry_trials" -lt "$trials" ]] && trials="$dry_trials"
    [[ "$dry_trials" -lt "$primary_trials" ]] && primary_trials="$dry_trials"
  fi
  if [[ ! -e "$schedule" ]]; then
    local workloads="all"
    [[ "$mode" == "latency" ]] && workloads="$latency_workloads"
    python3 "$root/bench/confirm_schedule.py" --binary "$timing_binary" --mode "$mode" \
      --trials "$trials" --primary-trials "$primary_trials" \
      --primary-cells "$root/bench/primary_cells.csv" \
      --capped-cells "$root/bench/capped_cells.csv" \
      --schedule-seed "$schedule_seed" --workloads "$workloads" --out "$schedule"
  fi
  hash_or_verify_artifact "$schedule"
  local total done_count
  total=$(grep -vc '^#' "$schedule")
  done_count=0
  while IFS=$'\t' read -r workload n variant structure trial cell_trials exec_order process_seq tag; do
    [[ "$workload" == \#* ]] && continue
    done_count=$((done_count + 1))
    if reuse_or_clear_cell "$tag"; then
      continue
    fi
    printf '=== %s [%d/%d] ===\n' "$tag" "$done_count" "$total"
    bash "$root/bench/collect_environment.sh" > "$raw/system_$tag.txt"
    "${pin[@]+"${pin[@]}"}" "$binary" --mode "${extra_flags[@]}" \
      --workload "$workload" --n "$n" --variant "$variant" --structure "$structure" \
      --trials "$cell_trials" --trial-index "$trial" \
      --warmup "$warmup_trials" --warmup-seconds "$warmup_seconds" \
      --seed "$seed" --capped-trials 1 --batch-trials 0 \
      --exec-order "$exec_order" --process-seq "$process_seq" \
      --out-dir "$raw" --tag "$tag"
  done < "$schedule"
  audit_and_mark_phase "$mode" "$total" "runs_$mode-*.csv"
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
    if [[ -s "$raw/structural_$tag-$workload.csv" ]]; then
      continue
    fi
    "$timing_binary" --structural --workload "$workload" --out-dir "$raw" \
      --tag "$tag" --seed "$seed" --trials 40 --warmup "$warmup_trials"
  done
  audit_and_mark_phase structural 12 "structural_timing-*.csv"
  ;;
trace)
  # One trace file per registered external draw (bench/h5_trace_draws.csv):
  # H5 needs all twelve, not one distribution replayed under twelve labels.
  draws_csv="$root/bench/h5_trace_draws.csv"
  expected_trace_files=0
  while IFS=, read -r draw_id draw_seed draw_n draw_operations draw_update_share draw_interval_share draw_trials; do
    [[ "$draw_id" == "draw_id" ]] && continue
    trace_file="$meta/external-$draw_id.trace"
    if [[ ! -e "$trace_file" ]]; then
      python3 "$root/bench/traces/make_external_distribution.py" --out "$trace_file" \
        --seed "$draw_seed" --n "$draw_n" --operations "$draw_operations" \
        --update-share "$draw_update_share" --interval-share "$draw_interval_share"
    fi
    hash_or_verify_artifact "$trace_file"
    trials="$draw_trials"
    [[ -n "$dry" && "$dry_trials" -lt "$trials" ]] && trials="$dry_trials"
    # The draw's machine-independent counts, one row per recorded seed, are
    # what the H5 transfer joins to the external adapter's responses.
    if [[ ! -s "$raw/structural_trace-$draw_id-$draw_id.csv" ]]; then
      "$timing_binary" --structural --trace "$trace_file" --trace-id "$draw_id" --workload WT \
        --out-dir "$raw" --tag "trace-$draw_id" --seed "$seed" --trials "$trials" \
        --warmup "$warmup_trials"
    fi
    for structure in lazy persistent copy-on-push full-copy point-only checkpointing buffered fat-node external; do
      for ((trial = 0; trial < trials; ++trial)); do
        expected_trace_files=$((expected_trace_files + 1))
        tag="trace-$draw_id-$structure-t$(printf '%02d' "$trial")"
        if reuse_or_clear_cell "$tag"; then
          continue
        fi
        bash "$root/bench/collect_environment.sh" > "$raw/system_$tag.txt"
        "${pin[@]+"${pin[@]}"}" "$timing_binary" --mode batch --trace "$trace_file" --workload WT \
          --trace-id "$draw_id" --structure "$structure" --trials "$trials" --trial-index "$trial" \
          --warmup "$warmup_trials" --warmup-seconds "$warmup_seconds" \
          --seed "$seed" --capped-trials 1 --batch-trials 0 \
          --out-dir "$raw" --tag "$tag"
      done
    done
  done < "$draws_csv"
  audit_and_mark_phase trace "$expected_trace_files" "runs_trace-*.csv"
  ;;
*)
  echo "usage: $0 <campaign-id> [timing|alloc|latency|structural|trace]" >&2
  exit 2
  ;;
esac

printf 'phase complete: %s (%s)\n' "$phase" "$campaign"
