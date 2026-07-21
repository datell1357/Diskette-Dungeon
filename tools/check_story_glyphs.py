#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///

# ─── How to run ───
# 1. Install uv (if not installed):
#      curl -LsSf https://astral.sh/uv/install.sh | sh
# 2. Run directly (no venv, no pip install needed):
#      uv run tools/check_story_glyphs.py --header src/font_data.h --sources src/ui_story.c --report .omo/evidence/floppy-story-coherence/task-6-glyphs.json
# 3. Or make executable and run:
#      chmod +x tools/check_story_glyphs.py && ./tools/check_story_glyphs.py --header src/font_data.h --sources src/ui_story.c --report .omo/evidence/floppy-story-coherence/task-6-glyphs.json
# ──────────────────
from __future__ import annotations

import argparse
from collections import defaultdict
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Final, TypedDict


FONT_GLYPH: Final = re.compile(r"\{(\d+),")
KOREAN_SYLLABLE: Final = re.compile(r"[가-힣]")


@dataclass(frozen=True, slots=True)
class GlyphUse:
    character: str
    codepoint: str
    sources: tuple[str, ...]


class HeaderReport(TypedDict):
    path: str
    sha256: str
    glyph_count: int


class SourceReport(TypedDict):
    path: str
    sha256: str


class GlyphReport(TypedDict):
    character: str
    codepoint: str
    sources: tuple[str, ...]


class CheckReport(TypedDict):
    schema: str
    status: str
    header: HeaderReport
    sources: list[SourceReport]
    probe_text_count: int
    checked_korean_glyph_count: int
    missing: list[GlyphReport]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Verify Korean story glyphs against src/font_data.h.")
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--sources", type=Path, nargs="+", required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--probe-text", action="append", default=[])
    return parser.parse_args()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def header_codepoints(path: Path) -> frozenset[int]:
    return frozenset(int(match.group(1)) for match in FONT_GLYPH.finditer(path.read_text(encoding="utf-8")))


def korean_uses(sources: tuple[Path, ...], probes: tuple[str, ...]) -> tuple[GlyphUse, ...]:
    locations: defaultdict[int, set[str]] = defaultdict(set)
    for source in sources:
        for character in KOREAN_SYLLABLE.findall(source.read_text(encoding="utf-8")):
            locations[ord(character)].add(source.as_posix())
    for probe in probes:
        for character in KOREAN_SYLLABLE.findall(probe):
            locations[ord(character)].add("<probe-text>")
    return tuple(
        GlyphUse(chr(codepoint), f"U+{codepoint:04X}", tuple(sorted(source_names)))
        for codepoint, source_names in sorted(locations.items())
    )


def report_payload(header: Path, sources: tuple[Path, ...], probes: tuple[str, ...]) -> CheckReport:
    available = header_codepoints(header)
    used = korean_uses(sources, probes)
    missing = tuple(glyph for glyph in used if ord(glyph.character) not in available)
    return {
        "schema": "story-glyph-check-v1",
        "status": "pass" if not missing else "fail",
        "header": {"path": header.as_posix(), "sha256": sha256(header), "glyph_count": len(available)},
        "sources": [{"path": source.as_posix(), "sha256": sha256(source)} for source in sources],
        "probe_text_count": len(probes),
        "checked_korean_glyph_count": len(used),
        "missing": [
            {"character": glyph.character, "codepoint": glyph.codepoint, "sources": glyph.sources}
            for glyph in missing
        ],
    }


def main() -> int:
    arguments = parse_args()
    sources = tuple(arguments.sources)
    probes = tuple(arguments.probe_text)
    payload = report_payload(arguments.header, sources, probes)
    arguments.report.write_text(json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0 if payload["status"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
