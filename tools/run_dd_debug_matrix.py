#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///

# ─── How to run ───
# 1. Install uv (if not installed):
#      curl -LsSf https://astral.sh/uv/install.sh | sh
# 2. Run directly (no venv, no pip install needed):
#      uv run tools/run_dd_debug_matrix.py --binary build/DisketteDungeon_diag_mac --evidence-root build/evidence/run-unique --python "$(command -v python3)"
# 3. Or make executable and run:
#      chmod +x tools/run_dd_debug_matrix.py && ./tools/run_dd_debug_matrix.py --binary build/DisketteDungeon_diag_mac --evidence-root build/evidence/run-unique --python "$(command -v python3)"
# ──────────────────
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


REPO = Path(__file__).resolve().parent.parent


@dataclass(frozen=True, slots=True)
class MatrixError(Exception):
    message: str

    def __str__(self) -> str:
        return self.message


@dataclass(frozen=True, slots=True)
class Case:
    name: str
    command: tuple[str, ...]
    expected_exit: int
    expected_stdout: bytes | None = None
    expected_stderr: bytes | None = None
    repeat_of: str | None = None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--evidence-root", required=True)
    parser.add_argument("--python", required=True)
    return parser.parse_args()


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def source_digest() -> str:
    hasher = hashlib.sha256()
    sources = sorted(path for path in (REPO / "src").rglob("*") if path.suffix in {".c", ".h"})
    for path in sources:
        hasher.update(path.relative_to(REPO).as_posix().encode("utf-8"))
        hasher.update(b"\0")
        hasher.update(path.read_bytes())
        hasher.update(b"\0")
    return hasher.hexdigest()


def base(*extra: str) -> tuple[str, ...]:
    return ("--clean-profile", "--seed", "12345", "--difficulty", "1", "--weapon", "0", "--ngplus", "0", *extra)


def matrix_cases(root: Path, python: Path) -> tuple[Case, ...]:
    cases: list[Case] = []
    pairs = ((12345, 0, 5), (12345, 1, 4), (12345, 2, 4), (12345, 3, 4), (12366, 0, 5))
    for seed, biome, room in pairs:
        for action in ("snapshot-room", "snapshot-reward"):
            arguments = ("--clean-profile", "--seed", str(seed), "--difficulty", "1", "--weapon", "0", "--ngplus", "0", "--jump", f"{biome},{room}", "--action", action)
            name = f"snapshot-{seed}-{biome}-{room}-{action}"
            cases.extend((Case(f"{name}-a", arguments, 0), Case(f"{name}-b", arguments, 0, repeat_of=f"{name}-a")))
    cases.extend((
        Case("qne", base("--jump", "2,4", "--force-target", "0,0,0,0", "--force-pending", "1", "--death-action", "finalize-twice", "--action", "fixture-qne"), 0),
        Case("qae", base("--jump", "2,4", "--force-target", "0,8,1,1", "--force-pending", "2", "--death-action", "self-destruct", "--action", "fixture-qae"), 0),
        Case("qcol", base("--jump", "2,4", "--force-pending", "1", "--force-event", "2,1,2,1", "--action", "fixture-qcol-input"), 0),
        Case("modifiers", base("--action", "fixture-modifiers"), 0),
        Case("haste", base("--jump", "2,4", "--action", "fixture-haste"), 0),
        Case("endings", base("--action", "fixture-endings"), 0),
        Case("invariant", base("--action", "fixture-invariant-failure"), 3, b"", b'{"error":"invariant","what":"forced","expected":1,"actual":0}\n'),
    ))
    for branch in ("keep", "discard"):
        telemetry = root / "telemetry" / f"f10-{branch}.jsonl"
        cases.append(Case(f"f10-{branch}", base("--jump", "0,5", "--auto", "--god", "--f10-branch", branch, "--duration-ms", "180000", "--telemetry", str(telemetry), "--telemetry-interval-ms", "1000"), 0))
    negative = (
        (("--bogus",), "unknown-option"), (("--seed",), "missing-operand"),
        (("--seed", "1", "--seed", "2"), "duplicate-option"), (("--seed", "xyz"), "malformed-number"),
        (("--seed", "0"), "out-of-range"),
        (("--seed", "12345", "--difficulty", "1", "--weapon", "0", "--ngplus", "0", "--action", "snapshot-room"), "clean-profile-required"),
        (base("--jump", "0,5", "--telemetry", str(root / "telemetry" / "invalid-a.jsonl")), "duration-required"),
        (("--duration-ms", "1000"), "duration-without-telemetry"),
        (base("--jump", "0,5", "--duration-ms", "1000", "--telemetry", str(root / "telemetry" / "invalid-b.jsonl"), "--action", "snapshot-room"), "action-telemetry-incompatible"),
        (("--isolated-profile", str(root / "unsafe" / "home"), "--action", "fixture-save-roundtrip", "--expect", "v1"), "unsafe-isolated-profile"),
    )
    for number, (arguments, error) in enumerate(negative, 1):
        cases.append(Case(f"negative-{number}", arguments, 2, b"", json.dumps({"error": error}, separators=(",", ":")).encode("utf-8") + b"\n"))
    materializer = REPO / "tools" / "materialize_save_fixture.py"
    for fixture, action, expected in (("valid-v1", "fixture-save-roundtrip", "v1"), ("valid-v2", "fixture-save-roundtrip", "v2"), ("bad-v1-checksum", "fixture-save-reject", "v1"), ("bad-v2-checksum", "fixture-save-reject", "v2")):
        fixture_root = root / "save"
        cases.append(Case(f"materialize-{fixture}", (str(python), str(materializer), "--case", fixture, "--root", str(fixture_root)), 0))
        cases.append(Case(f"save-{fixture}", ("--isolated-profile", str(fixture_root / "home"), "--action", action, "--expect", expected), 0))
    return tuple(cases)


