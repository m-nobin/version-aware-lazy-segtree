"""Shared vocabulary and drawing style for every figure and table.

One place decides what each structure is called, what colour and marker it
wears, and how a figure is laid out. Colour follows the structure, never its
rank in the plot, so a figure that drops a series does not repaint the rest —
a reader who has learned the blue line on page 3 still knows it on page 11.

The categorical hues are the first seven slots of a colour-vision-deficiency
safe order, validated for adjacent pairs; every series also carries a distinct
marker, so identity never rests on colour alone. The non-persistent reference
is deliberately grey and dashed: it is a floor, not a competitor.
"""

from __future__ import annotations

import math
import textwrap

import matplotlib as mpl
import matplotlib.pyplot as plt

# Ink and surface. Figures are drawn for print on white, which is the only
# surface a PDF in a paper is ever composited on.
INK = "#0b0b0b"
INK_SOFT = "#52514e"
GRID = "#dcdbd6"
SURFACE = "#ffffff"

# name -> (label, colour, marker, z-order boost)
STRUCTURES = {
    "persistent": ("This work", "#2a78d6", "o", 3),
    "copy-on-push": ("Copy-on-push", "#eb6834", "s", 2),
    "checkpointing": ("Checkpoint and replay", "#1baf7a", "^", 2),
    "buffered": ("Buffered node slots", "#eda100", "D", 1),
    "fat-node": ("Fat nodes", "#e87ba4", "v", 1),
    "point-only": ("No lazy tags", "#008300", "P", 1),
    "full-copy": ("Full copy per version", "#4a3aa7", "X", 1),
    "lazy": ("No history kept", INK_SOFT, ".", 0),
}

#: Drawing order, so the legend reads in a stable, meaningful sequence.
ORDER = list(STRUCTURES)

#: The five structures that complete every cell of the campaign. The two
#: no-sharing baselines run out of memory long before the others and are shown
#: in the feasibility and space figures instead, where their ceiling is the
#: result; putting a truncated run's per-operation time beside a complete one
#: would compare a sprint with a marathon.
COMPARABLE = ["persistent", "copy-on-push", "checkpointing", "buffered", "fat-node"]

#: Everything that keeps history, including the two that cannot reach the
#: larger sizes.
PERSISTENT = COMPARABLE + ["point-only", "full-copy"]

CONTROL = "lazy"

WORKLOADS = {
    "W1": "Balanced: half updates, half queries",
    "W2": "Update-heavy: 90% of operations write",
    "W3": "Query-heavy: 90% of operations read",
    "W4": "Single-element updates",
    "W5": "Whole-array updates",
    "W6": "Updates that change nothing",
    "W7": "Checkpoint spacing",
    "W8": "Number of stored versions",
    "W9": "Recent versions read more often",
    "W10": "Updates concentrated in one region",
    "W11": "Width of the updated range",
    "W12": "Reading only old versions",
}

AXIS_LABELS = {
    "none": None,
    "k": "Versions between checkpoints",
    "updates": "Versions stored",
    "width": "Updated range width (elements)",
    "theta": "Read concentration on recent versions",
    "hot_share": "Share of the array updates fall in",
    "zero_delta_share": "Share of updates that change nothing",
    "age_share": "Share of history a read may reach",
}


def label(structure: str) -> str:
    """Display name for a structure."""
    return STRUCTURES[structure][0]


def colour(structure: str) -> str:
    """Series colour for a structure."""
    return STRUCTURES[structure][1]


def marker(structure: str) -> str:
    """Series marker for a structure."""
    return STRUCTURES[structure][2]


def apply() -> None:
    """Install the figure style. Call once before drawing anything."""
    mpl.rcParams.update(
        {
            "figure.dpi": 160,
            "savefig.dpi": 320,
            "savefig.bbox": "tight",
            "savefig.pad_inches": 0.02,
            "figure.facecolor": SURFACE,
            "axes.facecolor": SURFACE,
            "savefig.facecolor": SURFACE,
            "font.family": "sans-serif",
            "font.sans-serif": ["DejaVu Sans"],
            "font.size": 9,
            "axes.titlesize": 10.5,
            "axes.titleweight": "bold",
            "axes.titlepad": 9,
            "axes.labelsize": 9,
            "axes.labelcolor": INK,
            "axes.labelpad": 5,
            "axes.edgecolor": GRID,
            "axes.linewidth": 0.8,
            "axes.spines.top": False,
            "axes.spines.right": False,
            "axes.grid": True,
            "axes.axisbelow": True,
            "grid.color": GRID,
            "grid.linewidth": 0.6,
            "grid.alpha": 0.9,
            "xtick.color": INK_SOFT,
            "ytick.color": INK_SOFT,
            "xtick.labelsize": 8.5,
            "ytick.labelsize": 8.5,
            "xtick.direction": "out",
            "ytick.direction": "out",
            "legend.frameon": False,
            "legend.fontsize": 8.5,
            "legend.handlelength": 1.9,
            "legend.columnspacing": 1.4,
            "legend.handletextpad": 0.6,
            "lines.linewidth": 1.9,
            "lines.markersize": 5.0,
            "lines.markeredgewidth": 0.0,
            "text.color": INK,
            "pdf.fonttype": 42,
            "ps.fonttype": 42,
        }
    )


