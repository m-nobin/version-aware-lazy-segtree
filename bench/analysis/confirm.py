"""Registered confirmatory statistics.

Everything inferential the registered protocol
(docs/research/registered-protocol.md) allows lives here; the pilot-era
exploratory statistics stay in data.py. The estimator conventions:

- The comparison unit is one cell (workload, n, axis, variant) against one
  baseline structure. Within a cell, trials pair on the trial index because
  every structure replays the same stream under the same machine state.
- The effect is the log throughput ratio ``log(baseline / subject)`` per
  paired trial; positive means the subject is faster. The point estimate is
  the median of the per-trial log ratios and the interval is a seeded
  percentile bootstrap of that median. Unlike the pilot's observed
  percentiles, these intervals are confidence intervals and are named so.
- Classification against the registered practical margin ``delta`` uses the
  interval, never the point estimate: *meaningfully faster* when the whole
  interval clears ``log(delta)``, *meaningfully slower* when it clears
  ``-log(delta)`` downward, *practically equivalent* when it lies inside
  ``[-log(delta), log(delta)]``, otherwise *inconclusive*. An inconclusive
  cell whose interval half-width exceeds ``log(delta)`` is additionally
  flagged underpowered.
- Trials that hit the memory cap replayed a shorter stream; they never enter
  a ratio. They are censored feasibility outcomes and are reported by the
  feasibility stage instead.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import pathlib
import re
import sys
import warnings

import numpy as np
import pandas as pd
import scipy
from scipy import stats

sys.path.insert(0, str(pathlib.Path(__file__).parent))

import data  # noqa: E402

DELTA = 1.05
LOG_DELTA = float(np.log(DELTA))
BOOTSTRAP_RESAMPLES = 10_000
BOOTSTRAP_SEED = 20270214
MIN_TRIALS = 20
CELL_KEYS = ["workload", "n", "axis", "variant"]
H2_EXPECTED_CELLS = 6
H3_MEDIAN_APE_LIMIT = 15.0
H3_P90_APE_LIMIT = 30.0
H3_STRUCTURES = [
    "persistent",
    "copy-on-push",
    "point-only",
    "checkpointing",
    "buffered",
    "fat-node",
]
H4_EXPECTED_CELLS = 12
H4_AGREEMENT_LIMIT = 0.80
H5_EXPECTED_DRAWS = 12
H5_FACTOR = 1.5
H5_SHARE_LIMIT = 0.80
SENSITIVITY_EXPECTED_CELLS = 16
MIXEDLM_OPTIMIZERS = ("lbfgs", "bfgs", "cg")
RANDOM_VARIANCE_TOLERANCE = 1e-10
COVARIANCE_RELATIVE_TOLERANCE = 1e-12

METRICS = {
    "update": "update_ns_per_op",
    "query": "query_ns_per_op",
}


class RegisteredAnalysisError(RuntimeError):
    """A registered decision cannot be computed without changing its rules."""

    def __init__(self, message: str, diagnostics: list[dict] | None = None):
        super().__init__(message)
        self.diagnostics = diagnostics or []


def registered_wilcoxon(values: np.ndarray) -> tuple[float, float]:
    """Two-sided Wilcoxon with deterministic zero and finite-value rules."""
    values = np.asarray(values, dtype=float)
    if values.ndim != 1 or len(values) == 0:
        raise RegisteredAnalysisError("Wilcoxon requires a non-empty one-dimensional sample")
    if not np.isfinite(values).all():
        raise RegisteredAnalysisError("Wilcoxon input contains a non-finite paired effect")
    if np.all(values == 0.0):
        return 0.0, 1.0
    statistic, pvalue = stats.wilcoxon(
        values,
        alternative="two-sided",
        zero_method="wilcox",
        correction=False,
        method="auto",
    )
    if not np.isfinite(statistic) or not np.isfinite(pvalue):
        raise RegisteredAnalysisError("Wilcoxon returned a non-finite statistic or p-value")
    return float(statistic), float(pvalue)


def finite_positive(values: pd.Series, context: str) -> np.ndarray:
    """Return a metric array or fail closed before a logarithm is taken."""
    array = values.to_numpy(float)
    if not np.isfinite(array).all() or (array <= 0).any():
        raise RegisteredAnalysisError(f"{context} contains a non-finite or non-positive metric")
    return array


def bootstrap_median_ci(
    values: np.ndarray,
    resamples: int = BOOTSTRAP_RESAMPLES,
    seed: int = BOOTSTRAP_SEED,
) -> tuple[float, float]:
    """Seeded percentile bootstrap confidence interval for the median."""
    values = np.asarray(values, dtype=float)
    if not np.isfinite(values).all():
        raise RegisteredAnalysisError("bootstrap input contains a non-finite effect")
    if len(values) < 2:
        return (float("nan"), float("nan"))
    rng = np.random.default_rng(seed)
    draws = rng.choice(values, size=(resamples, len(values)), replace=True)
    medians = np.median(draws, axis=1)
    return tuple(np.percentile(medians, [2.5, 97.5], method="linear"))


def classify(lo: float, hi: float, log_delta: float = LOG_DELTA) -> str:
    """The registered four-state decision from one interval."""
    if not np.isfinite(lo) or not np.isfinite(hi):
        return "inconclusive"
    if lo > log_delta:
        return "meaningfully faster"
    if hi < -log_delta:
        return "meaningfully slower"
    if -log_delta <= lo and hi <= log_delta:
        return "practically equivalent"
    return "inconclusive"


def paired_cell_ratios(
    runs: pd.DataFrame,
    subject: str,
    metric: str,
    baselines: list[str] | None = None,
    log_delta: float = LOG_DELTA,
) -> pd.DataFrame:
    """One row per (cell, baseline): paired effect, interval, classification.

    Only complete trials pair; a capped trial is censored. Cells with fewer
    than four shared complete pairs are reported with an empty classification
    rather than silently dropped. A complete trial whose metric is non-finite
    or non-positive aborts the stage; it is never dropped from the pairing.
    """
    rows = []
    for keys, cell in runs.groupby(CELL_KEYS, sort=False):
        mine = cell[(cell["structure"] == subject) & cell["complete"]]
        if mine["trial"].duplicated().any():
            raise RegisteredAnalysisError(f"duplicate {subject} trial in cell {keys}")
        mine = mine.set_index("trial")[metric]
        for name, other in cell.groupby("structure"):
            if name == subject or (baselines is not None and name not in baselines):
                continue
            complete_other = other[other["complete"]]
            if complete_other["trial"].duplicated().any():
                raise RegisteredAnalysisError(f"duplicate {name} trial in cell {keys}")
            theirs = complete_other.set_index("trial")[metric]
            shared = mine.index.intersection(theirs.index)
            mine_values = finite_positive(mine.loc[shared], f"{subject} {metric} in cell {keys}")
            baseline_values = finite_positive(
                theirs.loc[shared], f"{name} {metric} in cell {keys}"
            )
            row = dict(zip(CELL_KEYS, keys))
            row.update(
                {
                    "subject": subject,
                    "baseline": name,
                    "metric": metric,
                    "pairs": int(len(shared)),
                }
            )
            if len(shared) >= 4:
                log_ratios = np.log(baseline_values / mine_values)
                lo, hi = bootstrap_median_ci(log_ratios)
                _, pvalue = registered_wilcoxon(log_ratios)
                verdict = classify(lo, hi, log_delta)
                row.update(
                    {
                        "median_log_ratio": float(np.median(log_ratios)),
                        "ratio": float(np.exp(np.median(log_ratios))),
                        "ci_lo": float(lo),
                        "ci_hi": float(hi),
                        "p": float(pvalue),
                        "classification": verdict,
                        "underpowered": bool(
                            verdict == "inconclusive" and (hi - lo) / 2 > log_delta
                        ),
                    }
                )
            else:
                row.update(
                    {
                        "median_log_ratio": np.nan,
                        "ratio": np.nan,
                        "ci_lo": np.nan,
                        "ci_hi": np.nan,
                        "p": np.nan,
                        "classification": "",
                        "underpowered": False,
                    }
                )
            rows.append(row)
    return pd.DataFrame(rows)


def primary_family(
    runs: pd.DataFrame,
    primary_cells: pd.DataFrame,
    subject: str = "persistent",
    baseline: str = "copy-on-push",
    metric: str = METRICS["update"],
    log_delta: float = LOG_DELTA,
) -> pd.DataFrame:
    """H2: the small primary inferential family, Holm-controlled.

    ``primary_cells`` carries the registered (workload, n, axis, variant) rows.
    A cell with fewer pairs than its registered trial count keeps its interval
    for the record but is classified inconclusive with ``p = 1`` whatever the
    interval says, per the registered precision rule.
    """
    table = paired_cell_ratios(runs, subject, metric, [baseline], log_delta)
    keys = normalize_registered_cells(primary_cells, H2_EXPECTED_CELLS, "H2")
    keys = keys.rename(columns={"trials": "registered_trials"})
    table = table.merge(
        keys[CELL_KEYS + ["registered_trials"]],
        on=CELL_KEYS,
        validate="one_to_one",
    )
    expected = keys[CELL_KEYS]
    observed = table[CELL_KEYS]
    missing = expected.merge(observed, how="left", indicator=True)
    missing = missing[missing["_merge"] == "left_only"]
    if not missing.empty:
        raise RegisteredAnalysisError(
            "H2 is missing registered primary cells: "
            + missing[CELL_KEYS].to_dict("records").__repr__()
        )
    if len(table) != H2_EXPECTED_CELLS or table.duplicated(CELL_KEYS).any():
        raise RegisteredAnalysisError("H2 requires exactly one result per registered cell")
    if (table["pairs"] > table["registered_trials"]).any():
        raise RegisteredAnalysisError("H2 contains more pairs than its registered trial count")
    insufficient = table["pairs"] < table["registered_trials"]
    table["p_status"] = np.where(insufficient, "insufficient_pairs", "computed")
    table.loc[insufficient, "p"] = 1.0
    table["p_holm"] = data.holm(table["p"].tolist())
    table.loc[insufficient, "underpowered"] = True
    table.loc[insufficient, "classification"] = "inconclusive"
    return table


def broad_regime(
    runs: pd.DataFrame,
    subject: str = "persistent",
    metric: str = METRICS["update"],
    log_delta: float = LOG_DELTA,
) -> pd.DataFrame:
    """The exploratory regime map: every cell, every baseline, BH-flagged.

    Exploratory by registration: nothing here is promoted to a primary
    finding, and the abstract quotes no cell counts from it.
    """
    table = paired_cell_ratios(runs, subject, metric, None, log_delta)
    scored = table["p"].notna()
    if scored.any():
        table.loc[scored, "q"] = data.benjamini_hochberg(table.loc[scored, "p"].tolist())
        table["exploratory_significant"] = table["q"] < 0.05
    return table


def summarize_warnings(caught: list) -> tuple[list[str], int]:
    """Fold an optimizer's warning stream into one line per distinct message.

    MixedLM emits the same few messages once per iteration, so the raw list is
    hundreds of duplicates repeated into every row of the decision CSV. The
    diagnostic has to answer which warnings fired and how loudly, so each
    message is kept once in first-occurrence order with its count, and the
    total is reported separately.
    """
    totals: dict[str, int] = {}
    for item in caught:
        message = str(item.message)
        totals[message] = totals.get(message, 0) + 1
    return (
        [message if count == 1 else f"{message} (x{count})" for message, count in totals.items()],
        sum(totals.values()),
    )


def hierarchical(
    runs: pd.DataFrame,
    subject: str = "persistent",
    baseline: str = "copy-on-push",
    metric: str = METRICS["update"],
) -> pd.DataFrame:
    """Fit the registered MixedLM and retain its complete fit diagnostics.

    Optimizers are attempted in the registered order on fresh model objects.
    A fit is accepted only if it reports convergence and both its random-effect
    covariance and fixed-effect covariance are finite and nonsingular. No OLS
    or other inferential fallback is permitted.
    """
    import statsmodels.formula.api as smf

    rows = []
    for keys, cell in runs.groupby(CELL_KEYS, sort=False):
        mine = cell[(cell["structure"] == subject) & cell["complete"]]
        theirs = cell[(cell["structure"] == baseline) & cell["complete"]]
        merged = mine.merge(theirs, on="trial", suffixes=("_s", "_b"), validate="one_to_one")
        for _, row in merged.iterrows():
            subject_value = float(row[f"{metric}_s"])
            baseline_value = float(row[f"{metric}_b"])
            if (
                not np.isfinite(subject_value)
                or not np.isfinite(baseline_value)
                or subject_value <= 0
                or baseline_value <= 0
            ):
                raise RegisteredAnalysisError(
                    f"hierarchical input contains an invalid metric in cell {keys}"
                )
            rows.append(
                {
                    "cell": "|".join(str(k) for k in keys),
                    "trial": int(row["trial"]),
                    "log_ratio": float(np.log(baseline_value / subject_value)),
                }
            )
    long = pd.DataFrame(rows)
    if long.empty or long["cell"].nunique() < 1:
        raise RegisteredAnalysisError("hierarchical model has no complete paired observations")

    diagnostics = []
    fitted = None
    effects = pd.Series(dtype=float)
    conf = pd.DataFrame()
    accepted = None
    for optimizer in MIXEDLM_OPTIMIZERS:
        attempt = {"optimizer": optimizer}
        try:
            model = smf.mixedlm("log_ratio ~ 0 + C(cell)", long, groups=long["trial"])
            with warnings.catch_warnings(record=True) as caught:
                warnings.simplefilter("always")
                candidate = model.fit(method=optimizer, maxiter=500, disp=False)
            warning_messages, warning_count = summarize_warnings(caught)
            candidate_effects = candidate.fe_params.filter(like="C(cell)")
            candidate_conf = candidate.conf_int().loc[candidate_effects.index]
            random_covariance = np.asarray(candidate.cov_re, dtype=float)
            fixed_covariance = np.asarray(
                candidate.cov_params().loc[candidate_effects.index, candidate_effects.index],
                dtype=float,
            )
            random_min_eigenvalue = (
                float(np.linalg.eigvalsh(random_covariance).min())
                if random_covariance.size
                else float("nan")
            )
            fixed_min_eigenvalue = (
                float(np.linalg.eigvalsh(fixed_covariance).min())
                if fixed_covariance.size
                else float("nan")
            )
            fixed_max_eigenvalue = (
                float(np.linalg.eigvalsh(fixed_covariance).max())
                if fixed_covariance.size
                else float("nan")
            )
            finite = bool(
                np.isfinite(candidate_effects.to_numpy(float)).all()
                and np.isfinite(candidate_conf.to_numpy(float)).all()
                and np.isfinite(random_covariance).all()
                and np.isfinite(fixed_covariance).all()
            )
            nonsingular = bool(
                random_min_eigenvalue > RANDOM_VARIANCE_TOLERANCE
                and fixed_min_eigenvalue > 0
                and fixed_min_eigenvalue
                > COVARIANCE_RELATIVE_TOLERANCE * fixed_max_eigenvalue
            )
            attempt.update(
                {
                    "converged": bool(candidate.converged),
                    "warnings": warning_messages,
                    "warning_count": warning_count,
                    "random_covariance_min_eigenvalue": random_min_eigenvalue,
                    "fixed_covariance_min_eigenvalue": fixed_min_eigenvalue,
                    "fixed_covariance_max_eigenvalue": fixed_max_eigenvalue,
                    "finite": finite,
                    "singular": not nonsingular,
                    "accepted": bool(candidate.converged and finite and nonsingular),
                }
            )
            diagnostics.append(attempt)
            if attempt["accepted"]:
                fitted = candidate
                effects = candidate_effects
                conf = candidate_conf
                accepted = attempt
                break
        except Exception as error:  # statsmodels exposes optimizer failures through several types
            attempt.update(
                {
                    "converged": False,
                    "warnings": [],
                    "warning_count": 0,
                    "finite": False,
                    "singular": True,
                    "accepted": False,
                    "error": f"{type(error).__name__}: {error}",
                }
            )
            diagnostics.append(attempt)

    if fitted is None or accepted is None:
        raise RegisteredAnalysisError(
            "registered MixedLM failed convergence/covariance acceptance; no fallback is allowed",
            diagnostics,
        )
    out = pd.DataFrame(
        {
            "cell": [name.split("[")[1].rstrip("]") for name in effects.index],
            "effect": effects.to_numpy(float),
            "ci_lo": conf[0].to_numpy(float),
            "ci_hi": conf[1].to_numpy(float),
        }
    )
    out["subject"] = subject
    out["baseline"] = baseline
    out["classification"] = [classify(lo, hi) for lo, hi in zip(out["ci_lo"], out["ci_hi"])]
    out["optimizer"] = accepted["optimizer"]
    out["converged"] = accepted["converged"]
    out["warning_count"] = accepted["warning_count"]
    out["warnings"] = json.dumps(accepted["warnings"])
    out["random_covariance_min_eigenvalue"] = accepted[
        "random_covariance_min_eigenvalue"
    ]
    out["fixed_covariance_min_eigenvalue"] = accepted["fixed_covariance_min_eigenvalue"]
    out.attrs["fit_diagnostics"] = diagnostics
    return out


def checksum_agreement(runs: pd.DataFrame) -> pd.DataFrame:
    """Cross-structure answer agreement per replayed stream.

    Under fresh-process orchestration no single process sees two structures,
    so the in-binary cross-check cannot fire; this is where it happens
    instead. The control keeps no history and is excluded. Returns the
    disagreeing (cell, seed) rows; empty means every eligible cell agrees.
    """
    historical = runs[(runs["structure"] != "lazy") & (runs["status"] == "ok")]
    failures = []
    for keys, group in historical.groupby(CELL_KEYS[:1] + ["n", "axis", "variant", "seed"]):
        if group["checksum"].nunique() > 1:
            failure = dict(zip(["workload", "n", "axis", "variant", "seed"], keys))
            failure["checksums"] = group.groupby("structure")["checksum"].first().to_dict()
            failures.append(failure)
    return pd.DataFrame(failures)


H1_STRUCTURES = ["persistent", "copy-on-push", "point-only", "full-copy"]


def h1_identities(runs: pd.DataFrame, structural: pd.DataFrame) -> pd.DataFrame:
    """H1: the derived record-count identities hold exactly.

    Predicted stored records per stream, from machine-independent counts:
    the subject stores Σ F (Proposition 10.5), copy-on-push Σ (F + 2P),
    point-only Σ N (the intersecting family) and full copy (2n − 1) per
    nonzero update. Compared for exact equality against the measured
    ``nodes − build_nodes`` of every complete trial; no tolerance, no
    p-value.
    """
    keys = CELL_KEYS + ["seed"]
    eligible = runs[runs["complete"] & runs["structure"].isin(H1_STRUCTURES)]
    merged = eligible.merge(
        structural,
        on=keys,
        how="left",
        suffixes=("", "_structural"),
        validate="many_to_one",
        indicator=True,
    )
    if eligible.empty or (merged["_merge"] != "both").any():
        raise RegisteredAnalysisError("H1 is missing a complete run or structural-count row")
    merged = merged.drop(columns=["_merge"])
    n = merged["n"].astype("int64")
    predictions = {
        "persistent": merged["sum_update_visits"],
        "copy-on-push": merged["sum_update_visits"] + 2 * merged["sum_pushes"],
        "point-only": merged["sum_intersecting"],
        "full-copy": (2 * n - 1) * merged["nonzero_updates"],
    }
    rows = []
    for structure, predicted in predictions.items():
        subset = merged["structure"] == structure
        if not subset.any():
            raise RegisteredAnalysisError(f"H1 has no complete {structure} trial")
        measured = (merged.loc[subset, "nodes"] - merged.loc[subset, "build_nodes"]).astype(
            "int64"
        )
        expected = predicted[subset].astype("int64")
        mismatch = measured != expected
        rows.append(
            {
                "structure": structure,
                "rows": int(subset.sum()),
                "mismatches": int(mismatch.sum()),
                "exact": bool(not mismatch.any()),
            }
        )
    return pd.DataFrame(rows)


def h3_decisions(cell_predictions: pd.DataFrame) -> pd.DataFrame:
    """H3: one median-aggregated prediction error per equally weighted cell."""
    required = {
        "op",
        "structure",
        "trials",
        "actual_median",
        "predicted_median",
        "actual_mean",
        "predicted_mean",
        "ape_median",
        "ape_mean",
        "expected_cell_count",
        "expected_inventory_sha256",
        "model_artifact_sha256",
        *CELL_KEYS,
    }
    missing_columns = sorted(required.difference(cell_predictions.columns))
    if missing_columns:
        raise RegisteredAnalysisError(
            f"H3 cell predictions are missing columns: {', '.join(missing_columns)}"
        )
    if cell_predictions.empty:
        raise RegisteredAnalysisError("H3 cell prediction file is empty")
    model_hashes = cell_predictions["model_artifact_sha256"].astype(str)
    if model_hashes.nunique() != 1 or re.fullmatch(r"[0-9a-f]{64}", model_hashes.iloc[0]) is None:
        raise RegisteredAnalysisError("H3 model-artifact provenance is invalid")
    cells = cell_predictions[cell_predictions["structure"].isin(H3_STRUCTURES)].copy()
    keys = CELL_KEYS + ["structure", "op"]
    if cells.duplicated(keys).any():
        raise RegisteredAnalysisError("H3 contains more than one prediction/effect per cell")
    numeric = [
        "trials",
        "actual_median",
        "predicted_median",
        "actual_mean",
        "predicted_mean",
        "ape_median",
        "ape_mean",
    ]
    if not np.isfinite(cells[numeric].to_numpy(float)).all():
        raise RegisteredAnalysisError("H3 contains a non-finite cell prediction")
    response_columns = [
        "actual_median",
        "predicted_median",
        "actual_mean",
        "predicted_mean",
    ]
    invalid_response = (cells[response_columns] <= 0).any().any()
    if (cells["trials"] < 1).any() or invalid_response:
        raise RegisteredAnalysisError("H3 contains a cell without a positive complete response")
    for aggregation in ("median", "mean"):
        recomputed = (
            np.abs(
                cells[f"predicted_{aggregation}"] - cells[f"actual_{aggregation}"]
            )
            / cells[f"actual_{aggregation}"]
            * 100
        )
        if not np.allclose(recomputed, cells[f"ape_{aggregation}"], rtol=1e-12, atol=1e-12):
            raise RegisteredAnalysisError(f"H3 {aggregation} APE does not match its cell values")
    inventory_hashes = cells["expected_inventory_sha256"].astype(str)
    if inventory_hashes.nunique() != 1:
        raise RegisteredAnalysisError("H3 cell groups use different registered holdout inventories")
    if re.fullmatch(r"[0-9a-f]{64}", inventory_hashes.iloc[0]) is None:
        raise RegisteredAnalysisError("H3 registered holdout inventory metadata is invalid")
    # The inventory is per structure: the registered holdout cells minus that
    # structure's registered pilot-capped cells, fixed by the prepare stage.
    expected_counts = cells["expected_cell_count"].astype(int)
    if (cells.assign(count=expected_counts).groupby("structure")["count"].nunique() != 1).any():
        raise RegisteredAnalysisError("H3 structure groups disagree on their holdout inventory")
    if (expected_counts <= 0).any():
        raise RegisteredAnalysisError("H3 registered holdout inventory metadata is invalid")

    rows = []
    for structure in H3_STRUCTURES:
        for op in ("update", "query"):
            group = cells[(cells["structure"] == structure) & (cells["op"] == op)]
            if group.empty:
                raise RegisteredAnalysisError(f"H3 {structure}/{op} has no holdout predictions")
            expected_count = int(group["expected_cell_count"].iloc[0])
            if len(group) != expected_count:
                raise RegisteredAnalysisError(
                    f"H3 {structure}/{op} requires {expected_count} holdout cells, "
                    f"found {len(group)}"
                )
            median_ape = float(np.median(group["ape_median"]))
            p90_ape = float(np.percentile(group["ape_median"], 90, method="linear"))
            mean_median_ape = float(np.median(group["ape_mean"]))
            mean_p90_ape = float(np.percentile(group["ape_mean"], 90, method="linear"))
            rows.append(
                {
                    "structure": structure,
                    "op": op,
                    "cells": int(len(group)),
                    "trials": int(group["trials"].sum()),
                    "aggregation": "median within cell; equal weight across cells",
                    "median_ape": median_ape,
                    "p90_ape": p90_ape,
                    "supported": bool(
                        median_ape <= H3_MEDIAN_APE_LIMIT and p90_ape <= H3_P90_APE_LIMIT
                    ),
                    "mean_sensitivity_median_ape": mean_median_ape,
                    "mean_sensitivity_p90_ape": mean_p90_ape,
                    "mean_sensitivity_supported": bool(
                        mean_median_ape <= H3_MEDIAN_APE_LIMIT
                        and mean_p90_ape <= H3_P90_APE_LIMIT
                    ),
                }
            )
    return pd.DataFrame(rows)


def normalize_registered_cells(
    cells: pd.DataFrame, expected_count: int, context: str = "H4"
) -> pd.DataFrame:
    required = CELL_KEYS + ["trials"]
    missing = sorted(set(required).difference(cells.columns))
    if missing:
        raise RegisteredAnalysisError(
            f"registered cell file is missing columns: {', '.join(missing)}"
        )
    try:
        n_values = pd.to_numeric(cells["n"], errors="raise").to_numpy(float)
        variants = pd.to_numeric(cells["variant"], errors="raise").to_numpy(float)
        trial_values = pd.to_numeric(cells["trials"], errors="raise").to_numpy(float)
    except (TypeError, ValueError) as error:
        raise RegisteredAnalysisError(f"{context} cell file has invalid numeric values") from error
    if (
        not np.isfinite(n_values).all()
        or not np.isfinite(variants).all()
        or not np.isfinite(trial_values).all()
        or (n_values <= 0).any()
        or (n_values != np.floor(n_values)).any()
        or (trial_values != np.floor(trial_values)).any()
    ):
        raise RegisteredAnalysisError(f"{context} cell file has invalid numeric values")
    if cells["workload"].astype(str).str.len().eq(0).any() or cells["axis"].astype(
        str
    ).str.len().eq(0).any():
        raise RegisteredAnalysisError(f"{context} cell file has an empty cell label")
    normalized = cells[required].assign(
        n=n_values.astype(int),
        variant=variants,
        trials=trial_values.astype(int),
    )
    if normalized.duplicated(CELL_KEYS).any() or len(normalized) != expected_count:
        raise RegisteredAnalysisError(
            f"{context} cell file must contain exactly {expected_count} unique cells"
        )
    if (normalized["trials"] < MIN_TRIALS).any():
        raise RegisteredAnalysisError(f"registered {context} trial counts must be at least twenty")
    return normalized


def h4_agreement(
    machine_a: pd.DataFrame, machine_b: pd.DataFrame, cells: pd.DataFrame
) -> dict:
    """H4: classification stability across microarchitectures.

    Every one of the twelve versioned replication cells must occur exactly
    once on each machine with a non-empty classification. Missing, duplicate,
    or unclassified cells abort H4 rather than shrinking its denominator.
    """
    registered = normalize_registered_cells(cells, H4_EXPECTED_CELLS, "H4")
    keys = CELL_KEYS + ["subject", "baseline", "metric"]

    def registered_rows(table: pd.DataFrame, machine: str) -> pd.DataFrame:
        selected = registered.merge(table, on=CELL_KEYS, how="left", validate="one_to_many")
        selected = selected[
            (selected["baseline"] == table["baseline"].iloc[0])
            & (selected["metric"] == table["metric"].iloc[0])
        ]
        if len(selected) != H4_EXPECTED_CELLS or selected.duplicated(CELL_KEYS).any():
            raise RegisteredAnalysisError(
                f"H4 {machine} input does not contain exactly one result per registered cell"
            )
        if selected["classification"].isna().any() or (selected["classification"] == "").any():
            raise RegisteredAnalysisError(f"H4 {machine} has an unclassified registered cell")
        if (selected["pairs"] != selected["trials"]).any():
            raise RegisteredAnalysisError(
                f"H4 {machine} does not have every registered paired trial"
            )
        return selected

    if machine_a.empty or machine_b.empty:
        raise RegisteredAnalysisError("H4 requires non-empty classification tables")
    for column in ("subject", "baseline", "metric"):
        if machine_a[column].nunique() != 1 or machine_b[column].nunique() != 1:
            raise RegisteredAnalysisError(f"H4 inputs must contain one registered {column}")
        if machine_a[column].iloc[0] != machine_b[column].iloc[0]:
            raise RegisteredAnalysisError(f"H4 machine inputs use different {column} values")
    left = registered_rows(machine_a, "machine A")
    right = registered_rows(machine_b, "machine B")
    merged = left.merge(right, on=keys, suffixes=("_a", "_b"), validate="one_to_one")
    agree = merged["classification_a"] == merged["classification_b"]
    return {
        "cells": int(len(merged)),
        "agreement": float(agree.mean()),
        "supported": bool(float(agree.mean()) >= H4_AGREEMENT_LIMIT),
        "disagreements": merged.loc[
            ~agree, keys + ["classification_a", "classification_b"]
        ].reset_index(drop=True),
    }


def h5_region(
    actual: np.ndarray,
    predicted: np.ndarray,
    factor: float = H5_FACTOR,
    expected_cells: int | None = None,
) -> dict:
    """H5: share of eligible cells whose observed outcome falls inside the
    registered predictive region ``predicted × [1/factor, factor]``."""
    actual = np.asarray(actual, dtype=float)
    predicted = np.asarray(predicted, dtype=float)
    if actual.shape != predicted.shape or actual.ndim != 1:
        raise RegisteredAnalysisError("H5 actual and predicted arrays are not aligned")
    if expected_cells is not None and len(actual) != expected_cells:
        raise RegisteredAnalysisError(
            f"H5 requires {expected_cells} registered cells, found {len(actual)}"
        )
    if not np.isfinite(actual).all() or not np.isfinite(predicted).all():
        raise RegisteredAnalysisError("H5 contains a non-finite response or prediction")
    if (actual <= 0).any() or (predicted <= 0).any():
        raise RegisteredAnalysisError("H5 contains a non-positive response or prediction")
    inside = (actual >= predicted / factor) & (actual <= predicted * factor)
    share = float(inside.mean())
    return {
        "eligible": int(len(actual)),
        "inside": int(inside.sum()),
        "share": share,
        "supported": bool(share >= H5_SHARE_LIMIT),
    }


def h5_decision(cell_predictions: pd.DataFrame, registered_draws: pd.DataFrame) -> pd.DataFrame:
    """H5 over twelve registered trace draws and both operation responses."""
    draw_columns = [
        "draw_id",
        "seed",
        "n",
        "operations",
        "update_share",
        "interval_share",
        "trials",
    ]
    required = {
        "op",
        "actual",
        "predicted",
        "source_model",
        "target_structure",
        "model_artifact_sha256",
        "transfer_responses_sha256",
        *draw_columns,
    }
    missing = sorted(required.difference(cell_predictions.columns))
    if missing:
        raise RegisteredAnalysisError(
            f"H5 cell predictions are missing columns: {', '.join(missing)}"
        )
    if set(cell_predictions["source_model"].astype(str)) != {"copy-on-push"}:
        raise RegisteredAnalysisError("H5 was not produced from the copy-on-push model")
    if set(cell_predictions["target_structure"].astype(str)) != {"external"}:
        raise RegisteredAnalysisError("H5 does not target the external adapter")
    for column in ("model_artifact_sha256", "transfer_responses_sha256"):
        values = cell_predictions[column].astype(str)
        if values.nunique() != 1 or re.fullmatch(r"[0-9a-f]{64}", values.iloc[0]) is None:
            raise RegisteredAnalysisError(f"H5 {column} provenance is invalid")
    registry_missing = sorted(set(draw_columns).difference(registered_draws.columns))
    if registry_missing:
        raise RegisteredAnalysisError(
            f"H5 draw registry is missing columns: {', '.join(registry_missing)}"
        )
    draws = registered_draws["draw_id"].astype(str)
    if len(draws) != H5_EXPECTED_DRAWS or draws.duplicated().any():
        raise RegisteredAnalysisError(
            f"H5 requires exactly {H5_EXPECTED_DRAWS} unique registered trace draws"
        )
    registry_numeric = registered_draws[draw_columns[1:]].to_numpy(float)
    integer_columns = registered_draws[["seed", "n", "operations", "trials"]].to_numpy(
        float
    )
    if (
        not np.isfinite(registry_numeric).all()
        or (integer_columns <= 0).any()
        or (integer_columns != np.floor(integer_columns)).any()
        or (registered_draws["trials"].to_numpy(float) < MIN_TRIALS).any()
        or not registered_draws["update_share"].between(0.0, 1.0, inclusive="both").all()
        or not registered_draws["interval_share"].between(
            0.0, 1.0, inclusive="right"
        ).all()
    ):
        raise RegisteredAnalysisError("H5 draw registry contains invalid parameters")
    expected = pd.MultiIndex.from_product(
        [draws.tolist(), ["update", "query"]], names=["draw_id", "op"]
    ).to_frame(index=False)
    expected = expected.merge(registered_draws[draw_columns], on="draw_id", validate="many_to_one")
    cells = cell_predictions.assign(draw_id=cell_predictions["draw_id"].astype(str))
    if cells.duplicated(["draw_id", "op"]).any():
        raise RegisteredAnalysisError("H5 contains duplicate draw/operation predictions")
    expected_keys = set(map(tuple, expected[["draw_id", "op"]].itertuples(index=False, name=None)))
    observed_keys = set(map(tuple, cells[["draw_id", "op"]].itertuples(index=False, name=None)))
    if observed_keys != expected_keys or len(cells) != H5_EXPECTED_DRAWS * 2:
        raise RegisteredAnalysisError("H5 contains missing or extra draw/operation cells")
    aligned = expected.merge(
        cells,
        on=["draw_id", "op"],
        how="left",
        validate="one_to_one",
        suffixes=("_registered", "_observed"),
    )
    if aligned[["actual", "predicted"]].isna().any().any():
        raise RegisteredAnalysisError("H5 is missing a registered draw/operation cell")
    for column in draw_columns[1:]:
        registered = aligned[f"{column}_registered"].to_numpy(float)
        observed = aligned[f"{column}_observed"].to_numpy(float)
        if not np.array_equal(registered, observed):
            raise RegisteredAnalysisError(f"H5 {column} does not match the draw registry")
    result = h5_region(
        aligned["actual"].to_numpy(float),
        aligned["predicted"].to_numpy(float),
        expected_cells=H5_EXPECTED_DRAWS * 2,
    )
    return pd.DataFrame([result | {"draws": H5_EXPECTED_DRAWS, "operations": 2}])


def paired_effects(
    runs: pd.DataFrame, subject: str, baseline: str, metric: str
) -> pd.DataFrame:
    """Complete paired trial effects with structure-order covariates."""
    rows = []
    for keys, cell in runs.groupby(CELL_KEYS, sort=False):
        mine = cell[(cell["structure"] == subject) & cell["complete"]]
        theirs = cell[(cell["structure"] == baseline) & cell["complete"]]
        shared_columns = ["trial", metric]
        if "exec_order" in runs:
            shared_columns.append("exec_order")
        merged = mine[shared_columns].merge(
            theirs[shared_columns], on="trial", suffixes=("_s", "_b"), validate="one_to_one"
        )
        if merged.empty:
            continue
        subject_values = finite_positive(merged[f"{metric}_s"], f"{subject} {metric} {keys}")
        baseline_values = finite_positive(
            merged[f"{metric}_b"], f"{baseline} {metric} {keys}"
        )
        for index, row in merged.reset_index(drop=True).iterrows():
            record = dict(zip(CELL_KEYS, keys))
            record.update(
                {
                    "cell": "|".join(str(key) for key in keys),
                    "subject": subject,
                    "baseline": baseline,
                    "trial": int(row["trial"]),
                    "log_ratio": float(np.log(baseline_values[index] / subject_values[index])),
                }
            )
            if "exec_order_s" in row:
                record["order_gap"] = float(row["exec_order_s"] - row["exec_order_b"])
            rows.append(record)
    return pd.DataFrame(rows)


def restrict_primary_effects(
    effects: pd.DataFrame, primary_cells: pd.DataFrame | None, context: str
) -> pd.DataFrame:
    if primary_cells is None:
        return effects
    registered = normalize_registered_cells(primary_cells, H2_EXPECTED_CELLS, "H2")[CELL_KEYS]
    selected = effects.merge(registered, on=CELL_KEYS, how="inner", validate="many_to_one")
    observed = selected[CELL_KEYS].drop_duplicates()
    missing = registered.merge(observed, how="left", indicator=True)
    if (missing["_merge"] != "both").any():
        raise RegisteredAnalysisError(f"{context} is missing a registered H2 primary cell")
    return selected


def bootstrap_mean_ci(
    values: np.ndarray,
    resamples: int = BOOTSTRAP_RESAMPLES,
    seed: int = BOOTSTRAP_SEED,
) -> tuple[float, float]:
    values = np.asarray(values, dtype=float)
    if len(values) < 2:
        return (float("nan"), float("nan"))
    if not np.isfinite(values).all():
        raise RegisteredAnalysisError("mean bootstrap input contains a non-finite effect")
    rng = np.random.default_rng(seed)
    draws = rng.choice(values, size=(resamples, len(values)), replace=True)
    return tuple(
        np.percentile(np.mean(draws, axis=1), [2.5, 97.5], method="linear")
    )


def mean_median_sensitivity(
    runs: pd.DataFrame,
    subject: str = "persistent",
    baseline: str = "copy-on-push",
    metric: str = METRICS["update"],
    primary_cells: pd.DataFrame | None = None,
) -> pd.DataFrame:
    """Registered H2 sensitivity: mean versus median paired log effects."""
    effects = restrict_primary_effects(
        paired_effects(runs, subject, baseline, metric), primary_cells, "mean/median sensitivity"
    )
    rows = []
    for keys, group in effects.groupby(CELL_KEYS, sort=False):
        values = group["log_ratio"].to_numpy(float)
        median_lo, median_hi = bootstrap_median_ci(values)
        mean_lo, mean_hi = bootstrap_mean_ci(values)
        sufficient = len(values) >= 4
        row = dict(zip(CELL_KEYS, keys))
        row.update(
            {
                "subject": subject,
                "baseline": baseline,
                "pairs": int(len(values)),
                "sensitivity_status": "computed" if sufficient else "insufficient_pairs",
                "median_log_ratio": float(np.median(values)),
                "median_ci_lo": median_lo,
                "median_ci_hi": median_hi,
                "median_classification": (
                    classify(median_lo, median_hi) if sufficient else "inconclusive"
                ),
                "mean_log_ratio": float(np.mean(values)),
                "mean_ci_lo": mean_lo,
                "mean_ci_hi": mean_hi,
                "mean_classification": (
                    classify(mean_lo, mean_hi) if sufficient else "inconclusive"
                ),
                "effect_difference": float(np.mean(values) - np.median(values)),
                "classification_changed": bool(
                    sufficient
                    and classify(median_lo, median_hi) != classify(mean_lo, mean_hi)
                ),
            }
        )
        rows.append(row)
    if not rows:
        raise RegisteredAnalysisError("mean/median sensitivity has no eligible paired cells")
    return pd.DataFrame(rows)


def execution_order_regression(
    runs: pd.DataFrame,
    subject: str = "persistent",
    baseline: str = "copy-on-push",
    metric: str = METRICS["update"],
    primary_cells: pd.DataFrame | None = None,
) -> pd.DataFrame:
    """Regress paired log effects on the within-block execution-order gap.

    Cell fixed effects absorb cell difficulty. HC3 covariance is registered;
    the coefficient is a sensitivity diagnostic and cannot replace H2.
    """
    import statsmodels.formula.api as smf

    if "exec_order" not in runs:
        raise RegisteredAnalysisError("order sensitivity requires the exec_order column")
    effects = restrict_primary_effects(
        paired_effects(runs, subject, baseline, metric), primary_cells, "order sensitivity"
    )
    if "order_gap" not in effects or not np.isfinite(effects["order_gap"]).all():
        raise RegisteredAnalysisError("order sensitivity has invalid execution-order values")
    if effects["order_gap"].nunique() < 2:
        raise RegisteredAnalysisError(
            "exec_order is constant; the order effect is not identifiable"
        )
    fitted = smf.ols("log_ratio ~ 0 + C(cell) + order_gap", effects).fit(cov_type="HC3")
    estimate = float(fitted.params["order_gap"])
    lo, hi = (float(value) for value in fitted.conf_int().loc["order_gap"])
    pvalue = float(fitted.pvalues["order_gap"])
    if not np.isfinite([estimate, lo, hi, pvalue]).all():
        raise RegisteredAnalysisError("execution-order regression returned a non-finite result")
    return pd.DataFrame(
        [
            {
                "observations": int(fitted.nobs),
                "cells": int(effects["cell"].nunique()),
                "subject": subject,
                "baseline": baseline,
                "order_variable": "subject exec_order - baseline exec_order",
                "covariance": "HC3",
                "coefficient": estimate,
                "ci_lo": lo,
                "ci_hi": hi,
                "p": pvalue,
                "detected": bool(lo > 0 or hi < 0),
            }
        ]
    )


def leave_one_trial_out(
    runs: pd.DataFrame,
    subject: str = "persistent",
    baseline: str = "copy-on-push",
    metric: str = METRICS["update"],
    primary_cells: pd.DataFrame | None = None,
) -> pd.DataFrame:
    """Registered influence analysis for every omitted paired trial."""
    effects = restrict_primary_effects(
        paired_effects(runs, subject, baseline, metric),
        primary_cells,
        "leave-one-trial-out sensitivity",
    )
    rows = []
    for keys, group in effects.groupby(CELL_KEYS, sort=False):
        values = group["log_ratio"].to_numpy(float)
        if len(values) < 5:
            row = dict(zip(CELL_KEYS, keys))
            row.update(
                {
                    "subject": subject,
                    "baseline": baseline,
                    "omitted_trial": pd.NA,
                    "pairs_remaining": max(0, int(len(values) - 1)),
                    "sensitivity_status": "insufficient_pairs",
                    "full_effect": float(np.median(values)),
                    "leave_one_out_effect": np.nan,
                    "absolute_shift": np.nan,
                    "full_classification": "inconclusive",
                    "leave_one_out_classification": "inconclusive",
                    "classification_changed": False,
                }
            )
            rows.append(row)
            continue
        full_lo, full_hi = bootstrap_median_ci(values)
        full_effect = float(np.median(values))
        full_classification = classify(full_lo, full_hi)
        for position, (_, observation) in enumerate(group.reset_index(drop=True).iterrows()):
            reduced = np.delete(values, position)
            lo, hi = bootstrap_median_ci(reduced)
            effect = float(np.median(reduced))
            row = dict(zip(CELL_KEYS, keys))
            row.update(
                {
                    "subject": subject,
                    "baseline": baseline,
                    "omitted_trial": int(observation["trial"]),
                    "pairs_remaining": int(len(reduced)),
                    "sensitivity_status": "computed",
                    "full_effect": full_effect,
                    "leave_one_out_effect": effect,
                    "absolute_shift": abs(effect - full_effect),
                    "full_classification": full_classification,
                    "leave_one_out_classification": classify(lo, hi),
                    "classification_changed": full_classification != classify(lo, hi),
                }
            )
            rows.append(row)
    if not rows:
        raise RegisteredAnalysisError("leave-one-trial-out analysis has no eligible cells")
    return pd.DataFrame(rows)


SENSITIVITY_FIELD = {"compiler": "compiler", "allocator": "malloc_provider"}


def assert_dimension_changed(
    reference_raw: pathlib.Path, sensitivity_raw: pathlib.Path, dimension: str, mode: str = "timing"
) -> None:
    """Prove the sensitivity campaign really varied the dimension it names.

    A `release-verify-gcc` preset that resolves to the platform's default
    compiler, or a preload the loader ignored, produces a campaign that differs
    from the primary in name only. Its sensitivity result would then be a
    reassuring null about an experiment that never happened. Both campaigns
    must report one value each, and the two values must differ.
    """
    field = SENSITIVITY_FIELD[dimension]
    reference = data.environment_values(reference_raw, field, mode)
    sensitivity = data.environment_values(sensitivity_raw, field, mode)
    for label, values in (("primary", reference), (dimension, sensitivity)):
        if not values:
            raise RegisteredAnalysisError(
                f"{dimension} sensitivity cannot be checked: the {label} campaign records no {field}"
            )
        if len(values) > 1:
            raise RegisteredAnalysisError(
                f"the {label} campaign changed {field} mid-campaign: {sorted(values)}"
            )
        if values == {"unknown"}:
            raise RegisteredAnalysisError(
                f"{dimension} sensitivity cannot be checked: the {label} campaign's {field} is unknown"
            )
    if reference == sensitivity:
        raise RegisteredAnalysisError(
            f"{dimension} sensitivity ran the same {field} as the primary campaign: "
            f"{sorted(reference)[0]}"
        )


def campaign_sensitivity(
    reference_runs: pd.DataFrame,
    sensitivity_runs: pd.DataFrame,
    dimension: str,
    registered_cells: pd.DataFrame,
    subject: str = "persistent",
    baseline: str = "copy-on-push",
    metric: str = METRICS["update"],
) -> pd.DataFrame:
    """Compare primary- versus second-compiler/allocator classifications."""
    if dimension not in ("compiler", "allocator"):
        raise ValueError("sensitivity dimension must be compiler or allocator")
    registry = normalize_registered_cells(
        registered_cells, SENSITIVITY_EXPECTED_CELLS, f"{dimension} sensitivity"
    )
    workloads = set(registry["workload"].astype(str))
    reference = paired_cell_ratios(reference_runs, subject, metric, [baseline])
    alternative = paired_cell_ratios(sensitivity_runs, subject, metric, [baseline])
    reference = reference[reference["workload"].isin(workloads)]
    alternative = alternative[alternative["workload"].isin(workloads)]
    keys = CELL_KEYS + ["subject", "baseline", "metric"]
    expected_keys = set(map(tuple, registry[CELL_KEYS].itertuples(index=False, name=None)))
    reference_keys = set(map(tuple, reference[CELL_KEYS].itertuples(index=False, name=None)))
    alternative_keys = set(map(tuple, alternative[CELL_KEYS].itertuples(index=False, name=None)))
    if reference_keys != expected_keys or alternative_keys != expected_keys:
        raise RegisteredAnalysisError(
            f"{dimension} sensitivity is missing or adds a registered W1/W5/W11 cell"
        )
    merged = reference.merge(
        alternative, on=keys, suffixes=("_primary", f"_{dimension}"), validate="one_to_one"
    )
    if (
        (merged["classification_primary"] == "").any()
        or (merged[f"classification_{dimension}"] == "").any()
    ):
        raise RegisteredAnalysisError(f"{dimension} sensitivity contains an unclassified cell")
    registered_trials = registry[CELL_KEYS + ["trials"]].rename(
        columns={"trials": "registered_sensitivity_trials"}
    )
    merged = merged.merge(registered_trials, on=CELL_KEYS, validate="one_to_one")
    if (
        merged[f"pairs_{dimension}"] != merged["registered_sensitivity_trials"]
    ).any():
        raise RegisteredAnalysisError(
            f"{dimension} sensitivity does not have every registered paired trial"
        )
    merged["dimension"] = dimension
    merged["effect_shift"] = (
        merged[f"median_log_ratio_{dimension}"] - merged["median_log_ratio_primary"]
    )
    merged["classification_changed"] = (
        merged[f"classification_{dimension}"] != merged["classification_primary"]
    )
    return merged


def required_trials(sd_log_ratio: float, log_delta: float = LOG_DELTA, cap: int = 200) -> int:
    """Fixed trial count whose t-based 95% interval half-width stays within
    the practical margin, given a pilot log-ratio standard deviation."""
    count = 4
    while count <= cap:
        if stats.t.ppf(0.975, count - 1) * sd_log_ratio / np.sqrt(count) <= log_delta:
            return count
        count += 1
    return cap + 1


def precision(
    runs: pd.DataFrame,
    subject: str = "persistent",
    baseline: str = "copy-on-push",
    metric: str = METRICS["update"],
    log_delta: float = LOG_DELTA,
) -> pd.DataFrame:
    """The registered sample-size computation, from pilot per-cell paired
    log-ratio spread to the trial count each cell needs."""
    rows = []
    for keys, cell in runs.groupby(CELL_KEYS, sort=False):
        mine = cell[(cell["structure"] == subject) & cell["complete"]]
        theirs = cell[(cell["structure"] == baseline) & cell["complete"]]
        mine = mine.set_index("trial")[metric].dropna()
        theirs = theirs.set_index("trial")[metric].dropna()
        shared = mine.index.intersection(theirs.index)
        if len(shared) < 4:
            continue
        mine_values = finite_positive(mine.loc[shared], f"{subject} {metric} in cell {keys}")
        baseline_values = finite_positive(
            theirs.loc[shared], f"{baseline} {metric} in cell {keys}"
        )
        log_ratios = np.log(baseline_values / mine_values)
        sd = float(np.std(log_ratios, ddof=1))
        row = dict(zip(CELL_KEYS, keys))
        row.update(
            {
                "pilot_pairs": int(len(shared)),
                "sd_log_ratio": sd,
                "required_trials": required_trials(sd, log_delta),
            }
        )
        rows.append(row)
    return pd.DataFrame(rows)


def feasibility(runs: pd.DataFrame) -> pd.DataFrame:
    """Censored feasibility outcomes: the largest completed scale and the
    first capped scale per (workload, structure)."""
    return data.feasibility(data.summarise(runs))


def assert_blinded(runs: pd.DataFrame) -> None:
    """Reject named or malformed labels without reading any custody material."""
    labels = runs["structure"].astype(str)
    valid = {f"S{index:02d}" for index in range(1, 10)}
    if not set(labels).issubset(valid):
        raise RegisteredAnalysisError(
            "--blinded input must already contain only opaque SNN structure labels"
        )


def unavailable_record_path(output: pathlib.Path) -> pathlib.Path:
    """The explicit unavailable record that stands in for a decision CSV."""
    return output.with_name(output.stem + "_unavailable.json")


def record_unavailable(output: pathlib.Path, stage: str, error: RegisteredAnalysisError) -> None:
    """Replace a decision output with a hashed record of why it is unavailable.

    The registered failure policy makes only the affected decision
    unavailable: the record, not the missing CSV, is what the blinding stage
    hashes, so the locked command can continue and unblind the rest.
    """
    output.unlink(missing_ok=True)
    record = {
        "schema_version": 1,
        "stage": stage,
        "output": output.name,
        "status": "unavailable",
        "reason": str(error),
        "diagnostics": error.diagnostics,
        "recorded_utc": dt.datetime.now(dt.timezone.utc)
        .isoformat()
        .replace("+00:00", "Z"),
    }
    unavailable_record_path(output).write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")


def verify_file_sidecar(path: pathlib.Path) -> str:
    """Verify a generated prediction CSV against its adjacent SHA-256 sidecar."""
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    sidecar = path.with_suffix(path.suffix + ".sha256")
    fields = sidecar.read_text().split() if sidecar.is_file() else []
    if fields != [digest, path.name]:
        raise RegisteredAnalysisError(
            f"prediction checksum is missing or does not match: {sidecar}"
        )
    return digest


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--campaign", type=pathlib.Path, required=True)
    parser.add_argument(
        "--comparison-campaign",
        type=pathlib.Path,
        help="second machine/compiler/allocator campaign for a cross-campaign stage",
    )
    parser.add_argument(
        "--stage",
        choices=(
            "precision",
            "checksums",
            "h1",
            "h3",
            "h4",
            "h5",
            "primary",
            "regime",
            "hierarchical",
            "feasibility",
            "mean-median",
            "order",
            "leave-one-out",
            "compiler",
            "allocator",
        ),
        required=True,
    )
    parser.add_argument("--mode", default="timing")
    parser.add_argument("--metric", choices=tuple(METRICS), default="update")
    parser.add_argument("--subject", default="persistent")
    parser.add_argument("--baseline", default="copy-on-push")
    parser.add_argument(
        "--blinded",
        action="store_true",
        help="require already blinded SNN inputs; no key or name mapping is read",
    )
    parser.add_argument(
        "--primary-cells",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[1] / "primary_cells.csv",
    )
    parser.add_argument(
        "--h4-cells",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[1] / "h4_cells.csv",
    )
    parser.add_argument(
        "--h5-draws",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[1] / "h5_trace_draws.csv",
    )
    parser.add_argument(
        "--sensitivity-cells",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[1] / "sensitivity_cells.csv",
    )
    parser.add_argument(
        "--cell-predictions",
        type=pathlib.Path,
        help="registered per-cell prediction CSV required by H3 or H5",
    )
    parser.add_argument(
        "--model-artifact",
        type=pathlib.Path,
        help="frozen model artifact required to verify H3/H5 prediction provenance",
    )
    parser.add_argument(
        "--output-tag",
        choices=("contrast-a", "contrast-b"),
        help="opaque tag used when both possible blinded regime subjects are evaluated",
    )
    args = parser.parse_args(argv)

    raw = args.campaign / "raw"
    out = args.campaign / "analysis"
    out.mkdir(parents=True, exist_ok=True)
    metric = METRICS[args.metric]
    tag = f"_{args.output_tag}" if args.output_tag else ""
    path = out / f"{args.stage}_{args.metric}{tag}.csv"
    unavailable_record_path(path).unlink(missing_ok=True)

    try:
        table, input_incomplete_rows, comparison_incomplete_rows = run_stage(
            args, parser, raw, out, metric
        )
    except RegisteredAnalysisError as error:
        record_unavailable(path, args.stage, error)
        raise SystemExit(f"{args.stage} is unavailable: {error}") from error

    if args.stage not in ("h3", "h5"):
        table["input_incomplete_rows"] = input_incomplete_rows
        table["scipy_version"] = scipy.__version__
        table["numpy_version"] = np.__version__
        if comparison_incomplete_rows is not None:
            table["comparison_incomplete_rows"] = comparison_incomplete_rows
    table.to_csv(path, index=False)
    print(f"{len(table)} rows -> {path}")
    if not table.empty:
        pd.set_option("display.width", 200)
        print(table.head(40).to_string(index=False))


def run_stage(
    args: argparse.Namespace,
    parser: argparse.ArgumentParser,
    raw: pathlib.Path,
    out: pathlib.Path,
    metric: str,
) -> tuple[pd.DataFrame, int, int | None]:
    """Compute one registered stage; raise RegisteredAnalysisError to fail closed."""
    input_incomplete_rows = 0
    comparison_incomplete_rows = None
    if args.stage in ("h3", "h5"):
        if args.cell_predictions is None:
            parser.error(f"--stage {args.stage} requires --cell-predictions")
        if args.model_artifact is None:
            parser.error(f"--stage {args.stage} requires --model-artifact")
        import cost_model

        for path in (args.cell_predictions, args.model_artifact):
            if not path.is_file():
                raise RegisteredAnalysisError(
                    f"{args.stage.upper()} input does not exist: {path}"
                )
        verify_file_sidecar(args.cell_predictions)
        try:
            _, model_hash = cost_model.read_artifact(args.model_artifact)
        except SystemExit as error:
            raise RegisteredAnalysisError(f"{args.stage.upper()} model artifact: {error}")
        predictions = pd.read_csv(args.cell_predictions)
        if set(predictions["model_artifact_sha256"].astype(str)) != {model_hash}:
            raise RegisteredAnalysisError(
                f"{args.stage.upper()} predictions use another model artifact"
            )
        table = (
            h3_decisions(predictions)
            if args.stage == "h3"
            else h5_decision(predictions, pd.read_csv(args.h5_draws))
        )
    else:
        runs = data.load_runs(raw, args.mode)
        input_incomplete_rows = int(runs.attrs.get("incomplete_rows", 0))
        if args.blinded:
            assert_blinded(runs)

        if args.stage == "precision":
            table = precision(runs, args.subject, args.baseline, metric)
        elif args.stage == "checksums":
            table = checksum_agreement(runs)
            if not table.empty:
                print(table.to_string(index=False))
                raise SystemExit("cross-structure checksum disagreement")
            print("all eligible cells agree")
        elif args.stage == "h1":
            import cost_model

            structural = cost_model.load_structural(raw)
            table = h1_identities(runs, structural)
        elif args.stage == "primary":
            table = primary_family(
                runs, pd.read_csv(args.primary_cells), args.subject, args.baseline, metric
            )
        elif args.stage == "regime":
            table = broad_regime(runs, args.subject, metric)
        elif args.stage == "hierarchical":
            diagnostics_path = out / f"hierarchical_{args.metric}_diagnostics.json"
            try:
                table = hierarchical(runs, args.subject, args.baseline, metric)
                diagnostics_path.write_text(
                    json.dumps(table.attrs["fit_diagnostics"], indent=2, sort_keys=True) + "\n"
                )
            except RegisteredAnalysisError as error:
                diagnostics_path.write_text(
                    json.dumps(error.diagnostics, indent=2, sort_keys=True) + "\n"
                )
                raise
        elif args.stage == "feasibility":
            table = feasibility(runs)
        elif args.stage == "mean-median":
            table = mean_median_sensitivity(
                runs,
                args.subject,
                args.baseline,
                metric,
                pd.read_csv(args.primary_cells),
            )
        elif args.stage == "order":
            table = execution_order_regression(
                runs,
                args.subject,
                args.baseline,
                metric,
                pd.read_csv(args.primary_cells),
            )
        elif args.stage == "leave-one-out":
            table = leave_one_trial_out(
                runs,
                args.subject,
                args.baseline,
                metric,
                pd.read_csv(args.primary_cells),
            )
        elif args.stage in ("h4", "compiler", "allocator"):
            if args.comparison_campaign is None:
                parser.error(f"--stage {args.stage} requires --comparison-campaign")
            comparison_runs = data.load_runs(args.comparison_campaign / "raw", args.mode)
            comparison_incomplete_rows = int(
                comparison_runs.attrs.get("incomplete_rows", 0)
            )
            if args.blinded:
                assert_blinded(comparison_runs)
            if args.stage == "h4":
                (out / f"h4_{args.metric}_disagreements.csv").unlink(missing_ok=True)
                left = paired_cell_ratios(runs, args.subject, metric, [args.baseline])
                right = paired_cell_ratios(
                    comparison_runs, args.subject, metric, [args.baseline]
                )
                result = h4_agreement(left, right, pd.read_csv(args.h4_cells))
                table = pd.DataFrame(
                    [
                        {
                            "cells": result["cells"],
                            "agreement": result["agreement"],
                            "supported": result["supported"],
                            "subject": args.subject,
                            "baseline": args.baseline,
                        }
                    ]
                )
                result["disagreements"].to_csv(
                    out / f"h4_{args.metric}_disagreements.csv", index=False
                )
            else:
                assert_dimension_changed(
                    args.campaign / "raw", args.comparison_campaign / "raw", args.stage, args.mode
                )
                table = campaign_sensitivity(
                    runs,
                    comparison_runs,
                    args.stage,
                    pd.read_csv(args.sensitivity_cells),
                    args.subject,
                    args.baseline,
                    metric,
                )
        else:
            raise AssertionError(f"unhandled stage {args.stage}")
    return table, input_incomplete_rows, comparison_incomplete_rows


if __name__ == "__main__":
    main()
