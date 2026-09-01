"""Custody-separated blinding for the registered confirmatory analysis.

The named raw campaign and the HMAC key stay in a controlled custody
directory. The analyst campaign receives only generically named CSVs whose
``structure`` column already contains opaque ``S01``... labels. Confirmatory
analysis never imports a key or a name map.

Typical custodian workflow::

    blind.py seal ANALYST_CAMPAIGN --custody-dir CONTROLLED --study-id STUDY
    blind.py attach OTHER_ANALYST_CAMPAIGN --custody-dir CONTROLLED --study-id STUDY
    blind.py blind NAMED_CAMPAIGN ANALYST_CAMPAIGN \
        --custody-dir CONTROLLED --study-id STUDY
    # analyst runs every blinded primary and sensitivity stage
    blind.py unblind ANALYST_CAMPAIGN OTHER_ANALYST_CAMPAIGN \
        --custody-dir CONTROLLED --study-id STUDY
    blind.py verify-named NAMED_CAMPAIGN ANALYST_CAMPAIGN \
        --custody-dir CONTROLLED --study-id STUDY

``unblind`` hashes every machine's primary output before opening custody
material, then writes named copies under ``analysis/unblinded`` and records
the study-wide pre-unblinding hash and UTC timestamp.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import hmac
import json
import pathlib
import re
import secrets
import shutil

import pandas as pd

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
REQUIRED_LOCAL_BLINDED_OUTPUTS = [
    "primary_update.csv",
    "regime_update_contrast-a.csv",
    "regime_update_contrast-b.csv",
    "hierarchical_update.csv",
    "hierarchical_update_diagnostics.json",
    "regime_query_contrast-a.csv",
    "regime_query_contrast-b.csv",
    "hierarchical_query.csv",
    "hierarchical_query_diagnostics.json",
    "feasibility_update.csv",
    "mean-median_update.csv",
    "order_update.csv",
    "leave-one-out_update.csv",
]
REQUIRED_COORDINATOR_OUTPUTS = [
    *REQUIRED_LOCAL_BLINDED_OUTPUTS,
    "h4_update.csv",
    "compiler_update.csv",
    "allocator_update.csv",
]
# Backward-compatible public name for the complete coordinator requirement.
REQUIRED_BLINDED_OUTPUTS = REQUIRED_COORDINATOR_OUTPUTS


def canonical_json(value: dict | list) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z")


def label_map(key: bytes, structures: list[str] = STRUCTURES) -> dict[str, str]:
    """Map names to labels in digest order, revealing no source-table order."""
    digests = {
        name: hmac.new(key, name.encode(), hashlib.sha256).hexdigest() for name in structures
    }
    ordered = sorted(structures, key=lambda name: digests[name])
    return {name: f"S{position + 1:02d}" for position, name in enumerate(ordered)}


def blind_frame(frame: pd.DataFrame, key: bytes) -> pd.DataFrame:
    """Replace every known structure name; reject unknown names."""
    mapping = label_map(key)
    unknown = set(frame["structure"].astype(str)) - mapping.keys()
    if unknown:
        raise ValueError(f"structures without a blinded label: {sorted(unknown)}")
    return frame.assign(structure=frame["structure"].map(mapping))


def custody_paths(custody_dir: pathlib.Path, study_id: str) -> tuple[pathlib.Path, pathlib.Path]:
    root = custody_dir.resolve() / study_id
    return root / "blind_key.hex", root / "label_map.json"


def custody_input_manifest_path(
    custody_dir: pathlib.Path, study_id: str, receipt_id: str
) -> pathlib.Path:
    return custody_dir.resolve() / study_id / "input_manifests" / f"{receipt_id}.json"


def ensure_external_custody(campaign: pathlib.Path, custody_dir: pathlib.Path) -> None:
    campaign = campaign.resolve()
    custody = custody_dir.resolve()
    if custody == campaign or campaign in custody.parents or custody in campaign.parents:
        raise SystemExit("custody directory must be outside the analyst campaign")


def seal(campaign: pathlib.Path, custody_dir: pathlib.Path, study_id: str) -> pathlib.Path:
    """Create custody material and a key commitment in the analyst campaign."""
    ensure_external_custody(campaign, custody_dir)
    key_path, map_path = custody_paths(custody_dir, study_id)
    commitment_path = campaign / "blinding-commitment.json"
    if key_path.exists() or map_path.exists() or commitment_path.exists():
        raise SystemExit("refusing to overwrite existing blinding material or commitment")
    key_path.parent.mkdir(parents=True, exist_ok=False)
    campaign.mkdir(parents=True, exist_ok=True)
    key = secrets.token_bytes(32)
    key_path.write_text(key.hex() + "\n")
    map_path.write_bytes(canonical_json(label_map(key)))
    commitment = {
        "schema_version": 1,
        "study_id": study_id,
        "sealed_utc": utc_now(),
        "key_sha256": hashlib.sha256(key).hexdigest(),
        "label_count": len(STRUCTURES),
        "custody_policy": "key and name map held outside analyst campaign",
    }
    commitment_path.write_bytes(canonical_json(commitment))
    return commitment_path


def attach(campaign: pathlib.Path, custody_dir: pathlib.Path, study_id: str) -> pathlib.Path:
    """Attach an additional analyst campaign to an existing study-wide seal."""
    ensure_external_custody(campaign, custody_dir)
    key_path, map_path = custody_paths(custody_dir, study_id)
    if not key_path.is_file() or not map_path.is_file():
        raise SystemExit("study-wide custody material does not exist; run seal first")
    commitment_path = campaign / "blinding-commitment.json"
    if commitment_path.exists():
        raise SystemExit(f"refusing to overwrite {commitment_path}")
    key = bytes.fromhex(key_path.read_text().strip())
    if json.loads(map_path.read_text()) != label_map(key):
        raise SystemExit("custody label map does not match its key")
    campaign.mkdir(parents=True, exist_ok=True)
    commitment = {
        "schema_version": 1,
        "study_id": study_id,
        "sealed_utc": utc_now(),
        "key_sha256": hashlib.sha256(key).hexdigest(),
        "label_count": len(STRUCTURES),
        "custody_policy": "key and name map held outside analyst campaign",
    }
    commitment_path.write_bytes(canonical_json(commitment))
    return commitment_path


def load_custody(
    campaign: pathlib.Path, custody_dir: pathlib.Path, study_id: str
) -> tuple[bytes, dict[str, str]]:
    """Open controlled material and verify it against the analyst commitment."""
    ensure_external_custody(campaign, custody_dir)
    commitment = json.loads((campaign / "blinding-commitment.json").read_text())
    if commitment.get("study_id") != study_id:
        raise SystemExit("blinding commitment uses another study id")
    key_path, map_path = custody_paths(custody_dir, study_id)
    key = bytes.fromhex(key_path.read_text().strip())
    if hashlib.sha256(key).hexdigest() != commitment.get("key_sha256"):
        raise SystemExit("custody key does not match the analyst commitment")
    mapping = json.loads(map_path.read_text())
    if mapping != label_map(key):
        raise SystemExit("custody label map does not match the committed key")
    return key, mapping


def is_within(path: pathlib.Path, parent: pathlib.Path) -> bool:
    resolved = path.resolve()
    root = parent.resolve()
    return resolved == root or root in resolved.parents


def blind_campaign(
    source_campaign: pathlib.Path,
    analyst_campaign: pathlib.Path,
    custody_dir: pathlib.Path,
    study_id: str,
) -> int:
    """Materialize a name-free raw directory for the analyst."""
    if not is_within(source_campaign, custody_dir):
        raise SystemExit("named source campaign must be held inside the controlled custody tree")
    key, _ = load_custody(analyst_campaign, custody_dir, study_id)
    source_raw = source_campaign / "raw"
    target_raw = analyst_campaign / "raw"
    if target_raw.exists() and any(target_raw.iterdir()):
        raise SystemExit(f"refusing to overwrite blinded raw data under {target_raw}")
    target_raw.mkdir(parents=True, exist_ok=True)
    run_paths = sorted(source_raw.glob("runs_*.csv"))
    if not run_paths:
        raise SystemExit(f"no named run CSVs under {source_raw}")
    entries = []
    for index, source in enumerate(run_paths, start=1):
        frame = pd.read_csv(source)
        if frame.empty:
            continue
        workload = str(frame["workload"].iloc[0])
        mode = "timing"
        for candidate in ("timing", "alloc", "latency", "trace"):
            if source.name.startswith(f"runs_{candidate}"):
                mode = candidate
                break
        destination = target_raw / f"runs_{mode}-{workload}-B{index:06d}.csv"
        blind_frame(frame, key).to_csv(destination, index=False)
        entries.append(
            {
                "kind": "run",
                "source_file": source.name,
                "source_sha256": sha256_file(source),
                "blinded_file": destination.name,
                "blinded_sha256": sha256_file(destination),
            }
        )
    for source in sorted(source_raw.glob("structural*.csv")):
        destination = target_raw / source.name
        shutil.copy2(source, destination)
        entries.append(
            {
                "kind": "structural",
                "source_file": source.name,
                "source_sha256": sha256_file(source),
                "blinded_file": destination.name,
                "blinded_sha256": sha256_file(destination),
            }
        )
    recorded_source_files = {entry["source_file"] for entry in entries}
    for source in sorted(path for path in source_raw.iterdir() if path.is_file()):
        if source.name in recorded_source_files:
            continue
        entries.append(
            {
                "kind": "custody-only",
                "source_file": source.name,
                "source_sha256": sha256_file(source),
                "blinded_file": None,
                "blinded_sha256": None,
            }
        )
    receipt_id = secrets.token_hex(16)
    custody_manifest = {
        "schema_version": 1,
        "study_id": study_id,
        "created_utc": utc_now(),
        "source_campaign": str(source_campaign.resolve()),
        "files": entries,
    }
    custody_manifest_path = custody_input_manifest_path(custody_dir, study_id, receipt_id)
    custody_manifest_path.parent.mkdir(parents=True, exist_ok=True)
    if custody_manifest_path.exists():
        raise SystemExit("refusing to overwrite a custody input manifest")
    custody_manifest_path.write_bytes(canonical_json(custody_manifest))
    receipt = {
        "schema_version": 1,
        "study_id": study_id,
        "created_utc": utc_now(),
        "run_files": len(list(target_raw.glob("runs_*.csv"))),
        "contains_name_map": False,
        "custody_receipt_id": receipt_id,
        "custody_manifest_sha256": sha256_file(custody_manifest_path),
        "blinded_files": [
            {"file": entry["blinded_file"], "sha256": entry["blinded_sha256"]}
            for entry in entries
            if entry["blinded_file"] is not None
        ],
    }
    (analyst_campaign / "blinded-input-receipt.json").write_bytes(canonical_json(receipt))
    return int(receipt["run_files"])


def verify_named_campaign(
    source_campaign: pathlib.Path,
    analyst_campaign: pathlib.Path,
    custody_dir: pathlib.Path,
    study_id: str,
) -> str:
    """Verify that named post-unblind input is the source of the blinded copy."""
    if not is_within(source_campaign, custody_dir):
        raise SystemExit("named source campaign must be held inside the controlled custody tree")
    load_custody(analyst_campaign, custody_dir, study_id)
    receipt_path = analyst_campaign / "blinded-input-receipt.json"
    receipt = json.loads(receipt_path.read_text())
    if receipt.get("study_id") != study_id:
        raise SystemExit("blinded-input receipt uses another study id")
    receipt_id = str(receipt.get("custody_receipt_id", ""))
    if re.fullmatch(r"[0-9a-f]{32}", receipt_id) is None:
        raise SystemExit("blinded-input receipt has an invalid custody receipt id")
    manifest_path = custody_input_manifest_path(custody_dir, study_id, receipt_id)
    manifest_sha256 = sha256_file(manifest_path)
    if manifest_sha256 != receipt.get("custody_manifest_sha256"):
        raise SystemExit("custody input manifest does not match the analyst receipt")
    manifest = json.loads(manifest_path.read_text())
    if (
        manifest.get("study_id") != study_id
        or manifest.get("source_campaign") != str(source_campaign.resolve())
    ):
        raise SystemExit("named campaign does not match the custody input manifest")

    source_raw = source_campaign / "raw"
    analyst_raw = analyst_campaign / "raw"
    entries = manifest.get("files", [])
    expected_source = {str(entry["source_file"]) for entry in entries}
    current_source = {
        path.name for path in source_raw.iterdir() if path.is_file()
    }
    if current_source != expected_source:
        raise SystemExit("named campaign file inventory changed after blinding")
    for entry in entries:
        source = source_raw / entry["source_file"]
        if sha256_file(source) != entry["source_sha256"]:
            raise SystemExit(f"named campaign file changed after blinding: {source}")
        if entry["blinded_file"] is not None:
            blinded = analyst_raw / entry["blinded_file"]
            if sha256_file(blinded) != entry["blinded_sha256"]:
                raise SystemExit(
                    f"blinded campaign file changed after custody transfer: {blinded}"
                )
    return manifest_sha256


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def hash_primary_outputs(
    campaign: pathlib.Path,
    required_outputs: list[str] = REQUIRED_COORDINATOR_OUTPUTS,
) -> tuple[pathlib.Path, dict]:
    """Hash all blinded analysis outputs before any name map is opened."""
    analysis = campaign / "analysis"
    required = [analysis / name for name in required_outputs]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise SystemExit(
            "cannot unblind before required primary outputs exist: " + ", ".join(missing)
        )
    candidates = sorted(
        path
        for path in analysis.iterdir()
        if path.is_file()
        and path.name not in {"primary-results.json", "unblinding.json"}
        and path.suffix in {".csv", ".json"}
    )
    entries = [{"file": path.name, "sha256": sha256_file(path)} for path in candidates]
    aggregate = hashlib.sha256(canonical_json(entries)).hexdigest()
    manifest = {
        "schema_version": 1,
        "hashed_utc": utc_now(),
        "files": entries,
        "aggregate_sha256": aggregate,
    }
    path = analysis / "primary-results.json"
    path.write_bytes(canonical_json(manifest))
    return path, manifest


def unblind_study(
    campaigns: list[pathlib.Path], custody_dir: pathlib.Path, study_id: str
) -> list[pathlib.Path]:
    """Hash every machine before opening custody, then unblind the whole study."""
    if not campaigns:
        raise SystemExit("unblind requires at least one analyst campaign")
    resolved = [campaign.resolve() for campaign in campaigns]
    if len(set(resolved)) != len(resolved):
        raise SystemExit("analyst campaigns must be unique")

    commitments = []
    for campaign in campaigns:
        ensure_external_custody(campaign, custody_dir)
        analysis = campaign / "analysis"
        if (analysis / "unblinded").exists() or (analysis / "unblinding.json").exists():
            raise SystemExit("refusing to repeat or overwrite the unblinding stage")
        commitment_path = campaign / "blinding-commitment.json"
        if not commitment_path.is_file():
            raise SystemExit(f"missing blinding commitment: {commitment_path}")
        commitment = json.loads(commitment_path.read_text())
        if commitment.get("study_id") != study_id:
            raise SystemExit(f"blinding commitment uses another study id: {campaign}")
        commitments.append(commitment)
    key_hashes = {commitment.get("key_sha256") for commitment in commitments}
    if len(key_hashes) != 1:
        raise SystemExit("analyst campaigns do not share one blinding seal")

    hashed = []
    for index, campaign in enumerate(campaigns):
        required = (
            REQUIRED_COORDINATOR_OUTPUTS if index == 0 else REQUIRED_LOCAL_BLINDED_OUTPUTS
        )
        manifest_path, manifest = hash_primary_outputs(campaign, required)
        hashed.append((campaign, manifest_path, manifest))

    study_entries = [
        {
            "campaign_index": index + 1,
            "campaign_output_sha256": manifest["aggregate_sha256"],
        }
        for index, (_, _, manifest) in enumerate(hashed)
    ]
    study_sha256 = hashlib.sha256(canonical_json(study_entries)).hexdigest()

    mappings = [load_custody(campaign, custody_dir, study_id)[1] for campaign in campaigns]
    if any(mapping != mappings[0] for mapping in mappings[1:]):
        raise SystemExit("analyst campaigns do not resolve to one label map")
    inverse = {label: name for name, label in mappings[0].items()}
    unblinded_utc = utc_now()
    records = []
    for campaign, manifest_path, manifest in hashed:
        analysis = campaign / "analysis"
        destination = analysis / "unblinded"
        destination.mkdir()
        written = []
        for entry in manifest["files"]:
            source = analysis / entry["file"]
            if source.suffix != ".csv":
                continue
            frame = pd.read_csv(source)
            for column in frame.select_dtypes(include=["object", "string"]):
                frame[column] = frame[column].map(lambda value: inverse.get(value, value))
            target = destination / source.name
            frame.to_csv(target, index=False)
            written.append(target.name)
        record = {
            "schema_version": 2,
            "study_id": study_id,
            "unblinded_utc": unblinded_utc,
            "primary_manifest": manifest_path.name,
            "campaign_output_sha256": manifest["aggregate_sha256"],
            "study_output_sha256": study_sha256,
            "study_campaigns": len(campaigns),
            "unblinded_files": written,
        }
        path = analysis / "unblinding.json"
        path.write_bytes(canonical_json(record))
        records.append(path)
    return records


def unblind(campaign: pathlib.Path, custody_dir: pathlib.Path, study_id: str) -> pathlib.Path:
    """Compatibility wrapper for a one-campaign study."""
    return unblind_study([campaign], custody_dir, study_id)[0]


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    seal_parser = subparsers.add_parser("seal")
    seal_parser.add_argument("campaign", type=pathlib.Path)
    attach_parser = subparsers.add_parser("attach")
    attach_parser.add_argument("campaign", type=pathlib.Path)
    blind_parser = subparsers.add_parser("blind")
    blind_parser.add_argument("source_campaign", type=pathlib.Path)
    blind_parser.add_argument("campaign", type=pathlib.Path)
    verify_parser = subparsers.add_parser("verify-named")
    verify_parser.add_argument("source_campaign", type=pathlib.Path)
    verify_parser.add_argument("campaign", type=pathlib.Path)
    unblind_parser = subparsers.add_parser("unblind")
    unblind_parser.add_argument("campaign", type=pathlib.Path, nargs="+")
    for command_parser in (
        seal_parser,
        attach_parser,
        blind_parser,
        verify_parser,
        unblind_parser,
    ):
        command_parser.add_argument("--custody-dir", type=pathlib.Path, required=True)
        command_parser.add_argument("--study-id", required=True)
    args = parser.parse_args(argv)

    if args.command == "seal":
        path = seal(args.campaign, args.custody_dir, args.study_id)
        print(f"blinding commitment -> {path}")
    elif args.command == "attach":
        path = attach(args.campaign, args.custody_dir, args.study_id)
        print(f"shared blinding commitment -> {path}")
    elif args.command == "blind":
        count = blind_campaign(
            args.source_campaign, args.campaign, args.custody_dir, args.study_id
        )
        print(f"{count} blinded run files -> {args.campaign / 'raw'}")
    elif args.command == "verify-named":
        digest = verify_named_campaign(
            args.source_campaign, args.campaign, args.custody_dir, args.study_id
        )
        print(f"named input matches custody manifest sha256 {digest}")
    else:
        paths = unblind_study(args.campaign, args.custody_dir, args.study_id)
        for path in paths:
            print(f"unblinding record -> {path}")


if __name__ == "__main__":
    main()
