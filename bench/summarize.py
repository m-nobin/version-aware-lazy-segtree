#!/usr/bin/env python3
"""Turn raw benchmark CSV into the summary tables committed alongside it.

Reads runs.csv and memory.csv from the raw directory and writes one Markdown
table per workload into the summary directory. Median across trials is
reported with the min-max spread, because a handful of trials on a laptop is
not enough for a mean to mean anything.

    python3 bench/summarize.py [--raw DIR] [--summary DIR] [--tag SUFFIX]

Standard library only, so it runs wherever the benchmark itself runs.
"""

import argparse
import csv
import os
from collections import defaultdict
from statistics import median

CELL = ("workload", "structure", "n", "axis", "variant")


def read(path):
    with open(path, newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def variant_label(axis, variant):
    if axis == "none":
        return "-"
    value = float(variant)
    if axis == "k" and value == 0:
        return "unbounded"
    if axis in ("skew", "hot_share", "zero_delta_share"):
        return f"{value:g}"
    return f"{int(value)}"


def per_op(total_ns, count):
    return float(total_ns) / count if count else 0.0


def spread(values):
    if len(values) == 1:
        return f"{values[0]:.0f}"
    return f"{median(values):.0f} [{min(values):.0f}-{max(values):.0f}]"


def growth(samples):
    """Retained MiB at a quarter, half, three quarters and the end of the run."""
    if not samples:
        return ["-"] * 4
    ordered = sorted(samples, key=lambda row: int(row["op_index"]))
    last = int(ordered[-1]["op_index"])
    out = []
    for fraction in (0.25, 0.5, 0.75, 1.0):
        target = fraction * last
        pick = min(ordered, key=lambda row: abs(int(row["op_index"]) - target))
        out.append(f"{int(pick['bytes']) / 1048576:.2f}")
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--raw", default="bench/results/raw")
    parser.add_argument("--summary", default="bench/results/summary")
    parser.add_argument("--tag", default="")
    args = parser.parse_args()

    suffix = f"_{args.tag}" if args.tag else ""
    runs = read(os.path.join(args.raw, f"runs{suffix}.csv"))
    memory_path = os.path.join(args.raw, f"memory{suffix}.csv")
    memory = read(memory_path) if os.path.exists(memory_path) else []

    by_cell = defaultdict(list)
    for row in runs:
        by_cell[tuple(row[key] for key in CELL)].append(row)

    # Growth curves come from the first trial only: they are a shape, and
    # averaging shapes across trials would blur the step at each checkpoint.
    memory_by_cell = defaultdict(list)
    for row in memory:
        if row["trial"] == "0":
            memory_by_cell[tuple(row[key] for key in CELL)].append(row)

    os.makedirs(args.summary, exist_ok=True)
    workloads = sorted({key[0] for key in by_cell}, key=lambda name: int(name[1:]))
    index = ["# Benchmark summary", "", f"Generated from `{args.raw}` by `bench/summarize.py`.", ""]

    for workload in workloads:
        lines = [
            f"# {workload}",
            "",
            "Median over trials, min-max in brackets. Times are nanoseconds per "
            "operation. Retained MiB is the payload each header documents, not "
            "RSS. `memory_cap` marks a trial that stopped when retained bytes "
            "passed the cap: that is where the structure became unrunnable at "
            "this size, not a failure of the run.",
            "",
            "| structure | n | variant | update ns/op | query ns/op | build ns | "
            "MiB @25% | @50% | @75% | @end | peak alloc MiB | status |",
            "|---|---|---|---|---|---|---|---|---|---|---|---|",
        ]
        cells = sorted(
            (key for key in by_cell if key[0] == workload),
            key=lambda key: (int(key[2]), float(key[4]), key[1]),
        )
        for key in cells:
            rows = by_cell[key]
            updates = [per_op(row["update_ns"], int(row["updates"])) for row in rows]
            queries = [per_op(row["query_ns"], int(row["queries"])) for row in rows]
            builds = [float(row["build_ns"]) for row in rows]
            curve = growth(memory_by_cell[key])
            peak = max(int(row["alloc_peak_bytes"]) for row in rows)
            lines.append(
                "| {} | {} | {} | {} | {} | {:.0f} | {} | {} | {} | {} | {} | {} |".format(
                    key[1],
                    key[2],
                    variant_label(key[3], key[4]),
                    spread(updates),
                    spread(queries),
                    median(builds),
                    curve[0],
                    curve[1],
                    curve[2],
                    curve[3],
                    f"{peak / 1048576:.2f}" if peak else "-",
                    ",".join(sorted({row["status"] for row in rows})),
                )
            )
        with open(os.path.join(args.summary, f"{workload}.md"), "w", encoding="utf-8") as handle:
            handle.write("\n".join(lines) + "\n")
        index.append(f"- [{workload}]({workload}.md)")

    write_feasibility(args.summary, runs)
    index.append("- [feasibility](feasibility.md)")

    with open(os.path.join(args.summary, "index.md"), "w", encoding="utf-8") as handle:
        handle.write("\n".join(index) + "\n")
    print(f"wrote {len(workloads)} tables into {args.summary}")


def write_feasibility(summary_dir, runs):
    """Where each structure stops being runnable.

    The ceiling is a result in its own right: a baseline that cannot reach the
    sizes the others reach has told you something about persistence strategies,
    and burying it as a missing row would be the dishonest way to report it.
    """
    reached = defaultdict(lambda: (0, 0))
    capped = defaultdict(list)
    for row in runs:
        key = (row["structure"], row["workload"])
        size, versions = int(row["n"]), int(row["updates"]) + 1
        if row["status"] == "ok":
            reached[key] = max(reached[key], (size, versions))
        else:
            capped[key].append((size, versions))

    lines = [
        "# Feasibility ceilings",
        "",
        "Largest (n, versions) each structure completed, and the smallest cell "
        "where it hit the memory cap. A capped cell is not a failed run: it is "
        "the point past which that structure cannot be measured at this cap.",
        "",
        "| structure | workload | largest n | versions there | first capped n | capped at versions |",
        "|---|---|---|---|---|---|",
    ]
    for key in sorted(set(reached) | set(capped), key=lambda k: (k[0], int(k[1][1:]))):
        size, versions = reached.get(key, (0, 0))
        cap = min(capped[key]) if capped.get(key) else None
        lines.append(
            "| {} | {} | {} | {} | {} | {} |".format(
                key[0],
                key[1],
                size or "-",
                versions or "-",
                cap[0] if cap else "-",
                cap[1] if cap else "-",
            )
        )
    with open(os.path.join(summary_dir, "feasibility.md"), "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
