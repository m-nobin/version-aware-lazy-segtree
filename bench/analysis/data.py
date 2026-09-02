"""Load the campaign, aggregate it, and test the head-to-head claims.

Every number that reaches a figure, a table or a sentence comes through here,
so the statistics are decided once rather than per plot.

The statistic is the median of the recorded trials, with a percentile
bootstrap interval around it. Trial counts here are small by design — each
trial replays two hundred thousand operations, so within-trial averaging has
already done most of the work — and a median with a bootstrap interval says
what a small sample supports without assuming a shape the sample cannot show.

Comparisons are paired on the trial index and tested with the Wilcoxon
signed-rank test (the column is named ``u`` for historical reasons; it holds
the Wilcoxon W statistic). One multiplicity family is one ``head_to_head``
call: every cell-by-baseline comparison for one subject and one metric, so
the campaign has two families, updates and queries. Within each family the
``significant`` flag is Benjamini-Hochberg at a 5% false-discovery rate; the
Holm-adjusted values are computed and kept beside it (see
``benjamini_hochberg`` for why Holm is not the deciding procedure here), and
Cliff's delta reports separation without a unit. ``ratio_lo``/``ratio_hi``
are the observed 2.5/97.5 percentiles of the per-trial ratios — the spread of
what was measured, not a confidence interval for the true ratio.
"""

from __future__ import annotations

import pathlib

import numpy as np
import pandas as pd
from scipy import stats

BOOTSTRAP_RESAMPLES = 10_000
BOOTSTRAP_SEED = 20260821


def load_runs(raw: pathlib.Path, mode: str = "timing") -> pd.DataFrame:
    """Read every per-workload CSV a campaign wrote.

    A run still in progress leaves a partial last line; half a row is not a
    data point, so incomplete rows are dropped rather than repaired.
    """
    frames = []
    incomplete_rows = 0
    for path in sorted(raw.glob(f"runs_{mode}-W*.csv")):
        try:
            frame = pd.read_csv(path)
        except pd.errors.EmptyDataError:
            # A campaign still in progress has opened its next file but not yet
            # written the header. An empty file is not a result.
            continue
        incomplete = frame.isna().any(axis=1)
        incomplete_rows += int(incomplete.sum())
        frames.append(frame.loc[~incomplete])
    if not frames:
        raise SystemExit(f"no {mode} results under {raw}")
    runs = pd.concat(frames, ignore_index=True)

    runs["update_ns_per_op"] = runs["update_ns"] / runs["updates"].replace(0, np.nan)
    runs["query_ns_per_op"] = runs["query_ns"] / runs["queries"].replace(0, np.nan)
    runs["ops"] = runs["updates"] + runs["queries"]
    runs["batch_ns_per_op"] = runs["batch_ns"].where(runs["batch_ns"] > 0) / runs["ops"]
    runs["instrumented_ns_per_op"] = (runs["update_ns"] + runs["query_ns"]) / runs["ops"]
    runs["retained_mib"] = runs["bytes"] / 1024**2
    runs["peak_alloc_mib"] = runs["alloc_peak_bytes"] / 1024**2
    # Nodes the update stream added, per update: a machine-independent measure
    # of how much a structure copies, comparable against a proved bound.
    runs["nodes_per_update"] = (runs["nodes"] - runs["build_nodes"]) / runs["updates"].replace(
        0, np.nan
    )
    runs["build_ns_per_element"] = runs["build_ns"] / runs["n"]
    runs["complete"] = runs["status"] == "ok"
    runs.attrs["incomplete_rows"] = incomplete_rows
    return runs


def load_memory(raw: pathlib.Path, mode: str = "timing") -> pd.DataFrame:
    """Read the growth samples taken during each trial."""
    frames = []
    for path in sorted(raw.glob(f"memory_{mode}-W*.csv")):
        try:
            frames.append(pd.read_csv(path).dropna())
        except pd.errors.EmptyDataError:
            continue
    if not frames:
        return pd.DataFrame()
    samples = pd.concat(frames, ignore_index=True)
    samples["retained_mib"] = samples["bytes"] / 1024**2
    return samples


