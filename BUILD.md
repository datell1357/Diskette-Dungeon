# 빌드 & 제출 가이드

## 제출 빌드 (Windows .exe, deferred)

```sh
sh tools/build_win.sh
```

- macOS/Linux에서 mingw-w64로 크로스 컴파일 (`brew install mingw-w64`).
- 산출물: `build/DisketteDungeon.exe` — 단독 실행, 외부 파일/런타임 의존 0.
- 스크립트가 **용량 게이트**(1,474,560 bytes) 자동 검사. 초과 시 빌드 실패.
- 현재 크기: 약 983KB (여유 ~490KB).
- 백엔드: OpenGL 3.3 (D3D11은 exe가 1.49MB로 용량 초과라 GL 채택).
- **실제 Windows PC 실행과 제출 검증은 별도 승인된 Windows 단계로 연기되며, 이 Mac 단계에서 검증하지 않는다.**

## 개발 빌드 (mac)

```sh
sh tools/build_mac.sh   # Metal backend, DD_DEBUG included
./build/DisketteDungeon_diag_mac
```

### 디버그 옵션 (DD_DEBUG 빌드 전용 — 제출 exe에는 없음)

| 옵션 | 효과 |
|---|---|
| `--auto` | 봇이 자동 플레이 (스모크 테스트) |
| `--god` | 무적 |
| `--weapon N` | 시작 무기 (0~5) |
| `--jump B,R` | 바이옴 B(0~3) 방 R(0~8)로 점프, 핵심조각 B개 지급 |
| `--ending N` | 엔딩 N(0빈손/1표준/2트루) 바로 재생 |

봇 실행 시 1초마다 stdout에 상태 텔레메트리 출력.
### Mac-first bounded diagnostics

All commands below are macOS/Metal development evidence. Unless noted otherwise,
the diagnostic command exits 0, writes JSONL to stdout, and writes nothing to
stderr. Repeat deterministic commands in fresh processes and byte-compare the
JSONL outputs.

