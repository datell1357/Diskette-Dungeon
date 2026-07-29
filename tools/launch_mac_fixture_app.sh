#!/bin/sh
set -eu

usage() {
    echo "usage: $0 --evidence-root DIR --run-id ID --action ACTION --checkpoint CHECKPOINT [--core-count N] [--retry-phase 1|2|3]" >&2
    exit 2
}

fail() {
    echo "error: $*" >&2
    exit 2
}

sha256() {
    shasum -a 256 "$1" | awk '{print $1}'
}

evidence_root=
run_id=
action=
checkpoint=
core_count=
retry_phase=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --evidence-root) [ "$#" -ge 2 ] || usage; evidence_root=$2; shift 2 ;;
        --run-id) [ "$#" -ge 2 ] || usage; run_id=$2; shift 2 ;;
        --action) [ "$#" -ge 2 ] || usage; action=$2; shift 2 ;;
        --checkpoint) [ "$#" -ge 2 ] || usage; checkpoint=$2; shift 2 ;;
        --core-count) [ "$#" -ge 2 ] || usage; core_count=$2; shift 2 ;;
        --retry-phase) [ "$#" -ge 2 ] || usage; retry_phase=$2; shift 2 ;;
        *) usage ;;
    esac
done
[ -n "$evidence_root" ] && [ -n "$run_id" ] && [ -n "$action" ] && [ -n "$checkpoint" ] || usage
case "$run_id" in *[!A-Za-z0-9._-]*|'') fail "run-id must be a filesystem-safe token";; esac
mkdir -p "$evidence_root"
evidence_root=$(CDPATH= cd -- "$evidence_root" && pwd -P)

case "$action:$checkpoint" in
    fixture-ddd-opening:insert|fixture-ddd-opening:seek|fixture-ddd-opening:retry|\
    fixture-ddd-opening:recover|fixture-ddd-opening:transfer|fixture-ddd-opening:title-handoff|\
    fixture-ddd-opening:skip-key|fixture-ddd-opening:skip-mouse)
        [ -z "$core_count" ] || fail "opening fixtures do not accept --core-count"
        if [ -n "$retry_phase" ] && { [ "$checkpoint" != retry ] || ! { [ "$retry_phase" = 1 ] || [ "$retry_phase" = 2 ] || [ "$retry_phase" = 3 ]; }; }; then
            fail "--retry-phase is only valid for retry with 1, 2, or 3"
        fi
        core_label=none
        ;;
    fixture-ddd-ending:recovery-failed)
        [ "$core_count" = 0 ] || fail "recovery-failed requires --core-count 0"
        core_label=$core_count
        ;;
    fixture-ddd-ending:partial-recovery)
        case "$core_count" in 1|2|3) ;; *) fail "partial-recovery requires --core-count 1, 2, or 3";; esac
        core_label=$core_count
        ;;
    fixture-ddd-ending:complete-recovery)
        [ "$core_count" = 4 ] || fail "complete-recovery requires --core-count 4"
        core_label=$core_count
        ;;
    fixture-ddd-options:options-toggle)
        [ -z "$core_count" ] || fail "options fixture does not accept --core-count"
        core_label=isolated-v4
        ;;
    fixture-ddd-start-intro:first-run|fixture-ddd-start-intro:placement|fixture-ddd-start-intro:latch|\
    fixture-ddd-start-intro:drive-stop-hold|fixture-ddd-start-intro:track|fixture-ddd-start-intro:fragment|\
    fixture-ddd-start-intro:handoff|fixture-ddd-start-intro:repeat-bypass|fixture-ddd-start-intro:queued-replay|\
    fixture-ddd-start-intro:post-replay-bypass)
        [ -z "$core_count" ] && [ -z "$retry_phase" ] || fail "start-intro fixtures do not accept core counts or retry phases"
        core_label=none
        ;;
    fixture-ddd-ui-showcase:death|fixture-ddd-ui-showcase:door|fixture-ddd-ui-showcase:pause|\
    fixture-ddd-ui-showcase:hit|fixture-ddd-ui-showcase:boss-reward|fixture-ddd-ui-showcase:relic-swap|\
    fixture-ddd-ui-showcase:memory-event|fixture-ddd-ui-showcase:boss-intro|fixture-ddd-ui-showcase:core-flashback|\
    fixture-ddd-ui-showcase:fire-trail-start|fixture-ddd-ui-showcase:fire-trail-mid|fixture-ddd-ui-showcase:fire-trail-end|\
    fixture-ddd-ui-showcase:lance-thrust-start|fixture-ddd-ui-showcase:lance-thrust-mid|fixture-ddd-ui-showcase:lance-thrust-end|\
    fixture-ddd-ui-showcase:wand-rain-start|fixture-ddd-ui-showcase:wand-rain-mid|fixture-ddd-ui-showcase:wand-rain-end|\
    fixture-ddd-ui-showcase:wand-charge-base|fixture-ddd-ui-showcase:wand-charge-25|fixture-ddd-ui-showcase:wand-charge-50|\
    fixture-ddd-ui-showcase:wand-charge-75|fixture-ddd-ui-showcase:wand-charge-full|\
    fixture-ddd-ui-showcase:cannon-frag-wall|fixture-ddd-ui-showcase:cannon-rail-start|\
    fixture-ddd-ui-showcase:cannon-rail-mid|fixture-ddd-ui-showcase:cannon-rail-end)
        [ -z "$core_count" ] && [ -z "$retry_phase" ] || fail "showcase fixtures do not accept core counts or retry phases"
        core_label=none
        ;;
    *) fail "unsupported action/checkpoint tuple: $action/$checkpoint" ;;