def load_system(raw: pathlib.Path) -> dict:
    """Read the machine description recorded before the first workload."""
    paths = sorted(raw.glob("system_timing-*.txt"))
    if not paths:
        return {}
    facts = {}
    for line in paths[0].read_text().splitlines():
        if "=" in line:
            key, _, value = line.partition("=")
            facts[key] = value
    return facts


def load_environment(raw: pathlib.Path, mode: str = "timing") -> dict:
    """Read the build and protocol description recorded with the results."""
    paths = sorted(raw.glob(f"environment_{mode}-*.txt"))
    if not paths:
        return {}
    facts = {}
    for line in paths[0].read_text().splitlines():
        if "=" in line:
            key, _, value = line.partition("=")
            facts[key] = value
    return facts


def environment_values(raw: pathlib.Path, key: str, mode: str = "timing") -> set:
    """Every distinct value a campaign recorded for one environment field.

    `load_environment` reads the first file only, which is enough to describe a
    campaign but not to prove one. A guard that must hold for every measured
    process reads all of them, so a build or preload that changed partway
    through is visible as more than one value.
    """
    values = set()
    for path in sorted(raw.glob(f"environment_{mode}-*.txt")):
        for line in path.read_text().splitlines():
            name, separator, value = line.partition("=")
            if separator and name == key:
                values.add(value)
    return values


def power_states(raw: pathlib.Path) -> pd.DataFrame:
    """Which power source and load each workload was measured under."""
    rows = []
    for path in sorted(raw.glob("system_timing-*.txt")):
        facts = dict(
            line.partition("=")[::2] for line in path.read_text().splitlines() if "=" in line
        )
        rows.append(
            {
                "workload": path.stem.split("-")[-1],
                "power_source": facts.get("power_source", "unknown"),
                "load_average": facts.get("load_average", "unknown"),
            }
        )
    return pd.DataFrame(rows)


def _bootstrap_median(values: np.ndarray, rng: np.random.Generator) -> tuple[float, float]:
    """Percentile bootstrap interval for the median of a small sample."""
    if len(values) < 2:
        return (float("nan"), float("nan"))
    draws = rng.choice(values, size=(BOOTSTRAP_RESAMPLES, len(values)), replace=True)
    medians = np.median(draws, axis=1)
    return tuple(np.percentile(medians, [2.5, 97.5]))


CELL_KEYS = ["workload", "structure", "n", "axis", "variant"]

METRICS = [
    "update_ns_per_op",
    "query_ns_per_op",
    "batch_ns_per_op",
    "instrumented_ns_per_op",
    "retained_mib",
    "peak_alloc_mib",
    "nodes_per_update",
    "build_ns",
    "update_p50",
    "update_p99",
    "update_p999",
    "update_max",
    "query_p50",
    "query_p99",
    "query_p999",
    "query_max",
    "versions_stored",
]


