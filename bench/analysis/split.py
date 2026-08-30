"""Deterministic training/hold-out split of measurement cells.

A cell is one (workload, n, axis, variant) tuple. Every seed and trial of a
cell lands in the same partition, so a model fit on the training cells cannot
learn a cell from some of its seeds and be scored on the rest. The assignment
is the SHA-256 of the cell key under a fixed salt, so it is reproducible from
this file alone and independent of the order rows appear in.

The salt below is the pilot-development salt. The registered protocol (PR6)
fixes its own salt and hold-out share before any confirmatory run; the split
of the confirmatory campaign is whatever that registration says, not this.

    uv run --project bench/analysis bench/analysis/split.py --self-test
    uv run --project bench/analysis bench/analysis/split.py W3 100000 none 0
"""

from __future__ import annotations

import hashlib
import sys

PILOT_SALT = "valseg-pilot-split-2026-08-30"
HOLD_OUT_SHARE = 0.3


def cell_key(workload: str, n: int, axis: str, variant: float) -> str:
    """Canonical text of a cell: the variant is printed with six decimals as
    the benchmark writes it, so a float that round-trips through CSV keys the
    same cell."""
    return f"{workload}|{int(n)}|{axis}|{float(variant):.6f}"


def partition(
    workload: str,
    n: int,
    axis: str,
    variant: float,
    salt: str = PILOT_SALT,
    hold_out_share: float = HOLD_OUT_SHARE,
) -> str:
    """'training' or 'holdout' for one cell."""
    digest = hashlib.sha256(f"{salt}|{cell_key(workload, n, axis, variant)}".encode()).digest()
    fraction = int.from_bytes(digest[:8], "big") / 2**64
    return "holdout" if fraction < hold_out_share else "training"


def assign(frame, salt: str = PILOT_SALT, hold_out_share: float = HOLD_OUT_SHARE):
    """Add a ``partition`` column to a DataFrame with workload, n, axis and
    variant columns."""
    return frame.assign(
        partition=[
            partition(w, n, a, v, salt, hold_out_share)
            for w, n, a, v in zip(frame["workload"], frame["n"], frame["axis"], frame["variant"])
        ]
    )


def self_test() -> None:
    # Same cell, same answer, whatever the seed or trial.
    a = partition("W3", 100000, "none", 0.0)
    assert a == partition("W3", 100000, "none", 0.0)
    assert partition("W3", 100000, "none", 0.0000001) == a  # float noise keys the same cell
    # The salt changes the answer for some cell, so it is not a no-op.
    cells = [("W1", 10**k, "none", 0.0) for k in range(3, 7)] + [
        ("W7", 100000, "width", float(w)) for w in (1, 16, 256, 4096)
    ]
    assert any(partition(*c) != partition(*c, salt="other") for c in cells)
    # Share is roughly honoured over many synthetic cells.
    many = [partition(f"W{i % 12 + 1}", 1000 * i, "none", i * 0.5) for i in range(1, 2001)]
    share = many.count("holdout") / len(many)
    assert 0.25 < share < 0.35, share
    print("split self-test ok")


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        self_test()
    elif len(sys.argv) == 5:
        print(partition(sys.argv[1], int(sys.argv[2]), sys.argv[3], float(sys.argv[4])))
    else:
        print(__doc__)
        sys.exit(2)
