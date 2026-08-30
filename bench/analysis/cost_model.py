"""Pilot-only development of the mechanistic time model.

    uv run --project bench/analysis bench/analysis/cost_model.py [--cache-bytes N]

Fits, for every structure and for updates and queries separately,

    log(ns_per_op) = alpha + beta * visited_records + gamma * allocated_records
                     + theta * working_set_transition
                     + lambda * version_distance_transition
                     + phi * full_coverage_share

on the training cells of the exploratory pilot and reports the error on the
pilot's hold-out cells. bytes_touched of the plan's form is a fixed multiple
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

This is model development, not the confirmatory evaluation: the pilot is one
machine and one compiler, the split salt is the pilot salt, and the hold-out
numbers here inform the thresholds the registered protocol (PR6) freezes. They
are not the H3 result. Outputs go to bench/results/summary/, which is not
under version control.
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


def load_structural(directory: pathlib.Path) -> pd.DataFrame:
    frames = []
    for path in sorted(directory.glob("structural*-W*.csv")):
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
    ape = np.abs(predicted - actual) / actual * 100
    positive = predicted > 0
    log_ratio = np.abs(np.log(predicted[positive] / actual[positive]))
    return {
        "rows": int(len(actual)),
        "nonpositive_predictions": int((~positive).sum()),
        "mape_median": float(np.median(ape)),
        "mape_p90": float(np.percentile(ape, 90)),
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
    models = []
    for op in ("update", "query"):
        for structure, group in rows.groupby("structure"):
            train = model_frame(group, op, cache_bytes)
            train = train[train["partition"] == "training"]
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


def artifact(cache_bytes: int, models: list[dict], split_salt: str, hold_out_share: float) -> dict:
    return {
        "schema_version": 1,
        "response_transform": "natural_log",
        "prediction_inverse": "exp",
        "cache_bytes": int(cache_bytes),
        "split": {
            "salt": split_salt,
            "hold_out_share": hold_out_share,
            "unit": "stream-equivalence group of measurement cells",
        },
        "candidate_columns": CANDIDATE_COLUMNS,
        "models": models,
    }


def artifact_bytes(value: dict) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()


def write_artifact(path: pathlib.Path, value: dict) -> str:
    encoded = artifact_bytes(value)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(encoded)
    return hashlib.sha256(encoded).hexdigest()


def read_artifact(path: pathlib.Path) -> tuple[dict, str]:
    encoded = path.read_bytes()
    value = json.loads(encoded)
    if value.get("schema_version") != 1:
        raise SystemExit(f"unsupported model artifact schema in {path}")
    if value.get("response_transform") != "natural_log":
        raise SystemExit(f"unsupported response transform in {path}")
    if value.get("candidate_columns") != CANDIDATE_COLUMNS:
        raise SystemExit(f"candidate-variable schema does not match {path}")
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
    return value, hashlib.sha256(encoded).hexdigest()


def evaluate(rows: pd.DataFrame, model_artifact: dict) -> tuple[pd.DataFrame, pd.DataFrame]:
    """Evaluate a previously fixed artifact; this function never refits it."""
    cache_bytes = int(model_artifact["cache_bytes"])
    records = []
    residuals = []
    for model in model_artifact["models"]:
        op = model["op"]
        structure = model["structure"]
        group = rows[rows["structure"] == structure]
        frame = model_frame(group, op, cache_bytes)
        coefficients = np.asarray(model["coefficients"], dtype=float)
        kept = list(model["columns"])
        for name in ("training", "holdout"):
            part = frame[frame["partition"] == name]
            if part.empty:
                continue
            predicted = predict(part, coefficients, kept)
            summary = errors(part["response"].to_numpy(float), predicted)
            records.append(
                {
                    "op": op,
                    "structure": structure,
                    "partition": name,
                    "cells": int(
                        part.drop_duplicates(["workload", "n", "axis", "variant"]).shape[0]
                    ),
                    **summary,
                    "form": "alpha" + "".join(f" + {column}" for column in kept),
                    **{
                        f"coef_{column}": value
                        for column, value in zip(["alpha"] + kept, coefficients)
                    },
                }
            )
            scored = part.assign(
                predicted=predicted,
                ape=np.abs(predicted - part["response"]) / part["response"] * 100,
            )
            residuals.append(
                scored.groupby("workload")["ape"]
                .median()
                .rename("mape_median")
                .reset_index()
                .assign(op=op, structure=structure, partition=name)
            )
    table = pd.DataFrame(records)
    diagnostics = pd.concat(residuals, ignore_index=True) if residuals else pd.DataFrame()
    return table, diagnostics


def load_rows(
    raw: pathlib.Path,
    structural_directory: pathlib.Path,
    split_salt: str,
    hold_out_share: float,
) -> pd.DataFrame:
    structural = split.add_stream_groups(load_structural(structural_directory))
    runs = data.load_runs(raw, "timing")
    runs = runs[runs["complete"]]
    keys = ["workload", "n", "axis", "variant", "seed"]
    rows = runs.merge(
        structural.drop(columns=["k"]), on=keys, how="inner", suffixes=("", "_structural")
    )
    if rows.empty:
        raise SystemExit(
            "runs and structural counts share no (workload, n, axis, variant, seed) rows"
        )
    return split.assign(
        rows,
        salt=split_salt,
        hold_out_share=hold_out_share,
        group_column="stream_group",
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--cache-bytes",
        type=int,
        default=None,
        help="cache size for the working-set transition; default: l2_bytes from the "
        "campaign's system capture, else 16 MiB",
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
        choices=("pilot", "fit", "evaluate"),
        default="pilot",
        help="fit without holdout evaluation, evaluate a fixed artifact, or do both for the pilot",
    )
    parser.add_argument(
        "--model-artifact",
        type=pathlib.Path,
        default=None,
        help="artifact to write in fit/pilot mode or read in evaluate mode",
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
    args = parser.parse_args()
    if not 0.0 < args.hold_out_share < 1.0:
        parser.error("--hold-out-share must be strictly between zero and one")
    cache_bytes = args.cache_bytes or captured_cache_bytes(args.raw) or 16 * 1024 * 1024
    structural_directory = args.structural or args.raw
    model_path = args.model_artifact or args.summary / "cost_model_pilot_fit.json"

    if args.stage == "evaluate":
        model_artifact, model_hash = read_artifact(model_path)
        if int(model_artifact["cache_bytes"]) != cache_bytes:
            raise SystemExit("--cache-bytes does not match the fixed model artifact")
        split_spec = model_artifact["split"]
        rows = load_rows(
            args.raw,
            structural_directory,
            str(split_spec["salt"]),
            float(split_spec["hold_out_share"]),
        )
    else:
        rows = load_rows(
            args.raw, structural_directory, args.split_salt, args.hold_out_share
        )
        # The fitting function receives only the training partition. Holdout
        # responses remain outside the model-selection call and are used only
        # by the explicit pilot/evaluate stage below.
        training_rows = rows[rows["partition"] == "training"]
        model_artifact = artifact(
            cache_bytes,
            fit_models(training_rows, cache_bytes),
            args.split_salt,
            args.hold_out_share,
        )
        model_hash = write_artifact(model_path, model_artifact)
        print(f"fixed model artifact: {model_path} (sha256 {model_hash})")
        if args.stage == "fit":
            return

    table, diagnostics = evaluate(rows, model_artifact)
    args.summary.mkdir(parents=True, exist_ok=True)
    table.to_csv(args.summary / "cost_model_pilot.csv", index=False)
    diagnostics.to_csv(args.summary / "cost_model_pilot_residuals.csv", index=False)

    pd.set_option("display.width", 200)
    print("exploratory pilot, one machine; not the registered hold-out evaluation")
    print(f"fixed model artifact sha256: {model_hash}")
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