def run_case(case: Case, binary: Path, root: Path, source_sha: str, binary_sha: str, outputs: dict[str, bytes]) -> dict[str, str | int | bool | list[str] | None]:
    command = case.command if case.command[0] == str(binary) or case.command[0].endswith(".py") else (str(binary), *case.command)
    if case.name.startswith("materialize-"):
        command = case.command
    environment = dict(os.environ)
    home = root / "homes" / case.name
    if "--isolated-profile" in command:
        home = Path(command[command.index("--isolated-profile") + 1])
    environment["HOME"] = str(home)
    try:
        completed = subprocess.run(command, cwd=REPO, env=environment, check=False, capture_output=True)
        stdout, stderr, exit_code = completed.stdout, completed.stderr, completed.returncode
    except OSError as exc:
        stdout, stderr, exit_code = b"", str(exc).encode("utf-8"), 127
    passed = exit_code == case.expected_exit
    if case.expected_stdout is not None:
        passed = passed and stdout == case.expected_stdout
    if case.expected_stderr is not None:
        passed = passed and stderr == case.expected_stderr
    if case.repeat_of is not None:
        passed = passed and stdout == outputs[case.repeat_of]
    outputs[case.name] = stdout
    return {
        "name": case.name, "command": list(command), "exit": exit_code,
        "stdout_sha256": digest(stdout), "stderr_sha256": digest(stderr),
        "source_sha256": source_sha, "binary_sha256": binary_sha, "pass": passed,
    }


def main() -> int:
    args = parse_args()
    binary, root, python = Path(args.binary).resolve(), Path(args.evidence_root).resolve(), Path(args.python).resolve()
    if root.exists() or root.is_symlink():
        print("error: evidence-root must be a unique path that does not yet exist", file=sys.stderr)
        return 2
    if not binary.is_file() or not python.is_file():
        print("error: --binary and --python must identify accessible regular files", file=sys.stderr)
        return 2
    root.mkdir(parents=True)
    (root / "telemetry").mkdir()
    source_sha, binary_sha = source_digest(), file_digest(binary)
    outputs: dict[str, bytes] = {}
    rows = [run_case(case, binary, root, source_sha, binary_sha, outputs) for case in matrix_cases(root, python)]
    summary = {"schema": 1, "source_sha256": source_sha, "binary_sha256": binary_sha, "pass": all(row["pass"] for row in rows), "rows": rows}
    (root / "matrix-summary.json").write_text(json.dumps(summary, separators=(",", ":")), encoding="utf-8")
    print(json.dumps({"evidence_root": str(root), "pass": summary["pass"], "rows": len(rows)}, separators=(",", ":")))
    return 0 if summary["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