```sh
# Build modes (the invalid mode exits 2 and does not mutate either binary).
DD_DEBUG_BUILD=1 sh tools/build_mac.sh
DD_DEBUG_BUILD=0 sh tools/build_mac.sh  # Finder 실행용 build/DisketteDungeon.app 포함
DD_DEBUG_BUILD=2 sh tools/build_mac.sh

# Ten deterministic snapshot pairs: run each command twice.
build/DisketteDungeon_diag_mac --clean-profile --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --jump 0,5 --action snapshot-room
build/DisketteDungeon_diag_mac --clean-profile --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --jump 0,5 --action snapshot-reward
build/DisketteDungeon_diag_mac --clean-profile --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --jump 1,4 --action snapshot-room
build/DisketteDungeon_diag_mac --clean-profile --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --jump 1,4 --action snapshot-reward
build/DisketteDungeon_diag_mac --clean-profile --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --jump 2,4 --action snapshot-room
build/DisketteDungeon_diag_mac --clean-profile --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --jump 2,4 --action snapshot-reward
build/DisketteDungeon_diag_mac --clean-profile --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --jump 3,4 --action snapshot-room
build/DisketteDungeon_diag_mac --clean-profile --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --jump 3,4 --action snapshot-reward
build/DisketteDungeon_diag_mac --clean-profile --seed 12366 --difficulty 1 --weapon 0 --ngplus 0 --jump 0,5 --action snapshot-room
build/DisketteDungeon_diag_mac --clean-profile --seed 12366 --difficulty 1 --weapon 0 --ngplus 0 --jump 0,5 --action snapshot-reward

# QNE: Slime Volatile promotion and finalize-twice accounting; exit 0.
build/DisketteDungeon_diag_mac --clean-profile --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --jump 2,4 --force-target 0,0,0,0 --force-pending 1 --death-action finalize-twice --action fixture-qne
# QAE: elite Bomber replacement, Haste, and self-destruct accounting; exit 0.
build/DisketteDungeon_diag_mac --clean-profile --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --jump 2,4 --force-target 0,8,1,1 --force-pending 2 --death-action self-destruct --action fixture-qae
# QCOL: occupied-pending E denial, then public Q discard, direct resolver re-entry,
# and a key_repeat=true probe; exit 0.
build/DisketteDungeon_diag_mac --clean-profile --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --jump 2,4 --force-pending 1 --force-event 2,1,2,1 --action fixture-qcol-input

# Public F10 input, one run per branch; each exits 0 on f10_resolved.
build/DisketteDungeon_diag_mac --clean-profile --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --jump 0,5 --auto --god --f10-branch keep --duration-ms 180000 --telemetry build/evidence/mac-first/f10-keep.jsonl --telemetry-interval-ms 1000
build/DisketteDungeon_diag_mac --clean-profile --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --jump 0,5 --auto --god --f10-branch discard --duration-ms 180000 --telemetry build/evidence/mac-first/f10-discard.jsonl --telemetry-interval-ms 1000

# Dense raw frame telemetry and independent analyzer (both analyzer runs must
# produce byte-identical summaries). The game exits 0; analyzer exits 0 on pass.
build/DisketteDungeon_diag_mac --clean-profile --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --jump 3,7 --auto --god --duration-ms 240000 --telemetry build/evidence/mac-first/mac-dense-raw.jsonl --telemetry-interval-ms 1000
python3.12 tools/analyze_mac_frames.py --input build/evidence/mac-first/mac-dense-raw.jsonl --warmup-ms 60000 --measurement-ms 180000 --output build/evidence/mac-first/mac-dense-summary.json

# Production save fixtures; materializer and game commands exit 0.
python3.12 tools/materialize_save_fixture.py --case valid-v1 --root build/evidence/mac-first/save/valid-v1
HOME="$PWD/build/evidence/mac-first/save/valid-v1/home" build/DisketteDungeon_diag_mac --isolated-profile "$PWD/build/evidence/mac-first/save/valid-v1/home" --action fixture-save-roundtrip --expect v1
python3.12 tools/materialize_save_fixture.py --case valid-v2 --root build/evidence/mac-first/save/valid-v2
HOME="$PWD/build/evidence/mac-first/save/valid-v2/home" build/DisketteDungeon_diag_mac --isolated-profile "$PWD/build/evidence/mac-first/save/valid-v2/home" --action fixture-save-roundtrip --expect v2
python3.12 tools/materialize_save_fixture.py --case valid-v3-00 --root build/evidence/mac-first/save/valid-v3-00
HOME="$PWD/build/evidence/mac-first/save/valid-v3-00/home" build/DisketteDungeon_diag_mac --isolated-profile "$PWD/build/evidence/mac-first/save/valid-v3-00/home" --action fixture-save-roundtrip --expect v3
python3.12 tools/materialize_save_fixture.py --case valid-v3-01 --root build/evidence/mac-first/save/valid-v3-01
HOME="$PWD/build/evidence/mac-first/save/valid-v3-01/home" build/DisketteDungeon_diag_mac --isolated-profile "$PWD/build/evidence/mac-first/save/valid-v3-01/home" --action fixture-save-roundtrip --expect v3
python3.12 tools/materialize_save_fixture.py --case valid-v3-10 --root build/evidence/mac-first/save/valid-v3-10
HOME="$PWD/build/evidence/mac-first/save/valid-v3-10/home" build/DisketteDungeon_diag_mac --isolated-profile "$PWD/build/evidence/mac-first/save/valid-v3-10/home" --action fixture-save-roundtrip --expect v3
python3.12 tools/materialize_save_fixture.py --case valid-v3-11 --root build/evidence/mac-first/save/valid-v3-11
HOME="$PWD/build/evidence/mac-first/save/valid-v3-11/home" build/DisketteDungeon_diag_mac --isolated-profile "$PWD/build/evidence/mac-first/save/valid-v3-11/home" --action fixture-save-roundtrip --expect v3
python3.12 tools/materialize_save_fixture.py --case bad-v1-checksum --root build/evidence/mac-first/save/bad-v1-checksum
HOME="$PWD/build/evidence/mac-first/save/bad-v1-checksum/home" build/DisketteDungeon_diag_mac --isolated-profile "$PWD/build/evidence/mac-first/save/bad-v1-checksum/home" --action fixture-save-reject --expect v1
python3.12 tools/materialize_save_fixture.py --case bad-v2-checksum --root build/evidence/mac-first/save/bad-v2-checksum
HOME="$PWD/build/evidence/mac-first/save/bad-v2-checksum/home" build/DisketteDungeon_diag_mac --isolated-profile "$PWD/build/evidence/mac-first/save/bad-v2-checksum/home" --action fixture-save-reject --expect v2
python3.12 tools/materialize_save_fixture.py --case bad-v3-checksum --root build/evidence/mac-first/save/bad-v3-checksum
HOME="$PWD/build/evidence/mac-first/save/bad-v3-checksum/home" build/DisketteDungeon_diag_mac --isolated-profile "$PWD/build/evidence/mac-first/save/bad-v3-checksum/home" --action fixture-save-reject --expect v3
python3.12 tools/materialize_save_fixture.py --case bad-v1-length --root build/evidence/mac-first/save/bad-v1-length
HOME="$PWD/build/evidence/mac-first/save/bad-v1-length/home" build/DisketteDungeon_diag_mac --isolated-profile "$PWD/build/evidence/mac-first/save/bad-v1-length/home" --action fixture-save-reject --expect v1
python3.12 tools/materialize_save_fixture.py --case bad-v2-length --root build/evidence/mac-first/save/bad-v2-length
HOME="$PWD/build/evidence/mac-first/save/bad-v2-length/home" build/DisketteDungeon_diag_mac --isolated-profile "$PWD/build/evidence/mac-first/save/bad-v2-length/home" --action fixture-save-reject --expect v2
python3.12 tools/materialize_save_fixture.py --case bad-v3-length --root build/evidence/mac-first/save/bad-v3-length
HOME="$PWD/build/evidence/mac-first/save/bad-v3-length/home" build/DisketteDungeon_diag_mac --isolated-profile "$PWD/build/evidence/mac-first/save/bad-v3-length/home" --action fixture-save-reject --expect v3

# Finite modifier, Haste clock, and ending/coda matrices; each exits 0.
build/DisketteDungeon_diag_mac --clean-profile --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --action fixture-modifiers
build/DisketteDungeon_diag_mac --clean-profile --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --jump 2,4 --action fixture-haste
build/DisketteDungeon_diag_mac --clean-profile --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --action fixture-endings
```

