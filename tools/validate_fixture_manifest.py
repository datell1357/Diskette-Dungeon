# /// script
# requires-python = ">=3.11"
# ///
# ─── How to run ───
# python3 tools/validate_fixture_manifest.py --bundle <bundle.app> --source src/ui_story.c --action fixture-ddd-opening --checkpoint insert
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path


class ValidationError(Exception):
    pass


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_json(path: Path) -> dict[str, object]:
    try:
        decoded = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValidationError(f"cannot read {path.name}: {exc}") from exc
    if not isinstance(decoded, dict):
        raise ValidationError(f"{path.name} must be a JSON object")
    return decoded


def required_string(record: dict[str, object], key: str) -> str:
    value = record.get(key)
    if not isinstance(value, str) or not value:
        raise ValidationError(f"missing string field {key}")
    return value


def validate_png(path: Path) -> tuple[int, int, int, int]:
    header = path.read_bytes()[:33]
    if header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise ValidationError("window.png is not a PNG with IHDR")
    width, height, depth, color = struct.unpack(">IIBB", header[16:26])
    if (width, height, depth, color) != (1920, 1144, 8, 6):
        raise ValidationError(
            f"window.png must be 1920x1144 RGBA, got {width}x{height} depth={depth} color={color}"
        )
    return width, height, depth, color


def validate(bundle: Path, source: Path, action: str, checkpoint: str, core_count: int | None) -> dict[str, object]:
    manifest = read_json(bundle / "manifest.json")
    envelope = read_json(bundle / "receipt-envelope.json")
    receipt = read_json(bundle / "receipt.json")
    if required_string(manifest, "capture") != "window.png":
        raise ValidationError("manifest capture must be window.png")
    if required_string(manifest, "receipt_envelope") != "receipt-envelope.json":
        raise ValidationError("manifest receipt envelope is not canonical")
    if required_string(receipt, "action") != action or required_string(receipt, "checkpoint") != checkpoint:
        raise ValidationError("receipt action/checkpoint mismatch")
    if core_count is not None and receipt.get("core_count") != core_count:
        raise ValidationError("receipt core_count mismatch")
    source_sha = sha256(source)
    receipt_sha = sha256(bundle / "receipt.json")
    executable_sha = sha256(bundle / "Contents/MacOS/DisketteDungeonFixture")
    diagnostic_binary = source.parent.parent / "build" / "DisketteDungeon_diag_mac"
    if not diagnostic_binary.is_file():
        raise ValidationError("authoritative diagnostic binary is missing")
    diagnostic_binary_sha = sha256(diagnostic_binary)
    if executable_sha != diagnostic_binary_sha:
        raise ValidationError("bundle executable SHA does not match authoritative diagnostic binary")
    for record_name, record in (("manifest", manifest), ("envelope", envelope)):
        if required_string(record, "src_ui_story_sha256") != source_sha:
            raise ValidationError("source SHA mismatch")
        if required_string(record, "raw_receipt_sha256") != receipt_sha:
            raise ValidationError("raw receipt SHA mismatch")
        if required_string(record, "bundle_executable_sha256") != executable_sha:
            raise ValidationError("bundle executable SHA mismatch")
        if required_string(record, "diagnostic_binary_sha256") != diagnostic_binary_sha:
            raise ValidationError(f"{record_name} diagnostic binary SHA mismatch")
    if envelope.get("raw_receipt") != receipt:
        raise ValidationError("envelope receipt does not equal raw receipt")
    if not isinstance(manifest.get("verified_pid"), int) or manifest["verified_pid"] <= 0:
        raise ValidationError("manifest verified_pid is invalid")
    if not isinstance(manifest.get("verified_window_id"), int) or manifest["verified_window_id"] <= 0:
        raise ValidationError("manifest verified_window_id is invalid")
    if manifest.get("capture_backend") != "macos-screencapture-window-id-no-shadow":
        raise ValidationError("manifest capture backend is not the verified native window capture")
    required_string(manifest, "run_id")
    required_string(manifest, "captured_at")
    png = validate_png(bundle / "window.png")
    capture_sha = sha256(bundle / "window.png")
    if required_string(manifest, "capture_sha256") != capture_sha:
        raise ValidationError("manifest capture SHA mismatch")
    stability = read_json(bundle / "diagnostics" / "final.capture-stability.json")
    settled = stability.get("settled_validation")
    if not isinstance(settled, dict) or settled.get("status") != "pass":
        raise ValidationError("final capture did not pass settled strict validation")
    if stability.get("settled_sha256") != capture_sha or stability.get("stable_sha256") != capture_sha:
        raise ValidationError("final capture bytes changed or do not match the settled diagnostic")
    if stability.get("stable_after_settle") is not True:
        raise ValidationError("final capture did not remain byte-stable after settling")
    if (
        manifest.get("png_signature") != "89504e470d0a1a0a"
        or manifest.get("png_width") != png[0]
        or manifest.get("png_height") != png[1]
        or manifest.get("png_bit_depth") != png[2]
        or manifest.get("png_color_type") != png[3]
    ):
        raise ValidationError("manifest PNG metadata mismatch")
    return {
        "status": "pass",
        "bundle": str(bundle),
        "action": action,
        "checkpoint": checkpoint,
        "core_count": core_count,
        "png": {"width": png[0], "height": png[1], "depth": png[2], "color": png[3]},
        "source_sha256": source_sha,
        "diagnostic_binary_sha256": diagnostic_binary_sha,
        "bundle_executable_sha256": executable_sha,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--action", required=True)
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--core-count", type=int)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        result = validate(args.bundle, args.source, args.action, args.checkpoint, args.core_count)
    except ValidationError as exc:
        print(json.dumps({"status": "fail", "error": str(exc)}, sort_keys=True), file=sys.stderr)
        return 2
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
