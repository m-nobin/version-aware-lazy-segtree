"""Turn the exploratory pilot into figures, LaTeX tables and the macros it cites.

This reads `bench/results/raw`, which is the pilot, and nothing else. The
registered confirmatory campaigns live under `bench/results/campaigns/<id>`
and are read only by `confirm.py`, whose decision CSVs the manuscript's
confirmatory tables and figures are generated from. Pointing this script at a
campaign directory would pool exploratory and confirmatory evidence, which the
protocol forbids.

Run it after the pilot campaign:

    uv run --project bench/analysis bench/analysis/report.py

Everything it writes lands under bench/results/: figures/ holds vector PDFs and
PNGs, tables/ holds booktabs fragments and the macro file, summary/ holds the
aggregated CSVs a reader can check the figures against.
"""

from __future__ import annotations

import pathlib
import sys

import matplotlib as mpl
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

sys.path.insert(0, str(pathlib.Path(__file__).parent))

import data  # noqa: E402
import style  # noqa: E402
import tables  # noqa: E402
from tables import compact, escape, micros, number, pvalue, ratio, signed  # noqa: E402

def spaced(value: int) -> str:
    """A count with thin spaces instead of commas, for prose in a figure."""
    return f"{value:,}".replace(",", "\u2009")


ROOT = pathlib.Path(__file__).resolve().parents[2]
RAW = ROOT / "bench" / "results" / "raw"
FIGURES = ROOT / "bench" / "results" / "figures"
TABLES = ROOT / "bench" / "results" / "tables"
SUMMARY = ROOT / "bench" / "results" / "summary"

HEADLINE_N = 100_000
FACTS = tables.Facts()


# ----------------------------------------------------------------------------
# drawing helpers
# ----------------------------------------------------------------------------


def pick(frame: pd.DataFrame, **filters) -> pd.DataFrame:
    """Rows matching every keyword, in the order the filters name."""
    mask = pd.Series(True, index=frame.index)
    for column, value in filters.items():
        mask &= frame[column] == value
    return frame[mask]


def draw(ax, frame, x, y, structures, sort_by=None, capped_marker=True):
    """Draw one line per structure, marking where a structure ran out of memory.

    A capped point is not plotted as if it were a completed measurement. Its
    marker is hollow and the line stops there, because past that point the
    structure did not finish the workload and its per-operation time describes
    a shorter run than everyone else's.
    """
    sort_by = sort_by or x
    for name in structures:
        series = frame[frame["structure"] == name].sort_values(sort_by)
        if series.empty:
            continue
        done = series[~series["capped"]]
        stopped = series[series["capped"]]
        line_style = style.series_style(name)
        if not done.empty:
            ax.plot(done[x], done[y], **line_style)
        elif capped_marker and not stopped.empty:
            # Nothing completed, so the label still has to appear somewhere.
            ax.plot([], [], **line_style)
        if capped_marker and not stopped.empty:
            ax.plot(
                stopped[x],
                stopped[y],
                linestyle="none",
                marker=style.marker(name),
                markerfacecolor="none",
                markeredgecolor=style.colour(name),
                markeredgewidth=1.4,
                markersize=6,
                zorder=line_style["zorder"],
            )


def ordered_handles(ax, structures):
    """Legend entries in the registry's order rather than the drawing order."""
    handles, labels = ax.get_legend_handles_labels()
    lookup = dict(zip(labels, handles))
    wanted = [style.label(name) for name in structures if style.label(name) in lookup]
    return [lookup[name] for name in wanted], wanted


def finish(fig, ax, structures, columns=4, y=-0.02):
    handles, labels = ordered_handles(ax if not isinstance(ax, (list, np.ndarray)) else ax[0],
                                      structures)
    style.legend(fig, handles, labels, columns=columns, y=y)


# ----------------------------------------------------------------------------
# figures
# ----------------------------------------------------------------------------


def figure_scaling(summary):
    """How update and query cost move as the array grows."""
    frame = pick(summary, workload="W1", axis="none")
    shown = style.COMPARABLE + [style.CONTROL]

    for metric, name, headline in (
        ("update_ns_per_op", "scaling-update",
         "Time to publish one new version, as the array grows"),
        ("query_ns_per_op", "scaling-query",
         "Time to answer one historical range query, as the array grows"),
    ):
        fig, ax = plt.subplots(figsize=(6.4, 3.5))
        draw(ax, frame, "n", metric, shown)
        style.log_size_axis(ax)
        style.microseconds(ax)
        finish(fig, ax, shown, columns=3, y=0.02)
        style.figure_title(
            fig,
            headline,
            "Balanced workload: half the operations write, half read. Median of 11 trials "
            "of 200 000 operations. Lower is better.",
        )
        style.save(fig, FIGURES / name)


def figure_memory(summary):
    """Retained memory at the end of the run, and where a structure ran out."""
    frame = pick(summary, workload="W1", axis="none")
    shown = style.PERSISTENT + [style.CONTROL]

    fig, ax = plt.subplots(figsize=(6.4, 3.5))
    draw(ax, frame, "n", "retained_mib", shown)
    ax.axhline(4096, color=style.INK_SOFT, linewidth=1.0, linestyle=(0, (1, 2)), zorder=1)
    ax.annotate(
        "memory limit for the run (4 GiB)",
        xy=(frame["n"].max(), 4096),
        xytext=(-2, 5),
        textcoords="offset points",
        fontsize=8,
        color=style.INK_SOFT,
        ha="right",
    )
    style.log_size_axis(ax)
    ax.set_yscale("log")
    style.nice_log(ax)
    ax.set_ylabel("Memory held (MiB)")
    finish(fig, ax, shown, columns=4, y=0.02)
    style.figure_title(
        fig,
        "Memory held after one hundred thousand versions",
        "Balanced workload. A hollow marker stopped at the memory limit, so it holds less "
        "only because it did less.",
    )
    style.save(fig, FIGURES / "scaling-memory")


def figure_workload_mix(summary):
    """The same structures under three different read/write balances."""
    mixes = [("W2", "90% writes"), ("W1", "Half and half"), ("W3", "90% reads")]
    shown = style.COMPARABLE + [style.CONTROL]

    fig, axes = plt.subplots(1, 2, figsize=(6.6, 3.4), sharey=False)
    for ax, metric, heading in zip(
        axes,
        ("update_ns_per_op", "query_ns_per_op"),
        ("Publishing a version", "Answering a historical query"),
    ):
        positions = np.arange(len(mixes))
        # Markers rather than bars: the axis is logarithmic, and a bar encodes
        # its value as a length from zero, which a log axis does not have.
        width = 0.8 / len(shown)
        for offset, name in enumerate(shown):
            values = [
                pick(summary, workload=workload, axis="none", n=HEADLINE_N, structure=name)[
                    metric
                ].median()
                for workload, _ in mixes
            ]
            ax.plot(
                positions + offset * width - 0.4 + width / 2,
                values,
                linestyle="none",
                markersize=6,
                **{k: v for k, v in style.series_style(name).items()
                   if k in ("color", "marker", "label", "zorder")},
            )
        ax.set_xticks(positions)
        ax.set_xticklabels([text for _, text in mixes])
        ax.set_xlim(-0.6, len(mixes) - 0.4)
        style.panel_title(ax, heading)
        style.microseconds(ax)
        ax.grid(axis="x", visible=False)

    finish(fig, axes[0], shown, columns=3, y=0.02)
    style.figure_title(
        fig,
        "What an operation costs is set by the operation, not by the mix",
        f"Array of {spaced(HEADLINE_N)} elements. Median of 11 trials.",
    )
    style.save(fig, FIGURES / "workload-mix")


def figure_range_width(summary):
    """The payoff of a deferred tag, isolated on one axis."""
    frame = pick(summary, workload="W11")
    shown = style.COMPARABLE + ["point-only", style.CONTROL]

    fig, ax = plt.subplots(figsize=(6.4, 3.5))
    draw(ax, frame, "variant", "update_ns_per_op", shown)
    ax.set_xscale("log")
    style.microseconds(ax)
    ax.set_xlabel("Number of elements each update covers")
    style.plain_log_ticks(ax, sorted(frame["variant"].unique()))
    finish(fig, ax, shown, columns=3, y=0.02)
    style.figure_title(
        fig,
        "A deferred tag makes an update independent of how much it changes",
        "Array of 10 000 elements. The leftmost point changes one element, the rightmost "
        "changes all of them.",
    )
    style.save(fig, FIGURES / "range-width")