def series_style(structure: str) -> dict:
    """Line keyword arguments for one structure."""
    name, hue, mark, boost = STRUCTURES[structure]
    style = {
        "color": hue,
        "marker": mark,
        "label": name,
        "zorder": 3 + boost,
    }
    if structure == CONTROL:
        style.update({"linestyle": (0, (4, 2)), "linewidth": 1.5, "markersize": 4})
    return style


def figure_title(fig, headline: str, subtitle: str | None = None, bottom: float = 0.0,
                 width: int = 108) -> None:
    """Give a figure a headline and a quieter line beneath it, then lay it out.

    The title block lives in figure coordinates measured in inches from the
    top, so a tall figure and a short one reserve the same physical space for
    it and neither ends up with the headline sitting on the plot.

    Call this last: it performs the layout the reserved space depends on.
    """
    lines = textwrap.wrap(subtitle, width) if subtitle else []
    height = fig.get_figheight()
    reserved = 0.30 + 0.15 * len(lines)
    fig.tight_layout(rect=(0, bottom, 1, 1 - reserved / height))
    fig.text(0.008, 1 - 0.16 / height, headline, fontsize=10.5, fontweight="bold",
             ha="left", va="top", color=INK)
    for index, line in enumerate(lines):
        fig.text(0.008, 1 - (0.36 + 0.15 * index) / height, line, fontsize=8.5,
                 color=INK_SOFT, ha="left", va="top")


def panel_title(ax, heading: str) -> None:
    """Name one panel inside a multi-panel figure."""
    ax.set_title(heading, loc="left", fontsize=9.5)


def thin(values, ratio: float = 1.6):
    """Drop tick positions that would print on top of their neighbour.

    A log axis whose sampled values sit closer together than a factor of
    ``ratio`` renders two labels in the same few millimetres. Keeping the
    first of each such cluster leaves the axis readable without moving any
    data point.
    """
    kept = []
    for value in sorted(values):
        if not kept or value >= kept[-1] * ratio:
            kept.append(value)
    return kept


def nice_log(ax, axis: str = "y") -> None:
    """Label a log axis at 1, 2 and 5 per decade in plain numbers.

    A log axis spanning barely more than one decade gets one or two decade
    ticks from the default locator, and silencing the minor formatter then
    leaves it with almost no labels at all. Stepping it at 1/2/5 keeps a
    readable number of gridlines whatever the span, and writing them as plain
    numbers rather than powers keeps the reader out of exponent arithmetic.
    """
    target = ax.yaxis if axis == "y" else ax.xaxis
    low, high = ax.get_ylim() if axis == "y" else ax.get_xlim()
    decades = math.log10(max(high, 1e-9) / max(low, 1e-9))
    if decades <= 2.5:
        subs = (1.0, 2.0, 5.0)
    elif decades <= 5.0:
        subs = (1.0,)
    else:
        subs = None  # let the locator thin the decades itself
    target.set_major_locator(
        mpl.ticker.LogLocator(base=10, subs=subs) if subs
        else mpl.ticker.LogLocator(base=10, numticks=8)
    )
    target.set_minor_locator(mpl.ticker.NullLocator())
    target.set_major_formatter(mpl.ticker.FuncFormatter(_plain))


def _plain(value, _position=None) -> str:
    """A tick value as a person would write it: 250, 2k, 10k, 1M."""
    for scale, suffix in ((1e6, "M"), (1e3, "k")):
        if value >= scale:
            trimmed = value / scale
            return f"{trimmed:g}{suffix}"
    return f"{value:g}"


def plain_log_ticks(ax, values, axis: str = "x") -> None:
    """Put ticks exactly where the data is and silence the decade minor ticks.

    A log axis with four sampled values otherwise labels powers of ten that
    were never measured, which invites the reader to interpolate between
    points that do not exist.
    """
    target = ax.xaxis if axis == "x" else ax.yaxis
    setter = ax.set_xticks if axis == "x" else ax.set_yticks
    labeller = ax.set_xticklabels if axis == "x" else ax.set_yticklabels
    setter(list(values))
    labeller([f"{value:g}" for value in values])
    target.set_minor_formatter(mpl.ticker.NullFormatter())


def legend(fig, handles, labels, columns: int = 4, y: float = -0.02) -> None:
    """One legend for the whole figure, below the plot area."""
    fig.legend(
        handles,
        labels,
        loc="upper center",
        bbox_to_anchor=(0.5, y),
        ncol=columns,
        frameon=False,
    )


def nanoseconds(ax) -> None:
    """Label a time axis in the unit the numbers are actually in."""
    ax.set_ylabel("Time per operation (nanoseconds)")


def log_size_axis(ax) -> None:
    """Array-size axis, log scaled, labelled without exponent notation."""
    ax.set_xscale("log")
    ax.set_xlabel("Array size (elements)")
    ax.set_xticks([1_000, 10_000, 100_000, 1_000_000])
    ax.set_xticklabels(["1 thousand", "10 thousand", "100 thousand", "1 million"])
    ax.xaxis.set_minor_formatter(mpl.ticker.NullFormatter())


def save(fig, path) -> None:
    """Write a figure as both vector PDF and raster PNG."""
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(path.with_suffix(".pdf"))
    fig.savefig(path.with_suffix(".png"))
    plt.close(fig)
