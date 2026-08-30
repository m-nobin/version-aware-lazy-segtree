"""Pilot-only development of the mechanistic time model.

    uv run --project bench/analysis bench/analysis/cost_model.py [--cache-bytes N]

Fits, for every structure and for updates and queries separately,

    ns_per_op = alpha + beta * visited_records + gamma * allocated_records
                + theta * working_set_transition

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
from the runs CSV (nodes stored per update, retained bytes).

This is model development, not the confirmatory evaluation: the pilot is one
machine and one compiler, the split salt is the pilot salt, and the hold-out
numbers here inform the thresholds the registered protocol (PR6) freezes. They
are not the H3 result. Outputs go to bench/results/summary/, which is not
under version control.
"""

from __future__ import annotations

import argparse
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


def captured_cache_bytes(raw: pathlib.Path) -> int | None:
    """The l2_bytes line of the first system capture, if the campaign wrote one."""
    for path in sorted(raw.glob("system_*.txt")):
        for line in path.read_text().splitlines():
            if line.startswith("l2_bytes=") and line.split("=", 1)[1].strip():
                return int(line.split("=", 1)[1])
    return None


def load_structural(raw: pathlib.Path) -> pd.DataFrame:
    frames = []
    for path in sorted(raw.glob("structural*-W*.csv")):
        try:
            frames.append(pd.read_csv(path).dropna())
        except pd.errors.EmptyDataError:
            continue
    if not frames:
        raise SystemExit(
            f"no structural counts under {raw}; run valseg_bench --structural with the "
            "campaign's --seed, --warmup and --trials first"
        )
    return pd.concat(frames, ignore_index=True)


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
        visits = visits.where(structure != "point-only", rows["sum_intersecting"] / updates)
        visits = visits.where(
            structure != "full-copy", (2 * n - 1) * rows["nonzero_updates"] / updates
        )
        out["visited_records"] = visits
        out["allocated_records"] = rows["nodes_per_update"].fillna(0.0)
        out["response"] = rows["update_ns_per_op"]
    else:
        replay = np.where(
            structure == "checkpointing", rows["sum_replay_entries"] / queries, 0.0
        )
        out["visited_records"] = rows["sum_query_visits"] / queries + replay
        out["allocated_records"] = 0.0
        out["response"] = rows["query_ns_per_op"]
    # Working-set transition: how many cache doublings the retained payload
    # is above the given cache size; zero while it fits.
    out["working_set_transition"] = np.log2(
        np.maximum(1.0, rows["bytes"].astype(float) / cache_bytes)
    )
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
    y = train["response"].to_numpy(float)
    coefficients, *_ = np.linalg.lstsq(X, y, rcond=None)
    return coefficients, kept


def predict(frame: pd.DataFrame, coefficients: np.ndarray, columns: list[str]) -> np.ndarray:
    return design(frame, columns) @ coefficients


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
    args = parser.parse_args()
    cache_bytes = args.cache_bytes or captured_cache_bytes(args.raw) or 16 * 1024 * 1024

    runs = data.load_runs(args.raw, "timing")
    runs = runs[runs["complete"]]
    structural = load_structural(args.raw)
    keys = ["workload", "n", "axis", "variant", "seed"]
    rows = runs.merge(
        structural.drop(columns=["k"]), on=keys, how="inner", suffixes=("", "_structural")
    )
    if rows.empty:
        raise SystemExit(
            "runs and structural counts share no (workload, n, axis, variant, seed) rows"
        )
    rows = split.assign(rows)

    columns = ["visited_records", "allocated_records", "working_set_transition"]
    records = []
    residuals = []
    for op in ("update", "query"):
        for structure, group in rows.groupby("structure"):
            frame = pd.concat(
                [group[keys + ["structure", "partition", "trial"]],
                 predictors(group, op, cache_bytes)],
                axis=1,
            ).dropna()
            train = frame[frame["partition"] == "training"]
            hold = frame[frame["partition"] == "holdout"]
            if len(train) < 8 or hold.empty:
                continue
            coefficients, kept = fit(train, columns)
            training_cells = train.drop_duplicates(keys[:4]).shape[0]
            holdout_cells = hold.drop_duplicates(keys[:4]).shape[0]
            for name, part in (("training", train), ("holdout", hold)):
                predicted = predict(part, coefficients, kept)
                summary = errors(part["response"].to_numpy(float), predicted)
                records.append(
                    {
                        "op": op,
                        "structure": structure,
                        "partition": name,
                        "cells": training_cells if name == "training" else holdout_cells,
                        **summary,
                        "form": "alpha + " + " + ".join(kept),
                        **{f"coef_{c}": v for c, v in zip(["alpha"] + kept, coefficients)},
                    }
                )
                part = part.assign(
                    predicted=predicted,
                    ape=np.abs(predicted - part["response"]) / part["response"] * 100,
                )
                residuals.append(
                    part.groupby("workload")["ape"]
                    .median()
                    .rename("mape_median")
                    .reset_index()
                    .assign(op=op, structure=structure, partition=name)
                )

    table = pd.DataFrame(records)
    diagnostics = pd.concat(residuals, ignore_index=True) if residuals else pd.DataFrame()
    SUMMARY.mkdir(parents=True, exist_ok=True)
    table.to_csv(SUMMARY / "cost_model_pilot.csv", index=False)
    diagnostics.to_csv(SUMMARY / "cost_model_pilot_residuals.csv", index=False)

    pd.set_option("display.width", 200)
    print("exploratory pilot, one machine; not the registered hold-out evaluation")
    print(f"cache bytes for the working-set transition: {cache_bytes}\n")
    shown = ["op", "structure", "partition", "cells", "rows", "nonpositive_predictions",
             "mape_median", "mape_p90", "log_ratio_median", "form"]
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
