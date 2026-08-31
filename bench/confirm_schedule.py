"""Generate the fresh-process schedule for a confirmatory campaign.

One schedule line is one process: one (workload, n, variant, structure)
cell at one recorded trial index. The cell inventory comes from
``valseg_bench --list-cells`` so the binary stays the single source of truth;
this script only orders it.

Ordering is the registered balanced design: trials are blocks, and within
each trial block every process of that trial runs in an order shuffled by
``random.Random((schedule_seed, trial))``. The schedule seed is registered,
so the order is reproducible and recorded, and no structure systematically
owns the start or end of a block.

Trial counts: cells listed in the primary-cells CSV get ``--primary-trials``;
cells listed in the capped-cells CSV get ``--capped-trials`` (their result is
the cap, and re-measuring a truncated replay adds nothing); every other cell
gets ``--trials``. K-sweep variants beyond the first are checkpointing-only,
mirroring the runner's own rule.

    python3 bench/confirm_schedule.py --binary build/release-verify/bench/valseg_bench \
        --mode timing --out schedule.tsv
"""

from __future__ import annotations

import argparse
import csv
import io
import pathlib
import random
import subprocess
import sys

STRUCTURES = [
    "lazy",
    "persistent",
    "copy-on-push",
    "full-copy",
    "point-only",
    "checkpointing",
    "buffered",
    "fat-node",
    "external",
]


def read_cells(binary: str) -> list[dict]:
    text = subprocess.run(
        [binary, "--list-cells"], check=True, capture_output=True, text=True
    ).stdout
    return list(csv.DictReader(io.StringIO(text)))


def read_cell_list(path: pathlib.Path | None) -> set[tuple[str, ...]]:
    """Keys from a registered cell list, or empty. Primary cells are keyed
    (workload, n, variant); capped cells additionally carry the structure,
    because reaching the memory cap is a per-structure outcome."""
    if path is None or not path.exists():
        return set()
    with path.open() as handle:
        rows = list(csv.DictReader(handle))
    return {
        (row["workload"], row["n"], row["variant"])
        + ((row["structure"],) if "structure" in row else ())
        for row in rows
    }


def build_jobs(
    cells: list[dict],
    structures: list[str],
    workloads: set[str] | None,
) -> list[dict]:
    first_variant: dict[tuple[str, str], str] = {}
    for cell in cells:
        first_variant.setdefault((cell["workload"], cell["n"]), cell["variant"])
    jobs = []
    for cell in cells:
        if workloads is not None and cell["workload"] not in workloads:
            continue
        variant_index = [
            c["variant"] for c in cells if (c["workload"], c["n"]) == (cell["workload"], cell["n"])
        ].index(cell["variant"])
        for structure in structures:
            # K is a parameter of exactly one structure; every other structure
            # runs the sweep's traffic once, at the first sweep point.
            if (
                cell["axis"] == "k"
                and structure != "checkpointing"
                and cell["variant"] != first_variant[(cell["workload"], cell["n"])]
            ):
                continue
            jobs.append({**cell, "structure": structure, "variant_index": variant_index})
    return jobs


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--binary", required=True)
    parser.add_argument("--mode", choices=("timing", "alloc", "latency"), required=True)
    parser.add_argument("--trials", type=int, default=20)
    parser.add_argument("--primary-trials", type=int, default=40)
    parser.add_argument("--capped-trials", type=int, default=2)
    bench = pathlib.Path(__file__).resolve().parent
    parser.add_argument("--primary-cells", type=pathlib.Path, default=bench / "primary_cells.csv")
    parser.add_argument("--capped-cells", type=pathlib.Path, default=bench / "capped_cells.csv")
    parser.add_argument("--schedule-seed", type=int, required=True)
    parser.add_argument("--workloads", default="all", help="comma list, default all")
    parser.add_argument("--structures", default=",".join(STRUCTURES))
    parser.add_argument("--out", type=pathlib.Path, required=True)
    args = parser.parse_args()

    workloads = None if args.workloads == "all" else set(args.workloads.split(","))
    structures = args.structures.split(",")
    unknown = set(structures) - set(STRUCTURES)
    if unknown:
        parser.error(f"unknown structures: {sorted(unknown)}")

    primary = read_cell_list(args.primary_cells)
    capped = read_cell_list(args.capped_cells)
    jobs = build_jobs(read_cells(args.binary), structures, workloads)

    def trials_for(job: dict) -> int:
        if (job["workload"], job["n"], job["variant"], job["structure"]) in capped:
            return args.capped_trials
        if (job["workload"], job["n"], job["variant"]) in primary:
            return args.primary_trials
        return args.trials

    max_trials = max((trials_for(job) for job in jobs), default=0)
    lines = []
    for trial in range(max_trials):
        block = [job for job in jobs if trial < trials_for(job)]
        random.Random(f"{args.schedule_seed}|{trial}").shuffle(block)
        for job in block:
            tag = (
                f"{args.mode}-{job['workload']}-n{job['n']}-v{job['variant_index']}"
                f"-{job['structure']}-t{trial:02d}"
            )
            lines.append(
                "\t".join(
                    [
                        job["workload"],
                        job["n"],
                        job["variant"],
                        job["structure"],
                        str(trial),
                        str(trials_for(job)),
                        tag,
                    ]
                )
            )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        "# workload\tn\tvariant\tstructure\ttrial\ttrials\ttag\n" + "\n".join(lines) + "\n"
    )
    print(f"{len(lines)} processes -> {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
