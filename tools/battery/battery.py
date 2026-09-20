#!/usr/bin/env python3
"""Module battery: one patch per module type, played and measured with g1patchtest.

Every module is put in a patch of its own with what it needs to show signs of life: an
oscillator on its audio inputs, an LFO on its control inputs and a running clock on its
logic inputs, plus the keyboard's note and gate. Its first four outputs go to the four
outputs of the G1, so one run measures them all.

    tools/battery/battery.py            # generate, run and report
    tools/battery/battery.py --only OscA --keep
"""
import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

NS = {"m": "http://nmedit.sf.net/ns/ModuleDescriptions"}
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

# The rig around the module under test, by module type.
KEYBOARD, OUT4, OSC_A, LFO_A, CLK_GEN = 1, 3, 7, 24, 68
M_KEYB, M_OSC, M_LFO, M_CLK, M_OUT, M_TEST = 1, 2, 3, 4, 5, 6
HEADER = "0 127 0 127 2 0 0 1 4000 2 0 0 0 0 15 2 1 1 1 1 1 1 1 "


# A parameter with no default in modules.xml is uploaded as 0, which for a level or an amount
# leaves the module mute and impossible to judge. The battery opens those up (and says so in
# the report), leaving every other parameter at the default NME would send.
OPEN_UP = re.compile(r"(?i)\b(level|amount|gain|mix|depth|volume)\b")


class Module:
    def __init__(self, el):
        self.type = int(el.get("index"))
        self.name = el.get("name") or f"type{self.type}"
        self.category = el.get("category") or ""
        self.params = []
        self.opened = []	# parameters the battery raised from 0
        self.inputs = []	# (index, signal, name)
        self.outputs = []
        for ch in el:
            tag = ch.tag.split("}")[-1]
            if tag == "parameter" and ch.get("class") == "parameter":
                value = int(ch.get("defaultValue") or 0)
                name = ch.get("name") or ""
                if value == 0 and OPEN_UP.search(name):
                    top = int(ch.get("maxValue") or 127)
                    value = min(100, top)
                    self.opened.append(name)
                self.params.append(value)
            elif tag == "connector":
                entry = (int(ch.get("index")), ch.get("signal") or "audio", ch.get("name") or "")
                (self.inputs if ch.get("type") == "input" else self.outputs).append(entry)
        self.inputs.sort()
        self.outputs.sort()


def read_modules(path):
    root = ET.parse(path).getroot()
    out = {}
    for el in root.findall(".//m:module", NS):
        if el.get("index") is None:
            continue		# the Morph section, which is not a module of the patch
        m = Module(el)
        out[m.type] = m
    return out


def patch_for(mods, test):
    """The .pch text for one module under test."""
    rig = [(M_KEYB, KEYBOARD), (M_OSC, OSC_A), (M_LFO, LFO_A), (M_CLK, CLK_GEN), (M_OUT, OUT4)]
    modules = [(i, t) for i, t in rig] + [(M_TEST, test.type)]
    cables = []

    def cable(src_mod, src_conn, dst_mod, dst_conn):
        # color, module, connector, type (1 = output, 0 = input)
        cables.append(f"0 {dst_mod} {dst_conn} 0 {src_mod} {src_conn} 1")

    # What feeds each input of the module under test, by the kind of signal it takes.
    source = {
        "audio": (M_OSC, 0),		# OscA's output
        "control": (M_LFO, 1),		# LFOA's control output: a signal that moves
        "logic": (M_CLK, 0),		# a running clock, so sequencers and envelopes fire
        "master-slave": (M_OSC, 1),	# the oscillator's slave output, for slave modules
    }
    # A reset or a sync held by a running clock freezes the module at its start, which looks
    # exactly like a module that does not work, so those inputs are left alone.
    for index, signal, name in test.inputs:
        if re.search(r"(?i)\b(reset|sync)\b", name):
            continue
        src = source.get(signal, source["audio"])
        cable(src[0], src[1], M_TEST, index)
    # Its outputs go to the four outputs of the G1, so one run measures them all. A
    # master-slave output carries no signal of its own, so it is left out.
    usable = [c for c in test.outputs if c[1] != "master-slave"]
    for i, (index, _signal, _name) in enumerate(usable[:4]):
        cable(M_TEST, index, M_OUT, i)

    def params_line(i, t):
        m = mods[t]
        return f"{i} {t} {len(m.params)} " + " ".join(str(v) for v in m.params) + " "

    names = {M_KEYB: "Keyboard", M_OSC: "OscA", M_LFO: "LFOA", M_CLK: "ClkGen",
             M_OUT: "4Output", M_TEST: test.name}
    lines = ["[Header]", "Version=Nord Modular patch 3.0", HEADER, "[/Header]",
             "[ModuleDump]", "1 "]
    for row, (i, t) in enumerate(modules):
        lines.append(f"{i} {t} {2 + (row % 3) * 3} {2 + row * 2} ")
    lines += ["[/ModuleDump]", "[ModuleDump]", "0 ", "[/ModuleDump]",
              "[CurrentNoteDump]", "64 0 0 64 0 0 ", "[/CurrentNoteDump]",
              "[CableDump]", "1 "] + [c + " " for c in cables]
    lines += ["[/CableDump]", "[CableDump]", "0 ", "[/CableDump]", "[ParameterDump]", "1 "]
    for i, t in modules:
        lines.append(params_line(i, t))
    lines += ["[/ParameterDump]", "[ParameterDump]", "0 ", "[/ParameterDump]",
              "[MorphMapDump]", "0 0 0 0 ", "[/MorphMapDump]",
              "[KeyboardAssignment]", "0 0 0 0 ", "[/KeyboardAssignment]",
              "[KnobMapDump]", "[/KnobMapDump]", "[CtrlMapDump]", "[/CtrlMapDump]",
              "[NameDump]", "1 "]
    for i, t in modules:
        lines.append(f"{i} {names[i]}")
    lines += ["[/NameDump]", "[NameDump]", "0 ", "[/NameDump]"]
    return "\n".join(lines) + "\n"