esac

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
binary=$repo/build/DisketteDungeon_diag_mac
source_file=$repo/src/ui_story.c
[ -x "$binary" ] || fail "diagnostic binary is missing or not executable: $binary"
[ -f "$source_file" ] || fail "source file is missing: $source_file"
binary_sha=$(sha256 "$binary")
source_sha=$(sha256 "$source_file")
run_root=$evidence_root/runs/$run_id-$binary_sha
bundle=$run_root/apps/DisketteDungeonFixture-$action-$checkpoint-$core_label.app
[ ! -e "$bundle" ] || fail "refusing to clobber existing bundle target: $bundle"

mkdir -p "$run_root/apps"
mkdir "$bundle" || fail "could not create no-clobber bundle target: $bundle"
mkdir "$bundle/Contents" "$bundle/Contents/MacOS"
mkdir "$bundle/diagnostics"
receipt=$bundle/receipt.json
stderr=$bundle/stderr.jsonl
app_state=$bundle/app-state.json
warmup_state=$bundle/warmup-state.json
diagnostics=$bundle/diagnostics
settled_state_1=$diagnostics/settled-state-1.json
settled_state_2=$diagnostics/settled-state-2.json
settled_capture_1=$diagnostics/settled-1.png
settled_capture_2=$diagnostics/settled-2.png
window_state=$bundle/window-list.json
capture=$bundle/window.png
envelope=$bundle/receipt-envelope.json
manifest=$bundle/manifest.json
exe=$bundle/Contents/MacOS/DisketteDungeonFixture
bundle_id="local.diskettedungeon.fixture.$run_id.$(printf '%s' "$action" | tr '-' '.').$(printf '%s' "$checkpoint" | tr '-' '.')"
display_name="DisketteDungeonFixture $action $checkpoint $core_label $run_id"

cp "$binary" "$exe"
chmod 755 "$exe"
cat > "$bundle/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>DisketteDungeonFixture</string>
  <key>CFBundleIdentifier</key><string>$bundle_id</string>
  <key>CFBundleName</key><string>$display_name</string>
  <key>CFBundleDisplayName</key><string>$display_name</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>NSHighResolutionCapable</key><true/>
</dict></plist>
EOF
bundle_sha=$(sha256 "$exe")

if [ -n "$retry_phase" ]; then
    DD_DEBUG_RETRY_PHASE="$retry_phase"; export DD_DEBUG_RETRY_PHASE
fi
if [ "$action" = fixture-ddd-options ]; then
    fixture_root=$repo/build/evidence/gui-options-$run_id/save
    python3 "$repo/tools/materialize_save_fixture.py" --case valid-v4-10 --root "$fixture_root" > "$diagnostics/options-materialize.json"
    (
        cd "$repo"
        HOME="$fixture_root/home" exec "$exe" --isolated-profile "$fixture_root/home" \
            --action "$action" --checkpoint "$checkpoint" --expect v4 --hold-ms 10000
    ) > "$receipt" 2> "$stderr" &
