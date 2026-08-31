"""Blinded structure labels for the confirmatory analysis.

The registered protocol runs the primary analysis on blinded labels: the
statistician sees ``S01``..``Snn``, not structure names. The mapping is the
HMAC-SHA256 of each structure name under a campaign key, so it is
deterministic given the key and unguessable without it.

The key and the mapping live under ``<campaign>/sealed/``, which no analysis
stage reads or writes except ``--stage unblind``. Creating the seal is done
once, before any confirmatory measurement:

    uv run --project bench/analysis bench/analysis/blind.py seal <campaign-dir>

``blind_frame`` replaces the ``structure`` column; ``label_map`` inverts the
seal for the one unblinding step after the primary analysis output hashes
are fixed.
"""

from __future__ import annotations

import hashlib
import hmac
import json
import pathlib
import secrets
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


def label_map(key: bytes, structures: list[str] = STRUCTURES) -> dict[str, str]:
    """structure name -> blinded label, ordered by digest so the label order
    carries no information about the fixed structure table order."""
    digests = {
        name: hmac.new(key, name.encode(), hashlib.sha256).hexdigest() for name in structures
    }
    ordered = sorted(structures, key=lambda name: digests[name])
    return {name: f"S{position + 1:02d}" for position, name in enumerate(ordered)}


def blind_frame(frame, key: bytes):
    """Replace the structure column with blinded labels."""
    mapping = label_map(key, sorted(set(frame["structure"]) | set(STRUCTURES)))
    unknown = set(frame["structure"]) - mapping.keys()
    if unknown:
        raise ValueError(f"structures without a blinded label: {sorted(unknown)}")
    return frame.assign(structure=frame["structure"].map(mapping))


def seal(campaign: pathlib.Path) -> pathlib.Path:
    """Create <campaign>/sealed/ with a fresh key and the mapping. Refuses to
    overwrite: one campaign gets exactly one seal."""
    sealed = campaign / "sealed"
    key_path = sealed / "blind_key.hex"
    if key_path.exists():
        raise SystemExit(f"refusing to reseal: {key_path} exists")
    sealed.mkdir(parents=True, exist_ok=True)
    key = secrets.token_bytes(32)
    key_path.write_text(key.hex() + "\n")
    mapping = label_map(key)
    (sealed / "label_map.json").write_text(json.dumps(mapping, indent=2, sort_keys=True) + "\n")
    return key_path


def load_key(campaign: pathlib.Path) -> bytes:
    return bytes.fromhex((campaign / "sealed" / "blind_key.hex").read_text().strip())


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "seal":
        path = seal(pathlib.Path(sys.argv[2]))
        print(f"sealed blinded labels under {path.parent}")
    else:
        print(__doc__)
        sys.exit(2)
