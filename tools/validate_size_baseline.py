#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///

# ─── How to run ───
# 1. Install uv (if not installed):
#      curl -LsSf https://astral.sh/uv/install.sh | sh
# 2. Run directly (no venv, no pip install needed):
#      uv run tools/validate_size_baseline.py --manifest baseline.json --out BASELINE_SIZE
# 3. Or make executable and run:
#      chmod +x tools/validate_size_baseline.py && ./tools/validate_size_baseline.py --manifest baseline.json --out BASELINE_SIZE
# ──────────────────
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import sys


SHA256 = re.compile(r"^[0-9a-f]{64}$")
REQUIRED_KEYS = frozenset({
    "baseline_path", "release_sha256", "size", "source_sha256", "origin",
    "approved_by", "approved_at",
})


@dataclass(frozen=True, slots=True)
class ManifestError(Exception):
    message: str

    def __str__(self) -> str:
        return self.message


@dataclass(frozen=True, slots=True)
class BaselineManifest:
    baseline_path: Path
    release_sha256: str
    size: int
    source_sha256: str
    origin: str
    approved_by: str
    approved_at: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--out", required=True)
    return parser.parse_args()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def required_string(value: str | int | bool | None, field: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ManifestError(f"{field} must be a nonempty string")
    return value


def parse_manifest(path: Path) -> BaselineManifest:
    try:
        decoded = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ManifestError(f"cannot read manifest: {exc}") from exc
    if not isinstance(decoded, dict):
        raise ManifestError("manifest must be a JSON object")
    if set(decoded) != REQUIRED_KEYS:
        raise ManifestError("manifest keys must exactly match the baseline schema")
    baseline_path = required_string(decoded["baseline_path"], "baseline_path")
    release_sha256 = required_string(decoded["release_sha256"], "release_sha256")
    source_sha256 = required_string(decoded["source_sha256"], "source_sha256")
    origin = required_string(decoded["origin"], "origin")
    approved_by = required_string(decoded["approved_by"], "approved_by")
    approved_at = required_string(decoded["approved_at"], "approved_at")
    size = decoded["size"]
    if isinstance(size, bool) or not isinstance(size, int) or size < 1:
        raise ManifestError("size must be a positive integer")
    if SHA256.fullmatch(release_sha256) is None or SHA256.fullmatch(source_sha256) is None:
        raise ManifestError("release_sha256 and source_sha256 must be lowercase SHA-256 values")
    return BaselineManifest(
        baseline_path=Path(baseline_path), release_sha256=release_sha256, size=size,
        source_sha256=source_sha256, origin=origin, approved_by=approved_by,
        approved_at=approved_at,
    )


def validate(manifest: BaselineManifest) -> None:
    path = manifest.baseline_path
    if not path.is_file():
        raise ManifestError("baseline_path must identify an accessible regular file")
    actual_size = path.stat().st_size
    if actual_size != manifest.size:
        raise ManifestError("baseline size does not match manifest")
    if sha256_file(path) != manifest.release_sha256:
        raise ManifestError("baseline SHA-256 does not match manifest")


def main() -> int:
    args = parse_args()
    try:
        manifest = parse_manifest(Path(args.manifest))
        validate(manifest)
        Path(args.out).write_text(str(manifest.size), encoding="utf-8")
    except ManifestError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