elif [ -n "$core_count" ]; then
    open -W -n "$bundle" --stdout "$receipt" --stderr "$stderr" --args \
        --clean-profile --seed 1 --difficulty 0 --weapon 0 --ngplus 1 \
        --action "$action" --checkpoint "$checkpoint" --core-count "$core_count" --hold-ms 10000 &
else
    open -W -n "$bundle" --stdout "$receipt" --stderr "$stderr" --args \
        --clean-profile --seed 1 --difficulty 0 --weapon 0 --ngplus 1 \
        --action "$action" --checkpoint "$checkpoint" --hold-ms 10000 &
fi
open_pid=$!

pid=
ready=0
attempt=0
while [ "$attempt" -lt 250 ]; do
    candidates=$(pgrep -f "$exe" || true)
    for candidate in $candidates; do
        comm=$(ps -p "$candidate" -o comm= | sed 's/^[[:space:]]*//')
        if [ "$comm" = "$exe" ]; then pid=$candidate; break; fi
    done
    if [ -n "$pid" ] && [ -s "$receipt" ] && grep -F '"fixture_ready":true' "$receipt" >/dev/null; then
        ready=1
        break
    fi
    if ! kill -0 "$open_pid" 2>/dev/null; then
        wait "$open_pid" || true
        fail "fixture launcher exited before a flushed fixture_ready receipt"
    fi
    sleep 0.1
    attempt=$((attempt + 1))
done
[ "$ready" = 1 ] || { wait "$open_pid" || true; fail "fixture_ready receipt was not observed while the app was live"; }

orca computer list-windows --app "pid:$pid" --json > "$window_state"
window_identity=$(python3 - "$display_name" "$pid" "$window_state" <<'PY'
import json, sys
needle = sys.argv[1]
pid = int(sys.argv[2])
data = json.load(open(sys.argv[3]))
for window in data.get('result', {}).get('windows', []):
    app = window.get('app') or {}
    if app.get('pid') == pid and app.get('name') == needle and window.get('title') and isinstance(window.get('id'), int):
        print(f"{window['id']}\t{window['title']}")
        break
else:
    raise SystemExit(1)
PY
) || { wait "$open_pid" || true; fail "no matching fixture window title for verified PID $pid"; }
window_id=$(printf '%s\n' "$window_identity" | cut -f1)
window_title=$(printf '%s\n' "$window_identity" | cut -f2-)
[ -n "$window_id" ] && [ -n "$window_title" ] || { wait "$open_pid" || true; fail "fixture window identity is incomplete"; }

capture_time=$(date -u +%Y-%m-%dT%H:%M:%SZ)
python3 - "$envelope" "$receipt" "$source_sha" "$binary_sha" "$bundle_sha" "$capture_time" <<'PY'
import json, pathlib, sys
out, raw, source_sha, binary_sha, bundle_sha, captured_at = sys.argv[1:]
payload = json.loads(pathlib.Path(raw).read_text())
envelope = {
    "schema": 1,
    "captured_at": captured_at,
    "raw_receipt": payload,
    "src_ui_story_sha256": source_sha,
    "diagnostic_binary_sha256": binary_sha,
    "bundle_executable_sha256": bundle_sha,
    "raw_receipt_sha256": __import__('hashlib').sha256(pathlib.Path(raw).read_bytes()).hexdigest(),
}
pathlib.Path(out).write_text(json.dumps(envelope, sort_keys=True, indent=2) + "\n")
PY
receipt_sha=$(sha256 "$receipt")

capture_path() {
    python3 - "$1" <<'PY'
import json, sys
data = json.load(open(sys.argv[1]))
shot = data.get("result", {}).get("screenshot") or {}
path = shot.get("path")
if not path:
    raise SystemExit(1)
print(path)
PY
}