Snapshot output must contain the fixed run/room/door/entity/pickup/event/end
records and preserve observed RNG/checksum fields. QNE/QAE/QCOL receipts must
assert all fixed stats and accounting fields: `finalizer_calls`,
`effects_applied`, `volatile_bursts`, `bomber_bursts`, enemy-bullet, kill/drop,
byte, RNG, event, memory-log, pickup, shard, and core deltas. F10 must prove
one available-state non-repeat public pair, no fallthrough, one direct
same-decision resolver re-entry returning false with zero deltas, one
`key_repeat=true` pair, and `post_resolution_nonrepeat_events=0`. Raw telemetry
is pre-clamp monotonic data: the analyzer requires at least 10440 measured
frames, 179000..181000 ms elapsed, p95 <=18.000 ms, worst <=33.334 ms, and
maximum contiguous rolling FPS<58 <=1000 ms. Save fixtures require the exact
V1=48-byte/12-word, V2=68-byte/17-word, and V3=76-byte/19-word layouts.
The current writer emits V3 with checksum offset 72 covering its first 18
words. Valid V1/V2 migration and V3 roundtrip must production-load, save, and
reload; legacy V1/V2 validate their own checksum and initialize V3-only flags
to zero. Malformed V1/V2/V3 lengths or checksums must reject to defaults
without writeback, with exact marker ownership/content and no real-profile
mutation.

