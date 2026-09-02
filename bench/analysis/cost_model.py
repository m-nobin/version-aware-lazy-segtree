"""Prepare, fit and evaluate the frozen mechanistic time model.

    uv run --project bench/analysis bench/analysis/cost_model.py --stage prepare [options]
    uv run --project bench/analysis bench/analysis/cost_model.py --stage fit [options]
    uv run --project bench/analysis bench/analysis/cost_model.py --stage evaluate [options]

Fits, for every structure and for updates and queries separately,

    log(ns_per_op) = alpha + beta * visited_records + gamma * allocated_records
                     + theta * working_set_transition
                     + lambda * version_distance_transition
                     + phi * full_coverage_share

on training cells and reports the error on separately stored holdout cells.
bytes_touched of the plan's form is a fixed multiple
of the other two within one structure and is not a separate predictor. A
predictor that is constant (allocated_records for queries) or exactly
collinear with the rest (visited_records equals allocated_records for the
tag-retaining subject, Proposition 10.5) is dropped from that fit, and the
`form` column says which predictors were kept. The split is bench/analysis/split.py; every seed of a
cell lands on one side. The predictors are the candidate variables frozen in
docs/research/cost-model.md and come from structural_*.csv (machine-independent
counts from the frontier oracles, written by `valseg_bench --structural`) and
from the runs CSV (nodes stored per update, retained bytes). Equivalent cells
whose complete generated inputs match for every seed are kept in one split
partition using the structural stream fingerprints.

The prepare stage assigns stream groups before it loads any timing response,
then writes disjoint training and holdout response files plus a checksum
manifest. The fit stage receives only the manifest and training file. It writes
and hashes a canonical model artifact without opening the holdout response.
The evaluate stage verifies that artifact hash before opening the holdout file.
The same commands support exploratory and confirmatory campaigns; output names
and messages are neutral unless the caller supplies an explicit analysis label.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import sys

import numpy as np
import pandas as pd

sys.path.insert(0, str(pathlib.Path(__file__).parent))

import data  # noqa: E402
import split  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
RAW = ROOT / "bench" / "results" / "raw"
SUMMARY = ROOT / "bench" / "results" / "summary"
PARTITION_MANIFEST_NAME = "cost_model_partitions.json"
TRAINING_RESPONSES_NAME = "cost_model_training_responses.csv"
HOLDOUT_RESPONSES_NAME = "cost_model_holdout_responses.csv"
CAPPED_CELLS = ROOT / "bench" / "capped_cells.csv"
CAPPED_COLUMNS = ["workload", "n", "variant", "structure"]
CANDIDATE_COLUMNS = [
    "visited_records",
    "allocated_records",
    "working_set_transition",
    "version_distance_transition",
    "full_coverage_share",
]
STRUCTURAL_COLUMNS = {
    "workload",
    "n",
    "axis",
    "variant",
    "seed",
    "k",
    "stream_fingerprint",
    "updates",
    "nonzero_updates",
    "queries",
    "sum_update_visits",
    "sum_checkpoint_update_visits",
    "sum_intersecting",
    "sum_query_visits",
    "sum_replay_entries",
    "sum_query_version_distance",
    "full_coverage_updates",
    "full_coverage_queries",
}


def captured_cache_bytes(raw: pathlib.Path) -> int | None:
    """The l2_bytes line of the first system capture, if the campaign wrote one."""
    for path in sorted(raw.glob("system_*.txt")):
        for line in path.read_text().splitlines():
            if line.startswith("l2_bytes=") and line.split("=", 1)[1].strip():
                return int(line.split("=", 1)[1])
    return None


def load_structural(
    directory: pathlib.Path, pattern: str = "structural*-W[0-9]*.csv"
) -> pd.DataFrame:
    """Structural counts under ``directory``. The default pattern takes the
    synthetic W1-W12 files only, so a trace phase's ``structural_trace-WT*``
    rows never enter the H3 split; ``external_responses`` reads those."""
    frames = []
    for path in sorted(directory.glob(pattern)):
        try:
            frames.append(pd.read_csv(path))
        except pd.errors.EmptyDataError:
            continue
    if not frames:
        raise SystemExit(
            f"no structural counts under {directory}; run valseg_bench --structural with the "
            "campaign's --seed, --warmup and --trials first"
        )
    structural = pd.concat(frames, ignore_index=True)
    missing = sorted(STRUCTURAL_COLUMNS.difference(structural.columns))
    if missing:
        raise SystemExit(f"structural counts missing columns: {', '.join(missing)}")
    if structural[list(STRUCTURAL_COLUMNS)].isna().any().any():
        raise SystemExit("structural counts contain missing values")
    keys = ["workload", "n", "axis", "variant", "seed"]
    duplicated = structural.duplicated(keys, keep=False)
    if duplicated.any():
        copies = structural[duplicated]
        if any(len(group.drop_duplicates()) != 1 for _, group in copies.groupby(keys)):
            raise SystemExit("conflicting structural rows share one campaign key")
        structural = structural.drop_duplicates(keys)
    return structural


def predictors(rows: pd.DataFrame, op: str, cache_bytes: int) -> pd.DataFrame:
    """The candidate variables for one operation type, per trial row.

    Every per-update predictor is divided by the number of updates, the
    response's denominator, so a zero-delta share lowers both sides alike.
    """
    out = pd.DataFrame(index=rows.index)
    n = rows["n"].astype(float)
    structure = rows["structure"]
    updates = rows["updates"].replace(0, np.nan)
    queries = rows["queries"].replace(0, np.nan)
    if op == "update":
        visits = rows["sum_update_visits"] / updates
        visits = visits.where(
            structure != "checkpointing",
            (rows["sum_update_visits"] + rows["sum_checkpoint_update_visits"]) / updates,
        )
        visits = visits.where(structure != "point-only", rows["sum_intersecting"] / updates)
        visits = visits.where(
            structure != "full-copy", (2 * n - 1) * rows["nonzero_updates"] / updates
        )
        out["visited_records"] = visits
        out["allocated_records"] = rows["nodes_per_update"].fillna(0.0)
        out["version_distance_transition"] = 0.0
        out["full_coverage_share"] = rows["full_coverage_updates"] / updates
        out["response"] = rows["update_ns_per_op"]
    else:
        replay = np.where(
            structure == "checkpointing", rows["sum_replay_entries"] / queries, 0.0
        )
        out["visited_records"] = rows["sum_query_visits"] / queries + replay
        out["allocated_records"] = 0.0
        mean_distance = rows["sum_query_version_distance"] / queries
        out["version_distance_transition"] = np.log2(1.0 + mean_distance)
        out["full_coverage_share"] = rows["full_coverage_queries"] / queries
        out["response"] = rows["query_ns_per_op"]
    # Working-set transition: how many cache doublings the retained payload
    # is above the given cache size; zero while it fits.
    out["working_set_transition"] = np.log2(
        np.maximum(1.0, rows["bytes"].astype(float) / cache_bytes)
    )
    out["fit_response"] = np.log(out["response"])
    return out


def design(frame: pd.DataFrame, columns: list[str]) -> np.ndarray:
    return np.column_stack(
        [np.ones(len(frame))] + [frame[c].to_numpy(float) for c in columns]
    )


def fit(train: pd.DataFrame, columns: list[str]) -> tuple[np.ndarray, list[str]]:
    """Least squares on a full-rank design. Constant predictors are dropped
    first; then, while the design is rank-deficient, the earliest predictor
    whose removal restores full rank is dropped (visited_records before
    allocated_records, so the subject keeps its allocation count). The columns
    kept are returned."""
    kept = [c for c in columns if train[c].nunique() > 1]
    while kept:
        X = design(train, kept)
        if np.linalg.matrix_rank(X) == X.shape[1]:
            break
        for candidate in kept:
            trial = [c for c in kept if c != candidate]
            X = design(train, trial)
            if np.linalg.matrix_rank(X) == X.shape[1]:
                kept = trial
                break
        else:
            kept = kept[1:]
    X = design(train, kept)
    y = train["fit_response"].to_numpy(float)
    coefficients, *_ = np.linalg.lstsq(X, y, rcond=None)
    return coefficients, kept


def predict(frame: pd.DataFrame, coefficients: np.ndarray, columns: list[str]) -> np.ndarray:
    return np.exp(design(frame, columns) @ coefficients)


def errors(actual: np.ndarray, predicted: np.ndarray) -> dict[str, float]:
    """Absolute percentage error over every row. A non-positive prediction is
    a miss, not a missing value: it counts with its full error, and the
    number of such rows is reported beside the quantiles. The log ratio is
    over the rows where it is defined."""
    actual = np.asarray(actual, dtype=float)
    predicted = np.asarray(predicted, dtype=float)
    if actual.shape != predicted.shape or actual.ndim != 1:
        raise ValueError("actual and predicted responses must be aligned one-dimensional arrays")
    if not np.isfinite(actual).all() or not np.isfinite(predicted).all():
        raise ValueError("model evaluation contains a non-finite response or prediction")
    if (actual <= 0).any():
        raise ValueError("model evaluation requires strictly positive responses")
    ape = np.abs(predicted - actual) / actual * 100
    positive = predicted > 0
    log_ratio = np.abs(np.log(predicted[positive] / actual[positive]))
    return {
        "rows": int(len(actual)),
        "nonpositive_predictions": int((~positive).sum()),
        "mape_median": float(np.median(ape)),
        "mape_p90": float(np.percentile(ape, 90, method="linear")),
        "log_ratio_median": float(np.median(log_ratio)) if positive.any() else np.nan,
    }


def model_frame(group: pd.DataFrame, op: str, cache_bytes: int) -> pd.DataFrame:
    keys = ["workload", "n", "axis", "variant", "seed"]
    return pd.concat(
        [
            group[keys + ["structure", "partition", "trial", "stream_group"]],
            predictors(group, op, cache_bytes),
        ],
        axis=1,
    ).dropna()


def fit_models(rows: pd.DataFrame, cache_bytes: int) -> list[dict]:
    """Fit only rows assigned to training and return a serializable artifact."""
    if "partition" not in rows or set(rows["partition"].unique()) != {"training"}:
        raise ValueError("fitting accepts training responses only")
    models = []
    for op in ("update", "query"):
        for structure, group in rows.groupby("structure"):
            train = model_frame(group, op, cache_bytes)
            if len(train) < 8:
                continue
            coefficients, kept = fit(train, CANDIDATE_COLUMNS)
            models.append(
                {
                    "op": op,
                    "structure": structure,
                    "columns": kept,
                    "coefficients": [float(value) for value in coefficients],
                    "training_cells": int(
                        train.drop_duplicates(["workload", "n", "axis", "variant"]).shape[0]
                    ),
                    "training_rows": int(len(train)),
                }
            )
    return models


def artifact(
    cache_bytes: int,
    models: list[dict],
    split_salt: str,
    hold_out_share: float,
    partition_manifest_hash: str,
    training_response_hash: str,
) -> dict:
    return {
        "schema_version": 2,
        "response_transform": "natural_log",
        "prediction_inverse": "exp",
        "cache_bytes": int(cache_bytes),
        "split": {
            "salt": split_salt,
            "hold_out_share": hold_out_share,
            "unit": "stream-equivalence group of measurement cells",
        },
        "candidate_columns": CANDIDATE_COLUMNS,
        "training_input": {
            "partition_manifest_sha256": partition_manifest_hash,
            "training_responses_sha256": training_response_hash,
        },
        "models": models,
    }


def artifact_bytes(value: dict) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()


def write_artifact(path: pathlib.Path, value: dict) -> str:
    encoded = artifact_bytes(value)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(encoded)
    digest = hashlib.sha256(encoded).hexdigest()
    path.with_suffix(path.suffix + ".sha256").write_text(f"{digest}  {path.name}\n")
    return digest


def read_artifact(path: pathlib.Path) -> tuple[dict, str]:
    encoded = path.read_bytes()
    value = json.loads(encoded)
    if value.get("schema_version") != 2:
        raise SystemExit(f"unsupported model artifact schema in {path}")
    if value.get("response_transform") != "natural_log":
        raise SystemExit(f"unsupported response transform in {path}")
    if value.get("candidate_columns") != CANDIDATE_COLUMNS:
        raise SystemExit(f"candidate-variable schema does not match {path}")
    training_input = value.get("training_input", {})
    if not training_input.get("partition_manifest_sha256") or not training_input.get(
        "training_responses_sha256"
    ):
        raise SystemExit(f"training-input provenance is missing from {path}")
    split_spec = value.get("split", {})
    if not split_spec.get("salt") or not 0.0 < float(split_spec.get("hold_out_share", 0)) < 1.0:
        raise SystemExit(f"invalid split specification in {path}")
    models = value.get("models", [])
    if not models:
        raise SystemExit(f"model artifact contains no fitted models: {path}")
    for model in models:
        columns = model.get("columns", [])
        coefficients = model.get("coefficients", [])
        if model.get("op") not in ("update", "query"):
            raise SystemExit(f"invalid operation in model artifact: {path}")
        if any(column not in CANDIDATE_COLUMNS for column in columns):
            raise SystemExit(f"unknown predictor in model artifact: {path}")
        if len(coefficients) != len(columns) + 1:
            raise SystemExit(f"coefficient count does not match model form: {path}")
    digest = hashlib.sha256(encoded).hexdigest()
    sidecar = path.with_suffix(path.suffix + ".sha256")
    sidecar_fields = sidecar.read_text().split() if sidecar.is_file() else []
    if sidecar_fields != [digest, path.name]:
        raise SystemExit(f"model artifact checksum is missing or does not match: {sidecar}")
    return value, digest


def evaluate(
    rows: pd.DataFrame, model_artifact: dict
) -> tuple[pd.DataFrame, pd.DataFrame, pd.DataFrame]:
    """Evaluate a previously fixed artifact; this function never refits it.

    H3's registered unit is one equally weighted measurement cell, not one
    trial. The third returned table therefore reduces the possibly different
    trial counts to one actual/predicted pair per
    (workload, n, axis, variant, structure, operation). Both median and mean
    reductions are retained so the registered aggregation sensitivity does
    not need to reopen holdout responses.
    """
    if "partition" not in rows or set(rows["partition"].unique()) != {"holdout"}:
        raise ValueError("evaluation accepts holdout responses only")
    cache_bytes = int(model_artifact["cache_bytes"])
    records = []
    residuals = []
    cell_predictions = []
    for model in model_artifact["models"]:
        op = model["op"]
        structure = model["structure"]
        group = rows[rows["structure"] == structure]
        frame = model_frame(group, op, cache_bytes)
        coefficients = np.asarray(model["coefficients"], dtype=float)
        kept = list(model["columns"])
        part = frame[frame["partition"] == "holdout"]
        if part.empty:
            continue
        predicted = predict(part, coefficients, kept)
        if not np.isfinite(predicted).all():
            raise ValueError(f"{structure} {op} produced a non-finite holdout prediction")
        scored = part.assign(predicted=predicted)
        cell_keys = ["workload", "n", "axis", "variant"]
        cells = (
            scored.groupby(cell_keys, as_index=False, dropna=False)
            .agg(
                trials=("trial", "nunique"),
                actual_median=("response", "median"),
                predicted_median=("predicted", "median"),
                actual_mean=("response", "mean"),
                predicted_mean=("predicted", "mean"),
            )
            .assign(op=op, structure=structure, partition="holdout")
        )
        for aggregation in ("median", "mean"):
            cells[f"ape_{aggregation}"] = (
                np.abs(
                    cells[f"predicted_{aggregation}"] - cells[f"actual_{aggregation}"]
                )
                / cells[f"actual_{aggregation}"]
                * 100
            )
        summary = errors(
            cells["actual_median"].to_numpy(float),
            cells["predicted_median"].to_numpy(float),
        )
        records.append(
            {
                "op": op,
                "structure": structure,
                "partition": "holdout",
                "cells": int(
                    cells[cell_keys].drop_duplicates().shape[0]
                ),
                "trials": int(cells["trials"].sum()),
                **summary,
                "form": "alpha" + "".join(f" + {column}" for column in kept),
                **{
                    f"coef_{column}": value
                    for column, value in zip(["alpha"] + kept, coefficients)
                },
            }
        )
        cell_predictions.append(cells)
        residuals.append(
            cells.groupby("workload")["ape_median"]
            .median()
            .rename("mape_median")
            .reset_index()
            .assign(op=op, structure=structure, partition="holdout")
        )
    table = pd.DataFrame(records)
    diagnostics = pd.concat(residuals, ignore_index=True) if residuals else pd.DataFrame()
    cell_table = (
        pd.concat(cell_predictions, ignore_index=True) if cell_predictions else pd.DataFrame()
    )
    return table, diagnostics, cell_table


def transfer_external(
    rows: pd.DataFrame,
    model_artifact: dict,
    source_structure: str = "copy-on-push",
    target_structure: str = "external",
) -> pd.DataFrame:
    """Apply the in-house copy-on-push model to external WT draws without refitting."""
    rows = rows.copy()
    trace_columns = [
        "trace_seed",
        "trace_operations",
        "trace_update_share",
        "trace_interval_share",
    ]
    required_external_columns = {*trace_columns, "status"}
    missing_trace_columns = sorted(required_external_columns.difference(rows.columns))
    if missing_trace_columns:
        raise ValueError(
            "external transfer is missing registered trace/status metadata: "
            + ", ".join(missing_trace_columns)
        )
    if "partition" not in rows:
        rows["partition"] = "external"
    if "stream_group" not in rows:
        rows["stream_group"] = rows["workload"].astype(str)
    target = rows[rows["structure"] == target_structure]
    if target.empty:
        raise ValueError(f"external transfer has no {target_structure} response rows")
    if (target["status"] != "ok").any():
        raise ValueError("external transfer requires complete status=ok trials")
    if target.duplicated(["workload", "trial"]).any():
        raise ValueError("external transfer contains duplicate draw/trial responses")
    models = [
        model for model in model_artifact["models"] if model["structure"] == source_structure
    ]
    if {model["op"] for model in models} != {"update", "query"}:
        raise ValueError(f"artifact lacks both {source_structure} operation models")

    records = []
    for model in models:
        op = model["op"]
        frame = model_frame(target, op, int(model_artifact["cache_bytes"]))
        predicted = predict(
            frame,
            np.asarray(model["coefficients"], dtype=float),
            list(model["columns"]),
        )
        if not np.isfinite(predicted).all():
            raise ValueError(f"external {op} transfer produced a non-finite prediction")
        scored = frame.assign(predicted=predicted)
        for draw_id, group in scored.groupby("workload", sort=False):
            actual = group["response"].to_numpy(float)
            prediction = group["predicted"].to_numpy(float)
            errors(actual, prediction)
            actual_cell = float(np.median(actual))
            predicted_cell = float(np.median(prediction))
            source_rows = target[target["workload"] == draw_id]
            metadata = source_rows[["n", *trace_columns]].drop_duplicates()
            if len(metadata) != 1:
                raise ValueError(f"external draw {draw_id} has inconsistent trace metadata")
            trace = metadata.iloc[0]
            records.append(
                {
                    "draw_id": str(draw_id),
                    "seed": int(trace["trace_seed"]),
                    "n": int(trace["n"]),
                    "operations": int(trace["trace_operations"]),
                    "update_share": float(trace["trace_update_share"]),
                    "interval_share": float(trace["trace_interval_share"]),
                    "op": op,
                    "trials": int(group["trial"].nunique()),
                    "actual": actual_cell,
                    "predicted": predicted_cell,
                    "ape": abs(predicted_cell - actual_cell) / actual_cell * 100,
                    "source_model": source_structure,
                    "target_structure": target_structure,
                }
            )
    result = pd.DataFrame(records)
    if result.duplicated(["draw_id", "op"]).any():
        raise ValueError("external transfer produced duplicate draw/operation cells")
    return result


EXTERNAL_TRACE_COLUMNS = {
    "seed": "trace_seed",
    "operations": "trace_operations",
    "update_share": "trace_update_share",
    "interval_share": "trace_interval_share",
}


def external_responses(
    runs: pd.DataFrame, structural: pd.DataFrame, draws: pd.DataFrame
) -> pd.DataFrame:
    """The H5 transfer input: the external adapter's trace-phase trials joined
    to their structural counts and to the registered draw parameters.

    Fails closed on a registered draw with no trials, a trial under an
    unregistered draw, a trial without counts for its seed, or a domain size
    that disagrees with the registry. Trial status is kept, not filtered:
    ``transfer_external`` is the one place that requires complete trials.
    """
    keys = ["workload", "n", "axis", "variant", "seed"]
    target = runs[runs["structure"] == "external"].copy()
    if target.empty:
        raise ValueError("no external-adapter trials in the trace phase")
    registered = set(draws["draw_id"].astype(str))
    present = set(target["workload"].astype(str))
    if registered - present:
        raise ValueError(
            "registered trace draws without external trials: "
            + ", ".join(sorted(registered - present))
        )
    if present - registered:
        raise ValueError(
            "external trials under unregistered draws: " + ", ".join(sorted(present - registered))
        )
    rows = target.merge(
        structural.drop(columns=["k"]),
        on=keys,
        how="left",
        suffixes=("", "_structural"),
        validate="many_to_one",
        indicator=True,
    )
    if (rows["_merge"] != "both").any():
        raise ValueError("external trials without structural counts for their seed")
    rows = rows.drop(columns=["_merge"])
    registry = draws.rename(columns={"draw_id": "workload", "n": "trace_n", **EXTERNAL_TRACE_COLUMNS})
    registry = registry[["workload", "trace_n", *EXTERNAL_TRACE_COLUMNS.values()]]
    registry["workload"] = registry["workload"].astype(str)
    rows["workload"] = rows["workload"].astype(str)
    rows = rows.merge(registry, on="workload", how="left", validate="many_to_one")
    if (rows["n"] != rows["trace_n"]).any():
        raise ValueError("external trial domain size disagrees with the registered draw")
    rows = rows.drop(columns=["trace_n"])
    rows["partition"] = "external"
    rows["stream_group"] = rows["workload"]
    return rows


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_csv_artifact(frame: pd.DataFrame, path: pathlib.Path) -> str:
    """Write a generated CSV and a checksum sidecar consumed by decisions."""
    frame.to_csv(path, index=False)
    digest = sha256_file(path)
    path.with_suffix(path.suffix + ".sha256").write_text(f"{digest}  {path.name}\n")
    return digest


def prepare_membership(
    structural_directory: pathlib.Path, split_salt: str, hold_out_share: float
) -> pd.DataFrame:
    """Assign cells from predictor-only structural data, before response access."""
    structural = split.add_stream_groups(load_structural(structural_directory))
    structural = split.assign(
        structural,
        salt=split_salt,
        hold_out_share=hold_out_share,
        group_column="stream_group",
    )
    keys = ["workload", "n", "axis", "variant", "seed"]
    membership = structural[keys + ["stream_group", "partition"]].drop_duplicates()
    if membership.duplicated(keys).any():
        raise SystemExit("one structural campaign key received multiple split memberships")
    return structural


def write_partition_manifest(path: pathlib.Path, value: dict) -> str:
    encoded = artifact_bytes(value)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(encoded)
    return hashlib.sha256(encoded).hexdigest()


def prepare_response_partitions(
    raw: pathlib.Path,
    structural_directory: pathlib.Path,
    output_directory: pathlib.Path,
    split_salt: str,
    hold_out_share: float,
    cache_bytes: int,
    capped_cells: pd.DataFrame,
) -> tuple[pathlib.Path, str]:
    """Write disjoint response files after predictor-only membership is fixed.

    ``capped_cells`` is the registered pilot-capped structure-cell list. Its
    holdout members are recorded in the manifest so that every structure's
    H3 holdout inventory is fixed prospectively as the holdout cells minus
    that structure's registered caps.
    """
    # This call completes every membership decision without opening a timing
    # CSV. Keep it before data.load_runs: the order is part of the holdout
    # contract and is covered by the integration test.
    structural = prepare_membership(structural_directory, split_salt, hold_out_share)

    runs = data.load_runs(raw, "timing")
    runs = runs[runs["complete"]]
    keys = ["workload", "n", "axis", "variant", "seed"]
    rows = runs.merge(
        structural.drop(columns=["k"]),
        on=keys,
        how="left",
        suffixes=("", "_structural"),
        validate="many_to_one",
        indicator=True,
    )
    if rows.empty:
        raise SystemExit(
            "runs and structural counts share no (workload, n, axis, variant, seed) rows"
        )
    if (rows["_merge"] != "both").any():
        raise SystemExit("timing responses exist without predictor-only split membership")
    rows = rows.drop(columns=["_merge"])
    if set(rows["partition"].unique()) != {"training", "holdout"}:
        raise SystemExit("prepared responses must contain both training and holdout partitions")

    output_directory.mkdir(parents=True, exist_ok=True)
    files = {}
    for partition, filename in (
        ("training", TRAINING_RESPONSES_NAME),
        ("holdout", HOLDOUT_RESPONSES_NAME),
    ):
        destination = output_directory / filename
        selected = rows[rows["partition"] == partition]
        selected.to_csv(destination, index=False)
        files[partition] = {
            "file": filename,
            "sha256": sha256_file(destination),
            "rows": int(len(selected)),
            "cells": int(
                selected.drop_duplicates(["workload", "n", "axis", "variant"]).shape[0]
            ),
        }

    expected_holdout = (
        structural[structural["partition"] == "holdout"]
        [["workload", "n", "axis", "variant"]]
        .drop_duplicates()
        .sort_values(["workload", "n", "axis", "variant"])
    )
    expected_holdout["n"] = expected_holdout["n"].astype(int)
    expected_holdout["variant"] = expected_holdout["variant"].astype(float)
    # The capped registry carries no axis column, so (workload, n, variant)
    # must identify one holdout cell; the workload inventory fixes one axis per
    # workload and this check records that invariant.
    if expected_holdout.duplicated(["workload", "n", "variant"]).any():
        raise SystemExit("holdout cells are not identified by (workload, n, variant)")
    capped_holdout = registered_capped_cells(capped_cells).merge(
        expected_holdout[["workload", "n", "variant"]], on=["workload", "n", "variant"]
    )
    manifest = {
        "schema_version": 3,
        "cache_bytes": int(cache_bytes),
        "split": {
            "salt": split_salt,
            "hold_out_share": hold_out_share,
            "unit": "stream-equivalence group of measurement cells",
        },
        "expected_holdout_cells": expected_holdout.to_dict("records"),
        "registered_capped_holdout_cells": capped_holdout.sort_values(CAPPED_COLUMNS)
        .to_dict("records"),
        "responses": files,
    }
    manifest_path = output_directory / PARTITION_MANIFEST_NAME
    manifest_hash = write_partition_manifest(manifest_path, manifest)
    return manifest_path, manifest_hash


def registered_capped_cells(cells: pd.DataFrame) -> pd.DataFrame:
    """Normalize the registered capped structure-cell list; reject duplicates."""
    if not set(CAPPED_COLUMNS).issubset(cells.columns):
        raise SystemExit("capped-cell registry must carry workload, n, variant and structure")
    normalized = cells[CAPPED_COLUMNS].assign(
        workload=cells["workload"].astype(str),
        n=cells["n"].astype(int),
        variant=cells["variant"].astype(float),
        structure=cells["structure"].astype(str),
    )
    if normalized.duplicated().any():
        raise SystemExit("capped-cell registry contains a duplicate structure-cell")
    return normalized.reset_index(drop=True)


def read_partition_manifest(path: pathlib.Path) -> tuple[dict, str]:
    encoded = path.read_bytes()
    value = json.loads(encoded)
    if value.get("schema_version") != 3:
        raise SystemExit(f"unsupported response-partition manifest schema in {path}")
    if int(value.get("cache_bytes", 0)) <= 0:
        raise SystemExit(f"invalid cache size in {path}")
    split_spec = value.get("split", {})
    if not split_spec.get("salt") or not 0.0 < float(split_spec.get("hold_out_share", 0)) < 1.0:
        raise SystemExit(f"invalid split specification in {path}")
    expected = pd.DataFrame(value.get("expected_holdout_cells", []))
    expected_columns = ["workload", "n", "axis", "variant"]
    if expected.empty or set(expected.columns) != set(expected_columns):
        raise SystemExit(f"expected holdout-cell inventory is missing from {path}")
    if expected.duplicated(expected_columns).any():
        raise SystemExit(f"expected holdout-cell inventory contains duplicates in {path}")
    if "registered_capped_holdout_cells" not in value:
        raise SystemExit(f"registered capped holdout inventory is missing from {path}")
    capped = pd.DataFrame(value["registered_capped_holdout_cells"], columns=CAPPED_COLUMNS)
    if capped.duplicated().any():
        raise SystemExit(f"registered capped holdout inventory contains duplicates in {path}")
    if not capped.empty:
        outside = capped.merge(
            expected, on=["workload", "n", "variant"], how="left", indicator=True
        )
        if (outside["_merge"] != "both").any():
            raise SystemExit(f"registered capped holdout cell is outside the holdout in {path}")
    responses = value.get("responses", {})
    for partition in ("training", "holdout"):
        entry = responses.get(partition, {})
        if not entry.get("file") or not entry.get("sha256") or int(entry.get("rows", 0)) <= 0:
            raise SystemExit(f"invalid {partition} response entry in {path}")
        if pathlib.Path(entry["file"]).name != entry["file"]:
            raise SystemExit(f"response path must be a filename in {path}")
    return value, hashlib.sha256(encoded).hexdigest()


def load_prepared_responses(
    manifest_path: pathlib.Path, manifest: dict, partition: str
) -> tuple[pd.DataFrame, str]:
    entry = manifest["responses"][partition]
    response_path = manifest_path.parent / entry["file"]
    digest = sha256_file(response_path)
    if digest != entry["sha256"]:
        raise SystemExit(f"{partition} response checksum does not match: {response_path}")
    rows = pd.read_csv(response_path)
    if len(rows) != int(entry["rows"]):
        raise SystemExit(f"{partition} response row count does not match: {response_path}")
    if "partition" not in rows or set(rows["partition"].unique()) != {partition}:
        raise SystemExit(f"{partition} response file contains another partition: {response_path}")
    return rows, digest


def fit_from_manifest(manifest_path: pathlib.Path, model_path: pathlib.Path) -> tuple[dict, str]:
    """Fit from the training file only; never resolves or opens the holdout file."""
    manifest, manifest_hash = read_partition_manifest(manifest_path)
    training_rows, training_hash = load_prepared_responses(
        manifest_path, manifest, "training"
    )
    split_spec = manifest["split"]
    model_artifact = artifact(
        int(manifest["cache_bytes"]),
        fit_models(training_rows, int(manifest["cache_bytes"])),
        str(split_spec["salt"]),
        float(split_spec["hold_out_share"]),
        manifest_hash,
        training_hash,
    )
    if not model_artifact["models"]:
        raise SystemExit("training responses produced no fitted models")
    model_hash = write_artifact(model_path, model_artifact)
    return model_artifact, model_hash


def evaluate_from_manifest(
    manifest_path: pathlib.Path, model_path: pathlib.Path
) -> tuple[pd.DataFrame, pd.DataFrame, pd.DataFrame, str, int]:
    # Verify and hash the fitted artifact before any holdout-response path is
    # resolved. This is the one holdout-opening point in the analysis.
    model_artifact, model_hash = read_artifact(model_path)
    manifest, manifest_hash = read_partition_manifest(manifest_path)
    if model_artifact["training_input"]["partition_manifest_sha256"] != manifest_hash:
        raise SystemExit("model artifact was fitted from a different partition manifest")
    if model_artifact["split"] != manifest["split"]:
        raise SystemExit("model artifact and response manifest use different split specifications")
    if int(model_artifact["cache_bytes"]) != int(manifest["cache_bytes"]):
        raise SystemExit("model artifact and response manifest use different cache sizes")
    if (
        model_artifact["training_input"]["training_responses_sha256"]
        != manifest["responses"]["training"]["sha256"]
    ):
        raise SystemExit("model artifact training-response hash does not match the manifest")
    holdout_rows, _ = load_prepared_responses(manifest_path, manifest, "holdout")
    # A registered-capped structure-cell's result is the cap itself: it is
    # excluded from that structure's H3 inventory whether or not it completed.
    capped = pd.DataFrame(manifest["registered_capped_holdout_cells"], columns=CAPPED_COLUMNS)
    if not capped.empty:
        flagged = holdout_rows.merge(
            capped.assign(_capped=True), on=CAPPED_COLUMNS, how="left"
        )
        holdout_rows = holdout_rows[flagged["_capped"].isna().to_numpy()]
    table, diagnostics, cells = evaluate(holdout_rows, model_artifact)
    if table.empty:
        raise SystemExit("holdout responses produced no model-evaluation rows")
    expected = pd.DataFrame(manifest["expected_holdout_cells"])
    cell_keys = ["workload", "n", "axis", "variant"]
    observed_keys = cells[cell_keys].drop_duplicates()
    unexpected = observed_keys.merge(expected, on=cell_keys, how="left", indicator=True)
    if (unexpected["_merge"] != "both").any():
        raise SystemExit("model evaluation produced a cell outside the registered holdout")
    inventory_hash = hashlib.sha256(
        artifact_bytes(
            {
                "expected_holdout_cells": manifest["expected_holdout_cells"],
                "registered_capped_holdout_cells": manifest["registered_capped_holdout_cells"],
            }
        )
    ).hexdigest()
    capped_per_structure = capped.groupby("structure").size() if not capped.empty else {}
    cells["expected_cell_count"] = [
        int(len(expected)) - int(capped_per_structure.get(structure, 0))
        for structure in cells["structure"]
    ]
    cells["expected_inventory_sha256"] = inventory_hash
    return table, diagnostics, cells, model_hash, int(model_artifact["cache_bytes"])


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--cache-bytes",
        type=int,
        default=None,
        help="cache size for the working-set transition; prepare defaults to the campaign capture",
    )
    parser.add_argument("--raw", type=pathlib.Path, default=RAW)
    parser.add_argument(
        "--structural",
        type=pathlib.Path,
        default=None,
        help="directory containing structural CSVs; default: --raw",
    )
    parser.add_argument(
        "--summary", type=pathlib.Path, default=SUMMARY, help="generated output directory"
    )
    parser.add_argument(
        "--stage",
        choices=("prepare", "fit", "evaluate", "external", "transfer", "pilot"),
        required=True,
        help=(
            "prepare disjoint responses, fit training only, evaluate holdout, build the external "
            "(H5) responses from the trace phase, transfer the fixed model, or run all three H3 "
            "stages for a pilot"
        ),
    )
    parser.add_argument(
        "--h5-draws",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[1] / "h5_trace_draws.csv",
        help="registered external draw parameters joined by the external stage",
    )
    parser.add_argument(
        "--model-artifact",
        type=pathlib.Path,
        default=None,
        help="artifact to write in fit/pilot mode or verify and read in evaluate mode",
    )
    parser.add_argument(
        "--partition-directory",
        type=pathlib.Path,
        default=None,
        help="directory for disjoint response files and their manifest",
    )
    parser.add_argument(
        "--partition-manifest",
        type=pathlib.Path,
        default=None,
        help="prepared response manifest; defaults inside --partition-directory",
    )
    parser.add_argument(
        "--split-salt",
        default=split.PILOT_SALT,
        help="training/holdout salt fixed by the protocol",
    )
    parser.add_argument(
        "--hold-out-share",
        type=float,
        default=split.HOLD_OUT_SHARE,
        help="holdout fraction fixed by the protocol",
    )
    parser.add_argument(
        "--output-stem",
        default="cost_model",
        help="neutral stem for evaluation and residual CSV outputs",
    )
    parser.add_argument(
        "--analysis-label",
        default="fixed model holdout evaluation",
        help="context printed with the evaluation (for example, exploratory pilot)",
    )
    parser.add_argument(
        "--transfer-responses",
        type=pathlib.Path,
        help="external response/predictor rows from --stage external, required by transfer",
    )
    parser.add_argument(
        "--capped-cells",
        type=pathlib.Path,
        default=CAPPED_CELLS,
        help="registered pilot-capped structure-cell list excluded from H3 inventories",
    )
    args = parser.parse_args(argv)
    if not 0.0 < args.hold_out_share < 1.0:
        parser.error("--hold-out-share must be strictly between zero and one")
    structural_directory = args.structural or args.raw
    partition_directory = args.partition_directory or args.summary / "cost_model_inputs"
    manifest_path = args.partition_manifest or partition_directory / PARTITION_MANIFEST_NAME
    model_path = args.model_artifact or args.summary / "cost_model_fit.json"

    if args.stage == "external":
        rows = external_responses(
            data.load_runs(args.raw, "trace"),
            load_structural(structural_directory, "structural*-WT*.csv"),
            pd.read_csv(args.h5_draws),
        )
        args.summary.mkdir(parents=True, exist_ok=True)
        destination = args.summary / f"{args.output_stem}_external_responses.csv"
        output_hash = write_csv_artifact(rows, destination)
        print(f"{len(rows)} external trial rows -> {destination} (sha256 {output_hash})")
        return

    if args.stage == "transfer":
        if args.transfer_responses is None:
            parser.error("--stage transfer requires --transfer-responses")
        # The external stage writes this file. When that stage failed closed,
        # the locked script still reaches transfer, and a study record deserves
        # a stated reason rather than a pandas traceback.
        if not args.transfer_responses.exists():
            raise SystemExit(
                f"missing external responses: {args.transfer_responses}; "
                "the external stage has to succeed before the transfer stage"
            )
        model_artifact, model_hash = read_artifact(model_path)
        cells = transfer_external(pd.read_csv(args.transfer_responses), model_artifact)
        cells["model_artifact_sha256"] = model_hash
        cells["transfer_responses_sha256"] = sha256_file(args.transfer_responses)
        args.summary.mkdir(parents=True, exist_ok=True)
        destination = args.summary / f"{args.output_stem}_external_cells.csv"
        output_hash = write_csv_artifact(cells, destination)
        print(f"fixed model artifact sha256: {model_hash}")
        print(
            f"{len(cells)} external draw-operation cells -> {destination} "
            f"(sha256 {output_hash})"
        )
        return

    if args.stage in ("prepare", "pilot"):
        cache_bytes = args.cache_bytes or captured_cache_bytes(args.raw)
        if cache_bytes is None or cache_bytes <= 0:
            raise SystemExit("cache size is missing; pass --cache-bytes or capture l2_bytes")
        manifest_path, manifest_hash = prepare_response_partitions(
            args.raw,
            structural_directory,
            partition_directory,
            args.split_salt,
            args.hold_out_share,
            cache_bytes,
            pd.read_csv(args.capped_cells),
        )
        print(f"prepared disjoint response manifest: {manifest_path} (sha256 {manifest_hash})")
        if args.stage == "prepare":
            return

    if args.stage in ("fit", "pilot"):
        _, model_hash = fit_from_manifest(manifest_path, model_path)
        print(f"fixed model artifact: {model_path} (sha256 {model_hash})")
        if args.stage == "fit":
            return

    table, diagnostics, cells, model_hash, cache_bytes = evaluate_from_manifest(
        manifest_path, model_path
    )
    cells["model_artifact_sha256"] = model_hash
    args.summary.mkdir(parents=True, exist_ok=True)
    table.to_csv(args.summary / f"{args.output_stem}_evaluation.csv", index=False)
    diagnostics.to_csv(args.summary / f"{args.output_stem}_residuals.csv", index=False)
    cells_path = args.summary / f"{args.output_stem}_cells.csv"
    cells_hash = write_csv_artifact(cells, cells_path)

    pd.set_option("display.width", 200)
    print(args.analysis_label)
    print(f"fixed model artifact sha256: {model_hash}")
    print(f"cell predictions sha256: {cells_hash}")
    print(f"cache bytes for the working-set transition: {cache_bytes}\n")
    shown = [
        "op",
        "structure",
        "partition",
        "cells",
        "rows",
        "nonpositive_predictions",
        "mape_median",
        "mape_p90",
        "log_ratio_median",
        "form",
    ]
    print(table[shown].to_string(index=False, float_format=lambda v: f"{v:.1f}"))
    if not diagnostics.empty:
        worst = (
            diagnostics[diagnostics["partition"] == "holdout"]
            .sort_values("mape_median", ascending=False)
            .head(12)
        )
        print("\nlargest hold-out residuals by workload:")
        print(worst.to_string(index=False, float_format=lambda v: f"{v:.1f}"))


if __name__ == "__main__":
    main()
