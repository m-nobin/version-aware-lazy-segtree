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


def evaluate(rows: pd.DataFrame, model_artifact: dict) -> tuple[pd.DataFrame, pd.DataFrame]:
    """Evaluate a previously fixed artifact; this function never refits it."""
    if "partition" not in rows or set(rows["partition"].unique()) != {"holdout"}:
        raise ValueError("evaluation accepts holdout responses only")
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
        part = frame[frame["partition"] == "holdout"]
        if part.empty:
            continue
        predicted = predict(part, coefficients, kept)
        summary = errors(part["response"].to_numpy(float), predicted)
        records.append(
            {
                "op": op,
                "structure": structure,
                "partition": "holdout",
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
            .assign(op=op, structure=structure, partition="holdout")
        )
    table = pd.DataFrame(records)
    diagnostics = pd.concat(residuals, ignore_index=True) if residuals else pd.DataFrame()
    return table, diagnostics


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


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
) -> tuple[pathlib.Path, str]:
    """Write disjoint response files after predictor-only membership is fixed."""
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

    manifest = {
        "schema_version": 1,
        "cache_bytes": int(cache_bytes),
        "split": {
            "salt": split_salt,
            "hold_out_share": hold_out_share,
            "unit": "stream-equivalence group of measurement cells",
        },
        "responses": files,
    }
    manifest_path = output_directory / PARTITION_MANIFEST_NAME
    manifest_hash = write_partition_manifest(manifest_path, manifest)
    return manifest_path, manifest_hash


def read_partition_manifest(path: pathlib.Path) -> tuple[dict, str]:
    encoded = path.read_bytes()
    value = json.loads(encoded)
    if value.get("schema_version") != 1:
        raise SystemExit(f"unsupported response-partition manifest schema in {path}")
    if int(value.get("cache_bytes", 0)) <= 0:
        raise SystemExit(f"invalid cache size in {path}")
    split_spec = value.get("split", {})
    if not split_spec.get("salt") or not 0.0 < float(split_spec.get("hold_out_share", 0)) < 1.0:
        raise SystemExit(f"invalid split specification in {path}")
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
) -> tuple[pd.DataFrame, pd.DataFrame, str, int]:
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
    table, diagnostics = evaluate(holdout_rows, model_artifact)
    if table.empty:
        raise SystemExit("holdout responses produced no model-evaluation rows")
    return table, diagnostics, model_hash, int(model_artifact["cache_bytes"])


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
        choices=("prepare", "fit", "evaluate", "pilot"),
        required=True,
        help="prepare disjoint responses, fit training only, evaluate holdout, or run all for a pilot",
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
    args = parser.parse_args(argv)
    if not 0.0 < args.hold_out_share < 1.0:
        parser.error("--hold-out-share must be strictly between zero and one")
    structural_directory = args.structural or args.raw
    partition_directory = args.partition_directory or args.summary / "cost_model_inputs"
    manifest_path = args.partition_manifest or partition_directory / PARTITION_MANIFEST_NAME
    model_path = args.model_artifact or args.summary / "cost_model_fit.json"

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
        )
        print(f"prepared disjoint response manifest: {manifest_path} (sha256 {manifest_hash})")
        if args.stage == "prepare":
            return

    if args.stage in ("fit", "pilot"):
        _, model_hash = fit_from_manifest(manifest_path, model_path)
        print(f"fixed model artifact: {model_path} (sha256 {model_hash})")
        if args.stage == "fit":
            return

    table, diagnostics, model_hash, cache_bytes = evaluate_from_manifest(manifest_path, model_path)
    args.summary.mkdir(parents=True, exist_ok=True)
    table.to_csv(args.summary / f"{args.output_stem}_evaluation.csv", index=False)
    diagnostics.to_csv(args.summary / f"{args.output_stem}_residuals.csv", index=False)

    pd.set_option("display.width", 200)
    print(args.analysis_label)
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