def summarise(runs: pd.DataFrame) -> pd.DataFrame:
    """Collapse trials to one row per cell, with spread and an interval.

    Every metric gets a median, the observed range, a coefficient of variation
    and a bootstrap interval on the median. The coefficient of variation is the
    number that says whether a cell is stable enough to compare: a cell whose
    trials disagree by more than a few percent is reported as such rather than
    quietly averaged.
    """
    rng = np.random.default_rng(BOOTSTRAP_SEED)
    runs = runs.copy()
    runs["versions_stored"] = runs["updates"] + 1

    rows = []
    for keys, cell in runs.groupby(CELL_KEYS, sort=False):
        row = dict(zip(CELL_KEYS, keys))
        row["trials"] = len(cell)
        row["capped"] = bool((cell["status"] == "memory_cap").any())
        row["status"] = "memory_cap" if row["capped"] else "ok"
        row["updates"] = int(cell["updates"].median())
        row["queries"] = int(cell["queries"].median())
        row["k"] = int(cell["k"].iloc[0])
        row["checksum_agreement"] = cell["checksum"].nunique() <= len(cell["seed"].unique())
        for metric in METRICS:
            if metric not in cell:
                continue
            values = cell[metric].dropna().to_numpy(dtype=float)
            if len(values) == 0:
                row[metric] = np.nan
                continue
            row[metric] = float(np.median(values))
            row[f"{metric}_min"] = float(values.min())
            row[f"{metric}_max"] = float(values.max())
            centre = float(np.mean(values))
            row[f"{metric}_cv"] = (
                float(np.std(values, ddof=1) / centre)
                if len(values) > 1 and centre
                else 0.0
            )
            low, high = _bootstrap_median(values, rng)
            row[f"{metric}_lo"] = low
            row[f"{metric}_hi"] = high
        rows.append(row)
    return pd.DataFrame(rows)


def cliffs_delta(left: np.ndarray, right: np.ndarray) -> float:
    """How often one sample sits below the other, on a -1 to 1 scale.

    Zero means the two are interleaved; -1 means every value on the left is
    smaller than every value on the right. Unlike a ratio of medians it does
    not depend on the unit and it does not hide a wide overlap.
    """
    comparisons = np.sign(np.subtract.outer(left, right))
    return float(comparisons.mean())


def benjamini_hochberg(pvalues: list[float]) -> list[float]:
    """Control the false discovery rate over one family of comparisons.

    Holm is the instinctive choice and it is the wrong one here. The exact
    Wilcoxon signed-rank test on eleven pairs cannot report a p-value below
    2^-10, about 0.00098, however large the effect: with every pair pointing the
    same way the test has already produced all the evidence it can. Any
    procedure controlling the family-wise error rate over several hundred
    comparisons demands a smaller p than that, so it would reject nothing at all
    -- not because the differences are small but because the correction and the
    trial count are incompatible. That is a property of the arithmetic, not a
    finding about the structures.

    Controlling the expected share of false positives among the comparisons that
    are reported is the question actually being asked of a campaign this wide,
    so that is what is controlled. The Holm-adjusted values are computed too and
    kept beside these, and the count of comparisons where every paired trial
    agreed in direction is reported alongside both, since it needs no
    distributional assumption at all.
    """
    count = len(pvalues)
    order = np.argsort(pvalues)[::-1]
    adjusted = np.empty(count, dtype=float)
    running = 1.0
    for position, index in enumerate(order):
        rank = count - position
        running = min(running, pvalues[index] * count / rank)
        adjusted[index] = running
    return adjusted.tolist()


def holm(pvalues: list[float]) -> list[float]:
    """Holm step-down correction over one family of comparisons."""
    count = len(pvalues)
    order = np.argsort(pvalues)
    adjusted = np.empty(count, dtype=float)
    running = 0.0
    for rank, index in enumerate(order):
        value = (count - rank) * pvalues[index]
        running = max(running, value)
        adjusted[index] = min(1.0, running)
    return adjusted.tolist()


