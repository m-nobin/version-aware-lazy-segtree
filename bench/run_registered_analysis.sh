#!/usr/bin/env bash
# The one locked registered-analysis script, in two halves that different
# people run. The analyst half sees only opaque campaign copies and the two
# lexically ordered contrast labels; it never receives a named campaign or the
# custody directory. The custodian half hashes every blinded output, unblinds,
# verifies the named inputs and runs the deterministic and model stages.
#
# A registered decision stage that fails closed leaves an explicit
# <stage>_unavailable.json record in place of its CSV; the half keeps running
# so that every other decision is produced and hashed, then exits non-zero.
# A prepare/fit failure of the cost model makes both H3 and H5 unavailable,
# because they share the frozen artifact; evaluate and transfer failures make
# only their own decision unavailable.
set -euo pipefail

usage() {
  cat >&2 <<'USAGE'
usage: run_registered_analysis.sh analyst ANALYST-A ANALYST-B COMPILER ALLOCATOR LABEL-A LABEL-B
       run_registered_analysis.sh custodian ANALYST-A ANALYST-B NAMED-A NAMED-B CUSTODY-DIR STUDY-ID H5-RESPONSES
USAGE
  exit 2
}

root="$(cd "$(dirname "$0")/.." && pwd)"
analysis="$root/bench/analysis"
confirm=(uv run --frozen --project "$analysis" python "$analysis/confirm.py")
blind=(uv run --frozen --project "$analysis" python "$analysis/blind.py")
model=(uv run --frozen --project "$analysis" python "$analysis/cost_model.py")
unavailable=()

finish() {
  if [[ "${#unavailable[@]}" -gt 0 ]]; then
    printf 'unavailable registered decision: %s\n' "${unavailable[@]}" >&2
    exit 1
  fi
  echo "registered analysis ($1) complete"
}

run_analyst() {
  [[ "$#" -eq 6 ]] || usage
  local analyst_a="$1" analyst_b="$2" compiler="$3" allocator="$4" label_a="$5" label_b="$6"
  if [[ ! "$label_a" =~ ^S0[1-9]$ || ! "$label_b" =~ ^S0[1-9]$ || ! "$label_a" < "$label_b" ]]; then
    echo "LABEL-A and LABEL-B must be distinct SNN labels in lexical order" >&2
    exit 2
  fi

  run_blinded() {
    local campaign="$1" stage="$2" metric="${3:-update}" contrast="${4:-$label_a}"
    local tag="${5:-}"
    local command=("${confirm[@]}" --campaign "$campaign" --stage "$stage" --blinded
      --subject "$contrast" --baseline "$label_b" --metric "$metric")
    if [[ -n "$tag" ]]; then command+=(--output-tag "$tag"); fi
    "${command[@]}" || unavailable+=("$campaign $stage $metric $tag")
  }

  local campaign metric stage
  for campaign in "$analyst_a" "$analyst_b"; do
    run_blinded "$campaign" primary
    for metric in update query; do
      run_blinded "$campaign" regime "$metric" "$label_a" contrast-a
      run_blinded "$campaign" regime "$metric" "$label_b" contrast-b
      run_blinded "$campaign" hierarchical "$metric"
    done
    for stage in feasibility mean-median order leave-one-out; do
      run_blinded "$campaign" "$stage"
    done
  done

  "${confirm[@]}" --campaign "$analyst_a" --comparison-campaign "$analyst_b" \
    --stage h4 --blinded --subject "$label_a" --baseline "$label_b" || unavailable+=(h4)
  "${confirm[@]}" --campaign "$analyst_a" --comparison-campaign "$compiler" \
    --stage compiler --blinded --subject "$label_a" --baseline "$label_b" || unavailable+=(compiler)
  "${confirm[@]}" --campaign "$analyst_a" --comparison-campaign "$allocator" \
    --stage allocator --blinded --subject "$label_a" --baseline "$label_b" || unavailable+=(allocator)
  finish analyst
}

run_custodian() {
  [[ "$#" -eq 7 ]] || usage
  local analyst_a="$1" analyst_b="$2" named_a="$3" named_b="$4" custody="$5" study_id="$6"
  local h5_responses="$7"

  "${blind[@]}" unblind "$analyst_a" "$analyst_b" --custody-dir "$custody" --study-id "$study_id"
  "${blind[@]}" verify-named "$named_a" "$analyst_a" --custody-dir "$custody" --study-id "$study_id"
  "${blind[@]}" verify-named "$named_b" "$analyst_b" --custody-dir "$custody" --study-id "$study_id"

  local campaign
  for campaign in "$named_a" "$named_b"; do
    "${confirm[@]}" --campaign "$campaign" --stage checksums
    "${confirm[@]}" --campaign "$campaign" --stage h1
  done

  local model_summary="$named_a/analysis/model"
  local model_inputs="$model_summary/inputs"
  local model_artifact="$model_summary/confirmatory-model.json"
  "${model[@]}" --stage prepare --raw "$named_a/raw" --summary "$model_summary" \
    --partition-directory "$model_inputs" --split-salt valseg-confirm-split-20270214 \
    --hold-out-share 0.30
  "${model[@]}" --stage fit --summary "$model_summary" --partition-directory "$model_inputs" \
    --model-artifact "$model_artifact"
  "${model[@]}" --stage evaluate --summary "$model_summary" \
    --partition-directory "$model_inputs" --model-artifact "$model_artifact" \
    --output-stem confirmatory-model --analysis-label "registered H3 holdout evaluation" \
    || unavailable+=(h3-evaluate)

  "${confirm[@]}" --campaign "$analyst_a" --stage h3 \
    --cell-predictions "$model_summary/confirmatory-model_cells.csv" \
    --model-artifact "$model_artifact" || unavailable+=(h3)
  "${model[@]}" --stage transfer --summary "$model_summary" \
    --model-artifact "$model_artifact" --transfer-responses "$h5_responses" \
    --output-stem confirmatory-model || unavailable+=(h5-transfer)
  "${confirm[@]}" --campaign "$analyst_a" --stage h5 \
    --cell-predictions "$model_summary/confirmatory-model_external_cells.csv" \
    --model-artifact "$model_artifact" || unavailable+=(h5)

  "${blind[@]}" verify-outputs "$analyst_a" "$analyst_b"
  finish custodian
}

[[ "$#" -ge 1 ]] || usage
half="$1"
shift
case "$half" in
  analyst) run_analyst "$@" ;;
  custodian) run_custodian "$@" ;;
  *) usage ;;
esac