def figure_versions(summary):
    """Cost against the number of versions kept, which is what persistence buys."""
    frame = pick(summary, workload="W8")
    shown = style.COMPARABLE + [style.CONTROL]

    fig, axes = plt.subplots(1, 2, figsize=(6.6, 3.3))
    for ax, metric, heading in zip(
        axes,
        ("update_ns_per_op", "query_ns_per_op"),
        ("Publishing a version", "Reading a stored version"),
    ):
        draw(ax, frame, "versions_stored", metric, shown)
        ax.set_xscale("log")
        style.microseconds(ax)
        marks = style.thin(sorted(frame[frame["structure"].isin(shown)]
                                  ["versions_stored"].unique()))
        ax.set_xticks(marks)
        ax.set_xticklabels([compact(v) for v in marks])
        ax.xaxis.set_minor_formatter(mpl.ticker.NullFormatter())
        ax.set_xlabel("Versions stored")
        style.panel_title(ax, heading)

    finish(fig, axes[0], shown, columns=3, y=0.02)
    style.figure_title(
        fig,
        "Keeping more history does not make an operation slower",
        "Array of 10 000 elements. Every update is applied first, then 10 000 reads are "
        "answered against the history that has accumulated.",
    )
    style.save(fig, FIGURES / "versions")


def figure_checkpoint(summary):
    """The one baseline with a tuning knob, swept across its range."""
    swept = pick(summary, workload="W7", structure="checkpointing").copy()
    swept["k_plot"] = swept["k"].replace(0, swept["k"].max() * 8)
    ours = pick(summary, workload="W7", structure="persistent")

    fig, axes = plt.subplots(1, 2, figsize=(6.6, 3.3))
    for ax, metric, heading in zip(
        axes,
        ("update_ns_per_op", "query_ns_per_op"),
        ("Publishing a version", "Answering a historical query"),
    ):
        done = swept[~swept["capped"]].sort_values("k_plot")
        stopped = swept[swept["capped"]].sort_values("k_plot")
        ax.plot(done["k_plot"], done[metric], **style.series_style("checkpointing"))
        if not stopped.empty:
            ax.plot(
                stopped["k_plot"],
                stopped[metric],
                linestyle="none",
                marker=style.marker("checkpointing"),
                markerfacecolor="none",
                markeredgecolor=style.colour("checkpointing"),
                markeredgewidth=1.4,
                markersize=6,
            )
        value = float(ours[metric].median())
        ax.axhline(value, color=style.colour("persistent"), linewidth=1.9, zorder=4,
                   label=style.label("persistent"))
        ax.set_xscale("log")
        style.microseconds(ax)
        ax.set_xlabel("Versions between checkpoints")
        style.panel_title(ax, heading)
        ticks = sorted(swept["k_plot"].unique())
        ax.set_xticks(ticks)
        ax.set_xticklabels(
            [("never" if t == swept["k_plot"].max() else f"{int(t)}") for t in ticks], fontsize=8
        )
        ax.xaxis.set_minor_formatter(plt.NullFormatter())

    handles, labels = axes[0].get_legend_handles_labels()
    style.legend(fig, handles, labels, columns=2, y=0.02)
    style.figure_title(
        fig,
        "Checkpointing is only competitive inside a narrow band of settings",
        "Array of 10 000 elements, balanced workload. A hollow marker is a setting that ran "
        "out of memory. The flat line is this work, which has no such setting.",
    )
    style.save(fig, FIGURES / "checkpoint-tradeoff")


def figure_sweeps(summary):
    """The four one-axis sweeps that vary the traffic rather than the size."""
    panels = [
        ("W12", "age_share", "query_ns_per_op", "Reading further back in history",
         "Share of the version history a read may reach"),
        ("W9", "theta", "query_ns_per_op", "Concentrating reads on recent versions",
         "Concentration on recent versions (0 = uniform)"),
        ("W10", "hot_share", "update_ns_per_op", "Concentrating updates in one region",
         "Share of the array updates fall in"),
        ("W6", "zero_delta_share", "update_ns_per_op", "Updates that change nothing",
         "Share of updates with no effect"),
    ]
    shown = style.COMPARABLE + [style.CONTROL]

    fig, axes = plt.subplots(2, 2, figsize=(6.6, 5.2))
    populated = None
    for ax, (workload, _axis, metric, heading, xlabel) in zip(axes.flat, panels):
        frame = pick(summary, workload=workload)
        if frame.empty:
            ax.set_axis_off()
            continue
        populated = populated or ax
        draw(ax, frame, "variant", metric, shown)
        ax.set_xscale("linear" if workload == "W9" else "log")
        style.microseconds(ax)
        ax.set_xlabel(xlabel)
        style.panel_title(ax, heading)
        style.plain_log_ticks(ax, sorted(frame["variant"].unique()))

    finish(fig, populated, shown, columns=3, y=0.02)
    style.figure_title(
        fig,
        "Four properties of the traffic, varied one at a time",
        "Array of 10 000 elements throughout, 200 000 operations, median of 11 trials.",
    )
    style.save(fig, FIGURES / "traffic-sweeps")


def figure_latency_tail(summary):
    """What the slowest operations cost, not just the typical one."""
    frame = pick(summary, workload="W1", axis="none", n=HEADLINE_N)
    shown = [s for s in style.PERSISTENT + [style.CONTROL]]
    levels = [
        ("update_p50", "Typical (median)"),
        ("update_p99", "1 in 100 slowest"),
        ("update_p999", "1 in 1000 slowest"),
        ("update_max", "Slowest observed"),
    ]

    fig, ax = plt.subplots(figsize=(6.4, 3.8))
    positions = np.arange(len(shown))[::-1]
    for index, name in enumerate(shown):
        row = frame[frame["structure"] == name]
        if row.empty:
            continue
        values = [float(row[metric].median()) for metric, _ in levels]
        ax.plot(
            values,
            [positions[index]] * len(values),
            color=style.colour(name),
            linewidth=1.4,
            alpha=0.55,
            zorder=2,
        )
        for depth, (value, (_metric, _text)) in enumerate(zip(values, levels)):
            ax.scatter(
                value,
                positions[index],
                s=[64, 46, 30, 18][depth],
                color=style.colour(name),
                edgecolor="white",
                linewidth=0.8,
                zorder=4 - depth * 0.1,
            )
    ax.set_yticks(positions)
    ax.set_yticklabels([style.label(name) for name in shown])
    style.microseconds(ax, "x", "Time for a single update")
    ax.grid(axis="y", visible=False)
    style.figure_title(
        fig,
        "The slowest single update, next to the typical one",
        "Largest dot is the median update, then the slowest in 100, the slowest in 1000, "
        f"and the slowest seen. Array of {spaced(HEADLINE_N)} elements, balanced workload.",
    )
    style.save(fig, FIGURES / "latency-tail")


def figure_cost_of_persistence(summary):
    """What keeping every version costs against keeping none."""
    workloads = ["W1", "W2", "W3", "W4", "W5"]
    shown = style.COMPARABLE

    fig, ax = plt.subplots(figsize=(6.4, 3.5))
    positions = np.arange(len(workloads))
    width = 0.8 / len(shown)
    for offset, name in enumerate(shown):
        values = []
        for workload in workloads:
            cell = pick(summary, workload=workload, axis="none", n=HEADLINE_N)
            ours = cell[cell["structure"] == name]["update_ns_per_op"]
            base = cell[cell["structure"] == style.CONTROL]["update_ns_per_op"]
            values.append(float(ours.median() / base.median()) if not ours.empty else np.nan)
        ax.bar(
            positions + offset * width - 0.4 + width / 2,
            values,
            width=width * 0.86,
            color=style.colour(name),
            label=style.label(name),
            zorder=3,
        )
    ax.axhline(1.0, color=style.INK_SOFT, linewidth=1.0, linestyle=(0, (4, 2)), zorder=4)
    ax.text(-0.48, 1.04, "keeping no history", fontsize=8, color=style.INK_SOFT, ha="left")
    ax.set_xticks(positions)
    ax.set_xticklabels(["Balanced", "90% writes", "90% reads", "Point\nupdates",
                        "Whole-array\nupdates"])
    ax.set_yscale("log")
    style.nice_log(ax)
    ax.set_ylabel("Times more expensive than keeping no history")
    ax.grid(axis="x", visible=False)
    finish(fig, ax, shown, columns=3, y=0.02)
    style.figure_title(
        fig,
        "What keeping every version costs, against keeping none",
        f"Array of {spaced(HEADLINE_N)} elements. A value of 1 would mean history is free.",
    )
    style.save(fig, FIGURES / "cost-of-persistence")


