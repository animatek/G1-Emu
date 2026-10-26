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

**From 2026-09-30 the open work is tracked as GitHub issues**, so nothing lives only in
someone's memory. Most urgent: **#6**, fast audio-rate modulation that crackles and does not
recover until a restart. Then the plugin and the first public beta (#7), the direct link with NME
(#8, with Animatek-NME#83), several instances and the original editor with four synths (#9,
Animatek-NME#84), and the window: a report button (#10), no Oct Shift (#11), an extras drawer with
Random (#12), parameter names above the knobs (#13) and mappable knobs (#14).

## Release plan (from 2026-10-07)

Decided with Javier on 2026-10-07. Each line is a target, not a promise: a release goes out when
what it carries works.

- **alpha.13 (2026-10-07):** Mike Fiction's panel, Presets (a page that says "in development") and
  Settings keys right of slot D, About, the host's tempo as MIDI clock (#20), mono outputs (#27),
  projects across OSes (#25), the direct link with NME (#8).
- **alpha.14: Presets.** The Presets page lists the synth's banks and programs, read from the OS
  over the PC Port the way Animatek NME reads them (PatchHandling `$17`, GetPatchList `$41 $14`,
  section and position; the reply's names, as `PatchListResponseMessage` decodes them in NME's
  `source/midi/NmMessages.cpp`), and loads one into the slot with a click, as Gearmulator's
  plugins do. It talks to the OS like `SynthSettingsLink`, hiding its traffic from the editor.
  Also the **first test of the expanded G1** (below): does the OS boot with eight DSPs?
- **alpha.15: two models, picked in the settings: Regular and Expanded.** Not every patch needs
  the voices, and a modest machine may not afford eight DSPs.
  - **Expanded** (eight DSPs, the 32-voice expansion board; tracked in #39, asked for in #24 and #39). What is known
    (`NOTES.md`, "Booting the DSPs"): DSPs 4-7 sit on HI08 ports `$200020`-`$20003F`; the loader
    sends its program to DSPs 3 and 7, and the OS goes to eight DSPs when DSP 7 raises HF2; the OS
    carries the programs for all of them. The first test: create DSPs 4-7, let DSP 7 answer, and
    see whether the OS boots them. **Done (2026-10-18):** it boots, one chain 0-7 with the codec on
    DSP 7, twice the voices; picked in Settings (standalone and plugin, saved with the project).
    The cost: about twice the CPU (2.7x real time offline against 4.7x here), on four threads.
  - **In Animatek NME:** the editor tells them apart when it connects (to be found: what the OS
    says about its DSPs, in the IAm reply or the synth settings) and shows which one each G1-Emu
    is, with an icon or similar.
  - **The Micro Modular (one DSP) is a model after all** (changed 2026-10-26, issue #82): its
    OS (`Roms/Micro/micro-os.bin`, from Clavia's public Micro updater) boots on the rack's boot ROM
    and loads its program into one DSP, so it is a third setting beside Regular and Expanded
    (`G1_MODEL=micro`), not a separate emulation. Cheap for the CPU, which is what the issue asks
    for. In progress on branch `micro-1dsp`; see `NOTES.md`, "Micro Modular in G1-Emu". It keeps
    to the 2026-10-07 rule of no hardware dumps. AG1TEK (one slot with the editor inside) stays a
    separate, paused idea.
  - **No hardware dumps** (decided 2026-10-07): nobody opens a synth to read its ROM, neither
    Javier's G1 nor a borrowed Micro. What only the ROM holds (the vectors, the start-up and the
    small loader that boots the DSPs) is written as G1-Emu's own boot instead, and the OS comes from
    Clavia's public updaters (#16). Checked against the rack ROM we have: both boots must behave
    the same. That boot is also the way to the Micro, with the Micro's updater.
- **alpha.16-23 (to 2026-10-19):** the numbers ran past the plan; the beta did not come with
  alpha.16. Out since: the Expanded (alpha.22, #39), G1-Emu FX (#76), projects that bring back
  their slots (#75), the fast boot (#78) and Octave Shift Enabled / Disabled (#77, alpha.23).
- **alpha.24: the Expanded holds** (decided with Javier on 2026-10-19). Heavy patches on the
  Expanded still drop out on some machines; the fix (`g1mc`/`g1thread`, on the `expanded-39`
  branch, not committed) goes in once Javier has heard it hold in Bitwig. With it both models
  hold, as the beta asks.
- **The first beta (beta 1), when Windows is clean**: the first release meant for everyone, and
  most of them are on Windows. Before it:
  - **#53** fixed: on Windows the plugin gets no notes from the DAW while NME connects.
  - **#59 and #65** ("no audio", never reproduced here) explained or closed: ask for logs now.
  - **#48** (Store) and **#64** (the Performance in the DAW) fixed or written in the known issues.
  - The known issues in the release notes solved or explained, the README and the notes written
    for a new user, and signing looked at again.
  - **Not before it any more:** Animatek NME telling the models apart, and the boot without a ROM
    (#16). Both after the beta.
  - **Plan B:** if the Expanded's dropouts drag on, the beta goes out with the Expanded marked
    *experimental* in Settings and in the notes, rather than waiting for it.
- **Without a ROM (#16), no date:** a goal for around the beta, not tied to a release. G1-Emu's
  own boot with the OS from Clavia's public updater would spare a new user the hardest step, finding
  a ROM. It goes slowly and is done when it is done.

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
5. **macOS and Windows.** **The backend is done, the CI is up and both have run on real
   machines.** `-DG1_BACKEND=juce` builds audio and MIDI on JUCE and is the
   default off Linux, `audiobridge.h` is shared with the native path, and
   `.github/workflows/build.yml` builds Linux both ways, Linux arm64, macOS and Windows on every
   push. The JUCE backend was first checked **on Linux**, where JUCE uses ALSA and can create virtual ports just as
   macOS and Windows do: it opens the card with four outputs, publishes `G1-Emu PC Port` and
   `G1-Emu MIDI`, takes a patch over the PC Port, answers the editor and sounds. That is the whole
   path. Real-machine tests have now covered those system-specific parts:

   - **macOS: the illegal instruction on Apple Silicon has a cause and a fix.** It was not the
     G1's `FV` extension: the core closes a loop body with `tst lc, maxDoIterations - 1`, G1-Emu
     runs one iteration per block, and a mask of zero is not an encodable AArch64 logical
     immediate, so asmjit refused the instruction and the block was left unfinished (`NOTES.md`,
     "The DSP JIT on ARM"). The overlay emits an unconditional jump there, and [CI run 35570337853](https://github.com/animatek/G1-Emu/actions/runs/35570337853) is
     green on all five jobs with the test gating each one, the new `Linux arm64` included.
     Javier has now also run the macOS application and confirmed the CoreAudio/CoreMIDI path with
     the editor on a real Mac.
   - **Windows: tried on a real Windows 11 machine, and blocked by a Microsoft bug, not ours.**
     The native build runs (audio opens over WASAPI, the DSP test passes), and an opt-in
     `G1_WINDOWS_MIDI_SERVICES` backend (`app/windowsmidi.cpp`, gated behind the preview SDK,
     see `docs/windows-build.md`) reaches Windows MIDI Services endpoint creation. But on this
     machine's build — Windows 11 25H2, build 26200.9457, in-box MIDI component 26100.8875 — the
     owned endpoint is never projected as MIDI 1.0 ports, and teardown leaves the shared MIDI
     service stuck. That matches Microsoft/MIDI issue #1047, fixed only in the November 2026
     Windows release: nothing left to fix here until that update ships. The default is still
     that G1-Emu publishes owned ports like the hardware does, with no silent fallback
     (`docs/next-steps.md`); but with the fix two months out, Settings now also has an opt-in
     manual MIDI device pairing patch (`docs/windows-build.md`, "Manual MIDI device pairing"),
     picking up the loopMIDI fallback first sketched below after all, until Windows MIDI
     Services can own ports on its own. Verified working end to end on this machine with separate
     `G1→NME` and `NME→G1` loopMIDI ports and Animatek NME completing its handshake.
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

### A macOS installer

Like the Windows one (`packaging/windows/G1-Emu.iss`): a `.pkg` built by CI's macOS job with
Apple's `pkgbuild`/`productbuild`, putting the app in `/Applications` and the plugins in
`/Library/Audio/Plug-Ins/VST3` and `/CLAP`, with a postinstall script that removes old copies from
`~/Library/Audio/Plug-Ins` so a DAW never finds two. The gain is larger than on Windows: what a
`.pkg` installs is not quarantined, so the README's `xattr` commands would go. Unsigned, the `.pkg`
itself still needs **System Settings → Privacy & Security → Open Anyway** once (signing and
notarising cost an Apple Developer account). Needs a Mac to test it on before it ships.

### Use the emulated G1 to improve NME

With the real OS running in the emulator there is a "lab" G1 without switching on the synth:
- **Automatic test bench for NME:** uploads, edits, banks, morphs, with no hardware and no risk to
  the patches in a real G1.
- **See what the OS does with each message** (`g1boot ... diff`, `G1_WATCH`): which messages it
  accepts or ignores, when it answers with ACK or NewPatchInSlot, which reloads it triggers and how
  long they take. That lets NME pace and order its messages to what the OS really supports.
- **Reproduce hangs:** record a session that hangs the real G1 and replay it in the emulator.

### A direct link between NME and the emulator

Beside the MIDI ports, never instead of them: the **PC Port** stays for the original Clavia
editor and any other editor. NME, which is ours, could also reach the emulator over a local
socket or named pipe (`juce::InterprocessConnection`) and carry the same SysEx. That would need no
virtual MIDI port or driver, so it would also work inside a VST3 and on Windows. It would let NME
pick which instance to edit. It plays the part of the G2's USB cable, but in software.
Emulating a real USB device is not worth it: it needs a kernel driver on every system, and the G1
never had USB. NME's transport is narrow (`source/midi/MidiDeviceManager.*`: connect, disconnect,
send SysEx, incoming to `NmProtocol`), so a second transport is small work on both sides.

### Browse nordmodular.com patches from NME

The nordmodular.com site, where people upload their own patches, is a great resource. It would be
good to ask its maintainer whether they could offer an API or connector, so that NME could log in
and browse both other users' patches and the user's own, and load them straight into the emulator
or a real G1. Only an idea for now, and it depends on someone else. It is NME work, not emulator
work.

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
- **Micro Modular:** one DSP, confirmed in its OS (`NOTES.md`, "The official OS update"), which
  makes a simpler engine with no links between DSPs. Open: its boot ROM (not in the update, so it
  needs the same boot replacement as #16) and the ESSI clock towards its codec.
  Now a G1-Emu model (2026-10-26): the Micro OS boots on the rack's boot ROM, see above.

### New modules inside the G1 (modified OS)

Own modules (Euclidean sequencer, additive oscillator, **MIDI out from the patch**) loaded into a
real G1 with an OS update over MIDI. It needs the module's DSP code, its entry in the OS module
tables and NME support; MIDI out also needs changes in the 68k OS. The ROM loader keeps the factory
OS and a MIDI update mode, so a bad OS can be recovered, and **the emulator is the place to test
everything before flashing the hardware.** A large, long-term project.