OUT_RE = re.compile(r"output (\d): (silence|peak\s+(-?[\d.]+) dBFS .*?, ~([\d.]+) Hz, mean ([-+][\d.]+), drift ([\d.]+))")


def run_one(binary, rom, path, seconds, note, input_hz):
    cmd = [binary, rom, path, "--seconds", str(seconds), "--note", str(note)]
    if input_hz:
        cmd += ["--input-sine", str(input_hz)]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    except subprocess.TimeoutExpired:
        return {"error": "timeout"}
    out = r.stdout + r.stderr
    res = {"uploaded": "uploaded in" in out, "outputs": []}
    if "the OS has not confirmed" in out or "NO REPLY" in out:
        res["error"] = "the OS did not take the patch"
    for m in OUT_RE.finditer(out):
        if m.group(2) == "silence":
            res["outputs"].append(None)
        else:
            res["outputs"].append({"peak": float(m.group(3)), "hz": float(m.group(4)),
                                   "mean": float(m.group(5)), "drift": float(m.group(6))})
    return res


# The OS caps the master volume at -36 dB, so a signal at full scale peaks around -62 dBFS
# and the numbers are small: these floors are set from that, not from 0 dBFS.
FLOOR_DB = -100.0	# below this there is nothing
MOVES = 0.02		# a slice-to-slice swing of this much of the peak counts as movement


def verdict(res):
    if res.get("error"):
        return "ERROR: " + res["error"]
    outs = [o for o in res["outputs"] if o and o["peak"] > FLOOR_DB]
    if not outs:
        return "silent"
    best = max(outs, key=lambda o: o["peak"])
    level = 10.0 ** (best["peak"] / 20.0)
    if best["hz"] >= 20:
        return f"sounds ({best['peak']:.0f} dBFS, {best['hz']:.0f} Hz)"
    if best["drift"] >= level * MOVES:
        return f"moves ({best['peak']:.0f} dBFS, {best['hz']:.2f} Hz)"
    return f"fixed level ({best['peak']:.0f} dBFS)"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", default=os.path.join(ROOT, "Roms", "NORD-MODULAR-RACK-VER-3.03.BIN"))
    ap.add_argument("--xml", default=os.path.abspath(os.path.join(ROOT, "..", "Nomad2026", "data", "modules.xml")))
    ap.add_argument("--binary", default=os.path.join(ROOT, "build", "tools", "patchtest",
                                                     "g1patchtest_artefacts", "Release", "g1patchtest"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "battery"))
    ap.add_argument("--seconds", type=float, default=2.0)
    ap.add_argument("--note", type=int, default=60)
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) // 2))
    ap.add_argument("--only", default=None, help="only modules whose name matches this")
    ap.add_argument("--keep", action="store_true", help="keep the generated patches")
    ap.add_argument("--verbose", action="store_true", help="the four outputs of each module")
    ap.add_argument("--input-sine", type=float, default=440.0,
                    help="a sine into the G1's audio inputs, so AudioIn has something to show")
    ap.add_argument("--param", action="append", default=[], metavar="I=V",
                    help="override a parameter of the module under test, to follow one up")
    args = ap.parse_args()

    mods = read_modules(args.xml)
    del mods[0]		# Morph is the patch's morph section, not a module the OS will take
    os.makedirs(args.out, exist_ok=True)
    rig = {KEYBOARD, OUT4, OSC_A, LFO_A, CLK_GEN}
    targets = [m for m in mods.values() if args.only is None or args.only.lower() in m.name.lower()]
    targets.sort(key=lambda m: (m.category, m.name))

    for over in args.param:
        i, _, v = over.partition("=")
        for m in targets:
            if int(i) < len(m.params):
                m.params[int(i)] = int(v)

    jobs = []
    for m in targets:
        path = os.path.join(args.out, f"{m.type:03d}-{m.name.replace('/', '_')}.pch")
        with open(path, "w") as f:
            f.write(patch_for(mods, m))
        jobs.append((m, path))

    print(f"{len(jobs)} modules, {args.seconds} s each, {args.jobs} at a time\n")
    results = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(run_one, args.binary, args.rom, p, args.seconds, args.note,
                               args.input_sine): m for m, p in jobs}
        for fut in concurrent.futures.as_completed(futures):
            m = futures[fut]
            results[m.type] = (m, fut.result())

    counts = {}
    print(f"{'type':>4}  {'module':<16} {'category':<12} in/out   verdict")
    for m in targets:
        _, res = results[m.type]
        v = verdict(res)
        counts[v.split()[0]] = counts.get(v.split()[0], 0) + 1
        io = f"{len(m.inputs)}/{len(m.outputs)}"
        star = " *" if m.type in rig else ""
        if m.opened:
            star += "   [opened: " + ", ".join(m.opened) + "]"
        print(f"{m.type:>4}  {m.name:<16} {m.category:<12} {io:<8} {v}{star}")
        if args.verbose:
            for i, o in enumerate(results[m.type][1].get("outputs", [])):
                if o:
                    print(f"        out {i + 1}: peak {o['peak']:7.1f} dBFS  {o['hz']:8.2f} Hz"
                          f"  mean {o['mean']:+.5f}  drift {o['drift']:.5f}")
    print("\n" + ", ".join(f"{k}: {v}" for k, v in sorted(counts.items())))
    if not args.keep:
        for _, p in jobs:
            os.remove(p)


if __name__ == "__main__":
    sys.exit(main())