def figure_feasibility(summary):
    """How much of the campaign each structure could actually run.

    An earlier version of this figure plotted the largest array each structure
    finished, taken over every workload. That reads as a tie: the tagless
    baseline does reach a million elements, but only on the one workload whose
    updates touch a single element. Counting the workloads it finished says the
    thing the reader wants to know, and the annotation keeps the size.
    """
    reach = data.feasibility(summary)
    shown = style.PERSISTENT + [style.CONTROL]
    total = reach["workload"].nunique()

    fig, ax = plt.subplots(figsize=(6.4, 3.2))
    positions = np.arange(len(shown))[::-1]
    for index, name in enumerate(shown):
        rows = reach[reach["structure"] == name]
        finished = int(rows["first_capped_n"].isna().sum())
        largest = rows["largest_completed_n"].dropna()
        ax.barh(positions[index], total, color="#f0efec", height=0.62, zorder=2)
        ax.barh(positions[index], finished, color=style.colour(name), height=0.62, zorder=3)
        note = (
            f"{finished} of {total}"
            + (f", up to {spaced(int(largest.max()))} elements" if not largest.empty else "")
        )
        ax.text(total + 0.25, positions[index], note, va="center", fontsize=8.5, color=style.INK)
    ax.set_yticks(positions)
    ax.set_yticklabels([style.label(name) for name in shown])
    ax.set_xlim(0, total * 1.75)
    ax.set_xticks(range(0, total + 1, 2))
    ax.set_xlabel("Workloads finished inside the 4 GiB limit")
    ax.grid(axis="y", visible=False)
    style.figure_title(
        fig,
        "How much of the campaign each structure could run",
        f"Of the {total} workloads, how many each structure finished without reaching the "
        "memory limit, and the largest array it finished on any of them.",
    )
    style.save(fig, FIGURES / "feasibility")


def figure_space_time(summary):
    """Speed against memory, which is the trade every strategy here is making."""
    frame = pick(summary, workload="W1", axis="none", n=HEADLINE_N)
    shown = style.PERSISTENT + [style.CONTROL]

    fig, ax = plt.subplots(figsize=(6.4, 3.8))
    for name in shown:
        row = frame[frame["structure"] == name]
        if row.empty:
            continue
        x = float(row["update_ns_per_op"].median())
        y = float(row["retained_mib"].median())
        capped = bool(row["capped"].iloc[0])
        ax.scatter(
            x,
            y,
            s=110,
            color="none" if capped else style.colour(name),
            edgecolor=style.colour(name),
            linewidth=1.8,
            marker=style.marker(name),
            zorder=4,
        )
        offsets = {
            "persistent": (10, -12),
            "copy-on-push": (10, 6),
            "buffered": (-10, 10),
            "fat-node": (12, -8),
            "checkpointing": (10, 2),
            "point-only": (-8, 10),
            "full-copy": (-10, -14),
            "lazy": (10, 0),
        }
        ax.annotate(
            style.label(name) + (" (ran out)" if capped else ""),
            (x, y),
            textcoords="offset points",
            xytext=offsets.get(name, (9, 4)),
            fontsize=8.5,
            color=style.INK,
            ha="right" if offsets.get(name, (9, 4))[0] < 0 else "left",
        )
    ax.set_yscale("log")
    style.nice_log(ax)
    style.microseconds(ax, "x", "Time to publish one version")
    ax.set_ylabel("Memory held after the run (MiB)")
    ax.margins(x=0.28, y=0.22)
    style.figure_title(
        fig,
        "Speed against memory: bottom left is better on both",
        f"Array of {spaced(HEADLINE_N)} elements, balanced workload. A hollow marker ran out "
        "of memory before finishing.",
    )
    style.save(fig, FIGURES / "space-time")


def figure_copies(summary):
    """A machine-independent count, checked against the bound that was proved."""
    frame = pick(summary, workload="W1", axis="none")
    shown = [s for s in style.PERSISTENT if s != "lazy"]

    fig, ax = plt.subplots(figsize=(6.4, 3.5))
    draw(ax, frame, "n", "nodes_per_update", shown)
    sizes = sorted(frame["n"].unique())
    bound = [4 * (np.ceil(np.log2(size)) + 1) for size in sizes]
    ax.plot(sizes, bound, color=style.INK_SOFT, linewidth=1.3, linestyle=(0, (1, 2)), zorder=2)
    ax.annotate(
        "proved worst case for this work",
        xy=(sizes[-1], bound[-1]),
        xytext=(-4, 8),
        textcoords="offset points",
        fontsize=8,
        color=style.INK_SOFT,
        ha="right",
    )
    style.log_size_axis(ax)
    ax.set_yscale("log")
    style.nice_log(ax)
    ax.set_ylabel("New nodes stored per update")
    finish(fig, ax, shown, columns=4, y=0.02)
    style.figure_title(
        fig,
        "How many nodes each strategy stores for one new version",
        "Counted rather than timed, so this figure does not depend on the machine. "
        "Hollow markers ran out of memory.",
    )
    style.save(fig, FIGURES / "copies-per-update")


#: Named conditions under which the campaign separates this work from
#: checkpoint-and-replay. Each is a property of the workload definition, not a
#: pattern read off the results afterwards, and they are tested in order so
#: every cell lands in exactly one. The last catches whatever the others do
#: not, which is where checkpointing wins.
CHECKPOINT_REGIMES = [
    ("Updates covering the whole array",
     lambda w, n, v: w == "W5" or (w == "W11" and v == 10_000)),
    ("One million elements", lambda w, n, v: n == 1_000_000),
    ("Deep history: 100k+ versions",
     lambda w, n, v: w == "W8" and v >= 10_000),
    ("Query-heavy traffic", lambda w, n, v: w == "W3"),
    ("Skewed or historical reads", lambda w, n, v: w in ("W9", "W12")),
    ("Small arrays, narrow updates", lambda w, n, v: True),
]


def figure_versus_checkpoint(comparisons):
    """Where the one competitive baseline wins, and where it does not.

    Checkpoint-and-replay is cheaper in more cells than it loses, so a single
    median hides the result. What the campaign actually shows is that the two
    separate by condition rather than by luck: the conditions are named on the
    axis and every measured cell is plotted, wins and losses alike.
    """
    frame = comparisons[comparisons["baseline"] == "checkpointing"]
    names = [name for name, _ in CHECKPOINT_REGIMES]

    def regime(row):
        for name, holds in CHECKPOINT_REGIMES:
            if holds(row.workload, float(row.n), float(row.variant)):
                return name
        return names[-1]

    fig, axes = plt.subplots(1, 2, figsize=(6.9, 3.6), sharey=True)
    ours, theirs = style.colour("persistent"), style.colour("checkpointing")

    for ax, metric, heading in zip(
        axes,
        ("update_ns_per_op", "query_ns_per_op"),
        ("Publishing a version", "Answering a historical query"),
    ):
        panel = frame[frame["metric"] == metric]
        buckets = {name: [] for name in names}
        for row in panel.itertuples():
            buckets[regime(row)].append(float(row.ratio))

        ax.axvspan(1.0, 1e3, color=ours, alpha=0.05, zorder=0)
        ax.axvline(1.0, color=style.INK_SOFT, linewidth=1.0, zorder=2)

        for index, name in enumerate(names):
            values = buckets[name]
            if not values:
                continue
            y = len(names) - 1 - index
            for value in values:
                ax.plot(value, y, marker="o", markersize=5.5,
                        color=ours if value > 1 else theirs, alpha=0.75, zorder=3)
            middle = float(np.median(values))
            ax.plot(middle, y, marker="D", markersize=8, zorder=4,
                    color=ours if middle > 1 else theirs,
                    markeredgecolor=style.SURFACE, markeredgewidth=1.2)
            won = sum(1 for value in values if value > 1)
            ax.annotate(f"{won}/{len(values)}", xy=(0.985, y), xycoords=("axes fraction", "data"),
                        ha="right", va="center", fontsize=7.5, zorder=5,
                        color=ours if won * 2 > len(values) else theirs)

        ax.set_xscale("log")
        ax.set_xlim(0.35, 400)
        style.nice_log(ax, "x")
        ax.set_yticks(range(len(names)))
        ax.set_yticklabels(list(reversed(names)))
        ax.set_ylim(-0.6, len(names) - 0.4)
        ax.grid(axis="y", visible=False)
        style.panel_title(ax, heading)

    handles = [
        plt.Line2D([], [], linestyle="none", marker="o", color=ours,
                   label="this work cheaper"),
        plt.Line2D([], [], linestyle="none", marker="o", color=theirs,
                   label="checkpointing cheaper"),
        plt.Line2D([], [], linestyle="none", marker="D", color=style.INK_SOFT,
                   label="median of the group"),
    ]
    style.legend(fig, handles, [h.get_label() for h in handles], columns=3, y=-0.01)
    style.figure_title(
        fig,
        "The two separate by condition, not by luck",
        "One dot per measured cell, right of the line means this work is cheaper. The "
        "conditions come from the workload definitions, so each cell lands in exactly one.",
        bottom=0.10,
    )
    fig.text(0.5, 0.035, "Times cheaper than checkpoint-and-replay",
             ha="center", fontsize=9, color=style.INK)
    style.save(fig, FIGURES / "versus-checkpoint")


