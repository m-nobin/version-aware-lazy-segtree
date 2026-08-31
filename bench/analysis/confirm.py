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
import pathlib
import sys

import numpy as np
import pandas as pd
from scipy import stats

sys.path.insert(0, str(pathlib.Path(__file__).parent))

import blind  # noqa: E402
import data  # noqa: E402

DELTA = 1.05
LOG_DELTA = float(np.log(DELTA))
BOOTSTRAP_RESAMPLES = 10_000
BOOTSTRAP_SEED = 20270214
MIN_TRIALS = 20
CELL_KEYS = ["workload", "n", "axis", "variant"]

METRICS = {
    "update": "update_ns_per_op",
    "query": "query_ns_per_op",
}


def bootstrap_median_ci(
    values: np.ndarray,
    resamples: int = BOOTSTRAP_RESAMPLES,
    seed: int = BOOTSTRAP_SEED,
) -> tuple[float, float]:
    """Seeded percentile bootstrap confidence interval for the median."""
    if len(values) < 2:
        return (float("nan"), float("nan"))
    rng = np.random.default_rng(seed)
    draws = rng.choice(values, size=(resamples, len(values)), replace=True)
    medians = np.median(draws, axis=1)
    return tuple(np.percentile(medians, [2.5, 97.5]))


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
    rather than silently dropped.
    """
    rows = []
    for keys, cell in runs.groupby(CELL_KEYS, sort=False):
        mine = cell[(cell["structure"] == subject) & cell["complete"]]
        mine = mine.set_index("trial")[metric].dropna()
        for name, other in cell.groupby("structure"):
            if name == subject or (baselines is not None and name not in baselines):
                continue
            theirs = other[other["complete"]].set_index("trial")[metric].dropna()
            shared = mine.index.intersection(theirs.index)
            row = dict(zip(CELL_KEYS, keys))
            row.update({"baseline": name, "metric": metric, "pairs": int(len(shared))})
            if len(shared) >= 4:
                log_ratios = np.log(
                    theirs.loc[shared].to_numpy(float) / mine.loc[shared].to_numpy(float)
                )
                lo, hi = bootstrap_median_ci(log_ratios)
                try:
                    _, pvalue = stats.wilcoxon(log_ratios, alternative="two-sided")
                except ValueError:  # every pair identical
                    pvalue = 1.0
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

    ``primary_cells`` carries the registered (workload, n, variant) rows.
    Cells with fewer than MIN_TRIALS pairs are labeled underpowered whatever
    their interval says, per the registered precision rule.
    """
    table = paired_cell_ratios(runs, subject, metric, [baseline], log_delta)
    keys = primary_cells.assign(
        n=primary_cells["n"].astype(int), variant=primary_cells["variant"].astype(float)
    )
    table = table.merge(keys[["workload", "n", "variant"]], on=["workload", "n", "variant"])
    if table.empty:
        return table
    scored = table["p"].notna()
    table.loc[scored, "p_holm"] = data.holm(table.loc[scored, "p"].tolist())
    table.loc[table["pairs"] < MIN_TRIALS, "underpowered"] = True
    table.loc[
        table["underpowered"] & (table["classification"] == "practically equivalent"),
        "classification",
    ] = "inconclusive"
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


def hierarchical(
    runs: pd.DataFrame,
    subject: str = "persistent",
    baseline: str = "copy-on-push",
    metric: str = METRICS["update"],
) -> pd.DataFrame:
    """The registered mixed-effects view of the broad regime: per-trial log
    ratios with a fixed effect per cell and a random intercept per trial
    block. Returns one row per cell with the model effect and its interval.
    """
    import statsmodels.formula.api as smf

    rows = []
    for keys, cell in runs.groupby(CELL_KEYS, sort=False):
        mine = cell[(cell["structure"] == subject) & cell["complete"]]
        theirs = cell[(cell["structure"] == baseline) & cell["complete"]]
        merged = mine.merge(theirs, on="trial", suffixes=("_s", "_b"))
        for _, row in merged.iterrows():
            if row[f"{metric}_s"] > 0 and row[f"{metric}_b"] > 0:
                rows.append(
                    {
                        "cell": "|".join(str(k) for k in keys),
                        "trial": int(row["trial"]),
                        "log_ratio": float(np.log(row[f"{metric}_b"] / row[f"{metric}_s"])),
                    }
                )
    long = pd.DataFrame(rows)
    if long.empty or long["cell"].nunique() < 1:
        return pd.DataFrame()
    model = smf.mixedlm("log_ratio ~ 0 + C(cell)", long, groups=long["trial"])
    # No explicit method: statsmodels' own fallback chain (bfgs, then lbfgs,
    # then cg) is more robust than pinning to one optimizer. lbfgs alone
    # converged to a spurious near-zero fixed effect on Linux's BLAS for this
    # design (raw trial index as the group, shared across every cell) while
    # working on macOS for the same seed; the fallback chain is stable on
    # both.
    fitted = model.fit(maxiter=200)
    effects = fitted.params.filter(like="C(cell)")
    conf = fitted.conf_int().loc[effects.index]
    out = pd.DataFrame(
        {
            "cell": [name.split("[")[1].rstrip("]") for name in effects.index],
            "effect": effects.to_numpy(float),
            "ci_lo": conf[0].to_numpy(float),
            "ci_hi": conf[1].to_numpy(float),
        }
    )
    out["classification"] = [classify(lo, hi) for lo, hi in zip(out["ci_lo"], out["ci_hi"])]
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
    merged = runs[runs["complete"]].merge(structural, on=keys, suffixes=("", "_structural"))
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
            continue
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


