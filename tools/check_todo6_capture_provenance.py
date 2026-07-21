#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path
from typing import Final


EXPECTED_CAPTURES: Final = {
    "core-flashback": {
        "state_record": "task-6-native/flashback-state.json",
        "persisted_png": "task-6-gameplay-story-rework.png",
        "pid": 32270,
        "window_id": 30670,
    },
    "complete-recovery": {
        "state_record": "task-6-native/ending-state.json",
        "persisted_png": "task-6-gameplay-story-rework-ending.png",
        "pid": 31458,
        "window_id": 30659,
    },
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def png_dimensions(path: Path) -> list[int]:
    header = path.read_bytes()[:24]
    if header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise ValueError(f"{path} is not a PNG")
    return list(struct.unpack(">II", header[16:24]))


def observed_processes() -> list[str]:
    result = subprocess.run(
        ["pgrep", "-fl", "DisketteDungeon_diag_mac"],
        check=False,
        text=True,
        capture_output=True,
    )
    return [line for line in result.stdout.splitlines() if line]


def capture_contract(captures: list[dict[str, object]]) -> dict[str, object]:
    checkpoint_names = [capture.get("checkpoint") for capture in captures]
    named_checkpoints = [name for name in checkpoint_names if isinstance(name, str)]
    duplicate_checkpoints = sorted(
        {name for name in named_checkpoints if named_checkpoints.count(name) > 1}
    )
    expected_names = set(EXPECTED_CAPTURES)
    actual_names = set(named_checkpoints)
    mapping_checks: dict[str, dict[str, bool]] = {}

    for capture in captures:
        checkpoint = capture.get("checkpoint")
        if not isinstance(checkpoint, str) or checkpoint not in EXPECTED_CAPTURES:
            continue
        expected = EXPECTED_CAPTURES[checkpoint]
        window = capture.get("window")
        mapping_checks[checkpoint] = {
            "state_record_match": capture.get("state_record") == expected["state_record"],
            "persisted_png_match": capture.get("persisted_png") == expected["persisted_png"],
            "pid_match": capture.get("pid") == expected["pid"],
            "window_id_match": isinstance(window, dict)
            and window.get("id") == expected["window_id"],
        }

    expected_mapping_match = all(
        checkpoint in mapping_checks
        and all(mapping_checks[checkpoint].values())
        for checkpoint in EXPECTED_CAPTURES
    )
    count_match = len(captures) == len(EXPECTED_CAPTURES)
    names_are_strings = len(named_checkpoints) == len(captures)
    no_duplicates = not duplicate_checkpoints
    missing_checkpoints = sorted(expected_names - actual_names)
    unknown_checkpoints = sorted(actual_names - expected_names)
    return {
        "expected_count": len(EXPECTED_CAPTURES),
        "observed_count": len(captures),
        "expected_checkpoints": sorted(expected_names),
        "observed_checkpoints": checkpoint_names,
        "missing_checkpoints": missing_checkpoints,
        "unknown_checkpoints": unknown_checkpoints,
        "duplicate_checkpoints": duplicate_checkpoints,
        "count_match": count_match,
        "checkpoint_names_are_strings": names_are_strings,
        "no_duplicate_checkpoints": no_duplicates,
        "exact_checkpoint_set_match": actual_names == expected_names,
        "expected_mappings": EXPECTED_CAPTURES,
        "mapping_checks": mapping_checks,
        "expected_mapping_match": expected_mapping_match,
        "pass": count_match
        and names_are_strings
        and no_duplicates
        and actual_names == expected_names
        and expected_mapping_match,
    }


def apply_negative_case(manifest: dict[str, object], negative_case: str | None) -> None:
    if negative_case is None:
        return
    captures = manifest["capture_manifest"]
    if not isinstance(captures, list):
        raise ValueError("capture_manifest must be a list")
    if negative_case == "empty":
        manifest["capture_manifest"] = []
        return
    if negative_case == "duplicate":
        manifest["capture_manifest"] = [captures[0], captures[0]]
        return
    if negative_case == "unknown":
        unknown_capture = dict(captures[0])
        unknown_capture["checkpoint"] = "unknown-checkpoint"
        manifest["capture_manifest"] = [unknown_capture, *captures[1:]]
        return
    if negative_case == "remove":
        manifest["capture_manifest"] = captures[1:]
        return
    if negative_case == "rename":
        renamed_capture = dict(captures[0])
        renamed_capture["checkpoint"] = "renamed-flashback"
        manifest["capture_manifest"] = [renamed_capture, *captures[1:]]
        return
    raise ValueError(f"unknown negative case: {negative_case}")


def validate(manifest_path: Path, workspace: Path, negative_case: str | None) -> dict[str, object]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    apply_negative_case(manifest, negative_case)
    captures = manifest["capture_manifest"]
    if not isinstance(captures, list) or not all(isinstance(capture, dict) for capture in captures):
        raise ValueError("capture_manifest must contain objects")
    contract = capture_contract(captures)
    expected_source = sha256(workspace / "src/ui_story.c")
    expected_binary = sha256(workspace / "build/DisketteDungeon_diag_mac")
    checks: list[dict[str, object]] = []

    if not contract["pass"]:
        active = observed_processes()
        return {
            "schema": "task-6-capture-provenance-check-v2",
            "manifest": str(manifest_path),
            "negative_case": negative_case,
            "source_sha256": expected_source,
            "binary_sha256": expected_binary,
            "capture_contract": contract,
            "captures": checks,
            "post_exit_cleanup": {
                "process_pattern": "DisketteDungeon_diag_mac",
                "active_processes": active,
                "pass": not active,
            },
            "status": "fail",
        }

    for capture in captures:
        state_path = manifest_path.parent / capture["state_record"]
        png_path = manifest_path.parent / capture["persisted_png"]
        state = json.loads(state_path.read_text(encoding="utf-8"))["result"]
        snapshot = state["snapshot"]
        window = snapshot["window"]
        checks.append(
            {
                "checkpoint": capture["checkpoint"],
                "state_record": capture["state_record"],
                "persisted_png": capture["persisted_png"],
                "pid_match": capture["pid"] == snapshot["app"]["pid"],
                "window_id_match": capture["window"]["id"] == window["id"],
                "window_title_match": capture["window"]["title"] == window["title"],
                "window_bounds_match": capture["window"]["bounds"]
                == [window["x"], window["y"], window["width"], window["height"]],
                "state_sha_match": capture["state_record_sha256"] == sha256(state_path),
                "png_sha_match": capture["png_sha256"] == sha256(png_path),
                "png_dimensions_match": capture["png_dimensions"] == png_dimensions(png_path),
                "state_capture_dimensions_match": capture["png_dimensions"]
                == [state["screenshot"]["width"], state["screenshot"]["height"]],
                "source_sha_match": capture["src_ui_story_sha256"] == expected_source,
                "binary_sha_match": capture["binary_sha256"] == expected_binary,
            }
        )

    active = observed_processes()
    passed = all(all(value is not False for value in check.values()) for check in checks) and not active
    return {
        "schema": "task-6-capture-provenance-check-v2",
        "manifest": str(manifest_path),
        "negative_case": negative_case,
        "source_sha256": expected_source,
        "binary_sha256": expected_binary,
        "capture_contract": contract,
        "captures": checks,
        "post_exit_cleanup": {"process_pattern": "DisketteDungeon_diag_mac", "active_processes": active, "pass": not active},
        "status": "pass" if passed else "fail",
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--workspace", required=True, type=Path)
    parser.add_argument(
        "--negative-case",
        choices=("empty", "duplicate", "unknown", "remove", "rename"),
    )
    args = parser.parse_args()
    try:
        report = validate(args.manifest, args.workspace, args.negative_case)
    except (AttributeError, KeyError, OSError, TypeError, ValueError, json.JSONDecodeError) as exc:
        report = {"schema": "task-6-capture-provenance-check-v2", "status": "fail", "error": str(exc)}
    print(json.dumps(report, ensure_ascii=False, sort_keys=True))
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
