#!/usr/bin/env python3
"""Independently analyze raw DD_DEBUG Mac frame telemetry.

This evidence helper is intentionally standalone: it has no game/runtime
imports and is not linked or shipped with the game.
"""

import argparse
from collections import deque
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile


SCHEMA = 2
WARMUP_MS = 60_000
MEASUREMENT_MS = 180_000
MEASUREMENT_END_US = (WARMUP_MS + MEASUREMENT_MS) * 1_000
ROLLING_WINDOW_US = 1_000_000
ROLLING_FPS_FLOOR = 58


class InputError(Exception):
    pass


def fail(message: str) -> "NoReturn":
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(2)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True)
    parser.add_argument("--warmup-ms", type=int, default=WARMUP_MS)
    parser.add_argument("--measurement-ms", type=int, default=MEASUREMENT_MS)
    parser.add_argument("--output", required=True)
    return parser.parse_args()


def strict_constant(value: str) -> None:
    raise ValueError(value)


def integer(value: object, field: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise InputError(f"{field} must be an integer")
    return value


def reject_symlink_components(path: Path, label: str) -> None:
    absolute = Path(os.path.abspath(path))
    current = Path(absolute.anchor)
    for part in absolute.parts[1:]:
        current /= part
        if current.is_symlink():
            raise InputError(f"{label} must not contain symlinks: {current}")
def record_kind(record: object, line_number: int) -> tuple[str, dict]:
    if not isinstance(record, dict):
        raise InputError(f"line {line_number}: record must be an object")
    schema = integer(record.get("schema"), f"line {line_number} schema")
    if schema != SCHEMA:
        raise InputError(f"line {line_number}: schema must be {SCHEMA}")
    kind = record.get("kind")
    if not isinstance(kind, str):
        raise InputError(f"line {line_number}: kind must be a string")
    return kind, record


def read_telemetry(path: Path) -> tuple[bytes, list[dict]]:
    try:
        raw = path.read_bytes()
    except OSError as exc:
        raise InputError(f"cannot read input: {exc}") from exc
    frames: list[dict] = []
    starts = 0
    stops = 0
    saw_stop = False
    expected_index = 0
    previous_elapsed = None
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise InputError(f"input is not UTF-8: {exc}") from exc
    lines = text.splitlines()
    if not lines:
        raise InputError("input is empty")
    for line_number, line in enumerate(lines, 1):
        if not line.strip():
            raise InputError(f"line {line_number}: blank lines are not allowed")
        try:
            record = json.loads(line, parse_constant=strict_constant)
        except (ValueError, json.JSONDecodeError) as exc:
            raise InputError(f"line {line_number}: invalid JSON") from exc
        kind, record = record_kind(record, line_number)
        if saw_stop:
            raise InputError(f"line {line_number}: records after telemetry_stop")
        if kind == "telemetry_start":
            starts += 1
            if starts != 1 or frames:
                raise InputError(f"line {line_number}: duplicate or misplaced telemetry_start")
            continue
        if kind == "telemetry_state":
            if starts != 1 or not frames:
                raise InputError(f"line {line_number}: misplaced telemetry_state")
            continue
        if kind == "frame":
            if starts != 1:
                raise InputError(f"line {line_number}: frame before telemetry_start")
            index = integer(record.get("frame_index"), f"line {line_number} frame_index")
            elapsed = integer(record.get("elapsed_us"), f"line {line_number} elapsed_us")
            delta = integer(record.get("delta_us"), f"line {line_number} delta_us")
            if index != expected_index:
                raise InputError(f"line {line_number}: missing or duplicate frame index")
            if elapsed < 0:
                raise InputError(f"line {line_number}: elapsed_us must be nonnegative")
            if not frames:
                if delta != 0:
                    raise InputError("first frame must have delta_us=0")
            else:
                if elapsed <= previous_elapsed:
                    raise InputError(f"line {line_number}: frame elapsed_us is not monotonic")
                if delta <= 0 or delta != elapsed - previous_elapsed:
                    raise InputError(f"line {line_number}: delta_us does not match elapsed_us")
            if elapsed > MEASUREMENT_END_US:
                raise InputError(f"line {line_number}: frame exceeds measurement endpoint")
            frames.append({"elapsed_us": elapsed, "delta_us": delta})
            expected_index += 1
            previous_elapsed = elapsed
            continue
        if kind == "telemetry_stop":
            if starts != 1 or not frames:
                raise InputError(f"line {line_number}: misplaced telemetry_stop")
            stops += 1
            if stops != 1:
                raise InputError(f"line {line_number}: duplicate telemetry_stop")
            stop_count = integer(record.get("frame_count"), f"line {line_number} frame_count")
            stop_elapsed = integer(record.get("elapsed_us"), f"line {line_number} elapsed_us")
            if stop_count != len(frames):
                raise InputError(f"line {line_number}: telemetry frame_count mismatch")
            if stop_elapsed != previous_elapsed:
                raise InputError(f"line {line_number}: telemetry elapsed_us mismatch")
            saw_stop = True
            continue
        raise InputError(f"line {line_number}: unsupported record kind {kind!r}")
    if starts != 1:
        raise InputError("exactly one telemetry_start is required")
    if stops != 1:
        raise InputError("exactly one telemetry_stop is required")
    return raw, frames


def milliseconds(microseconds: int) -> float:
    return microseconds / 1_000.0


def analyze(raw: bytes, frames: list[dict]) -> dict:
    included = [
        frame for frame in frames[1:]
        if WARMUP_MS * 1_000 < frame["elapsed_us"] <= MEASUREMENT_END_US
    ]
    deltas = [frame["delta_us"] for frame in included]
    elapsed_us = sum(deltas)
    frame_count = len(deltas)
    if deltas:
        ordered = sorted(deltas)
        rank = (95 * frame_count + 99) // 100 - 1
        p95_us = ordered[rank]
        worst_us = max(deltas)
    else:
        p95_us = 0
        worst_us = 0

    window = deque()
    max_below_us = 0
    below_start = None
    for frame in frames[1:]:
        timestamp = frame["elapsed_us"]
        while window and window[0] <= timestamp - ROLLING_WINDOW_US:
            window.popleft()
        window.append(timestamp)
        if timestamp <= WARMUP_MS * 1_000:
            continue
        if len(window) < ROLLING_FPS_FLOOR:
            if below_start is None:
                below_start = timestamp
        elif below_start is not None:
            max_below_us = max(max_below_us, timestamp - below_start)
            below_start = None
    if below_start is not None and included:
        max_below_us = max(max_below_us, included[-1]["elapsed_us"] - below_start)

    reasons = []
    if frame_count < 10_440:
        reasons.append("frame_count_lt_10440")
    if not 179_000 <= milliseconds(elapsed_us) <= 181_000:
        reasons.append("elapsed_ms_out_of_range")
    if milliseconds(p95_us) > 18.000:
        reasons.append("p95_frame_ms_gt_18.000")
    if milliseconds(worst_us) > 33.334:
        reasons.append("worst_frame_ms_gt_33.334")
    if milliseconds(max_below_us) > 1_000.000:
        reasons.append("max_contiguous_below_floor_ms_gt_1000")
    summary = {
        "schema": 1,
        "kind": "mac_frame_summary",
        "source_telemetry_sha256": hashlib.sha256(raw).hexdigest(),
        "frame_count": frame_count,
        "elapsed_ms": milliseconds(elapsed_us),
        "warmup_ms": WARMUP_MS,
        "measurement_ms": MEASUREMENT_MS,
        "p95_frame_ms": milliseconds(p95_us),
        "worst_frame_ms": milliseconds(worst_us),
        "rolling_window_ms": 1_000,
        "rolling_fps_floor": ROLLING_FPS_FLOOR,
        "max_contiguous_below_floor_ms": milliseconds(max_below_us),
        "status": "pass" if not reasons else "fail",
        "reasons": reasons,
    }
    return summary


def write_summary(path: Path, summary: dict) -> None:
    if path.exists() or path.is_symlink():
        if path.is_symlink() or not path.is_file():
            raise InputError(f"output must be a regular file: {path}")
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        payload = (json.dumps(summary, separators=(",", ":")) + "\n").encode("utf-8")
        fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=str(path.parent))
        try:
            with os.fdopen(fd, "wb") as stream:
                stream.write(payload)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, path)
        except OSError:
            try:
                os.unlink(temporary)
            except OSError:
                pass
            raise
    except OSError as exc:
        raise InputError(f"cannot write output: {exc}") from exc


def main() -> int:
    if sys.version_info < (3, 12):
        fail("Python 3.12 or newer is required")
    args = parse_args()
    if args.warmup_ms != WARMUP_MS or args.measurement_ms != MEASUREMENT_MS:
        fail("warmup and measurement must be 60000 and 180000 ms")
    input_arg = Path(args.input).expanduser()
    output_arg = Path(args.output).expanduser()
    try:
        reject_symlink_components(input_arg, "input")
        reject_symlink_components(output_arg, "output")
        input_path = input_arg.resolve()
        output_path = output_arg.resolve()
    except InputError as exc:
        fail(str(exc))
    if input_path == output_path:
        fail("output must differ from input")
    try:
        raw, frames = read_telemetry(input_path)
        summary = analyze(raw, frames)
        write_summary(output_path, summary)
    except InputError as exc:
        fail(str(exc))
    print(json.dumps(summary, separators=(",", ":")))
    return 0 if summary["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
