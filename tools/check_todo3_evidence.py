#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--task-evidence", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--receipt", required=True, type=Path)
    parser.add_argument("--screenshot", required=True, type=Path)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--main-source", required=True, type=Path)
    parser.add_argument("--ui-source", required=True, type=Path)
    args = parser.parse_args()
    task = json.loads(args.task_evidence.read_text(encoding="utf-8"))
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    receipt = json.loads(args.receipt.read_text(encoding="utf-8"))
    expected = task["validation"]["source_sha256"]
    actual = {
        "src/main.c": digest(args.main_source),
        "src/ui_story.c": digest(args.ui_source),
        "binary": digest(args.binary),
        "receipt": digest(args.receipt),
        "screenshot": digest(args.screenshot),
    }
    checks = {
        "main_source_current": actual["src/main.c"] == expected["src/main.c"],
        "ui_source_current": actual["src/ui_story.c"] == expected["src/ui_story.c"],
        "receipt_binds_main_source": receipt.get("source_sha256") == actual["src/main.c"],
        "manifest_binds_ui_source": manifest.get("source_sha256") == actual["src/ui_story.c"],
        "binary_current": actual["binary"] == task["validation"]["binary_sha256"] == manifest.get("binary_sha256"),
        "receipt_current": actual["receipt"] == manifest.get("receipt_sha256"),
        "screenshot_current": actual["screenshot"] == manifest.get("screenshot_sha256"),
    }
    print(json.dumps({"schema": 1, "kind": "todo3_evidence_provenance", "pass": all(checks.values()), "checks": checks, "hashes": actual}, separators=(",", ":")))
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
