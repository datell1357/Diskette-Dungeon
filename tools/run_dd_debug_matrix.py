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
class FixtureExpectation:
    action: str
    checkpoint: str
    state: str
    expected_ms: int | None = None
    recovery_result: str | None = None
    core_count: int | None = None
    natural_handoff: bool = False
    skip: bool = False


@dataclass(frozen=True, slots=True)
class Case:
    name: str
    command: tuple[str, ...]
    expected_exit: int
    expected_stdout: bytes | None = None
    expected_stderr: bytes | None = None
    repeat_of: str | None = None
    fixture: FixtureExpectation | None = None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--evidence-root", required=True)
    parser.add_argument("--python", required=True)
    parser.add_argument("--fixture-assertion-self-test", action="store_true")
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


def fixture_base(*extra: str) -> tuple[str, ...]:
    return ("--clean-profile", "--seed", "1", "--difficulty", "0", "--weapon", "0", "--ngplus", "1", *extra)


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
        Case("story-signals", base("--action", "fixture-story-signals"), 0),
        Case("invariant", base("--action", "fixture-invariant-failure"), 3, b"", b'{"error":"invariant","what":"forced","expected":1,"actual":0}\n'),
    ))
    for checkpoint, target_ms in (("insert", 1800), ("seek", 3600), ("retry", 7600), ("recover", 9600), ("transfer", 14000)):
        cases.append(Case(
            f"opening-{checkpoint}",
            fixture_base("--action", "fixture-ddd-opening", "--checkpoint", checkpoint, "--hold-ms", "10000"),
            0,
            fixture=FixtureExpectation("fixture-ddd-opening", checkpoint, "ST_BOOT", expected_ms=target_ms),
        ))
    for checkpoint, expectation in (
        ("title-handoff", FixtureExpectation("fixture-ddd-opening", "title-handoff", "ST_TITLE", natural_handoff=True)),
        ("skip-key", FixtureExpectation("fixture-ddd-opening", "skip-key", "ST_TITLE", skip=True)),
        ("skip-mouse", FixtureExpectation("fixture-ddd-opening", "skip-mouse", "ST_TITLE", skip=True)),
    ):
        cases.append(Case(
            f"opening-{checkpoint}",
            fixture_base("--action", "fixture-ddd-opening", "--checkpoint", checkpoint, "--hold-ms", "10000"),
            0,
            fixture=expectation,
        ))
    for checkpoint, core_count, recovery_result in (
        ("recovery-failed", 0, "bad"), ("partial-recovery", 1, "standard"),
        ("partial-recovery", 2, "standard"), ("partial-recovery", 3, "standard"),
        ("complete-recovery", 4, "true"),
    ):
        cases.append(Case(
            f"ending-{checkpoint}-{core_count}",
            fixture_base("--action", "fixture-ddd-ending", "--checkpoint", checkpoint, "--core-count", str(core_count), "--hold-ms", "10000"),
            0,
            fixture=FixtureExpectation("fixture-ddd-ending", checkpoint, "ST_ENDING", recovery_result=recovery_result, core_count=core_count),
        ))
    cases.append(Case(
        "ending-invalid-checkpoint",
        fixture_base("--action", "fixture-ddd-ending", "--checkpoint", "insert", "--core-count", "0", "--hold-ms", "10000"),
        2,
        b"",
        b'{"error":"invalid-checkpoint","action":"fixture-ddd-ending","checkpoint":"insert"}\n',
    ))
    cases.extend((
        Case(
            "opening-invalid-checkpoint",
            fixture_base("--action", "fixture-ddd-opening", "--checkpoint", "recovery-failed", "--hold-ms", "10000"),
            2, b"", b'{"error":"invalid-checkpoint","action":"fixture-ddd-opening","checkpoint":"recovery-failed"}\n',
        ),
        Case(
            "ending-invalid-core-count",
            fixture_base("--action", "fixture-ddd-ending", "--checkpoint", "partial-recovery", "--core-count", "0", "--hold-ms", "10000"),
            2, b"", b'{"error":"invalid-core-count"}\n',
        ),
        Case(
            "start-intro-invalid-checkpoint",
            fixture_base("--action", "fixture-ddd-start-intro", "--checkpoint", "insert", "--hold-ms", "10000"),
            2, b"", b'{"error":"invalid-checkpoint","action":"fixture-ddd-start-intro","checkpoint":"insert"}\n',
        ),
    ))
    for checkpoint, state in (
        ("first-run", "ST_INTRO"), ("placement", "ST_INTRO"), ("latch", "ST_INTRO"),
        ("drive-stop-hold", "ST_INTRO"), ("track", "ST_INTRO"), ("fragment", "ST_INTRO"),
        ("handoff", "ST_PLAY"), ("repeat-bypass", "ST_PLAY"), ("queued-replay", "ST_INTRO"),
        ("post-replay-bypass", "ST_PLAY"),
    ):
        cases.append(Case(
            f"start-intro-{checkpoint}",
            fixture_base("--action", "fixture-ddd-start-intro", "--checkpoint", checkpoint, "--hold-ms", "10000"),
            0,
            fixture=FixtureExpectation("fixture-ddd-start-intro", checkpoint, state),
        ))
    for checkpoint, state in (
        ("death", "ST_DEAD"), ("door", "ST_PLAY"), ("pause", "ST_PAUSE"),
        ("hit", "ST_PLAY"), ("boss-reward", "ST_PLAY"), ("relic-swap", "ST_RELIC_SWAP"),
        ("memory-event", "ST_PLAY"), ("boss-intro", "ST_PLAY"), ("core-flashback", "ST_FLASHBACK"),
    ):
        cases.append(Case(
            f"showcase-{checkpoint}",
            fixture_base("--action", "fixture-ddd-ui-showcase", "--checkpoint", checkpoint, "--hold-ms", "10000"),
            0,
            fixture=FixtureExpectation("fixture-ddd-ui-showcase", checkpoint, state),
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
    fixture_root = REPO / "build" / "evidence" / root.name / "save"
    for fixture, action, expected in (
        ("valid-v1", "fixture-save-roundtrip", "v1"),
        ("valid-v2", "fixture-save-roundtrip", "v2"),
        ("valid-v3-00", "fixture-save-roundtrip", "v3"),
        ("valid-v3-01", "fixture-save-roundtrip", "v3"),
        ("valid-v3-10", "fixture-save-roundtrip", "v3"),
        ("valid-v3-11", "fixture-save-roundtrip", "v3"),
        ("valid-v4-10", "fixture-save-roundtrip", "v4"),
        ("bad-v1-checksum", "fixture-save-reject", "v1"),
        ("bad-v2-checksum", "fixture-save-reject", "v2"),
        ("bad-v3-checksum", "fixture-save-reject", "v3"),
        ("bad-v4-checksum", "fixture-save-reject", "v4"),
        ("bad-v1-length", "fixture-save-reject", "v1"),
        ("bad-v2-length", "fixture-save-reject", "v2"),
        ("bad-v3-length", "fixture-save-reject", "v3"),
        ("bad-v4-length", "fixture-save-reject", "v4"),
    ):
        cases.append(Case(f"materialize-{fixture}", (str(python), str(materializer), "--case", fixture, "--root", str(fixture_root)), 0))
        cases.append(Case(f"save-{fixture}", ("--isolated-profile", str(fixture_root / "home"), "--action", action, "--expect", expected), 0))
    for checkpoint in ("options-toggle", "options-invalid-state", "options-invalid-input"):
        cases.append(Case(f"materialize-options-{checkpoint}", (str(python), str(materializer), "--case", "valid-v4-10", "--root", str(fixture_root)), 0))
        cases.append(Case(f"options-{checkpoint}", ("--isolated-profile", str(fixture_root / "home"), "--action", "fixture-ddd-options", "--checkpoint", checkpoint, "--expect", "v4"), 0))
    return tuple(cases)


def fixture_failure(stdout: bytes, expectation: FixtureExpectation, source_sha: str) -> str | None:
    lines = stdout.splitlines()
    if len(lines) != 1:
        return "fixture-jsonl-lines"
    try:
        receipt = json.loads(lines[0])
    except json.JSONDecodeError:
        return "fixture-json"
    if not isinstance(receipt, dict):
        return "fixture-json-object"
    required = (("action", expectation.action), ("checkpoint", expectation.checkpoint), ("state", expectation.state))
    if expectation.action in {"fixture-ddd-opening", "fixture-ddd-ending"}:
        required += (
            ("retry_count", 3), ("writeback", False), ("profile_unchanged", True),
            ("meta_save_events", 1), ("fixture_ready", True),
            ("core_mapping", {"0": "bad", "1": "standard", "2": "standard", "3": "standard", "4": "true"}),
        )
    if expectation.action in {"fixture-ddd-start-intro", "fixture-ddd-ui-showcase"}:
        required += (("fixture_ready", True),)
    for field, value in required:
        if receipt.get(field) != value:
            return field
    if receipt.get("source_sha256") != source_sha:
        return "source_sha256-mismatch"
    if expectation.action == "fixture-ddd-start-intro":
        if receipt.get("input_latches_clear") is not True:
            return "input_latches_clear"
        if expectation.checkpoint == "latch":
            if receipt.get("esc_ignored") is not True:
                return "esc_ignored"
            if receipt.get("input_state_unchanged") is not True:
                return "input_state_unchanged"
        if expectation.checkpoint == "drive-stop-hold":
            if receipt.get("drive_stop_hold") is not True or receipt.get("disk_inside_drive") is not True:
                return "drive_stop_hold"
        if expectation.checkpoint in {"handoff", "repeat-bypass", "post-replay-bypass"}:
            if receipt.get("intro_seen") != 1 or receipt.get("intro_replay_queued") != 0:
                return "intro_completion"
        if expectation.checkpoint == "handoff" and receipt.get("meta_save_events") != 1:
            return "handoff_save"
        if expectation.checkpoint == "repeat-bypass" and receipt.get("meta_save_events") != 1:
            return "repeat_bypass_save"
        if expectation.checkpoint == "queued-replay":
            if receipt.get("intro_seen") != 1 or receipt.get("intro_replay_queued") != 1:
                return "queued_replay"
        if expectation.checkpoint == "post-replay-bypass":
            if receipt.get("meta_save_events") != 2 or receipt.get("replay_complete") is not True:
                return "post_replay_bypass"
    if expectation.action == "fixture-ddd-ui-showcase":
        if receipt.get("state_after_drive") != expectation.state or receipt.get("state_after_1s") != expectation.state:
            return "showcase_state"
        if not isinstance(receipt.get("frames_after_drive"), int) or receipt["frames_after_drive"] < 2:
            return "showcase_frames"
    if expectation.expected_ms is not None:
        state_t_ms = receipt.get("state_t_ms")
        if not isinstance(state_t_ms, int) or not expectation.expected_ms <= state_t_ms <= expectation.expected_ms + 100:
            return "state_t_ms"
    if expectation.recovery_result is not None:
        if receipt.get("recovery_result") != expectation.recovery_result:
            return "recovery_result"
        if receipt.get("core_count") != expectation.core_count:
            return "core_count"
        if receipt.get("clean_profile_active") is not True or receipt.get("meta_load_suppressed") is not True:
            return "clean_profile"
    if expectation.natural_handoff:
        if receipt.get("natural_timeout") is not True or receipt.get("handoff_at_ms") != 15000:
            return "natural_handoff"
    if expectation.skip:
        if receipt.get("entered_play") is not False or receipt.get("skip_same_frame") is not True:
            return "skip"
    return None


def run_case(case: Case, binary: Path, root: Path, source_sha: str, receipt_source_sha: str, binary_sha: str, outputs: dict[str, bytes]) -> dict[str, str | int | bool | list[str] | None]:
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
    fixture_error = fixture_failure(stdout, case.fixture, receipt_source_sha) if case.fixture is not None else None
    passed = passed and fixture_error is None
    outputs[case.name] = stdout
    return {
        "name": case.name, "command": list(command), "exit": exit_code,
        "stdout_sha256": digest(stdout), "stderr_sha256": digest(stderr),
        "source_sha256": source_sha, "binary_sha256": binary_sha, "pass": passed,
        "fixture_error": fixture_error,
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
    if args.fixture_assertion_self_test:
        receipt = {
            "action": "fixture-ddd-ending", "checkpoint": "recovery-failed", "state": "ST_ENDING",
            "retry_count": 3, "writeback": False, "profile_unchanged": True, "meta_save_events": 0,
            "fixture_ready": True, "core_mapping": {"0": "bad", "1": "standard", "2": "standard", "3": "standard", "4": "true"},
        }
        expected = FixtureExpectation("fixture-ddd-ending", "recovery-failed", "ST_ENDING", recovery_result="bad", core_count=0)
        expected_source_sha = file_digest(REPO / "src" / "main.c")
        if fixture_failure(json.dumps(receipt).encode("utf-8"), expected, expected_source_sha) != "meta_save_events":
            print("error: fixture assertion self-test did not reject the wrong expected value", file=sys.stderr)
            return 1
        receipt["meta_save_events"] = 1
        receipt["source_sha256"] = "0" * 64
        wrong_source = json.dumps(receipt).encode("utf-8")
        if fixture_failure(wrong_source, expected, expected_source_sha) != "source_sha256-mismatch":
            print("error: fixture assertion self-test did not reject the wrong source hash", file=sys.stderr)
            return 1
        print('{"fixture_assertion_self_test":"pass","rejected_field":"meta_save_events","rejected_source_field":"source_sha256"}')
        return 0
    root.mkdir(parents=True)
    (root / "telemetry").mkdir()
    source_sha, receipt_source_sha, binary_sha = source_digest(), file_digest(REPO / "src" / "main.c"), file_digest(binary)
    outputs: dict[str, bytes] = {}
    rows = [run_case(case, binary, root, source_sha, receipt_source_sha, binary_sha, outputs) for case in matrix_cases(root, python)]
    summary = {"schema": 1, "source_sha256": source_sha, "binary_sha256": binary_sha, "pass": all(row["pass"] for row in rows), "rows": rows}
    (root / "matrix-summary.json").write_text(json.dumps(summary, separators=(",", ":")), encoding="utf-8")
    print(json.dumps({"evidence_root": str(root), "pass": summary["pass"], "rows": len(rows)}, separators=(",", ":")))
    return 0 if summary["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
