"""Emit LaTeX fragments the report includes verbatim.

No number in the report is typed by hand. Every table body and every figure in
benchmarking.tex is a generated file, and the prose reads its numbers from
generated macros, so the document cannot drift from the data it describes.
"""

from __future__ import annotations

import math
import pathlib

import numpy as np

ESCAPES = {"%": r"\%", "_": r"\_", "&": r"\&", "#": r"\#"}


def escape(text: str) -> str:
    """Make a plain string safe inside a LaTeX cell."""
    out = str(text)
    for symbol, replacement in ESCAPES.items():
        out = out.replace(symbol, replacement)
    return out


def number(value, digits: int = 0, dash: str = "--") -> str:
    """Format a number with thousands separators, or a dash when missing."""
    if value is None or (isinstance(value, float) and not np.isfinite(value)):
        return dash
    if digits == 0:
        return f"{round(float(value)):,}".replace(",", r"\,")
    return f"{float(value):,.{digits}f}".replace(",", r"\,")


def compact(value) -> str:
    """A time or size at three significant figures, with a unit prefix."""
    if value is None or not np.isfinite(value):
        return "--"
    value = float(value)
    for limit, suffix, scale in (
        (1e3, "", 1.0),
        (1e6, "k", 1e3),
        (1e9, "M", 1e6),
        (np.inf, "G", 1e9),
    ):
        if abs(value) < limit:
            scaled = value / scale
            digits = 0 if abs(scaled) >= 100 else (1 if abs(scaled) >= 10 else 2)
            return f"{scaled:.{digits}f}{suffix}"
    return f"{value:.3g}"


def micros(value) -> str:
    """A nanosecond measurement written in microseconds, three significant figures.

    Times are recorded in nanoseconds and reported in microseconds throughout,
    so one column never has to be read in a different unit from the figure
    beside it.
    """
    if value is None or not np.isfinite(value):
        return "--"
    scaled = float(value) / 1000.0
    if scaled == 0:
        return "0"
    if abs(scaled) >= 100:
        return f"{scaled:,.0f}".replace(",", r"\,")
    # Sub-microsecond values are the norm for a single tree operation, so the
    # decimals follow the magnitude rather than being fixed: two decimals would
    # round a twenty-nanosecond update down to one significant figure.
    digits = 2 - math.floor(math.log10(abs(scaled)))
    return f"{scaled:.{digits}f}"


def ratio(value) -> str:
    """A speed ratio, written the way it is read aloud."""
    if value is None or not np.isfinite(value):
        return "--"
    value = float(value)
    if value >= 1000:
        return rf"{value:,.0f}$\times$".replace(",", r"\,")
    if value >= 100:
        return rf"{value:.0f}$\times$"
    if value >= 10:
        return rf"{value:.1f}$\times$"
    return rf"{value:.2f}$\times$"


def signed(value, digits: int = 1) -> str:
    """A number that may be negative, with a real minus sign rather than a hyphen."""
    if value is None or not np.isfinite(value):
        return "--"
    text = f"{float(value):.{digits}f}"
    return text.replace("-", "$-$", 1) if text.startswith("-") else text


def pvalue(value) -> str:
    """A corrected p-value, floored at what the sample size can resolve."""
    if value is None or not np.isfinite(value):
        return "--"
    if value < 1e-4:
        return r"$<$0.0001"
    return f"{value:.4f}"


def tabular(path: pathlib.Path, spec: str, header: list[str], rows: list[list[str]],
            groups: list[tuple[int, str]] | None = None,
            subheader: list[str] | None = None) -> None:
    """Write one booktabs tabular body to its own file.

    Groups insert a labelled midrule before the row at the given index, which
    is how a long table stays readable without repeating a column of labels. A
    subheader carries the units, so the header row can carry the meaning.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [rf"\begin{{tabular}}{{{spec}}}", r"\toprule"]
    lines.append(" & ".join(part for part in header if part != "") + r" \\")
    if subheader:
        lines.append(" & ".join(subheader) + r" \\")
    lines.append(r"\midrule")
    breaks = dict(groups or [])
    for index, row in enumerate(rows):
        if index in breaks:
            if index:
                lines.append(r"\addlinespace")
            lines.append(rf"\multicolumn{{{len(header)}}}{{l}}{{\itshape {breaks[index]}}} \\")
        lines.append(" & ".join(row) + r" \\")
    lines += [r"\bottomrule", r"\end{tabular}"]
    path.write_text("\n".join(lines) + "\n")


class Facts:
    """Collected numbers, written out as LaTeX macros.

    A macro name is a claim's identity. The report says \\Fact{oursUpdateAt1e5}
    rather than a literal, so re-running the campaign updates the sentence.
    """

    def __init__(self) -> None:
        self._values: dict[str, str] = {}

    def set(self, name: str, value: str) -> None:
        self._values[name] = value

    def __setitem__(self, name: str, value: str) -> None:
        self.set(name, value)

    def write(self, path: pathlib.Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        # An undefined macro must stop the build, not typeset nothing. A silently
        # blank number in a results section is worse than no number at all.
        lines = [
            "% Generated by bench/analysis/report.py. Do not edit.",
            r"\makeatletter",
            r"\newcommand{\Fact}[1]{%",
            r"  \@ifundefined{fact@#1}%",
            r"    {\PackageError{facts}{No measured value named '#1'}%",
            r"      {Re-run bench/analysis/report.py, or fix the name.}}%",
            r"    {\@nameuse{fact@#1}}}",
            r"\newcommand{\DefineFact}[2]{\@namedef{fact@#1}{#2}}",
            r"\makeatother",
        ]
        for name in sorted(self._values):
            lines.append(rf"\DefineFact{{{name}}}{{{self._values[name]}}}")
        path.write_text("\n".join(lines) + "\n")

    def as_dict(self) -> dict:
        return dict(self._values)
