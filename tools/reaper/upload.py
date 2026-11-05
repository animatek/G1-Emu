"""Acts as Animatek NME on G1-Emu's direct link: uploads a patch's packets into slot A.

    python upload.py <packets dir> [--wait-boot S] [--port P]

Looks for a plugin instance on the direct link's ports (47310 on), by its greeting line: a standalone
G1-Emu answers there too and is skipped. Retries while the host starts, waits for the G1 to boot,
then sends each packet-NN.syx (as `g1patchtest ... --dump-packets dir` writes them) and waits for its
ACK (cc $16, $36 or $7F), as NME does. Used by bench.ps1.
"""
import argparse
import glob
import os
import socket
import sys
import time

ap = argparse.ArgumentParser()
ap.add_argument("packets")
ap.add_argument("--wait-boot", type=float, default=8.0)
ap.add_argument("--port", type=int, default=47310)
ap.add_argument("--connect-timeout", type=float, default=90.0)
ap.add_argument("--gap", type=float, default=0.0, help="seconds between one packet's ACK and the next packet (a slow editor)")
ap.add_argument("--repeat", type=int, default=1, help="uploads in a row")
ap.add_argument("--any", action="store_true", help="take a standalone too, on --port only")
ap.add_argument("--split", type=float, default=0.0, help="send each upload's first packet in two halves, this many seconds apart (MIDI drivers deliver long SysEx in pieces)")
a = ap.parse_args()

packets = [open(f, "rb").read() for f in sorted(glob.glob(os.path.join(a.packets, "packet-*.syx")))]
t0 = time.time()
s = None
rx = b""


def greet(port):
    """The instance's greeting line on that port, or None. A standalone answers too: only a plugin's counts."""
    try:
        c = socket.create_connection(("127.0.0.1", port), timeout=0.5)
    except OSError:
        return None, None
    c.settimeout(1.0)
    data = b""
    try:
        while b"\n" not in data:
            d = c.recv(4096)
            if not d:
                break
            data += d
    except socket.timeout:
        pass
    line, _, rest = data.partition(b"\n")
    return c, (line.decode(errors="replace"), rest)


while time.time() - t0 < a.connect_timeout and not s:
    for port in (range(a.port, a.port + 1) if a.any else range(a.port, a.port + 8)):
        c, g = greet(port)
        if not c:
            continue
        if a.any or " plugin" in g[0]:
            s, rx = c, g[1]
            greeting = g[0]
            break
        c.close()
    if not s:
        time.sleep(0.5)
if not s:
    print("upload: no G1-Emu plugin on the direct link")
    sys.exit(1)
s.settimeout(0.05)


def pump():
    global rx
    try:
        while True:
            d = s.recv(4096)
            if not d:
                break
            rx += d
    except (socket.timeout, BlockingIOError):
        pass


def errors():
    """The synth's error notifications (sc $7E), as NME reads them: the code is the first data byte."""
    return [rx[i + 6] for i in range(len(rx) - 6) if rx[i] == 0xF0 and rx[i + 1] == 0x33 and rx[i + 5] == 0x7E]


def acks():
    n = 0
    for i in range(len(rx) - 5):
        if rx[i] == 0xF0 and rx[i + 1] == 0x33 and (rx[i + 2] >> 2) == 0x16 and rx[i + 5] in (0x36, 0x7F):
            n += 1
    return n


print("upload: connected after %.1f s: %s" % (time.time() - t0, greeting))
time.sleep(a.wait_boot)
pump()
rx = b""
failed = 0
for r in range(a.repeat):
    for i, p in enumerate(packets):
        if i and a.gap:
            time.sleep(a.gap)
        base = acks()
        if i == 0 and a.split:
            s.sendall(p[:len(p) // 2])
            time.sleep(a.split)
            s.sendall(p[len(p) // 2:])
        else:
            s.sendall(p)
        end = time.time() + 3
        while acks() <= base and not errors() and time.time() < end:
            pump()
        if errors():
            break
        if a.repeat == 1:
            print("upload: packet %d (%d bytes) %s" % (i, len(p), "ACK" if acks() > base else "NO ACK"))
    time.sleep(0.3)
    pump()
    if errors():
        failed += 1
        print("upload %d: REJECTED, code %s" % (r + 1, ",".join(str(c) for c in errors())))
    elif a.repeat > 1:
        print("upload %d: ok" % (r + 1))
    rx = b""
    if a.repeat > 1:
        time.sleep(1.5)
time.sleep(1.0)
pump()
s.close()
print("upload: done, %d of %d rejected" % (failed, a.repeat))
sys.exit(1 if failed else 0)
