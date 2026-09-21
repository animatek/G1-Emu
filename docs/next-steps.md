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

## 1. Done — the Apple Silicon crash was a mask of zero (2026-09-21)

The DSP56300 JIT raised an illegal instruction on the Apple Silicon runner, always in the
`finite DO` case. It was **not** the G1's `FV` loop-flag extension, and **not** an immediate that
AArch64 could not encode in the code the three earlier attempts kept rewriting.

The core's own error, once `g1dspcheck` sent the log to stderr flushed, says it in one line:

```
handleError@14: Error: 50 - InvalidImmediate: tst w5, 0, block at PC 000102, P mem size 1
```

`jitblock.cpp` ends a loop body with `test_(lc, Imm(maxDoIterations - 1))` + `jz`, and G1-Emu runs
one iteration per block (`maxDoIterations = 1`, in `g1dsp.cpp` and in the test), so the mask is
zero. AArch64 cannot encode zero as a logical immediate, asmjit refuses the instruction, and in
Release the error handler only logs: the block is left unfinished and the DSP runs into it. On x86
`test r32, 0` is a valid instruction, so nothing ever showed. `cmake/Dsp56300.cmake` now emits an
unconditional jump for that mask, which is what the test means anyway.

Verified on x86-64: `g1dspcheck` passes, and `g1patchtest` still gives 261.5 Hz at −61.8 dBFS on
outputs 1 and 2 with the links carrying two channels, the same numbers as before. And verified
where it matters: [CI run 35570337853](https://github.com/animatek/G1-Emu/actions/runs/35570337853) is green on all five jobs — Linux both ways, **Linux arm64**,
**macOS** and Windows — with the DSP test gating every one of them, no `continue-on-error`
anywhere.

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