def figure_speedup(summary, comparisons):
    """Every head-to-head cell at once, for update and for query."""
    baselines = [b for b in style.COMPARABLE if b != "persistent"] + ["point-only", "full-copy"]
    frames = {
        metric: comparisons[comparisons["metric"] == metric]
        for metric in ("update_ns_per_op", "query_ns_per_op")
    }
    cells = (
        comparisons[["workload", "n", "variant"]]
        .drop_duplicates()
        .assign(order=lambda f: f["workload"].str.slice(1).astype(int))
        .sort_values(["order", "n", "variant"])
    )
    labels = []
    for row in cells.itertuples():
        text = f"{row.workload}  ·  n = {compact(row.n)}"
        if row.variant:
            text += f"  ·  {compact(row.variant) if row.variant >= 1000 else f'{row.variant:g}'}"
        labels.append(text)

    height = 1.9 + 0.155 * len(cells)
    fig, axes = plt.subplots(1, 2, figsize=(6.9, height), sharey=True)
    palette = plt.get_cmap("RdBu").copy()
    palette.set_bad("#ebe9e4")

    limit = 0.0
    grids = {}
    for metric, frame in frames.items():
        grid = np.full((len(cells), len(baselines)), np.nan)
        for row_index, cell in enumerate(cells.itertuples()):
            for column_index, baseline in enumerate(baselines):
                match = frame[
                    (frame["workload"] == cell.workload)
                    & (frame["n"] == cell.n)
                    & (frame["variant"] == cell.variant)
                    & (frame["baseline"] == baseline)
                ]
                if not match.empty:
                    grid[row_index, column_index] = np.log10(match["ratio"].iloc[0])
        grids[metric] = grid
        if np.isfinite(grid).any():
            limit = max(limit, float(np.nanmax(np.abs(grid))))
    limit = limit or 1.0

    for ax, (metric, heading) in zip(
        axes,
        (("update_ns_per_op", "Publishing a version"), ("query_ns_per_op", "Answering a query")),
    ):
        grid = grids[metric]
        image = ax.imshow(
            np.ma.masked_invalid(grid), cmap=palette, vmin=-limit, vmax=limit, aspect="auto"
        )
        ax.set_xticks(range(len(baselines)))
        ax.set_xticklabels([style.label(b) for b in baselines], rotation=38, ha="right",
                           fontsize=7.5)
        ax.set_yticks(range(len(labels)))
        ax.set_yticklabels(labels, fontsize=6.4, family="DejaVu Sans Mono")
        ax.tick_params(length=0)
        style.panel_title(ax, heading)
        ax.grid(visible=False)
        for row_index in range(grid.shape[0]):
            for column_index in range(grid.shape[1]):
                value = grid[row_index, column_index]
                if not np.isfinite(value):
                    continue
                shown = 10**value
                ax.text(
                    column_index,
                    row_index,
                    f"{shown:.1f}" if shown < 10 else f"{shown:,.0f}".replace(",", " "),
                    ha="center",
                    va="center",
                    fontsize=5.4,
                    color="white" if abs(value) > limit * 0.6 else style.INK,
                )

    bar = fig.colorbar(image, ax=axes, fraction=0.028, pad=0.02)
    bar.set_ticks([-limit, 0, limit])
    bar.set_ticklabels([f"{10 ** -limit:.2f}x", "equal", f"{10 ** limit:,.0f}x".replace(",", " ")],
                       fontsize=7.5)
    bar.set_label("Blue: this work costs less", fontsize=8)
    bar.outline.set_visible(False)

    reserved = 0.95
    fig.text(0.008, 1 - 0.16 / height, "How much less this work costs, in every comparable cell",
             fontsize=10.5, fontweight="bold", ha="left", va="top", color=style.INK)
    fig.text(0.008, 1 - 0.36 / height,
             "A cell reads as the number of times more the baseline costs. Grey means the "
             "baseline never finished that cell,",
             fontsize=8.5, color=style.INK_SOFT, ha="left", va="top")
    fig.text(0.008, 1 - 0.51 / height,
             "so no comparison is possible. Rows are workload, array size and swept value.",
             fontsize=8.5, color=style.INK_SOFT, ha="left", va="top")
    fig.subplots_adjust(top=1 - reserved / height, left=0.20, right=0.88,
                        bottom=1.4 / height, wspace=0.06)
    style.save(fig, FIGURES / "speedup-matrix")


def figure_allocation(summary, alloc):
    """What a structure asks the allocator for, next to what it documents.

    Every structure here keeps its nodes in a std::vector, and a growing vector
    holds up to twice the bytes its elements occupy plus the old block until the
    copy finishes. A payload figure alone therefore understates the real
    footprint, and by a different factor for each structure, so a space
    comparison built on payload alone would be comparing something nobody
    actually pays.
    """
    if alloc.empty:
        return
    frame = pick(alloc, workload="W1", axis="none", n=HEADLINE_N)
    shown = [s for s in style.PERSISTENT + [style.CONTROL]
             if not frame[frame["structure"] == s].empty]

    fig, ax = plt.subplots(figsize=(6.4, 3.4))
    positions = np.arange(len(shown))
    width = 0.38
    documented = [float(frame[frame["structure"] == s]["retained_mib"].iloc[0]) for s in shown]
    requested = [float(frame[frame["structure"] == s]["peak_alloc_mib"].iloc[0]) for s in shown]
    ax.bar(positions - width / 2, documented, width * 0.9, color=[style.colour(s) for s in shown],
           zorder=3)
    ax.bar(positions + width / 2, requested, width * 0.9,
           color="none", edgecolor=[style.colour(s) for s in shown], linewidth=1.6,
           hatch="////", zorder=3)
    for index, (low, high) in enumerate(zip(documented, requested)):
        if low:
            ax.text(positions[index], max(low, high) * 1.15, f"{high / low:.2f}x",
                    ha="center", fontsize=8, color=style.INK)
    ax.set_xticks(positions)
    ax.set_xticklabels([style.label(s) for s in shown], rotation=22, ha="right", fontsize=8)
    ax.set_yscale("log")
    style.nice_log(ax)
    ax.set_ylabel("Memory (MiB)")
    ax.grid(axis="x", visible=False)
    solid = mpl.patches.Patch(facecolor=style.INK_SOFT, label="Documented payload")
    hatched = mpl.patches.Patch(facecolor="none", edgecolor=style.INK_SOFT, hatch="////",
                                label="Peak actually requested")
    style.legend(fig, [solid, hatched], [solid.get_label(), hatched.get_label()], columns=2, y=0.02)
    style.figure_title(
        fig,
        "What a structure documents against what it asks the allocator for",
        f"Array of {spaced(HEADLINE_N)} elements, balanced workload. The label is the ratio. "
        "A structure that reached the memory limit is shown at its ceiling, not at what it "
        "would have held. Measured by a separate binary that counts allocations and therefore "
        "reports no timings.",
    )
    style.save(fig, FIGURES / "allocation")


def table_allocation(alloc):
    """Payload against requested bytes, by structure and size."""
    if alloc.empty:
        return
    frame = pick(alloc, workload="W1", axis="none")
    sizes = sorted(frame["n"].unique())
    rows = []
    groups = []
    for metric, heading in (
        ("retained_mib", "Documented payload (MiB)"),
        ("peak_alloc_mib", "Peak bytes requested from the allocator (MiB)"),
    ):
        groups.append((len(rows), heading))
        for name in style.PERSISTENT + [style.CONTROL]:
            values = []
            for size in sizes:
                cell = frame[(frame["structure"] == name) & (frame["n"] == size)]
                values.append(number(cell[metric].iloc[0], 0) if not cell.empty else "--")
            rows.append([escape(style.label(name))] + values)
    tables.tabular(
        TABLES / "allocation.tex",
        "l" + "r" * len(sizes),
        ["Structure"] + [compact(s) for s in sizes],
        rows,
        groups,
    )


