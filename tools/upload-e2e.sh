#!/bin/bash
# Replays a patch upload over REAL ALSA ports, packet by packet, against a running g1run.
# g1patchtest talks to the emulated G1 directly; this goes through the native MIDI backend
# (app/alsamidi.h) the way NME does on Linux. See docs/upload-timeouts.md.
#
#   tools/upload-e2e.sh patch.pch [pause_seconds]     (default pause: 0.4 s between packets)
#
# Needs: a built tree (build/app/g1run, build/tools/patchtest), the ROM in Roms/, aseqsend,
# aseqdump, xxd, python3. It works on a COPY of the flash, so the user's G1 is not touched.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PATCH="$(readlink -f "${1:?usage: upload-e2e.sh patch.pch [pause_seconds]}")"
PAUSE="${2:-0.4}"
ROM="$ROOT/Roms/NORD-MODULAR-RACK-VER-3.03.BIN"
PT="$(find "$ROOT/build/tools/patchtest" -name g1patchtest -type f | head -1)"
W="$(mktemp -d)"
trap 'kill $GP $DP 2>/dev/null; rm -rf "$W"' EXIT

mkdir -p "$W/pk"
# a patch that hangs the emulator (korg.pch) never finishes this step: 60 s, and what was dumped stays
timeout 60 "$PT" "$ROM" "$PATCH" --seconds 0.1 --dump-packets "$W/pk" >/dev/null 2>&1
echo "packets: $(ls "$W/pk" | wc -l)"

cp ~/.local/share/Animatek/G1-Emu/flash.bin "$W/flash.bin" 2>/dev/null
G1_AUDIO=no G1_RAWMIDI= G1_MIDI_LOG=1 "$ROOT/build/app/g1run" "$ROM" "$W/flash.bin" >"$W/g1run.log" 2>&1 &
GP=$!
for _ in $(seq 1 60); do aconnect -l | grep -q "G1-Emu" && break; sleep 1; done
sleep 8   # the OS boots
PORT=$(aconnect -l | awk '/client [0-9]+: .G1-Emu/{c=$2} c && /PC Port/{sub(":","",c); print c":"$1; exit}')
echo "PC Port = $PORT"
aseqdump -p "$PORT" >"$W/dump.txt" 2>&1 &
DP=$!
sleep 1

for f in $(ls "$W/pk"/packet-*.syx | sort); do
	aseqsend -p "$PORT" "$(xxd -p "$f" | tr -d '\n' | sed 's/../& /g')"
	sleep "$PAUSE"
done
sleep 2

# What g1run logged: packets that arrived on the PC Port and what the OS answered.
python3 - "$W/g1run.log" <<'EOF'
import re, sys
out = bytearray(); inn = []
for l in open(sys.argv[1], errors='replace'):
    m = re.match(r'\[midi\] (in |out) PC Port\s+(\d+) bytes: (.*)', l)
    if not m: continue
    if m.group(1) == 'out':
        out += bytes(int(x, 16) for x in m.group(3).replace('...', '').split())
    else:
        inn.append(int(m.group(2)))
msgs = re.findall(rb'\xf0[\x00-\x7f]*\xf7', bytes(out))
acks = [m for m in msgs if len(m) > 6 and m[2] >> 2 == 0x16 and m[5] == 0x36]
print("packets received by the emulator:", len(inn), inn)
print("first-packet ACKs (0x36):", len(acks), "| SysEx messages sent back:", len(msgs))
EOF
kill -INT $GP 2>/dev/null; sleep 2
