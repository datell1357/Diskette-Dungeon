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
}


def fail(message: str) -> "NoReturn":
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(2)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--case",
        choices=("valid-v1", "valid-v2", "bad-v1-checksum", "bad-v2-checksum"),
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
    base_case = "valid-v1" if case.endswith("v1-checksum") else (
        "valid-v2" if case.endswith("v2-checksum") else case
    )
    words = FIXTURE_WORDS[base_case]
    data = bytearray(struct.pack("<%dI" % len(words), *words))
    if case.startswith("bad-"):
        # Corrupt one checksum byte and nothing else.
        data[-1] ^= 0x01
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
