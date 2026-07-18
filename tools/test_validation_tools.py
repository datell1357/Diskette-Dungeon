#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///

# ─── How to run ───
# 1. Install uv (if not installed):
#      curl -LsSf https://astral.sh/uv/install.sh | sh
# 2. Run directly (no venv, no pip install needed):
#      uv run tools/test_validation_tools.py
# 3. Or make executable and run:
#      chmod +x tools/test_validation_tools.py && ./tools/test_validation_tools.py
# ──────────────────
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parent.parent
PYTHON = sys.executable


def run(arguments: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [PYTHON, *arguments],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )


def write_json(path: Path, value: dict[str, str | int | bool | list[dict[str, str | bool]] | dict[str, str | bool]]) -> None:
    path.write_text(json.dumps(value), encoding="utf-8")


def test_size_validator() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        release = root / "approved.exe"
        release.write_bytes(b"approved-release")
        digest = hashlib.sha256(release.read_bytes()).hexdigest()
        manifest = root / "baseline.json"
        output = root / "baseline-size.txt"
        valid = {
            "baseline_path": str(release),
            "release_sha256": digest,
            "size": release.stat().st_size,
            "source_sha256": "a" * 64,
            "origin": "approved Windows release archive",
            "approved_by": "release-operator",
            "approved_at": "2026-07-17T00:00:00Z",
        }
        write_json(manifest, valid)

        # Given: a matching approved release baseline.
        # When: the size validator receives its manifest.
        # Then: it writes only the validated decimal size.
        result = run(["tools/validate_size_baseline.py", "--manifest", str(manifest), "--out", str(output)])
        assert result.returncode == 0, result.stderr
        assert output.read_text(encoding="utf-8") == str(release.stat().st_size)

        # Given: a manifest without mandatory approval metadata.
        # When: it is validated.
        # Then: it is rejected and produces no output file.
        invalid = dict(valid)
        invalid["approved_by"] = ""
        write_json(manifest, invalid)
        output.unlink()
        rejected = run(["tools/validate_size_baseline.py", "--manifest", str(manifest), "--out", str(output)])
        assert rejected.returncode != 0
        assert not output.exists()

        # Given: a manifest with a mismatched release hash and size.
        # When: it is validated.
        # Then: it is rejected.
        invalid = dict(valid)
        invalid["release_sha256"] = "b" * 64
        invalid["size"] = 1
        write_json(manifest, invalid)
        rejected = run(["tools/validate_size_baseline.py", "--manifest", str(manifest), "--out", str(output)])
        assert rejected.returncode != 0


def test_player_validator() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source_sha = "c" * 64
        release_sha = "d" * 64
        study = root / "study.json"
        sessions = [
            {
                "participant_id": f"P-{number}",
                "door_guarantee_correct": True,
                "capacity_or_death_next_action_correct": True,
                "stop_route_obvious": True,
                "stopped_voluntarily": True,
                "observation_code": "none",
            }
            for number in range(1, 6)
        ]
        receipt = {
            "schema": 1,
            "source_sha256": source_sha,
            "release_sha256": release_sha,
            "sessions": sessions,
        }
        write_json(study, receipt)

        # Given: the literal approved receipt schema with anonymized sessions tied to the exact hashes.
        # When: the player-study validator runs.
        # Then: its emitted verdict is pass.
        passed = run([
            "tools/validate_player_study.py", "--input", str(study),
            "--expected-source-sha", source_sha, "--expected-release-sha", release_sha,
        ])
        assert passed.returncode == 0, passed.stderr
        assert json.loads(passed.stdout)["verdict"] == "pass"

        # Given: a receipt with an unknown field.
        # When: it is validated.
        # Then: it is rejected rather than treated as a player pass.
        receipt["unapproved"] = True
        write_json(study, receipt)
        rejected = run([
            "tools/validate_player_study.py", "--input", str(study),
            "--expected-source-sha", source_sha, "--expected-release-sha", release_sha,
        ])
        assert rejected.returncode != 0


def main() -> int:
    test_size_validator()
    test_player_validator()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
