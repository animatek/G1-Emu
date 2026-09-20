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

## 1. The DSP56300 JIT raises an illegal instruction on Apple Silicon

**This is the blocker.** Everything else on this list can wait; a macOS build that cannot run its
own DSP is not a macOS build.

**The symptom.** `g1dspcheck` dies with `ILLEGAL` on the `macos-latest` runner, which is arm64. The
build before it succeeds completely: 68k core, DSP cores, JUCE backend and the window all compile.
Only running fails. Because of this the macOS **test** step carries `continue-on-error` in the
workflow; the macOS **build** step does not, and must not.

**What the test does** (`tools/g1dspcheck.cpp`, 136 lines, no ROM, no sound card). Five synthetic
programs, each run twice, with JIT block sizes 1 and 32:

| | |
| --- | --- |
| `shortProgramMove` | a short `MOVEM` |
| `invalidateProgramMove` | a `MOVEM` that writes P memory and must invalidate the JIT block |
| `foreverLoop(lc = 0)` and `(lc = 7)` | `DO FOREVER`, and that it keeps `LC` |
| `nestedLoop` | nested `DO`, `ENDDO`, and that `FV` survives both |

**The suspects, in order.** The core does carry an aarch64 JIT (`jitops_agu_aarch64.cpp`,
`jitops_alu_aarch64.cpp`, `jitops_ccr_aarch64.cpp`, `jitops_decode_aarch64.cpp`,
`jitops_helper_aarch64.cpp`, `jitops_jmp_aarch64.inl` in the Gearmulator clone), so aarch64 is
meant to work. What is new here is **ours**:

- `g1Lib/dsp56300.cpp` — our implementations of `op_Movem_aa` and `op_DoForever`.
- `cmake/Dsp56300.cmake` — the patches applied to the build copy of the core, including the
  `DO FOREVER` change inside `jitblock.cpp` (`bitTest`/`jnz`/`cmp`/`jle`/`dec`) and the `FV`
  save/restore in `jitops.cpp`.

Those are written against the emitter's **shared** mnemonics, which is why they compile for both
architectures — and why one of them can be wrong on only one of them. Read the aarch64 emitter
before assuming the x86 semantics carry over; a conditional jump or a flag test that means one
thing on x86 may not mean the same there.

**How to work on it without a Mac.** Push a branch: the workflow runs on `pull_request` and on
`workflow_dispatch`, so CI is the ARM machine. Narrow it down by cutting the test:
`main()` runs the five programs in a fixed order, so the quickest bisect is to comment out four of
them and see which one raises the illegal instruction, then which of the two block sizes. Say in
the changelog which one it was — that fact alone is worth the run.

**If the JIT cannot be fixed**, the fallback is the interpreter. `G1_INTERP=mask` already makes the
chosen DSPs run interpreted in `g1Lib/g1dsp.cpp`, but `g1dspcheck` drives the core directly and
does not read it, so that needs wiring too. Nobody has measured what the interpreter costs in
speed: the emulator needs 100% of real time with four DSPs, and today it has about 40% headroom on
a Ryzen 7 5700X with the JIT. **Measure it before promising it.**

**Done looks like:** `g1dspcheck` passing on macOS in CI, and the `continue-on-error` removed from
the workflow.

---

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