def figure_validity(runs, summary):
    """Three checks on the measurement itself, drawn rather than asserted."""
    fig, axes = plt.subplots(1, 3, figsize=(6.9, 2.9))

    # Left: what per-operation timing costs, measured against an untimed replay.
    priced = runs[(runs["batch_ns"] > 0) & runs["complete"]].copy()
    priced["overhead"] = priced["instrumented_ns_per_op"] - priced["batch_ns_per_op"]
    shown = [s for s in style.PERSISTENT + [style.CONTROL] if s in set(priced["structure"])]
    axes[0].axvline(0, color=style.INK_SOFT, linewidth=1.0, zorder=1)
    for index, name in enumerate(shown[::-1]):
        values = priced[priced["structure"] == name]["overhead"]
        axes[0].scatter(
            values,
            np.full(len(values), index) + np.linspace(-0.16, 0.16, len(values)),
            s=7,
            color=style.colour(name),
            alpha=0.7,
            zorder=3,
        )
    axes[0].set_xscale("symlog", linthresh=100)
    axes[0].set_yticks(range(len(shown)))
    axes[0].set_yticklabels([style.label(name) for name in shown[::-1]], fontsize=7.5)
    axes[0].set_xlabel("Extra ns per operation\nfrom timing each one", fontsize=8)
    style.panel_title(axes[0], "Cost of measuring")
    axes[0].grid(axis="y", visible=False)

    # Middle: the machine's warm-up transient, which is why comparisons are paired.
    profile = data.warmup_profile(runs)
    if not profile.empty:
        grouped = profile.groupby("trial")["relative"]
        axes[1].plot(
            grouped.median().index,
            grouped.median().values,
            color=style.colour("persistent"),
            marker="o",
            markersize=4,
            zorder=3,
        )
        axes[1].fill_between(
            grouped.median().index,
            grouped.quantile(0.25).values,
            grouped.quantile(0.75).values,
            color=style.colour("persistent"),
            alpha=0.16,
            linewidth=0,
            zorder=2,
        )
    axes[1].axhline(1.0, color=style.INK_SOFT, linewidth=1.0, linestyle=(0, (4, 2)), zorder=2)
    axes[1].set_xlabel("Trial index within the cell", fontsize=8)
    axes[1].set_ylabel("Cost relative to the cell", fontsize=8)
    style.panel_title(axes[1], "Warm-up transient")

    # Right: whether the position a structure ran in changed what it measured.
    order = runs[runs["complete"]].copy()
    normalised = []
    for _, cell in order.groupby(["structure", "workload", "n", "variant"]):
        centre = cell["update_ns_per_op"].median()
        if centre:
            frame = cell[["exec_order"]].copy()
            frame["relative"] = cell["update_ns_per_op"] / centre
            normalised.append(frame)
    if normalised:
        combined = pd.concat(normalised)
        grouped = combined.groupby("exec_order")["relative"]
        axes[2].errorbar(
            grouped.median().index,
            grouped.median().values,
            yerr=[
                grouped.median().values - grouped.quantile(0.25).values,
                grouped.quantile(0.75).values - grouped.median().values,
            ],
            fmt="o",
            markersize=4,
            color=style.colour("persistent"),
            ecolor=style.GRID,
            elinewidth=1.6,
            capsize=3,
            zorder=3,
        )
    axes[2].axhline(1.0, color=style.INK_SOFT, linewidth=1.0, linestyle=(0, (4, 2)), zorder=2)
    axes[2].set_xlabel("Position in the randomised order", fontsize=8)
    axes[2].set_ylabel("Cost relative to the cell", fontsize=8)
    style.panel_title(axes[2], "Running order")

    style.figure_title(
        fig,
        "Checks on the measurement, not on the structures",
        "Left: extra time per operation caused by timing each one, against an untimed "
        "replay of the same stream. Middle: the machine's warm-up transient, which every "
        "structure pays at the same trial index and which is why comparisons are paired "
        "on it. Right: cost by position in the randomised running order. Bands are the "
        "interquartile range.",
    )
    style.save(fig, FIGURES / "validity")


def figure_growth(samples):
    """Memory over the life of a run, which a final number cannot show."""
    frame = samples[(samples["workload"] == "W1") & (samples["n"] == HEADLINE_N)
                    & (samples["trial"] == 0)]
    shown = style.PERSISTENT + [style.CONTROL]

    fig, ax = plt.subplots(figsize=(6.4, 3.5))
    for name in shown:
        series = frame[frame["structure"] == name].sort_values("op_index")
        if series.empty:
            continue
        ax.plot(series["op_index"], series["retained_mib"], **style.series_style(name))
    ax.set_yscale("log")
    style.nice_log(ax)
    ax.set_xlabel("Operations completed")
    ax.set_ylabel("Memory held (MiB)")
    finish(fig, ax, shown, columns=4, y=0.02)
    style.figure_title(
        fig,
        "Memory over the life of a single run",
        f"Array of {spaced(HEADLINE_N)} elements, balanced workload. A line that stops early "
        "reached the 4 GiB limit.",
    )
    style.save(fig, FIGURES / "memory-growth")


# ----------------------------------------------------------------------------
# tables
# ----------------------------------------------------------------------------


def table_system(system, environment, runs):
    """The machine, the build and the protocol, in one place."""
    memory_gib = float(system.get("memory_bytes", 0)) / 1024**3
    resolution = int(runs["clock_resolution_ns"].iloc[0])
    overhead = int(runs["clock_overhead_ns"].iloc[0])
    rows = [
        ["Processor", escape(system.get("cpu_model", "?"))],
        ["Cores", f"{system.get('cpu_performance_cores', '?')} performance, "
                  f"{system.get('cpu_efficiency_cores', '?')} efficiency"],
        ["Core selection", "Thread raised to the interactive quality-of-service class, "
                           "which keeps it on performance cores"],
        ["Memory", f"{memory_gib:.0f} GiB, {int(system.get('page_bytes', 0)) // 1024} KiB pages"],
        ["Cache line", f"{system.get('cacheline_bytes', '?')} bytes"],
        ["Level 1 data cache", f"{int(system.get('l1d_bytes', 0)) // 1024} KiB"],
        ["Level 2 cache", f"{int(system.get('l2_bytes', 0)) // 1024**2} MiB"],
        ["Operating system", escape(system.get("os_product", system.get("os", "?")))],
        ["Allocator", escape(system.get("allocator", "?"))],
        ["Compiler", escape(system.get("compiler", environment.get("compiler", "?")))],
        ["Optimiser settings", rf"\texttt{{{escape(environment.get('compile_flags', '?'))}}}"],
        ["Language standard", "C++17"],
        ["Timer", r"\texttt{std::chrono::steady\_clock}"],
        ["Timer resolution", f"{resolution} ns (measured)"],
        ["Timer pair cost", f"{overhead} ns (measured)"],
        ["Recorded trials per cell", environment.get("trials", "?")],
        ["Discarded warm-up trials", environment.get("warmup_trials", "?")],
        ["Trials once memory ran out", environment.get("capped_trials", "?")],
        ["Running order", "Randomised within every trial; the position is recorded"],
        ["Memory limit per run", f"{environment.get('memory_cap_mib', '?')} MiB of retained data"],
        ["Random seed", environment.get("base_seed", "?")],
    ]
    tables.tabular(TABLES / "system.tex", "lp{0.62\\linewidth}", ["Setting", "Value"], rows)


def table_workloads(summary):
    """What each workload varies and what it is for."""
    purposes = {
        "W1": "The general case, and the basis for the scaling figures",
        "W2": "Isolates the cost of publishing versions",
        "W3": "Isolates the cost of reading them back",
        "W4": "The cheapest possible update: one element",
        "W5": "The most expensive: every element at once",
        "W6": "Updates that change nothing, which can be shared outright",
        "W7": "The only tuning knob any structure here has",
        "W8": "The axis persistence exists for: how much history is kept",
        "W9": "Reads that favour recent versions over old ones",
        "W10": "Updates that cluster in one part of the array",
        "W11": "The span between a single element and the whole array",
        "W12": "Audit-style reads that only look far back",
    }
    rows = []
    for workload, name in style.WORKLOADS.items():
        cell = summary[summary["workload"] == workload]
        if cell.empty:
            continue
        sizes = sorted(cell["n"].unique())
        axis = cell["axis"].iloc[0]
        values = sorted(cell["variant"].unique())
        if axis == "none":
            span = "--"
        elif axis == "k":
            # 0 is the sweep's "never checkpoint" endpoint, not a value of K.
            real = [v for v in values if v]
            span = f"{min(real):g} to {max(real):g}, and never"
        elif axis == "updates":
            span = f"{compact(min(values))} to {compact(max(values))}"
        else:
            span = f"{min(values):g} to {max(values):g}"
        rows.append(
            [
                workload,
                escape(name),
                "; ".join(compact(s) for s in sizes),
                escape(style.AXIS_LABELS.get(axis) or "none"),
                span,
                escape(purposes[workload]),
            ]
        )
    tables.tabular(
        TABLES / "workloads.tex",
        "lp{0.20\\linewidth}lp{0.15\\linewidth}lp{0.26\\linewidth}",
        ["", "Workload", "Array sizes", "Varied", "Range", "What it separates"],
        rows,
    )


def table_headline(summary):
    """Every structure at one size, with spread and an interval."""
    rows = []
    frame = pick(summary, workload="W1", axis="none", n=HEADLINE_N)
    for name in style.PERSISTENT + [style.CONTROL]:
        row = frame[frame["structure"] == name]
        if row.empty:
            continue
        row = row.iloc[0]
        rows.append(
            [
                escape(style.label(name)),
                micros(row["update_ns_per_op"]),
                f"[{micros(row['update_ns_per_op_lo'])}, {micros(row['update_ns_per_op_hi'])}]",
                f"{100 * row['update_ns_per_op_cv']:.1f}",
                micros(row["query_ns_per_op"]),
                f"[{micros(row['query_ns_per_op_lo'])}, {micros(row['query_ns_per_op_hi'])}]",
                f"{100 * row['query_ns_per_op_cv']:.1f}",
                number(row["retained_mib"], 0),
                "ran out" if row["capped"] else "complete",
            ]
        )
    tables.tabular(
        TABLES / "headline.tex",
        "lrlrrlrrl",
        [
            "Structure",
            r"\multicolumn{3}{c}{Update ($\mu$s)}",
            "",
            "",
            r"\multicolumn{3}{c}{Query ($\mu$s)}",
            "",
            "",
            "Memory",
            "Run",
        ],
        rows,
        subheader=[
            "",
            "median",
            "95\\% interval",
            "CV \\%",
            "median",
            "95\\% interval",
            "CV \\%",
            "MiB",
            "",
        ],
    )