The literal negative CLI matrix must be run with empty stdout, exactly one
LF-terminated JSON stderr record, exit 2, and no side effects:

```sh
build/DisketteDungeon_diag_mac --bogus
# {"error":"unknown-option"}
build/DisketteDungeon_diag_mac --seed
# {"error":"missing-operand"}
build/DisketteDungeon_diag_mac --seed 1 --seed 2
# {"error":"duplicate-option"}
build/DisketteDungeon_diag_mac --seed xyz
# {"error":"malformed-number"}
build/DisketteDungeon_diag_mac --seed 0
# {"error":"out-of-range"}
build/DisketteDungeon_diag_mac --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --action snapshot-room
# {"error":"clean-profile-required"}
build/DisketteDungeon_diag_mac --clean-profile --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --jump 0,5 --telemetry build/evidence/mac-first/invalid.jsonl
# {"error":"duration-required"}
build/DisketteDungeon_diag_mac --duration-ms 1000
# {"error":"duration-without-telemetry"}
build/DisketteDungeon_diag_mac --clean-profile --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --jump 0,5 --duration-ms 1000 --telemetry build/evidence/mac-first/invalid.jsonl --action snapshot-room
# {"error":"action-telemetry-incompatible"}
build/DisketteDungeon_diag_mac --isolated-profile "$PWD/build/evidence/mac-first/save/no-marker/home" --action fixture-save-roundtrip --expect v1
# {"error":"unsafe-isolated-profile"}
```

The deterministic invariant action must exit 3, keep stdout empty, and emit
exactly `{"error":"invariant","what":"forced","expected":1,"actual":0}` on stderr:

```sh
build/DisketteDungeon_diag_mac --clean-profile --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --action fixture-invariant-failure
```

`MetaSave` is 76 bytes with checksum offset 72. Mac phase status fields
must be explicit even on failure: `mac_phase_pass` is true only when every Mac
gate passes (otherwise false), while
`windows_implementation_complete=false`,
`windows_native_launch=false`, `presentmon_artifact=false`,
`official_size=false`, and `submission_ready=false`. The only allowed phase
verdicts are `MAC_PHASE_PASS / DEFERRED_WINDOWS` and
`MAC_PHASE_FAIL / DEFERRED_WINDOWS`.

Windows implementation, `_WIN32` validation, MinGW/OpenGL packaging, native
Windows launch/first-ten, official-size acceptance, PresentMon, and submission
readiness are deferred and unclaimed. `tools/build_win.sh` is unchanged; no
Mac result is Windows proof.

## 조작

WASD/방향키 이동 · 마우스/스페이스 공격 · Shift 대시 · E 줍기 · Q 추억 버리기 · Tab 가방 · Esc 일시정지

## 구조 (모든 에셋 = 코드 절차 생성, 데이터 파일 0)

```
src/main.c       유니티 빌드 진입점 + sokol 콜백
src/sokol_impl.c sokol 구현 TU (mac: Metal / win: GL)
src/render.c     씬/라이트/글로우 오프스크린 + 그림자 폴리곤 + 포스트(블룸·스캔라인·색수차·비네팅)
src/assets.c     스프라이트 아틀라스 절차 생성 (28종)
src/font.c       ASCII 5x7 비트맵 + 한글 자모 획 조합 폰트 (폰트 파일 없음)
src/audio.c      소프트신스: sfxr식 SFX + 생성형 칩튠 BGM (음원 파일 없음)
src/game.c       던전 생성 · 1.44MB 무게 시스템 · 메타 세이브
src/combat.c     플레이어/무기 6종/적 8종/보스 4종
src/ui_story.c   상태머신 · HUD · 인트로/회상/엔딩 3종/에필로그 · 디버그 봇
```

## 세이브 위치

- Windows: `%APPDATA%\DisketteDungeon\save.bin`
- macOS: `~/Library/Application Support/DisketteDungeon/save.bin`