def h4_agreement(
    machine_a: pd.DataFrame, machine_b: pd.DataFrame, cells: pd.DataFrame | None = None
) -> dict:
    """H4: classification stability across microarchitectures.

    Joins two classification tables on (cell, baseline, metric) and reports
    the agreement share over the prespecified replication cells (all shared
    classified cells when ``cells`` is None). Disagreements are returned for
    analysis, not hidden.
    """
    keys = CELL_KEYS + ["baseline", "metric"]
    merged = machine_a.merge(machine_b, on=keys, suffixes=("_a", "_b"))
    merged = merged[(merged["classification_a"] != "") & (merged["classification_b"] != "")]
    if cells is not None:
        cells = cells.assign(n=cells["n"].astype(int), variant=cells["variant"].astype(float))
        merged = merged.merge(cells[["workload", "n", "variant"]], on=["workload", "n", "variant"])
    agree = merged["classification_a"] == merged["classification_b"]
    return {
        "cells": int(len(merged)),
        "agreement": float(agree.mean()) if len(merged) else float("nan"),
        "disagreements": merged.loc[
            ~agree, keys + ["classification_a", "classification_b"]
        ].reset_index(drop=True),
    }


H5_FACTOR = 1.5


def h5_region(
    actual: np.ndarray, predicted: np.ndarray, factor: float = H5_FACTOR
) -> dict:
    """H5: share of eligible cells whose observed outcome falls inside the
    registered predictive region ``predicted × [1/factor, factor]``."""
    actual = np.asarray(actual, dtype=float)
    predicted = np.asarray(predicted, dtype=float)
    eligible = (actual > 0) & (predicted > 0)
    inside = (actual[eligible] >= predicted[eligible] / factor) & (
        actual[eligible] <= predicted[eligible] * factor
    )
    return {
        "eligible": int(eligible.sum()),
        "inside": int(inside.sum()),
        "share": float(inside.mean()) if eligible.any() else float("nan"),
    }


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
        log_ratios = np.log(
            theirs.loc[shared].to_numpy(float) / mine.loc[shared].to_numpy(float)
        )
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


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--campaign", type=pathlib.Path, required=True)
    parser.add_argument(
        "--stage",
        choices=(
            "precision",
            "checksums",
            "h1",
            "primary",
            "regime",
            "hierarchical",
            "feasibility",
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
        help="replace structure names with the campaign's sealed labels; subject and "
        "baseline must then be sealed labels too",
    )
    parser.add_argument(
        "--primary-cells",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[1] / "primary_cells.csv",
    )
    args = parser.parse_args()

    raw = args.campaign / "raw"
    out = args.campaign / "analysis"
    out.mkdir(parents=True, exist_ok=True)
    runs = data.load_runs(raw, args.mode)
    if args.blinded:
        runs = blind.blind_frame(runs, blind.load_key(args.campaign))
    metric = METRICS[args.metric]

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
        table = hierarchical(runs, args.subject, args.baseline, metric)
    else:
        table = feasibility(runs)

    path = out / f"{args.stage}_{args.metric}.csv"
    table.to_csv(path, index=False)
    print(f"{len(table)} rows -> {path}")
    if not table.empty:
        pd.set_option("display.width", 200)
        print(table.head(40).to_string(index=False))


if __name__ == "__main__":
    main()