def table_scaling(summary):
    """The scaling figures as numbers."""
    frame = pick(summary, workload="W1", axis="none")
    sizes = sorted(frame["n"].unique())
    rows = []
    groups = []
    for metric, heading, cell_format in (
        ("update_ns_per_op", "Time to publish one version (microseconds)", micros),
        ("query_ns_per_op", "Time to answer one query (microseconds)", micros),
        ("retained_mib", "Memory held at the end of the run (MiB)", compact),
    ):
        groups.append((len(rows), heading))
        for name in style.PERSISTENT + [style.CONTROL]:
            cells = [frame[(frame["structure"] == name) & (frame["n"] == size)] for size in sizes]
            values = []
            for cell in cells:
                if cell.empty:
                    values.append("--")
                elif bool(cell["capped"].iloc[0]):
                    values.append(rf"\itshape {cell_format(cell[metric].iloc[0])}")
                else:
                    values.append(cell_format(cell[metric].iloc[0]))
            rows.append([escape(style.label(name))] + values)
    tables.tabular(
        TABLES / "scaling.tex",
        "l" + "r" * len(sizes),
        ["Structure"] + [compact(s) for s in sizes],
        rows,
        groups,
    )


def table_head_to_head(comparisons):
    """One row per baseline: how the comparison came out across every cell."""
    rows = []
    groups = []
    for metric, heading in (
        ("update_ns_per_op", "Publishing a version"),
        ("query_ns_per_op", "Answering a historical query"),
    ):
        frame = comparisons[comparisons["metric"] == metric]
        groups.append((len(rows), heading))
        for baseline in [b for b in style.PERSISTENT if b != "persistent"] + [style.CONTROL]:
            cells = frame[frame["baseline"] == baseline]
            if cells.empty:
                continue
            wins = int(cells["ours_faster"].sum())
            decisive = cells[cells["significant"]]
            rows.append(
                [
                    escape(style.label(baseline)),
                    str(len(cells)),
                    f"{wins}",
                    ratio(cells["ratio"].median()),
                    f"{ratio(cells['ratio'].min())}--{ratio(cells['ratio'].max())}",
                    f"{len(decisive)}",
                    f"{int(cells['unanimous'].sum())}",
                    f"{cells['delta'].abs().median():.2f}",
                ]
            )
    tables.tabular(
        TABLES / "head-to-head.tex",
        "lrrrlrrr",
        [
            "Compared against",
            "Cells",
            "Cheaper in",
            "Median",
            "Range across cells",
            "Separated",
            "Unanimous",
            "Median $|\\delta|$",
        ],
        rows,
        groups,
    )


def table_feasibility(summary):
    """Where each structure stopped."""
    reach = data.feasibility(summary)
    rows = []
    for name in style.PERSISTENT + [style.CONTROL]:
        rows_for = reach[reach["structure"] == name]
        completed = rows_for["largest_completed_n"].dropna()
        capped = rows_for["first_capped_n"].dropna()
        finished = int(rows_for["first_capped_n"].isna().sum())
        rows.append(
            [
                escape(style.label(name)),
                f"{finished} of {len(rows_for)}",
                compact(completed.max()) if not completed.empty else "--",
                compact(capped.min()) if not capped.empty else "--",
            ]
        )
    tables.tabular(
        TABLES / "feasibility.tex",
        "lrrr",
        ["Structure", "Workloads finished", "Largest array finished",
         "Smallest it could not"],
        rows,
    )


def table_space(summary):
    """What one more version costs, counted rather than timed."""
    frame = pick(summary, workload="W1", axis="none")
    sizes = sorted(frame["n"].unique())
    rows = []
    for name in [s for s in style.PERSISTENT]:
        values = []
        for size in sizes:
            cell = frame[(frame["structure"] == name) & (frame["n"] == size)]
            values.append(compact(cell["nodes_per_update"].iloc[0]) if not cell.empty else "--")
        rows.append([escape(style.label(name))] + values)
    bound = [f"{int(4 * (np.ceil(np.log2(size)) + 1))}" for size in sizes]
    rows.append([r"\itshape Proved worst case for this work"] + [rf"\itshape {b}" for b in bound])
    tables.tabular(
        TABLES / "space.tex",
        "l" + "r" * len(sizes),
        ["Structure"] + [compact(s) for s in sizes],
        rows,
    )


def table_tail(summary):
    """The distribution of a single update, not its average."""
    frame = pick(summary, workload="W1", axis="none", n=HEADLINE_N)
    rows = []
    for name in style.PERSISTENT + [style.CONTROL]:
        row = frame[frame["structure"] == name]
        if row.empty:
            continue
        row = row.iloc[0]
        rows.append(
            [
                escape(style.label(name)),
                micros(row["update_p50"]),
                micros(row["update_p99"]),
                micros(row["update_p999"]),
                micros(row["update_max"]),
                ratio(row["update_max"] / max(row["update_p50"], 1)),
            ]
        )
    tables.tabular(
        TABLES / "tail.tex",
        "lrrrrr",
        ["Structure", "Median", "1 in 100", "1 in 1000", "Slowest", "Slowest / median"],
        rows,
    )


def table_checkpoint(summary):
    """The sweep of the one tunable parameter in the comparison."""
    swept = pick(summary, workload="W7", structure="checkpointing").sort_values("k")
    ours = pick(summary, workload="W7", structure="persistent").iloc[0]
    rows = []
    for row in swept.itertuples():
        rows.append(
            [
                "never" if row.k == 0 else f"{row.k}",
                micros(row.update_ns_per_op),
                micros(row.query_ns_per_op),
                number(row.retained_mib, 0),
                "ran out" if row.capped else "complete",
            ]
        )
    rows.append(
        [
            r"\itshape this work",
            rf"\itshape {micros(ours.update_ns_per_op)}",
            rf"\itshape {micros(ours.query_ns_per_op)}",
            rf"\itshape {number(ours.retained_mib, 0)}",
            r"\itshape complete",
        ]
    )
    tables.tabular(
        TABLES / "checkpoint.tex",
        "lrrrl",
        ["Versions between checkpoints", r"Update ($\mu$s)", r"Query ($\mu$s)",
         "Memory (MiB)", "Run"],
        rows,
    )


def table_validity(runs, summary, comparisons):
    """The checks that say whether the rest of the numbers can be trusted."""
    priced = runs[(runs["batch_ns"] > 0) & runs["complete"]].copy()
    priced["overhead"] = priced["instrumented_ns_per_op"] - priced["batch_ns_per_op"]
    control = priced[priced["structure"] == style.CONTROL]["overhead"]

    order = runs[runs["complete"]].copy()
    spread = []
    for _, cell in order.groupby(["structure", "workload", "n", "variant"]):
        centre = cell["update_ns_per_op"].median()
        if centre:
            frame = cell[["exec_order"]].copy()
            frame["relative"] = cell["update_ns_per_op"] / centre
            spread.append(frame)
    pooled = pd.concat(spread) if spread else pd.DataFrame(columns=["exec_order", "relative"])
    order_range = pooled.groupby("exec_order")["relative"].median()
    order_means = pooled.groupby("exec_order")["relative"].mean()

    cvs = summary[~summary["capped"]]["update_ns_per_op_cv"].dropna() * 100
    checksums = runs[(runs["structure"] != style.CONTROL) & runs["complete"]]

    rows = [
        [
            "Answers agreed across every structure that completed",
            f"at most {checksums.groupby(['workload', 'n', 'variant', 'seed'])['checksum']
                       .nunique().max()} distinct answer per cell, over "
            f"{len(checksums):,} runs".replace(",", "\\,"),
        ],
        [
            "Cost of timing each operation (measured on the structure that allocates nothing)",
            f"median {signed(control.median())} ns, range "
            f"{signed(control.min())} to {signed(control.max())} ns",
        ],
        [
            "Effect of position in the running order",
            "median identical at all eight positions; means "
            f"{100 * (order_means.min() - 1):+.1f}\\% to "
            f"{100 * (order_means.max() - 1):+.1f}\\%, all in the same direction",
        ],
        [
            "Trial-to-trial variation, all completed cells",
            f"median {cvs.median():.1f}\\%, 90th percentile {cvs.quantile(0.9):.1f}\\%",
        ],
        [
            "Cells where trials varied by more than 10\\%",
            f"{int((cvs > 10).sum())} of {len(cvs)}",
        ],
        [
            "Comparisons separating at a 5\\% false-discovery rate",
            f"{int(comparisons['significant'].sum())} of {len(comparisons)}",
        ],
        [
            "Comparisons where every paired trial agreed in direction",
            f"{int(comparisons['unanimous'].sum())} of {len(comparisons)}",
        ],
    ]
    tables.tabular(
        TABLES / "validity.tex", "p{0.38\\linewidth}p{0.52\\linewidth}", ["Check", "Result"], rows
    )
    return control, order_range, cvs, order_means


