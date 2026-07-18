#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///

# ─── How to run ───
# 1. Install uv (if not installed):
#      curl -LsSf https://astral.sh/uv/install.sh | sh
# 2. Run directly (no venv, no pip install needed):
#      uv run tools/validate_player_study.py --input study.json --expected-source-sha SOURCE --expected-release-sha RELEASE
# 3. Or make executable and run:
#      chmod +x tools/validate_player_study.py && ./tools/validate_player_study.py --input study.json --expected-source-sha SOURCE --expected-release-sha RELEASE
# ──────────────────
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import math
from pathlib import Path
import re
import sys


SHA256 = re.compile(r"^[0-9a-f]{64}$")
PARTICIPANT_ID = re.compile(r"^P-[1-9][0-9]*$")
OBSERVATION_CODES = frozenset({"none", "hesitated", "requested_help", "other"})
TOP_LEVEL = frozenset({"schema", "source_sha256", "release_sha256", "sessions"})
SESSION = frozenset({
    "participant_id", "door_guarantee_correct", "capacity_or_death_next_action_correct",
    "stop_route_obvious", "stopped_voluntarily", "observation_code",
})


@dataclass(frozen=True, slots=True)
class StudyError(Exception):
    message: str

    def __str__(self) -> str:
        return self.message


@dataclass(frozen=True, slots=True)
class Session:
    participant_id: str
    door_guarantee_correct: bool
    capacity_or_death_next_action_correct: bool
    stop_route_obvious: bool
    stopped_voluntarily: bool
    observation_code: str


@dataclass(frozen=True, slots=True)
class Study:
    source_sha256: str
    release_sha256: str
    sessions: tuple[Session, ...]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--expected-source-sha", required=True)
    parser.add_argument("--expected-release-sha", required=True)
    return parser.parse_args()


def exact_keys(record: dict[str, str | int | bool | list[dict[str, str | bool]] | dict[str, str | bool]], expected: frozenset[str], label: str) -> None:
    if set(record) != expected:
        raise StudyError(f"{label} has unknown, missing, or disallowed keys")


def string(record: dict[str, str | int | bool | list[dict[str, str | bool]] | dict[str, str | bool]], key: str) -> str:
    value = record[key]
    if not isinstance(value, str) or not value:
        raise StudyError(f"{key} must be a nonempty string")
    return value


def boolean(record: dict[str, str | int | bool | list[dict[str, str | bool]] | dict[str, str | bool]], key: str) -> bool:
    value = record[key]
    if not isinstance(value, bool):
        raise StudyError(f"{key} must be boolean")
    return value


def parse_session(raw: dict[str, str | bool]) -> Session:
    if set(raw) != SESSION:
        raise StudyError("session has unknown, missing, or disallowed keys")
    participant_id = raw["participant_id"]
    observation_code = raw["observation_code"]
    booleans = (
        raw["door_guarantee_correct"], raw["capacity_or_death_next_action_correct"],
        raw["stop_route_obvious"], raw["stopped_voluntarily"],
    )
    if not isinstance(participant_id, str) or PARTICIPANT_ID.fullmatch(participant_id) is None:
        raise StudyError("participant_id must use the P-<n> anonymized form")
    if not isinstance(observation_code, str) or observation_code not in OBSERVATION_CODES:
        raise StudyError("observation_code is not in the approved enum")
    if not all(isinstance(value, bool) for value in booleans):
        raise StudyError("session task outcomes must be boolean")
    return Session(
        participant_id=participant_id, door_guarantee_correct=raw["door_guarantee_correct"],
        capacity_or_death_next_action_correct=raw["capacity_or_death_next_action_correct"],
        stop_route_obvious=raw["stop_route_obvious"], stopped_voluntarily=raw["stopped_voluntarily"],
        observation_code=observation_code,
    )


def parse_study(path: Path) -> Study:
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise StudyError(f"cannot read study receipt: {exc}") from exc
    if not isinstance(raw, dict):
        raise StudyError("study receipt must be a JSON object")
    exact_keys(raw, TOP_LEVEL, "study receipt")
    if raw["schema"] != 1:
        raise StudyError("schema must be 1")
    source_sha256 = string(raw, "source_sha256")
    release_sha256 = string(raw, "release_sha256")
    if SHA256.fullmatch(source_sha256) is None or SHA256.fullmatch(release_sha256) is None:
        raise StudyError("source_sha256 and release_sha256 must be lowercase SHA-256 values")
    sessions = raw["sessions"]
    if not isinstance(sessions, list):
        raise StudyError("sessions must be an array")
    parsed: list[Session] = []
    for session in sessions:
        if not isinstance(session, dict):
            raise StudyError("session must be an object")
        parsed.append(parse_session(session))
    identifiers = [session.participant_id for session in parsed]
    if len(set(identifiers)) != len(identifiers):
        raise StudyError("participant_id values must be unique")
    return Study(source_sha256, release_sha256, tuple(parsed))


def verdict(study: Study, expected_source: str, expected_release: str) -> tuple[int, dict[str, str | int | bool]]:
    if study.source_sha256 != expected_source or study.release_sha256 != expected_release:
        raise StudyError("receipt hashes do not match the expected Todo 7 release linkage")
    total = len(study.sessions)
    minimum = math.ceil(total * 0.8)
    door_correct = sum(session.door_guarantee_correct for session in study.sessions)
    next_action_correct = sum(session.capacity_or_death_next_action_correct for session in study.sessions)
    stop_route_failures = sum(not session.stop_route_obvious for session in study.sessions)
    voluntary_stop_failures = sum(not session.stopped_voluntarily for session in study.sessions)
    passed = total >= 5 and door_correct >= minimum and next_action_correct >= minimum and stop_route_failures == 0 and voluntary_stop_failures == 0
    return (0 if passed else 3), {
        "schema": 1, "source_sha256": study.source_sha256, "release_sha256": study.release_sha256,
        "sessions": total, "comprehension_threshold": minimum, "door_guarantee_correct": door_correct,
        "capacity_or_death_next_action_correct": next_action_correct,
        "stop_route_failures": stop_route_failures, "voluntary_stop_failures": voluntary_stop_failures,
        "verdict": "pass" if passed else "blocked",
    }


def main() -> int:
    args = parse_args()
    try:
        study = parse_study(Path(args.input))
        code, result = verdict(study, args.expected_source_sha, args.expected_release_sha)
    except StudyError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(result, separators=(",", ":")))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
