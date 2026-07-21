#!/usr/bin/env python3
"""Materialize an agent-owned Diskette Dungeon save fixture.

This evidence helper only writes the requested fixture under an explicitly
owned root. It is not used by, linked into, or shipped with the game.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile


MARKER = ".dd-agent-owned-profile"
MARKER_TEXT = "Diskette Dungeon Mac evidence profile\n"
SAVE_RELATIVE = Path("home/Library/Application Support/DisketteDungeon/save.bin")

FIXTURE_WORDS = {
    "valid-v1": (
        0xD15C0DE7, 0x00000001, 0x0000007B, 0x00000015,
        0x00000002, 0x00000009, 0x00000003, 0x00000001,
        0x00000001, 0x00000000, 0x00003039, 0x31A33963,
    ),
    "valid-v2": (
        0xD15C0DE7, 0x00000002, 0x000001C8, 0x0000003F,
        0x00000003, 0x0000000C, 0x00000004, 0x00000001,
        0x00000001, 0x00000000, 0x0000D431, 0x00000001,
        0x00000002, 0x00000003, 0x00000004, 0x00000005,
        0x1A193D87,
    ),
    "valid-v3-00": (
        0xD15C0DE7, 0x00000003, 0x00000315, 0x0000003F,
        0x00000003, 0x0000000F, 0x00000005, 0x00000001,
        0x00000001, 0x00000000, 0x00010932, 0x00000001,
        0x00000002, 0x00000003, 0x00000004, 0x00000005,
        0x00000000, 0x00000000, 0x00000000,
    ),
    "valid-v3-01": (
        0xD15C0DE7, 0x00000003, 0x00000315, 0x0000003F,
        0x00000003, 0x0000000F, 0x00000005, 0x00000001,
        0x00000001, 0x00000000, 0x00010932, 0x00000001,
        0x00000002, 0x00000003, 0x00000004, 0x00000005,
        0x00000000, 0x00000001, 0x00000000,
    ),
    "valid-v3-10": (
        0xD15C0DE7, 0x00000003, 0x00000315, 0x0000003F,
        0x00000003, 0x0000000F, 0x00000005, 0x00000001,
        0x00000001, 0x00000000, 0x00010932, 0x00000001,
        0x00000002, 0x00000003, 0x00000004, 0x00000005,
        0x00000001, 0x00000000, 0x00000000,
    ),
    "valid-v3-11": (
        0xD15C0DE7, 0x00000003, 0x00000315, 0x0000003F,
        0x00000003, 0x0000000F, 0x00000005, 0x00000001,
        0x00000001, 0x00000000, 0x00010932, 0x00000001,
        0x00000002, 0x00000003, 0x00000004, 0x00000005,
        0x00000001, 0x00000001, 0x00000000,
    ),
    "valid-v4-10": (
        0xD15C0DE7, 0x00000004, 0x000003E7, 0x0000003F,
        0x00000003, 0x00000012, 0x00000006, 0x00000001,
        0x00000001, 0x00000000, 0x000181CD, 0x00000001,
        0x00000002, 0x00000003, 0x00000004, 0x00000005,
        0x00000001, 0x00000000, 0x00000001, 0x00000000,
        0x00000000,
    ),
}


def fail(message: str) -> "NoReturn":
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(2)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--case",
        choices=(
            "valid-v1", "valid-v2", "valid-v3-00", "valid-v3-01", "valid-v3-10", "valid-v3-11", "valid-v4-10",
            "bad-v1-checksum", "bad-v2-checksum", "bad-v3-checksum", "bad-v4-checksum",
            "bad-v1-length", "bad-v2-length", "bad-v3-length", "bad-v4-length",
        ),
        required=True,
    )
    parser.add_argument("--root", required=True)
    return parser.parse_args()


def ensure_not_symlink(path: Path, label: str) -> None:
    if path.is_symlink():
        fail(f"{label} must not be a symlink: {path}")


def ensure_directory_chain(path: Path) -> None:
    current = Path(path.anchor)
    for part in path.parts[1:]:
        current /= part
        if current.exists() or current.is_symlink():
            ensure_not_symlink(current, "profile directory")
            if not current.is_dir():
                fail(f"profile path is not a directory: {current}")
        else:
            current.mkdir()


def reject_symlink_components(path: Path, label: str) -> None:
    absolute = Path(os.path.abspath(path))
    current = Path(absolute.anchor)
    for part in absolute.parts[1:]:
        current /= part
        if current.is_symlink():
            fail(f"{label} must not contain symlinks: {current}")
def prepare_root(root: Path) -> None:
    if root.exists() or root.is_symlink():
        ensure_not_symlink(root, "profile root")
        if not root.is_dir():
            fail(f"profile root is not a directory: {root}")
        marker = root / MARKER
        if marker.exists() or marker.is_symlink():
            if not marker.is_file() or marker.is_symlink():
                fail(f"profile marker is not a regular file: {marker}")
            try:
                if marker.read_text(encoding="utf-8") != MARKER_TEXT:
                    fail(f"profile marker has unexpected contents: {marker}")
            except OSError as exc:
                fail(f"cannot read profile marker: {exc}")
        else:
            try:
                if any(root.iterdir()):
                    fail(f"existing profile root is not empty: {root}")
                marker.write_text(MARKER_TEXT, encoding="utf-8")
            except OSError as exc:
                fail(f"cannot initialize profile marker: {exc}")
    else:
        root.mkdir(parents=True)
        marker = root / MARKER
        marker.write_text(MARKER_TEXT, encoding="utf-8")
    ensure_not_symlink(root / MARKER, "profile marker")


def fixture_bytes(case: str) -> bytes:
    if case.startswith("bad-v3-"):
        base_case = "valid-v3-00"
    elif case.startswith("bad-v4-"):
        base_case = "valid-v4-10"
    else:
        base_case = case.replace("bad-", "valid-").replace("-checksum", "").replace("-length", "")
    words = list(FIXTURE_WORDS[base_case])
    checksum = 0x1D15C0DE
    for word in words[:-1]:
        checksum = (checksum * 31 + word) & 0xFFFFFFFF
    words[-1] = checksum
    data = bytearray(struct.pack("<%dI" % len(words), *words))
    if case.endswith("-checksum"):
        # Corrupt one checksum byte and nothing else.
        data[-1] ^= 0x01
    if case.endswith("-length"):
        data.extend(struct.pack("<I", 0))
    return bytes(data)


def write_fixture(path: Path, data: bytes) -> None:
    ensure_not_symlink(path, "save fixture")
    ensure_directory_chain(path.parent)
    fd, temporary = tempfile.mkstemp(prefix=".save.bin.", dir=str(path.parent))
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    except OSError as exc:
        try:
            os.unlink(temporary)
        except OSError:
            pass
        fail(f"cannot write save fixture: {exc}")


def main() -> int:
    if sys.version_info < (3, 12):
        fail("Python 3.12 or newer is required")
    args = parse_args()
    root_arg = Path(args.root).expanduser()
    reject_symlink_components(root_arg, "profile root")
    root = root_arg.resolve()
    if root == Path(root.anchor):
        fail("profile root must not be the filesystem root")
    prepare_root(root)
    save_path = root / SAVE_RELATIVE
    data = fixture_bytes(args.case)
    write_fixture(save_path, data)
    record = {
        "schema": 1,
        "kind": "save_fixture",
        "case": args.case,
        "path": str(save_path),
        "byte_length": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "wire_hex": data.hex(),
    }
    print(json.dumps(record, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
