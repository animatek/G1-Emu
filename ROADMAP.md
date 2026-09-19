# G1-Emu roadmap

What is done, what is next and the longer-term ideas. The technical detail of everything found so
far is in `NOTES.md`.

## Where we are

- The rack OS 3.03 boots on the emulated 68331, with its flash, its system clock (PIT) and the four
  DSP56303s loaded with their programs.
- It runs in real time with two virtual MIDI ports, **PC Port** (the editor) and **MIDI**, and
  JACK audio with four outputs and two inputs.
- **Animatek NME connects and builds patches**, and they **sound clean**: the DSPs run at their
  real clock and the links between them carry 9 words per sample.
- Control-rate modules work (envelopes, clocks, master/slave oscillators, chorus, overdrive).
- A 101-module battery with default settings: 56 sound clearly; most of the rest are control,
  logic, slow LFOs or sequencers without a clock, which that test cannot judge.
- `g1gui` shows the panel: display, 18 knobs and master volume, buttons and LEDs.

## Next

1. **The rest of the panel.** Still unidentified: Shift, Find, Oct Shift −/+, Assign/Morph and
   the dial (candidates: matrix bits 0.0, 0.1, 1.0, 1.1, 2.0, 2.1, 2.3–2.7). Confirm Panel Split
   (2.2) and the order of the five Oct Shift LEDs. Method: `G1_PRESS` in `g1patchtest`, from a
   screen where the button does something (see `NOTES.md`, "The panel").
2. **A test for every module** the battery cannot judge (slow LFOs, logic, sequencers with a
   clock, S&H, DrumSynth with a trigger...), with `g1patchtest`.
3. **Bigger patches** (mixers, filters, clocks, sequencers, several voices and slots). What fails
   now is in the modules or the OS, not in the transport.
4. **Compare the level with a real G1**: record the same OscA → 2Output at full master volume and
   see whether the hardware is louder (analog stage).
5. **macOS and Windows:** move audio and MIDI from ALSA/JACK to JUCE, which is already used by the
   window, and add CI builds for the three systems. Only then does a binary release make sense.
6. **Performance:** real-time with ~45% headroom on a Ryzen 7 5700X. If more is needed: skip the
   idle loop of DSPs with no voices, drop the TX side of the links (read from memory already), PGO.

## Ideas for later

### Use the emulated G1 to improve NME

With the real OS running in the emulator there is a "lab" G1 without switching on the synth:
- **Automatic test bench for NME:** uploads, edits, banks, morphs, with no hardware and no risk to
  the patches in a real G1.
- **See what the OS does with each message** (`g1boot ... diff`, `G1_WATCH`): which messages it
  accepts or ignores, when it answers with ACK or NewPatchInSlot, which reloads it triggers and how
  long they take. That lets NME pace and order its messages to what the OS really supports.
- **Reproduce hangs:** record a session that hangs the real G1 and replay it in the emulator.

### NME editing several G1s at once

The original editor could edit **up to four Nord Modulars at once** (its MIDI Setup has Ports 1–4,
each with its own In/Out). NME handles only one. With the emulator it makes more sense than ever:
edit the emulated and the real G1 side by side. This is NME work, not emulator work.

### Recreate modules from their DSP code

The OS carries the DSP code of every module (resource tables per type at `$1C3B0C`, `$1C3B24`,
`$1C3B28`; the loader `$122F72` uploads it). Reading it (oscillators, filters, envelopes, the
DrumSynth) shows each exact algorithm, which allows **native reimplementations** (C++, no
emulation) as a plugin or a VCV Rack module. **License:** the DSP code belongs to Clavia; it cannot
be copied or distributed, only studied and rewritten.

### A G1 patch as a plugin

Turn a `.pch` into a plugin that sounds like it: either the emulator with the patch loaded (needs
the user's ROM), or the patch "compiled" to native code with recreated modules (no ROM, only for
the modules that exist).

### Other G1 models

- **Nord Modular keyboard:** in principle the rack plus keys; the ROM/OS and the panel change.
- **Micro Modular:** possibly a single DSP, which would make a simpler engine. To confirm: its
  hardware and getting its OS.

### New modules inside the G1 (modified OS)

Own modules (Euclidean sequencer, additive oscillator, **MIDI out from the patch**) loaded into a
real G1 with an OS update over MIDI. It needs the module's DSP code, its entry in the OS module
tables and NME support; MIDI out also needs changes in the 68k OS. The ROM loader keeps the factory
OS and a MIDI update mode, so a bad OS can be recovered, and **the emulator is the place to test
everything before flashing the hardware.** A large, long-term project.
