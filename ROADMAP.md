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
- `g1gui` shows the panel: display, 18 knobs and master volume, the 18 buttons, the dial and the
  LEDs. Every button is identified and the dial works (it turns values on the G1's own screens).

**The short list, in order, with the detail of each: [`docs/next-steps.md`](docs/next-steps.md).**

## Next

**Product direction: standalone and VST3 share one engine.** Javier asked for one
performance MIDI destination per standalone instance (`G1Emu`, `G1Emu 2`, ...), with
MIDI and audio handed over by the DAW in VST3. The instance, state and host adapter
plan is in [Instance hosting](docs/instance-hosting.md). The emulator now takes over
its own `snd-virmidi` card and links both ports itself (`docs/bitwig-midi.md`), which
removes the helper script but not the driver's names ("Virtual Raw MIDI") nor its 16
subdevices: two endpoints called "PC Port" and "MIDI" need either a driver of the G1's
own (the 2026-09-20 attempt faulted on insertion; any new one gets validated in a VM
first) or the plugin, where no card is involved. Shared default flash files must be
addressed before multi-instance use.

1. **What the panel still owes: Assign/Morph.** It is the only key with no known effect; nothing
   changes in any screen reached so far, with or without Shift, before or after moving a knob.
   Shift, Find, Panel Split and the navigator are settled, and the Oct Shift keys belong to the
   keyboard model, which the rack's OS does not use (see `NOTES.md`, "The panel").
2. **The 29 modules the battery does not get a signal out of** (`docs/module-battery.md`): 17
   give nothing and 12 only a fixed level. The three looked at so far were the test's fault and
   not the emulator's, so the rest deserve the same one-by-one treatment — mostly a question of
   what their parameters are worth by default and what each one needs at its inputs. The tool
   takes `--only`, `--verbose` and `--param i=v` for exactly that.
3. **Bigger patches** (mixers, filters, clocks, sequencers, several voices and slots). What fails
   now is in the modules or the OS, not in the transport.
4. **Compare the level with a real G1**: record the same OscA → 2Output at full master volume and
   see whether the hardware is louder (analog stage).
5. **macOS and Windows.** **The backend is done and the CI is up**; what is left is running it on
   the two machines nobody here has. `-DG1_BACKEND=juce` builds audio and MIDI on JUCE and is the
   default off Linux, `audiobridge.h` is shared with the native path, and
   `.github/workflows/build.yml` builds Linux both ways, Linux arm64, macOS and Windows on every
   push. The JUCE backend was checked **on Linux**, where JUCE uses ALSA and can create virtual ports just as
   macOS and Windows do: it opens the card with four outputs, publishes `G1-Emu PC Port` and
   `G1-Emu MIDI`, takes a patch over the PC Port, answers the editor and sounds. That is the whole
   path, so what remains for the other two is the parts only their own systems can tell us:

   - **macOS: the illegal instruction on Apple Silicon has a cause and a fix.** It was not the
     G1's `FV` extension: the core closes a loop body with `tst lc, maxDoIterations - 1`, G1-Emu
     runs one iteration per block, and a mask of zero is not an encodable AArch64 logical
     immediate, so asmjit refused the instruction and the block was left unfinished (`NOTES.md`,
     "The DSP JIT on ARM"). The overlay emits an unconditional jump there, and [CI run 35570337853](https://github.com/animatek/G1-Emu/actions/runs/35570337853) is
     green on all five jobs with the test gating each one, the new `Linux arm64` included. What
     remains is trying CoreAudio, CoreMIDI and the virtual ports in an editor and a DAW on a real
     Mac.
   - **Windows:** the same, plus the one real unknown — whether `createNewDevice` makes a virtual
     port at all (see below). The standalone must not fail when it cannot: it should open ordinary
     MIDI ports, say so, and point at loopMIDI.
   - Both: a signed/notarised bundle, which is its own job and not this one.

   **The audio is the easy half.** JUCE 8 carries every backend we need and we already have them
   in the tree: `juce_CoreAudio_mac.cpp`, `juce_WASAPI_windows.cpp`, `juce_ASIO_windows.cpp`,
   `juce_DirectSound_windows.cpp`, `juce_ALSA_linux.cpp` and `juce_JackAudio.cpp`. One
   `AudioDeviceManager` and JUCE's device selector in the settings window. The one thing to keep
   in mind is that our own JACK client names its ports `out_1..out_4` and `in_L`/`in_R` like the
   back panel and connects itself: JUCE's JACK backend does not, so it is probably worth keeping
   the native path on Linux and using JUCE for the other two.

   **The virtual MIDI ports are the hard half**, and they are what makes an editor see a `G1-Emu`
   that is not hardware. Checked against the JUCE 8.0.12 in `../Nomad2026/JUCE`:

   | | Virtual ports | How |
   | --- | --- | --- |
   | macOS | **yes**, and nothing to install | CoreMIDI, `MIDISourceCreate` / `MIDIDestinationCreate` |
   | Linux | **yes** | ALSA sequencer, what we do now |
   | Windows | **only with Windows MIDI Services** | `MidiVirtualDeviceManager::CreateVirtualDevice` |

   On Windows `juce_Midi_windows.cpp` has three backends and only the first can create a port:
   `JUCE_USE_WINDOWS_MIDI_SERVICES`, then WinRT, then Win32 (WinMM). The flag is **off by
   default**, it needs a minimum Windows SDK, JUCE's own comment says it only worked on the Canary
   insider build of Windows 11 when it was written, and the code notes the virtual device needs a
   client plugin installed to function. That comment may well be out of date, but **it has to be
   tried on a real Windows 11 before anything is promised**. When it is not available,
   `MidiOutput::createNewDevice` returns nothing: the standalone should then open ordinary MIDI
   ports, say plainly that it could not make a virtual one, and point at loopMIDI — not fail.

   The good news is that **the raw-MIDI problem is Linux's alone**: macOS and Windows have no
   split between sequencer ports and raw devices, so the USB gadget of `docs/bitwig-midi.md` has
   no equivalent to fight elsewhere. And the plugin settles all three at once, because inside a
   VST3 the host hands over the MIDI and no virtual port is needed anywhere.
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