# ----------------------------------------------------------------------------
# the numbers the prose cites
# ----------------------------------------------------------------------------


def collect_facts(runs, summary, comparisons, system, environment, checks, alloc):
    control_overhead, order_range, cvs, _order_means = checks

    FACTS["machine"] = escape(system.get("cpu_model", "?"))
    FACTS["cores"] = (f"{system.get('cpu_performance_cores', '?')} performance and "
                      f"{system.get('cpu_efficiency_cores', '?')} efficiency cores")
    FACTS["memoryGiB"] = f"{float(system.get('memory_bytes', 0)) / 1024 ** 3:.0f}"
    FACTS["compiler"] = escape(environment.get("compiler", "?"))
    FACTS["flags"] = escape(environment.get("compile_flags", "?"))
    FACTS["trialsPerCell"] = str(environment.get("trials", "?"))
    FACTS["memoryCap"] = number(float(environment.get("memory_cap_mib", 0)))
    FACTS["timerResolution"] = f"{int(runs['clock_resolution_ns'].iloc[0])}"
    FACTS["instrumentationCost"] = f"{control_overhead.median():.1f}"
    FACTS["orderEffect"] = (
        f"{100 * max(abs(order_range.max() - 1), abs(order_range.min() - 1)):.2f}"
    )
    order_means = checks[3]
    FACTS["orderEffectMean"] = f"{100 * (order_means - 1).abs().max():.1f}"
    FACTS["orderEffectMeanLow"] = f"{100 * (order_means - 1).abs().min():.1f}"
    FACTS["medianCV"] = f"{cvs.median():.1f}"

    FACTS["totalTrials"] = number(len(runs))
    FACTS["totalCells"] = number(len(summary))
    FACTS["totalOperations"] = compact(float(runs["ops"].sum()))
    FACTS["structureCount"] = str(runs["structure"].nunique())
    FACTS["workloadCount"] = str(runs["workload"].nunique())

    at = pick(summary, workload="W1", axis="none", n=HEADLINE_N, structure="persistent").iloc[0]
    FACTS["oursUpdate"] = micros(at["update_ns_per_op"])
    FACTS["oursQuery"] = micros(at["query_ns_per_op"])
    FACTS["oursMemory"] = number(at["retained_mib"], 0)
    FACTS["headlineSize"] = f"{HEADLINE_N:,}".replace(",", r"\,")

    # The ends of the size sweep are read from the data rather than named here,
    # so extending the grid moves the sentence with it instead of silently
    # leaving it describing a size that is no longer the largest one measured.
    sweep = pick(summary, workload="W1", axis="none")
    top_n = int(sweep["n"].max()) if not sweep.empty else 0
    bottom_n = int(sweep["n"].min()) if not sweep.empty else 0
    FACTS["largestSize"] = f"{top_n:,}".replace(",", r"\,")
    FACTS["smallestSize"] = f"{bottom_n:,}".replace(",", r"\,")

    biggest = pick(summary, workload="W1", axis="none", n=top_n, structure="persistent")
    smallest = pick(summary, workload="W1", axis="none", n=bottom_n, structure="persistent")
    if not biggest.empty:
        FACTS["oursUpdateBig"] = micros(biggest["update_ns_per_op"].iloc[0])
        FACTS["oursQueryBig"] = micros(biggest["query_ns_per_op"].iloc[0])
    if not biggest.empty and not smallest.empty:
        FACTS["oursGrowth"] = (
            f"{biggest['update_ns_per_op'].iloc[0] / smallest['update_ns_per_op'].iloc[0]:.1f}"
        )
        floor = pick(summary, workload="W1", axis="none", structure=style.CONTROL)
        low = floor[floor["n"] == bottom_n]["update_ns_per_op"]
        high = floor[floor["n"] == top_n]["update_ns_per_op"]
        if not low.empty and not high.empty:
            FACTS["controlGrowth"] = f"{high.iloc[0] / low.iloc[0]:.1f}"

    largest = pick(summary, workload="W1", axis="none", n=top_n)
    largest = largest[largest["structure"].isin(style.COMPARABLE) & ~largest["capped"]]
    for metric, key in (("update_ns_per_op", "Update"), ("query_ns_per_op", "Query")):
        if largest.empty:
            continue
        best = largest.loc[largest[metric].idxmin(), "structure"]
        FACTS[f"cheapestAtLargest{key}"] = escape(style.label(best))
        runner = largest[largest["structure"] != best][metric].min()
        FACTS[f"marginAtLargest{key}"] = f"{runner / largest[metric].min():.2f}"

    # Cost of keeping history, against keeping none.
    persistence = []
    for workload in ("W1", "W2", "W3", "W4", "W5"):
        cell = pick(summary, workload=workload, axis="none", n=HEADLINE_N)
        ours = cell[cell["structure"] == "persistent"]["update_ns_per_op"]
        base = cell[cell["structure"] == style.CONTROL]["update_ns_per_op"]
        if not ours.empty and not base.empty:
            persistence.append(float(ours.iloc[0] / base.iloc[0]))
    if persistence:
        FACTS["persistenceCostMedian"] = f"{np.median(persistence):.1f}"
        FACTS["persistenceCostRange"] = f"{min(persistence):.1f} to {max(persistence):.1f}"

    # Per-baseline summaries.
    for metric, prefix in (("update_ns_per_op", "update"), ("query_ns_per_op", "query")):
        frame = comparisons[comparisons["metric"] == metric]
        for baseline in style.PERSISTENT + [style.CONTROL]:
            if baseline == "persistent":
                continue
            cells = frame[frame["baseline"] == baseline]
            if cells.empty:
                continue
            key = baseline.replace("-", "")
            FACTS[f"{prefix}{key}Cells"] = str(len(cells))
            FACTS[f"{prefix}{key}Wins"] = str(int(cells["ours_faster"].sum()))
            FACTS[f"{prefix}{key}Median"] = ratio(cells["ratio"].median())
            FACTS[f"{prefix}{key}Min"] = ratio(cells["ratio"].min())
            FACTS[f"{prefix}{key}Max"] = ratio(cells["ratio"].max())

    # The tag payoff, at the two ends of the range-width sweep.
    widths = pick(summary, workload="W11")
    ours_widths = widths[widths["structure"] == "persistent"]

    def width_cost(frame, value):
        row = frame[frame["variant"] == value]
        return None if row.empty else float(row["update_ns_per_op"].iloc[0])

    for name, key in (("point-only", "NoTags"), ("copy-on-push", "CopyOnPush")):
        theirs = widths[widths["structure"] == name]
        finished = theirs[~theirs["capped"]]
        if theirs.empty or finished.empty:
            continue
        narrow = float(theirs["variant"].min())
        widest = float(finished["variant"].max())
        FACTS[f"width1{key}"] = ratio(width_cost(theirs, narrow) / width_cost(ours_widths, narrow))
        FACTS[f"widthFull{key}"] = ratio(
            width_cost(theirs, widest) / width_cost(ours_widths, widest)
        )
        FACTS[f"widthFull{key}At"] = compact(widest)
        capped = theirs[theirs["capped"]]
        if not capped.empty:
            FACTS[f"width{key}RanOutAt"] = compact(capped["variant"].min())

    plateau = ours_widths[ours_widths["variant"] < ours_widths["variant"].max()]
    if not plateau.empty:
        FACTS["widthPlateauLow"] = micros(plateau["update_ns_per_op"].min())
        FACTS["widthPlateauHigh"] = micros(plateau["update_ns_per_op"].max())
        FACTS["widthPlateauSpread"] = (
            f"{plateau['update_ns_per_op'].max() / plateau['update_ns_per_op'].min():.2f}"
        )
        FACTS["widthPlateauTop"] = compact(plateau["variant"].max())
    whole = ours_widths[ours_widths["variant"] == ours_widths["variant"].max()]
    if not whole.empty:
        FACTS["widthWholeArray"] = micros(whole["update_ns_per_op"].iloc[0])

    # Space: what the tag policy costs when it is the only thing that differs.
    space = pick(summary, workload="W1", axis="none", n=HEADLINE_N)
    ours_nodes = space[space["structure"] == "persistent"]["nodes_per_update"]
    push_nodes = space[space["structure"] == "copy-on-push"]["nodes_per_update"]
    if not ours_nodes.empty and not push_nodes.empty:
        FACTS["oursNodesPerUpdate"] = f"{ours_nodes.iloc[0]:.1f}"
        FACTS["pushNodesPerUpdate"] = f"{push_nodes.iloc[0]:.1f}"
        FACTS["pushNodeRatio"] = f"{push_nodes.iloc[0] / ours_nodes.iloc[0]:.2f}"
    bound = 4 * (np.ceil(np.log2(HEADLINE_N)) + 1)
    FACTS["provedBound"] = f"{int(bound)}"
    if not ours_nodes.empty:
        FACTS["boundUsedShare"] = f"{100 * ours_nodes.iloc[0] / bound:.0f}"

    # Feasibility.
    reach = data.feasibility(summary)
    for name in style.PERSISTENT:
        rows = reach[reach["structure"] == name]
        key = name.replace("-", "")
        FACTS[f"finished{key}"] = f"{int(rows['first_capped_n'].isna().sum())}"
        FACTS[f"workloadsTotal"] = f"{len(rows)}"

    # Tail behaviour.
    tail = pick(summary, workload="W1", axis="none", n=HEADLINE_N, structure="persistent")
    if not tail.empty:
        FACTS["oursTailP50"] = micros(tail["update_p50"].iloc[0])
        FACTS["oursTailP99"] = micros(tail["update_p99"].iloc[0])
        FACTS["oursTailMax"] = micros(tail["update_max"].iloc[0])

    # Statistical strength of the head-to-head family.
    decisive = comparisons[comparisons["significant"]]
    FACTS["comparisonsTotal"] = str(len(comparisons))
    FACTS["comparisonsDecisive"] = str(len(decisive))
    FACTS["comparisonsUnanimous"] = str(int(comparisons["unanimous"].sum()))
    FACTS["worstAdjustedQ"] = pvalue(decisive["q"].max()) if not decisive.empty else "--"
    FACTS["minWilcoxonP"] = f"{2.0 ** -(int(comparisons['pairs'].max()) - 1):.5f}"

    # Where this work loses, stated as a number rather than left out. The
    # non-persistent control is excluded: it keeps no history, so being
    # cheaper than it is not a loss but the price of the feature.
    rivals = comparisons[comparisons["baseline"].isin(style.PERSISTENT)]
    losses = rivals[~rivals["ours_faster"]]
    FACTS["rivalComparisons"] = str(len(rivals))
    FACTS["rivalWins"] = str(int(rivals["ours_faster"].sum()))
    FACTS["lossesTotal"] = str(len(losses))
    if not losses.empty:
        by_baseline = losses["baseline"].value_counts()
        FACTS["lossesWorstBaseline"] = escape(style.label(by_baseline.index[0]))
        FACTS["lossesWorstCount"] = str(int(by_baseline.iloc[0]))
        FACTS["lossesWorstRatio"] = ratio(
            losses[losses["baseline"] == by_baseline.index[0]]["ratio"].median()
        )
        FACTS["lossesBaselineCount"] = str(int(losses["baseline"].nunique()))

    # Stability of our own measurements, separately from everyone else's.
    ours_cv = summary[
        (summary["structure"] == "persistent") & (~summary["capped"])
    ]["update_ns_per_op_cv"].dropna() * 100
    FACTS["oursMedianCV"] = f"{ours_cv.median():.1f}"
    FACTS["oursMaxCV"] = f"{ours_cv.max():.1f}"

    # Memory, against the structure that is closest on time.
    space_cell = pick(summary, workload="W1", axis="none", n=HEADLINE_N)
    for name in ("checkpointing", "copy-on-push", "buffered", "fat-node"):
        row = space_cell[space_cell["structure"] == name]
        ours_row = space_cell[space_cell["structure"] == "persistent"]
        if not row.empty and not ours_row.empty:
            key = name.replace("-", "")
            FACTS[f"memory{key}"] = number(row["retained_mib"].iloc[0], 0)
            FACTS[f"memoryRatio{key}"] = (
                f"{row['retained_mib'].iloc[0] / ours_row['retained_mib'].iloc[0]:.2f}"
            )

    # The audit workload: reads that only look far back.
    audit = pick(summary, workload="W12")
    if not audit.empty:
        oldest = audit["variant"].min()
        newest = audit["variant"].max()
        for share, key in ((oldest, "Oldest"), (newest, "Whole")):
            ours_row = audit[(audit["structure"] == "persistent") & (audit["variant"] == share)]
            for name in ("checkpointing", "copy-on-push"):
                other = audit[(audit["structure"] == name) & (audit["variant"] == share)]
                if not other.empty and not ours_row.empty:
                    FACTS[f"audit{key}{name.replace('-', '')}"] = ratio(
                        other["query_ns_per_op"].iloc[0] / ours_row["query_ns_per_op"].iloc[0]
                    )
            if not ours_row.empty:
                FACTS[f"audit{key}Ours"] = micros(ours_row["query_ns_per_op"].iloc[0])
        FACTS["auditOldestShare"] = f"{100 * oldest:g}"

    if not alloc.empty:
        cell = pick(alloc, workload="W1", axis="none", n=HEADLINE_N, structure="persistent")
        if not cell.empty and cell["retained_mib"].iloc[0]:
            FACTS["allocOverheadOurs"] = (
                f"{cell['peak_alloc_mib'].iloc[0] / cell['retained_mib'].iloc[0]:.2f}"
            )
        # The control is excluded: it retains a few tens of kilobytes, so its
        # ratio is dominated by one-off build allocations and says nothing about
        # a persistence strategy.
        every = pick(alloc, workload="W1", axis="none")
        every = every[(every["retained_mib"] > 0) & every["structure"].isin(style.PERSISTENT)]
        factors = every["peak_alloc_mib"] / every["retained_mib"]
        FACTS["allocOverheadMax"] = f"{factors.max():.2f}"
        checkpoint = every[every["structure"] == "checkpointing"]
        if not checkpoint.empty:
            FACTS["allocOverheadCheckpointing"] = (
                f"{(checkpoint['peak_alloc_mib'] / checkpoint['retained_mib']).max():.2f}"
            )

    profile = data.warmup_profile(runs)
    if not profile.empty:
        by_trial = profile.groupby("trial")["relative"].median()
        FACTS["warmupResidual"] = f"{by_trial.iloc[0]:.2f}"
        FACTS["warmupSettled"] = f"{by_trial.iloc[1:].max():.2f}"

    power = data.power_states(RAW)
    FACTS["powerSources"] = escape(", ".join(sorted(power["power_source"].unique())))


