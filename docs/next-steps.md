# Next steps

A handover, written on 2026-09-20 for whoever picks this up next. `ROADMAP.md` is the long view;
this is the short list of what to do now, in order, with enough context that nobody has to
reconstruct an evening of work first.

Read `AGENTS.md` before anything. Its rules are not negotiable: everything in English, no ROMs in
Git, the external Gearmulator clone is never modified, and **every change gets its line in
`CHANGELOG.md` in the same commit**.

Where things stand: the emulator sounds, has a window, finds its own ROM, publishes its own MIDI
ports, and **builds on Linux, macOS and Windows** with CI proving it on every push
(`.github/workflows/build.yml`). What stops a release is below.

---

## 1. The DSP56300 JIT still raises an illegal instruction on Apple Silicon

**Still open**, despite what this file and the changelog said on 2026-09-21 morning. The entries
that called it done cite [CI run 35566113403](https://github.com/animatek/G1-Emu/actions/runs/35566113403),
which is green only because the macOS `Test` step still had `continue-on-error`: its log ends in
`g1dspcheck (ILLEGAL)` like every run before it. The two runs since, with the test gating
([35566928286](https://github.com/animatek/G1-Emu/actions/runs/35566928286) and
[35567844751](https://github.com/animatek/G1-Emu/actions/runs/35567844751)), fail the same way.

What is actually known:

- The failing case is always **`finite DO`** (block size 1). It is the first case that runs both
  places the G1 extension patches: the DO entry in `do_exec` and `do_end`. Both `DO FOREVER` cases
  pass because they never leave the loop and so never reach `do_end`.
- The **"AArch64 cannot encode that immediate" theory is dead.** asmjit compiles its arm64 backend
  on any host, so the sequences can be assembled here on x86 and inspected without a Mac: the
  complemented masks, `BFC` and `BFI` with the zero register all encode with no error (the words
  are in `NOTES.md`, "The DSP JIT on ARM"). All three attempts changed an encoding that was never
  wrong, which is why the crash never moved.
- The ARM-only `#ifdef HAVE_ARM64` paths have therefore been removed from `cmake/Dsp56300.cmake`:
  one portable form again.

What to try next, in order:

1. **Read the core's own error.** In Release, `AsmJitErrorHandler` only logs (`assert` compiled
   out) and the broken block runs anyway; the log went to stdout unflushed and was lost.
   `g1dspcheck` now routes it to stderr flushed, prefixed `CORE:`. The next macOS run should say
   whether asmjit refused something and what.
2. **`Linux arm64 (native ALSA/JACK)`** (`ubuntu-24.04-arm`, free for public repositories) is now
   in the matrix. Same AArch64 JIT, not Apple's system: if it passes, the problem is what macOS
   does with the generated code (W^X / `MAP_JIT` / icache invalidation), not the code itself, and
   the macOS `mapRegion@276: MmuHelper: Failed to create memory mapping, err 22` lines in the log
   become the first suspect. If it fails, the bug is ours and can be debugged on a machine where
   a core dump and a debugger are available.
3. Only then, the interpreter fallback as a last resort.

## 2. What only the real machines can say

CI proves it compiles. It cannot plug in a sound card. Both of these need somebody sitting at the
machine, and both are quick once someone is:

- **macOS:** that CoreAudio opens, that CoreMIDI publishes `G1-Emu PC Port` and `G1-Emu MIDI`, and
  that an editor and a DAW see them. Every sign says they will — CoreMIDI creates virtual ports
  natively with nothing to install — but nobody has looked.
- **Windows:** the same, plus the one real unknown. JUCE only creates virtual MIDI ports through
  **Windows MIDI Services**; with the older WinRT or WinMM backends `createNewDevice` returns
  nothing. `JUCE_USE_WINDOWS_MIDI_SERVICES` is **off by default**, needs a minimum Windows SDK,
  and JUCE's own comment says it only worked on a Canary insider build when it was written. That
  comment may be stale. **Try it on a real Windows 11 before promising anything.**

`JuceMidi::virtualPorts()` already reports whether the ports were created, and `EmuHost` puts it
in the status line. What is missing is the graceful end: when they cannot be made, the standalone
should open ordinary MIDI ports, say so plainly, and point at loopMIDI — not fail.

---

## 3. Bring the audio device into the settings window

The settings window still offers "JACK / PipeWire", "ALSA" and "None", which are the native
backend's words. With the JUCE backend there is a list of real devices instead, and
`JuceAudio::devices()` already returns them as "type: name". The window should show that list when
the JUCE backend is in use, and the Linux wording when it is not.

Small, self-contained, and it is the difference between a usable macOS build and one where you
cannot choose your interface.

---

## 4. The module defaults, which live in another repo

`docs/module-battery.md` explains this in full and the measurements are done. The short version:
**the DrumSynth is not broken, it is born inaudible**, because all twelve of its parameters
default to 25 in NME's `modules.xml` — the only twelves 25s in the whole file — and 25 on the G1's
level law is about 45 dB below audible. Six more modules have a level with no default at all,
which goes up as 0, and `4-1Switch` and `1-4Switch` are born completely silent.

**The fix belongs in `../Nomad2026/data/modules.xml`, not here.** That is a different repo with its
own `AGENTS.md`: read it first. Javier has been told and the decision is his; the suggestion on the
table is tunes at 64 and levels at 100, which is what comparable modules use. Verify with
`tools/battery/battery.py` before and after.

---

## 5. The 29 modules the battery gets nothing out of

`docs/module-battery.md` has the table. Of the ones looked at so far, **every single one was the
test's fault or the module description's, never the emulator's**. It is cheap, it almost always
yields something, and each finding improves NME for the real G1 and not just the emulated one.
`tools/battery/battery.py` takes `--only`, `--verbose` and `--param i=v` for exactly this.

---

## 6. Signing and notarising

Not started, and its own job. Without it macOS and Windows will refuse to open what we ship, so it
has to happen before a release even if it happens last.

---

## Two things that are easy to get wrong

**Two emulators at once.** If the maintainer has `g1gui` open, `aseqsend -p "G1-Emu:PC Port"`
resolves by name and may reach theirs instead of yours. Address the client by number. An upload is
not destructive — it does not touch stored patches — but it replaces what they had loaded.

**The flash is real.** Pass a scratch flash as the second argument when testing
(`g1run ROM /tmp/flash-test.bin`); with no argument it opens
`~/.local/share/Animatek/G1-Emu/flash.bin`, which is the maintainer's. Javier has said the ROM is
fine to use and that patches may be uploaded freely, and **Bank 9 is set aside** for patches made
by an agent. That permission is about the emulator: before touching a slot from NME, always check
which device it is connected to, because it has reached the real G1 before.