def head_to_head(runs: pd.DataFrame, subject: str, metric: str) -> pd.DataFrame:
    """Compare one structure against every other, cell by cell and trial by trial.

    The comparison is paired, because the experiment is. Within one trial every
    structure replays the same generated stream, from the same seed, under
    whatever the machine happened to be doing at that moment; the trial index is
    a block, not an independent sample. Pairing on it removes the variation the
    structures share -- which on this machine is substantial, since the first
    recorded trials of a process pay an allocator warm-up every structure pays
    at once -- and leaves the difference between the structures, which is the
    thing being measured.

    So the statistic is the median of the per-trial ratios rather than the ratio
    of the medians, and the test is the Wilcoxon signed-rank test on the paired
    differences rather than a two-sample test that would throw the pairing away.

    Only cells where both structures completed are compared. A structure that
    stopped at the memory cap replayed a shorter stream, so its per-operation
    time answers a different question and a ratio against it would be a category
    error rather than a result.
    """
    rows = []
    for keys, cell in runs.groupby(["workload", "n", "axis", "variant"], sort=False):
        workload, size, axis, variant = keys
        mine = cell[(cell["structure"] == subject) & cell["complete"]]
        if mine.empty:
            continue
        mine = mine.set_index("trial")[metric].dropna()
        for name, other in cell.groupby("structure"):
            if name == subject:
                continue
            theirs = other[other["complete"]].set_index("trial")[metric].dropna()
            shared = mine.index.intersection(theirs.index)
            if len(shared) < 4:
                continue
            ours = mine.loc[shared].to_numpy(dtype=float)
            base = theirs.loc[shared].to_numpy(dtype=float)
            ratios = base / ours
            try:
                statistic, pvalue = stats.wilcoxon(base, ours, alternative="two-sided")
            except ValueError:  # every pair identical
                statistic, pvalue = 0.0, 1.0
            rows.append(
                {
                    "workload": workload,
                    "n": size,
                    "axis": axis,
                    "variant": variant,
                    "baseline": name,
                    "metric": metric,
                    "pairs": len(shared),
                    "ours": float(np.median(ours)),
                    "theirs": float(np.median(base)),
                    "ratio": float(np.median(ratios)),
                    "ratio_lo": float(np.percentile(ratios, 2.5)),
                    "ratio_hi": float(np.percentile(ratios, 97.5)),
                    "won_pairs": int((base > ours).sum()),
                    "delta": cliffs_delta(ours, base),
                    "u": float(statistic),
                    "p": float(pvalue),
                }
            )
    frame = pd.DataFrame(rows)
    if frame.empty:
        return frame
    frame["p_holm"] = holm(frame["p"].tolist())
    frame["q"] = benjamini_hochberg(frame["p"].tolist())
    frame["significant"] = frame["q"] < 0.05
    frame["unanimous"] = (frame["won_pairs"] == frame["pairs"]) | (frame["won_pairs"] == 0)
    frame["ours_faster"] = frame["ratio"] > 1.0
    return frame


def warmup_profile(runs: pd.DataFrame) -> pd.DataFrame:
    """Cost by trial index, relative to the cell, pooled over every cell.

    This is the machine's warm-up transient made visible. It is reported rather
    than assumed away, and it is the reason the comparisons above are paired.
    """
    frames = []
    for _, cell in runs[runs["complete"]].groupby(["workload", "structure", "n", "variant"]):
        centre = cell["update_ns_per_op"].median()
        if centre and np.isfinite(centre):
            frames.append(
                pd.DataFrame(
                    {
                        "trial": cell["trial"].to_numpy(),
                        "structure": cell["structure"].to_numpy(),
                        "relative": (cell["update_ns_per_op"] / centre).to_numpy(),
                    }
                )
            )
    return pd.concat(frames, ignore_index=True) if frames else pd.DataFrame()


def feasibility(summary: pd.DataFrame) -> pd.DataFrame:
    """Largest size each structure completed, and where it first ran out."""
    rows = []
    for (workload, structure), cell in summary.groupby(["workload", "structure"], sort=False):
        done = cell[~cell["capped"]]
        capped = cell[cell["capped"]]
        rows.append(
            {
                "workload": workload,
                "structure": structure,
                "largest_completed_n": int(done["n"].max()) if not done.empty else None,
                "versions_there": int(done.loc[done["n"].idxmax(), "versions_stored"])
                if not done.empty
                else None,
                "first_capped_n": int(capped["n"].min()) if not capped.empty else None,
                "capped_at_versions": int(capped.loc[capped["n"].idxmin(), "versions_stored"])
                if not capped.empty
                else None,
            }
        )
    return pd.DataFrame(rows)
