#!/usr/bin/env bash
# Run the registered decision pipeline in its fixed order. The four analyst
# campaigns must already contain custody-produced blinded raw data under raw/.
# The two named campaigns remain in controlled custody and are opened only
# after primary outputs have been hashed and unblinded. LABEL-A/LABEL-B are
# the registered contrast's two opaque labels in lexical order, not named roles.
set -euo pipefail

if [[ "$#" -ne 11 ]]; then
  echo "usage: $0 ANALYST-A ANALYST-B COMPILER ALLOCATOR NAMED-A NAMED-B LABEL-A LABEL-B CUSTODY-DIR STUDY-ID H5-RESPONSES" >&2
  exit 2
fi

analyst_a="$1"
analyst_b="$2"
compiler="$3"
allocator="$4"
named_a="$5"
named_b="$6"
label_a="$7"
label_b="$8"
custody="$9"
study_id="${10}"
h5_responses="${11}"
root="$(cd "$(dirname "$0")/.." && pwd)"
analysis="$root/bench/analysis"
confirm=(uv run --frozen --project "$analysis" python "$analysis/confirm.py")

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
  "${command[@]}"
}

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
  --stage h4 --blinded --subject "$label_a" --baseline "$label_b"
"${confirm[@]}" --campaign "$analyst_a" --comparison-campaign "$compiler" \
  --stage compiler --blinded --subject "$label_a" --baseline "$label_b"
"${confirm[@]}" --campaign "$analyst_a" --comparison-campaign "$allocator" \
  --stage allocator --blinded --subject "$label_a" --baseline "$label_b"

uv run --frozen --project "$analysis" python "$analysis/blind.py" unblind \
  "$analyst_a" "$analyst_b" \
  --custody-dir "$custody" --study-id "$study_id"
uv run --frozen --project "$analysis" python "$analysis/blind.py" verify-named \
  "$named_a" "$analyst_a" --custody-dir "$custody" --study-id "$study_id"
uv run --frozen --project "$analysis" python "$analysis/blind.py" verify-named \
  "$named_b" "$analyst_b" --custody-dir "$custody" --study-id "$study_id"

for campaign in "$named_a" "$named_b"; do
  "${confirm[@]}" --campaign "$campaign" --stage checksums
  "${confirm[@]}" --campaign "$campaign" --stage h1
done

model_summary="$named_a/analysis/model"
model_inputs="$model_summary/inputs"
model_artifact="$model_summary/confirmatory-model.json"
model=(uv run --frozen --project "$analysis" python "$analysis/cost_model.py")
"${model[@]}" --stage prepare --raw "$named_a/raw" --summary "$model_summary" \
  --partition-directory "$model_inputs" --split-salt valseg-confirm-split-20270214 \
  --hold-out-share 0.30
"${model[@]}" --stage fit --summary "$model_summary" --partition-directory "$model_inputs" \
  --model-artifact "$model_artifact"
"${model[@]}" --stage evaluate --summary "$model_summary" \
  --partition-directory "$model_inputs" --model-artifact "$model_artifact" \
  --output-stem confirmatory-model --analysis-label "registered H3 holdout evaluation"

"${confirm[@]}" --campaign "$analyst_a" --stage h3 \
  --cell-predictions "$model_summary/confirmatory-model_cells.csv" \
  --model-artifact "$model_artifact"
"${model[@]}" --stage transfer --summary "$model_summary" \
  --model-artifact "$model_artifact" --transfer-responses "$h5_responses" \
  --output-stem confirmatory-model
"${confirm[@]}" --campaign "$analyst_a" --stage h5 \
  --cell-predictions "$model_summary/confirmatory-model_external_cells.csv" \
  --model-artifact "$model_artifact"

echo "registered analysis complete: $analyst_a"