validate_capture() {
    python3 - "$1" "$action" "$checkpoint" <<'PY'
import struct, sys
import zlib
path, action, checkpoint = sys.argv[1:]
data = open(path, 'rb').read()
if data[:8] != b'\x89PNG\r\n\x1a\n' or data[12:16] != b'IHDR':
    raise SystemExit('capture is not a PNG')
w, h, depth, color = struct.unpack('>IIBB', data[16:26])
if (w, h, depth, color) != (1920, 1144, 8, 6):
    raise SystemExit('capture geometry/color is %dx%d depth=%d color=%d, expected 1920x1144 RGBA' % (w, h, depth, color))
pos = 8
chunks = []
while pos < len(data):
    size = struct.unpack('>I', data[pos:pos + 4])[0]
    kind = data[pos + 4:pos + 8]
    body = data[pos + 8:pos + 8 + size]
    pos += 12 + size
    if kind == b'IDAT':
        chunks.append(body)
    if kind == b'IEND':
        break
raw = zlib.decompress(b''.join(chunks))
stride = w * 4
rows = []
offset = 0
prior = bytearray(stride)
for _ in range(h):
    kind = raw[offset]
    scan = bytearray(raw[offset + 1:offset + 1 + stride])
    offset += stride + 1
    for i in range(stride):
        left = scan[i - 4] if i >= 4 else 0
        up = prior[i]
        upper_left = prior[i - 4] if i >= 4 else 0
        if kind == 1:
            scan[i] = (scan[i] + left) & 255
        elif kind == 2:
            scan[i] = (scan[i] + up) & 255
        elif kind == 3:
            scan[i] = (scan[i] + ((left + up) // 2)) & 255
        elif kind == 4:
            p = left + up - upper_left
            pa, pb, pc = abs(p - left), abs(p - up), abs(p - upper_left)
            scan[i] = (scan[i] + (left if pa <= pb and pa <= pc else up if pb <= pc else upper_left)) & 255
        elif kind != 0:
            raise SystemExit('unsupported PNG filter')
    rows.append(scan)
    prior = scan
def active(y0, y1):
    total = 0
    for y in range(y0, y1):
        row = rows[y]
        for x in range(0, stride, 4):
            if row[x] + row[x + 1] + row[x + 2] > 45:
                total += 1
    return total
if action == 'fixture-ddd-options':
    signature = (active(300, 480), active(480, 700), active(700, 880))
elif action in ('fixture-ddd-start-intro', 'fixture-ddd-ui-showcase'):
    signature = (active(100, 300), active(300, 600), active(600, 760))
else:
    signature = (active(680, 850), active(850, 980), active(980, 1135))
if min(signature) < 80:
    raise SystemExit('capture lacks full client composition: activity signature=%r' % (signature,))
def traffic_light(x0, x1, predicate):
    total = 0
    for y in range(14, 49):
        row = rows[y]
        for x in range(x0 * 4, x1 * 4, 4):
            r, g, b = row[x:x + 3]
            if predicate(r, g, b):
                total += 1
    return total
chrome = (
    traffic_light(14, 49, lambda r, g, b: r > 170 and g < 170 and b < 170),
    traffic_light(60, 96, lambda r, g, b: r > 160 and g > 120 and b < 110),
    traffic_light(105, 142, lambda r, g, b: r < 130 and g > 130 and b < 150),
)
inactive_chrome = (
    traffic_light(14, 49, lambda r, g, b: 65 <= r <= 100 and g - r <= 8 and b - r <= 14),
    traffic_light(60, 96, lambda r, g, b: 65 <= r <= 100 and g - r <= 8 and b - r <= 14),
    traffic_light(105, 142, lambda r, g, b: 65 <= r <= 100 and g - r <= 8 and b - r <= 14),
)
if min(chrome) >= 80:
    chrome_state = 'active'
elif min(inactive_chrome) >= 80:
    chrome_state = 'inactive'
else:
    raise SystemExit('capture lacks complete native title chrome: active=%r inactive=%r' % (chrome, inactive_chrome))
def region(x0, x1, y0, y1):
    total = 0
    for y in range(y0, y1):
        row = rows[y]
        for x in range(x0 * 4, x1 * 4, 4):
            if row[x] + row[x + 1] + row[x + 2] > 45:
                total += 1
    return total
if action == 'fixture-ddd-opening' and checkpoint in ('title-handoff', 'skip-key', 'skip-mouse'):
    layout = (region(720, 1200, 200, 300), region(780, 1150, 450, 530),
              region(700, 1200, 520, 610), region(700, 1200, 600, 700),
              region(600, 1300, 1060, 1135))
    minimums = (8000, 14000, 7000, 4300, 6000)
    label = 'title'
elif action == 'fixture-ddd-opening':
    layout = (region(560, 1380, 300, 900), region(600, 1300, 100, 280),
              region(550, 1400, 800, 1000), region(600, 1300, 1000, 1135),
              region(700, 1200, 450, 750))
    minimums = (20000, 5000, 10000, 4000, 12000)
    label = 'physical boot drive'
elif action == 'fixture-ddd-ending':
    layout = (region(520, 1040, 300, 750), region(1190, 1840, 300, 800),
              region(500, 1500, 800, 900), region(550, 1400, 900, 1050),
              region(600, 1300, 1060, 1135))
    minimums = (20000, 20000, 5000, 5000, 5000)
    label = 'ending drive/read-window/monitor'
elif action == 'fixture-ddd-options':
    layout = (region(600, 1320, 260, 440), region(520, 1400, 500, 700),
              region(520, 1400, 700, 880), region(650, 1300, 260, 440),
              region(300, 1650, 260, 880))
    minimums = (1000, 3000, 1000, 1000, 6000)
    label = 'options panel'
elif action in ('fixture-ddd-start-intro', 'fixture-ddd-ui-showcase'):
    layout = (region(500, 1400, 200, 760), region(650, 1300, 250, 600),
              region(520, 1400, 600, 780), region(600, 1300, 100, 300),
              region(300, 1650, 200, 760))
    minimums = (14000, 4000, 1000, 1000, 18000)
    label = 'intro/showcase panel'
else:
    raise SystemExit('unsupported capture action: %s' % action)
if any(value < minimum for value, minimum in zip(layout, minimums)):
    raise SystemExit('capture lacks complete rendered %s layout: anchors=%r' % (label, layout))
print('%d,%d,%d %d,%d,%d,%d,%d chrome=%s' % (signature + layout + (chrome_state,)))
PY
}

matching_window() {
    python3 - "$1" "$2" "$pid" "$window_id" "$window_title" <<'PY'
import json, sys
first, second, pid, window_id, title = sys.argv[1:]
pid = int(pid)
window_id = int(window_id)
def identity(path):
    result = json.load(open(path)).get('result', {})
    shot = result.get('screenshot') or {}
    snapshot = result.get('snapshot') or {}
    window = snapshot.get('window') or {}
    app = snapshot.get('app') or {}
    if shot.get('width') != 1920 or shot.get('height') != 1144 or shot.get('format') != 'png':
        raise SystemExit('settled observation has unexpected screenshot metadata')
    if result.get('screenshotStatus', {}).get('state') != 'captured':
        raise SystemExit('settled observation was not captured')
    if (app.get('pid') != pid or window.get('id') != window_id or window.get('title') != title
            or window.get('width') != 960 or window.get('height') != 572
            or window.get('isMinimized') or window.get('isOffscreen')):
        raise SystemExit('settled observation does not match the verified live window')
    return (app.get('pid'), window.get('id'), window.get('x'), window.get('y'), window.get('width'), window.get('height'), window.get('title'))
if identity(first) != identity(second):
    raise SystemExit('settled observations do not match the same full-composition window')
PY
}

matching_layout() {
    python3 - "$1" "$2" "$action" "$checkpoint" <<'PY'
import sys
first, second = (value.split()[1].split(',') for value in sys.argv[1:3])
action = sys.argv[3]
checkpoint = sys.argv[4]
first = [int(value) for value in first]
second = [int(value) for value in second]
if len(first) != 5 or len(second) != 5:
    raise SystemExit('rendered layout signature is incomplete')
if action == 'fixture-ddd-ending' or checkpoint not in ('title-handoff', 'skip-key', 'skip-mouse'):
    raise SystemExit(0)
if any(abs(left - right) / max(left, right) > 0.08 for left, right in zip(first, second)):
    raise SystemExit('rendered layout observations differ by more than 8 percent')
PY
}

orca computer get-app-state --app "pid:$pid" --window-id "$window_id" --restore-window --json > "$warmup_state"
geometry=$(python3 - "$warmup_state" <<'PY'
import json, sys
screenshot = json.load(open(sys.argv[1])).get("result", {}).get("screenshot") or {}
width, height = screenshot.get("width"), screenshot.get("height")
if not isinstance(width, int) or not isinstance(height, int):
    raise SystemExit("unknown")
print(f"{width}x{height}")
PY
) || geometry=unknown
if [ "$geometry" != 1920x1144 ]; then
    printf '{"error":"unsupported-capture-geometry","expected":"1920x1144","actual":"%s"}\n' "$geometry" >&2
    wait "$open_pid" || true
    exit 2
fi
if [ "$action" = fixture-ddd-ending ]; then
    sleep 3
else
    sleep 0.5
fi
capture_observation() {
    orca computer get-app-state --app "pid:$pid" --window-id "$window_id" --restore-window --json > "$1"
    observation_path=$(capture_path "$1") || return 1
    [ -f "$observation_path" ] || return 1
    cp "$observation_path" "${2%.png}.orca.png"
    screencapture -l"$window_id" -o -x "$2"
    initial_sha=$(sha256 "$2")
    sleep 0.1
    settled_sha=$(sha256 "$2")
    sleep 0.1
    stable_sha=$(sha256 "$2")
    python3 - "$3" "$2" "$initial_sha" "$settled_sha" "$stable_sha" <<'PY'
import json, pathlib, sys
(out, capture, initial_sha, settled_sha, stable_sha) = sys.argv[1:]
pathlib.Path(out).write_text(json.dumps({
    "schema": 1,
    "capture": capture,
    "initial_sha256": initial_sha,
    "settled_sha256": settled_sha,
    "stable_sha256": stable_sha,
    "delayed_compositor_write_detected": initial_sha != settled_sha,
    "stable_after_settle": settled_sha == stable_sha,
}, sort_keys=True, indent=2) + "\n")
PY
    [ "$settled_sha" = "$stable_sha" ]
}

refresh_capture_stability() {
    initial_sha=$(sha256 "$1")
    sleep 0.1
    settled_sha=$(sha256 "$1")
    sleep 0.1
    stable_sha=$(sha256 "$1")
    python3 - "$2" "$1" "$initial_sha" "$settled_sha" "$stable_sha" <<'PY'
import json, pathlib, sys
out, capture, initial_sha, settled_sha, stable_sha = sys.argv[1:]
pathlib.Path(out).write_text(json.dumps({
    "schema": 1,
    "capture": capture,
    "initial_sha256": initial_sha,
    "settled_sha256": settled_sha,
    "stable_sha256": stable_sha,
    "delayed_compositor_write_detected": initial_sha != settled_sha,
    "stable_after_settle": settled_sha == stable_sha,
}, sort_keys=True, indent=2) + "\n")
PY
    [ "$settled_sha" = "$stable_sha" ]
}

mark_final_capture_validated() {
    python3 - "$1" "$2" "$3" <<'PY'
import json, pathlib, sys
diagnostic, capture_sha, signature = sys.argv[1:]
path = pathlib.Path(diagnostic)
data = json.loads(path.read_text())
if data.get("settled_sha256") != capture_sha or data.get("stable_sha256") != capture_sha:
    raise SystemExit("final capture bytes differ from the settled stability diagnostic")
data["settled_validation"] = {"status": "pass", "result": signature}
path.write_text(json.dumps(data, sort_keys=True, indent=2) + "\n")
PY
}

discard_state_1=$diagnostics/discard-state-1.json
discard_state_2=$diagnostics/discard-state-2.json
discard_state_3=$diagnostics/discard-state-3.json
discard_capture_1=$diagnostics/discard-1.png
discard_capture_2=$diagnostics/discard-2.png
discard_capture_3=$diagnostics/discard-3.png
capture_observation "$app_state" "$capture" "$diagnostics/final.capture-stability.json" || { wait "$open_pid" || true; fail "final observation did not yield a stable screenshot"; }
printf '%s\n' 'Rejected compositor diagnostics: the warm-up observation is not an accepted artifact. The only accepted capture is ../window.png.' > "$diagnostics/rejected-observations.txt"
matching_window "$warmup_state" "$app_state" || { wait "$open_pid" || true; fail "final observation does not match the verified restored window"; }
final_attempts=$diagnostics/final.capture-attempts.jsonl
: > "$final_attempts"
final_signature=
capture_attempt=1
while [ "$capture_attempt" -le 3 ]; do
    if final_signature=$(validate_capture "$capture" 2>&1); then
        python3 - "$final_attempts" "$capture_attempt" "$capture" "$final_signature" <<'PY'
import json, pathlib, sys
path, attempt, capture, signature = sys.argv[1:]
with pathlib.Path(path).open("a") as out:
    out.write(json.dumps({"attempt": int(attempt), "capture": capture, "status": "pass", "signature": signature}, sort_keys=True) + "\n")
PY
        break
    fi
    python3 - "$final_attempts" "$capture_attempt" "$capture" "$final_signature" <<'PY'
import json, pathlib, sys
path, attempt, capture, error = sys.argv[1:]
with pathlib.Path(path).open("a") as out:
    out.write(json.dumps({"attempt": int(attempt), "capture": capture, "error": error, "status": "rejected"}, sort_keys=True) + "\n")
PY
    [ "$capture_attempt" -lt 3 ] || { wait "$open_pid" || true; fail "final capture is not fully composited after native recapture"; }
    screencapture -l"$window_id" -o -x "$capture"
    refresh_capture_stability "$capture" "$diagnostics/final.capture-stability.json" || { wait "$open_pid" || true; fail "native recapture did not become byte-stable"; }
    capture_attempt=$((capture_attempt + 1))
done
capture_sha=$(sha256 "$capture")
mark_final_capture_validated "$diagnostics/final.capture-stability.json" "$capture_sha" "$final_signature" || { wait "$open_pid" || true; fail "final capture does not match the settled stability diagnostic"; }
printf '%s\n' "$final_signature" > "$bundle/window.signature"
python3 - "$warmup_state" "$app_state" "$pid" "$window_id" "$window_title" <<'PY' || { wait "$open_pid" || true; fail "final capture does not match the restored-window observation"; }
import json, sys
settled, final, pid, window_id, title = sys.argv[1:]
pid = int(pid)
window_id = int(window_id)
def identity(path):
    result = json.load(open(path)).get('result', {})
    snapshot = result.get('snapshot') or {}
    window = snapshot.get('window') or {}
    app = snapshot.get('app') or {}
    if (app.get('pid') != pid or window.get('id') != window_id or window.get('title') != title
            or window.get('width') != 960 or window.get('height') != 572
            or window.get('isMinimized') or window.get('isOffscreen')):
        raise SystemExit('capture does not match the verified live window')
    return (app.get('pid'), window.get('id'), window.get('x'), window.get('y'), window.get('width'), window.get('height'), window.get('title'))
if identity(settled) != identity(final):
    raise SystemExit('final capture does not match the settled window observations')
PY
python3 - "$manifest" "$run_id" "$bundle_id" "$display_name" "$pid" "$window_id" "$window_title" "$source_sha" "$binary_sha" "$bundle_sha" "$receipt_sha" "$capture_time" "$capture" "$capture_sha" <<'PY'
import json, pathlib, struct, sys
(out, run_id, bundle_id, display_name, pid, window_id, window_title, source_sha, binary_sha,
 bundle_sha, receipt_sha, captured_at, capture, capture_sha) = sys.argv[1:]
header = pathlib.Path(capture).read_bytes()[:33]
width, height, depth, color = struct.unpack(">IIBB", header[16:26])
pathlib.Path(out).write_text(json.dumps({
    "schema": 1, "run_id": run_id, "bundle_id": bundle_id,
    "bundle_display_name": display_name, "verified_pid": int(pid),
    "verified_window_id": int(window_id), "window_title": window_title,
    "capture_backend": "macos-screencapture-window-id-no-shadow", "src_ui_story_sha256": source_sha,
    "diagnostic_binary_sha256": binary_sha, "bundle_executable_sha256": bundle_sha,
    "raw_receipt_sha256": receipt_sha,
    "capture_sha256": capture_sha,
    "captured_at": captured_at, "receipt_envelope": "receipt-envelope.json",
    "capture": "window.png", "png_signature": header[:8].hex(),
    "png_width": width, "png_height": height, "png_bit_depth": depth,
    "png_color_type": color
}, sort_keys=True, indent=2) + "\n")
PY
wait "$open_pid"
printf '%s\n' "$bundle"
