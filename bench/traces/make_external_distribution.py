"""Generate the externally motivated WT workload distribution.

No public operation trace of range-add updates with historical range-sum
queries was found under the registered search protocol
(docs/research/registered-protocol.md, section 8.2). This generator therefore
derives a distribution from published experimental criteria instead, which
the protocol registers as the external workload and reports as a limitation.

Derivation, from the experimental sections of Zhang, Markowetz, Tsotras,
Gunopulos, Seeger, "On computing temporal aggregates with range predicates"
(TODS 33(2), 2008; the MVSB-tree):

- ordered interval insertions over a uniformly distributed key domain, applied
  in transaction-time order: mapped to range-add updates whose endpoints are
  drawn uniformly over a fixed-length window at a uniform position;
- fixed-size additive values: mapped to uniform deltas in [1, 100];
- key-range/time-range SUM queries over the accumulated history: mapped to
  historical range-sum queries at a uniformly drawn published version over a
  uniformly placed key range.

The mapping choices this repository made (domain size, interval length share,
operation count, update share) are parameters below, stated in the registered
protocol; they are ours, not the paper's. The stream is deterministic in the
seed and uses python's random.Random, whose sequence is stable across
platforms and versions for these draw kinds.

    python3 bench/traces/make_external_distribution.py --out external.trace
"""

from __future__ import annotations

import argparse
import random

DEFAULTS = {
    "n": 100_000,          # key-domain partitions
    "operations": 200_000, # matches the frozen synthetic campaign length
    "update_share": 0.5,   # insertions versus aggregate queries
    "interval_share": 0.01,  # interval length as a share of the domain
    "seed": 20270214,
}


def generate(n: int, operations: int, update_share: float, interval_share: float,
             seed: int) -> list[str]:
    rng = random.Random(seed)
    width = max(1, int(n * interval_share))
    lines = [
        "# externally motivated distribution; derivation and provenance in",
        "# bench/traces/make_external_distribution.py and the registered protocol",
        f"# parameters: n={n} operations={operations} update_share={update_share}"
        f" interval_share={interval_share} seed={seed}",
        f"n,{n}",
    ]
    published = 1  # version 0 exists after the build
    for _ in range(operations):
        if rng.random() < update_share:
            start = rng.randrange(n - width + 1)
            delta = rng.randint(1, 100)
            lines.append(f"u,{start},{start + width - 1},{delta}")
            published += 1
        else:
            first, second = rng.randrange(n), rng.randrange(n)
            left, right = min(first, second), max(first, second)
            version = rng.randrange(published)
            lines.append(f"q,{left},{right},{version}")
    return lines


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--out", required=True)
    for key, value in DEFAULTS.items():
        parser.add_argument(f"--{key.replace('_', '-')}", type=type(value), default=value)
    args = parser.parse_args()
    lines = generate(args.n, args.operations, args.update_share, args.interval_share, args.seed)
    with open(args.out, "w") as handle:
        handle.write("\n".join(lines) + "\n")
    print(f"{len(lines)} lines -> {args.out}")


if __name__ == "__main__":
    main()
