#!/usr/bin/env python3
# gen_font.py — Galmuri9 BDF에서 게임이 실제 쓰는 글자만 추출해 C 헤더로 임베드.
# (Galmuri: SIL OFL 1.1, (c) Lee Minseo — 임베드/재배포 허용)
# 사용 글자 = src/*.c|h 문자열의 모든 비ASCII 문자 + ASCII 32~126 + 숫자/예비 한글.
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BDF = os.path.join(ROOT, "tools", "galmuri9.bdf")
OUT = os.path.join(ROOT, "src", "font_data.h")

# ---------------------------------------------------------------- collect chars
used = set(chr(c) for c in range(32, 127))
# 스마트 문장부호 폴백 대상도 포함
used |= set("·…—")
for fn in os.listdir(os.path.join(ROOT, "src")):
    if not (fn.endswith(".c") or fn.endswith(".h")) or fn == "font_data.h":
        continue
    with open(os.path.join(ROOT, "src", fn), encoding="utf-8") as f:
        for ch in f.read():
            if ord(ch) > 127:
                used.add(ch)
# 시드/통계 표기 등에서 쓸 수 있는 예비 글자
used |= set("난이도쉬움보통어려해금무기시작종료설정확인취소")
cps = sorted(ord(c) for c in used if ord(c) >= 32)

# ---------------------------------------------------------------- parse bdf
glyphs = {}
with open(BDF, encoding="utf-8") as f:
    lines = f.read().split("\n")
i = 0
font_ascent = 11
while i < len(lines):
    ln = lines[i]
    if ln.startswith("FONT_ASCENT"):
        font_ascent = int(ln.split()[1])
    if ln.startswith("STARTCHAR"):
        enc = None; dw = 0; bbx = (0,0,0,0); rows = []
        i += 1
        while not lines[i].startswith("ENDCHAR"):
            l = lines[i]
            if l.startswith("ENCODING"): enc = int(l.split()[1])
            elif l.startswith("DWIDTH"): dw = int(l.split()[1])
            elif l.startswith("BBX"):
                p = l.split(); bbx = (int(p[1]), int(p[2]), int(p[3]), int(p[4]))
            elif l == "BITMAP":
                i += 1
                while not lines[i].startswith("ENDCHAR"):
                    rows.append(lines[i].strip()); i += 1
                i -= 1
            i += 1
        if enc is not None:
            glyphs[enc] = (dw, bbx, rows)
    i += 1

# ---------------------------------------------------------------- emit
sel = [(cp, glyphs[cp]) for cp in cps if cp in glyphs]
missing = [cp for cp in cps if cp not in glyphs]
if missing:
    print("missing glyphs:", [hex(m) for m in missing])

bits = bytearray()
entries = []
for cp, (dw, (bw, bh, bx, by), rows) in sel:
    off = len(bits)
    # 행 단위 비트 패킹 (행마다 바이트 정렬)
    rb = (bw + 7) // 8
    for r in range(bh):
        hexrow = rows[r] if r < len(rows) else "00"
        v = bytes.fromhex(hexrow.zfill(rb * 2))
        bits.extend(v[:rb])
    entries.append((cp, dw, bw, bh, bx, by, off))

with open(OUT, "w", encoding="utf-8") as f:
    f.write("// 자동 생성: tools/gen_font.py — Galmuri9 (SIL OFL 1.1) 부분집합. 수동 수정 금지.\n")
    f.write("// 텍스트 추가 후 이 파일 재생성: python3 tools/gen_font.py\n")
    f.write(f"#define FONT_ASCENT {font_ascent}\n")
    f.write(f"#define FONT_NGLYPHS {len(entries)}\n")
    f.write("typedef struct { unsigned int cp; unsigned char dw,bw,bh; signed char bx,by; unsigned int off; } FontGlyph;\n")
    f.write("static const FontGlyph font_glyphs[FONT_NGLYPHS] = {\n")
    for e in entries:
        f.write("{%d,%d,%d,%d,%d,%d,%d},\n" % e)
    f.write("};\n")
    f.write(f"static const unsigned char font_bits[{max(1,len(bits))}] = {{\n")
    for k in range(0, len(bits), 24):
        f.write(",".join(str(b) for b in bits[k:k+24]) + ",\n")
    f.write("};\n")

print(f"glyphs: {len(entries)}, bitmap bytes: {len(bits)}, header: {os.path.getsize(OUT)}B")