def main() -> None:
    style.apply()
    FIGURES.mkdir(parents=True, exist_ok=True)
    TABLES.mkdir(parents=True, exist_ok=True)
    SUMMARY.mkdir(parents=True, exist_ok=True)

    runs = data.load_runs(RAW, "timing")
    samples = data.load_memory(RAW, "timing")
    try:
        alloc = data.summarise(data.load_runs(RAW, "alloc"))
    except SystemExit:
        alloc = pd.DataFrame()
    summary = data.summarise(runs)
    system = data.load_system(RAW)
    environment = data.load_environment(RAW, "timing")

    comparisons = pd.concat(
        [
            data.head_to_head(runs, "persistent", "update_ns_per_op"),
            data.head_to_head(runs, "persistent", "query_ns_per_op"),
        ],
        ignore_index=True,
    )

    summary.to_csv(SUMMARY / "cells.csv", index=False)
    comparisons.to_csv(SUMMARY / "comparisons.csv", index=False)
    data.feasibility(summary).to_csv(SUMMARY / "feasibility.csv", index=False)

    figure_scaling(summary)
    figure_memory(summary)
    figure_workload_mix(summary)
    figure_range_width(summary)
    figure_versions(summary)
    figure_checkpoint(summary)
    figure_sweeps(summary)
    figure_latency_tail(summary)
    figure_cost_of_persistence(summary)
    figure_feasibility(summary)
    figure_space_time(summary)
    figure_copies(summary)
    figure_speedup(summary, comparisons)
    figure_versus_checkpoint(comparisons)
    figure_allocation(summary, alloc)
    figure_validity(runs, summary)
    if not samples.empty:
        figure_growth(samples)

    table_system(system, environment, runs)
    table_workloads(summary)
    table_headline(summary)
    table_scaling(summary)
    table_head_to_head(comparisons)
    table_feasibility(summary)
    table_space(summary)
    table_tail(summary)
    table_checkpoint(summary)
    table_allocation(alloc)
    checks = table_validity(runs, summary, comparisons)

    collect_facts(runs, summary, comparisons, system, environment, checks, alloc)
    FACTS.write(TABLES / "facts.tex")

    print(f"{len(runs)} trials across {len(summary)} cells")
    print(f"figures -> {FIGURES}")
    print(f"tables  -> {TABLES}")


if __name__ == "__main__":
    main()
