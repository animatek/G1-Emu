# G1-Emu

Emulation of the **Nord Modular G1** on the Gearmulator core: the G1's original OS running on an
emulated 68331 and emulated DSP56303s, played from **Animatek NME** (`../Nomad2026/`) as if it
were the real synth. Working name: the product has no final name yet, and it is better if it does
not carry "Nord" or "Clavia", which are trademarks.

It is a separate project from `../Elektron-Emu/` (MM Voice). They share neither build nor ROMs.

## Status

**OS 3.03 boots on the emulated 68331 and loads its programs into the 4 DSP56303s.** It answers
NME's handshake on the PC PORT, and `g1run` runs it in real time with virtual ALSA MIDI ports. With
a patch and a note **it sounds, and sounds clean**: the DSPs run at their real clock (82.944 MHz)
and the links between them carry 9 words per sample (see `NOTES.md`). The level is low because the
OS itself caps the master volume at −36 dB; `g1run` compensates. Control-rate modules (envelopes,
clocks, master oscillators, the chorus LFO) work since the JIT loop-end fix. Four outputs and two
inputs over JACK. The panel window (`g1gui`) shows the display, knobs, buttons, LEDs and the
dial; all 18 buttons are identified (see `NOTES.md`, "The panel").

## Language

**Everything in this repo is in English**: code, comments, messages, documentation, changelog and
commit messages.

## Using it

```bash
./g1.sh      # runs the emulated G1 in the console; Ctrl+C saves the flash and quits
./g1gui.sh   # the same with its panel in a window (JUCE); closing it saves the flash
```

The window (`app/gui`) shows the display, the 18 knobs and the master volume, the identified
buttons and LEDs, a status bar with speed, emulator load and CPU cores, and a **Settings** button:
the audio driver and device, the output level, whether outputs 1/2 connect themselves, which
`snd-virmidi` card is taken over, and the notice about Clavia, ROMs and support, which is shown at
startup only on the first run (no settings file yet) and lives in that window afterwards. It writes `settings.conf` in the per-user G1-Emu data directory (`%APPDATA%/Animatek/G1-Emu` on Windows, `~/.local/share/Animatek/G1-Emu` elsewhere), a
plain `key = value` file that `g1run` reads too. Order: defaults, then the file, then the `G1_*`
variables, which always win so the scripts and the test bench keep working. Only the level applies
while it plays; the rest, on the next start. JUCE comes from
`../Nomad2026/JUCE` (or `G1_JUCE_DIR`); without it only the console and the tools are built.

It creates the ALSA client **G1-Emu** with two ports, like the hardware: **PC Port** (the editor
port: in NME, choose it as input and output) and **MIDI** (the regular MIDI IN/OUT). Programs that
read raw MIDI devices and not sequencer ports (Bitwig on Linux) see neither: for them the emulator
takes over the card whose ID is `G1` by itself and links its first port to the MIDI, in both
directions. That card is a USB MIDI gadget (`dummy_hcd` + `g_midi`, stock kernel modules), **not
`snd-virmidi`**, which hard-codes sixteen subdevices per device and floods the DAW's list
(`G1_RAWMIDI`, `docs/bitwig-midi.md`). The
flash (OS + stored patches) lives beside it as `flash.bin`; if missing, it is
created with the factory OS from the ROM. **That factory OS is not the one a real G1 runs**: `os =`
in the settings file (or `G1_OS`) names an OS image to run instead, such as Clavia's 3.03b update,
which loads the synth settings at power-on (Shift+Store's slots) where the factory OS does not
(`NOTES.md`, "The official OS update"). The OS part of a loaded flash is always replaced by the
one in use.

Audio goes through JACK (pipewire-jack): client **G1-Emu** with `out_1..out_4` and `in_L`/`in_R`,
like the back panel; `out_1`/`out_2` connect themselves to the sound card (`G1_JACK_CONNECT=0`
disables that). Without JACK, or with `G1_AUDIO=alsa`/`G1_AUDIO=device`, outputs 1/2 go through
ALSA; `G1_AUDIO=no` disables audio. Also: `G1_GAIN_DB` (default +36 dB, which undoes the −36 dB cap
the OS puts on the master volume); `G1_THREADS=0` runs the DSPs serially; `G1_MODEL=expanded` (or `model = expanded` in the settings
file, the plugin's Settings and project) builds the Expanded, eight DSPs in one chain with the
outputs from DSP 7 (`Microcontroller::outputDsp()`; the tools and tests take `G1_MODEL` too); the
DSPs run on four threads whatever the model (`G1_DSP_THREADS`), two each on an Expanded; `G1_RECORD=seconds`
records a 4-channel WAV. The memory map, the loader and the findings are in `NOTES.md`. The
template is Gearmulator's Nord Lead 2X emulation (`source/nord/n2x`).

## The VST3 (issue #7, beta)

`g1plugin` builds `G1-Emu.vst3` on `g1Core` alone: no ALSA/JACK backend, no file written except
`rom =` in the settings file when the user picks a ROM by hand, and `plugin.conf` beside it: how a
new instance's window starts (size and the extras' switches), as the last editor was left; a
project's own state still has the last word. Its one MIDI port is the PC Port:
`JuceMidi` (`app/jucemidi.h`) creates `G1-Emu PC Port` (`G1-Emu 2 PC Port`... per live instance) in
the processor, which keeps it across engine swaps, and the runner's worker polls it and sends the
G1's replies (`G1_PLUGIN_PC_PORT=0` leaves it out; on Windows JUCE makes no virtual port; in CLAP,
an instance created after the last one in its process closed has none, because JUCE 8 never
recreates its MIDI endpoints after shutting down). The 18 knobs are `KnobParameter`s, named after
what they move: the host's changes reach the ADC in `processBlock`, the panel's go to the host from
a timer, a new engine sets them (`knobsFromEngine`). 128 programs are Program Changes on channel 1,
since VST3 has no Program Change as MIDI. Outputs: Out 1/2 and Out 3/4 stereo, and the same four as
mono buses Out 1..4, off until the host turns them on (#27); a channel on both gets a copy.
`g1pluginfx` builds the same sources as **G1-Emu FX** (`G1_PLUGIN_FX=1`, VST3 category Fx, CLAP
audio-effect): the only difference is that its In L/R is on from the start, for hosts that never
offer an instrument's input (#76). Both share the instance numbers and the PC Port names.
**Threads:** every instance runs its DSPs on threads. Serial for the second and later ones was tried
and dropped (2026-10-26): it halves the CPU with the default patch, but real patches need more than
a core serially and a second instance alone dropped out in Bitwig. `Engine` still takes `_threaded`;
`G1_THREADS=0/1` overrides it.
The host's transport reaches the G1 as MIDI clock (`app/plugin/hostclock.h`, #20): Start or Song
Position + Continue, $F8 at 24 per beat and Stop, merged in frame order with the track's MIDI,
whose own clock bytes are dropped while the host gives a transport. It moves the G1 only with its
MIDI clock set to external. `g1hostclocktest` tests the clock alone; `g1vst3check plugin.vst3
--clock dir` (dir: `g1patchtest ... tools/patches/ClockTest.pch --dump-packets dir`) measures
MIDIGlobal following the host at 120 and 90 BPM. The same target builds `G1-Emu.clap` when
`G1_CLAP_DIR` (default `~/src/clap-juce-extensions`, not Gearmulator's older copy) has
clap-juce-extensions; `clap-validator` passes it. The design is
`docs/instance-hosting.md`; what matters when touching it:

- **Pacing** (`app/plugin/runner.*`): a worker thread keeps the emulator `ahead` = max block + 5 ms
  (`G1_PLUGIN_MARGIN_MS`) past what the host has taken, and stops there. Events are queued at
  `consumed + latency + offset`, where `latency = ahead + guard` (0.5 ms, at least 32 frames): the
  guard covers the one-DSP-sync overshoot, so no event is ever behind the emulator and each reaches
  the G1 at the same emulated frame whatever the host's block sizes. `g1runnertest` checks that
  as bit-identical output between blocks of 256 and random blocks. The audio callback never runs
  the emulator nor locks; offline it waits for the worker. Underruns are paid back by dropping
  the late frames, so timing stays locked.
- **State:** `Engine::userState()` (runs of bytes differing from the factory flash, with a hash of
  the factory flash, which only names the OS: a state saved under another OS still loads its banks
  and settings, with the OS in use, #25), gzip + base64 in XML, plus the 256 ADC values, the
  panel's two preferences, the last Bank Select/Program Change per channel (replayed at boot),
  **what each slot holds** (`<Slots>`, issue #25), and **the synth settings in use**
  (`<SynthSettings>`, issue #46, with the slots on and the active one, #75), stored with Shift+Store or not: the processor asks the
  `SynthSettingsLink` for them every 2 s, and on a restore writes them back once the `SlotKeeper`
  has put the slots back (a write during a slot's upload breaks it), checks they read back and
  retries up to 5 times, while the track's notes are held back (at most 15 s). The factory OS never
  loads stored ones and the update loads only Shift+Store's. The panel's knob turns come back as
  part of the slots (the keeper also hears the OS's Info message for a knob), and so do the System
  menu's patch settings (read again every 2 s while the System LED is lit). A state that cannot be applied is handed back
  untouched. A new instance copies the standalone's `flash.bin` (read only).
- **Slots** (`app/slotkeeper.*`): the flash does not hold a slot's patch (sent by an editor,
  loaded from the panel or a Program Change, edited since), so the `SlotKeeper` asks the OS for
  it over the PC Port like an editor (RequestPatch, 13 GetPatch) once a slot may have changed
  (the editor's edits, the G1's own Parameter and NewPatchInSlot reports) and the port has been
  quiet for 1 s, and hides its own traffic from the editor. It never speaks while an editor's
  upload is half sent. On a restore it uploads the saved sections after the G1 boots, after the
  Program Changes. `getStateInformation` saves the slots as last read (an edit less than ~1.5 s
  old may be missing). `g1slotkeepertest` and `g1vst3check plugin.vst3 --slots` test it.
- **Engine swaps** (a project's state arriving) happen on the message thread with processing
  suspended; the editor drops its panel first (`Editor::engineGoing`).
- **Latency** goes to the host (`setLatencySamples`) only once `m_lifecycle` is released: Cubase
  and Nuendo answer a latency change, which a new buffer size is, by deactivating and reactivating
  the plugin from inside that call (#66). `g1vst3check plugin.vst3 --buffer-change` checks it.
- Known: a patch load runs slower than real time (host-port syncs), ~0.3 s of dropouts at the
  Program Change while the G1 is muted anyway.
- **The G1's MIDI OUT** (the SCI) is the plugin's MIDI output: the worker puts its bytes back
  into messages (`midioutparser.h`, no SysEx) at the host frame its sound plays at, and
  `processBlock` swaps the track's MIDI in the buffer for them (`Runner::takeMidiOut`).

## Two backends for audio and MIDI

`-DG1_BACKEND=native` (the default on Linux) is ALSA sequencer ports and the JACK graph with the
back panel's port names. `-DG1_BACKEND=juce` (the default and the only choice everywhere else) is
JUCE: CoreAudio, WASAPI/ASIO, CoreMIDI, and virtual MIDI ports through
`MidiOutput::createNewDevice`. **The JUCE one can be forced on Linux**, which is how it gets
tested without a Mac — and it is: it opens the card, creates the two virtual ports, takes a patch
on the PC Port and sounds. The rate conversion and the queues between threads are `audiobridge.h`,
shared by both, so they sound alike by construction. `.github/workflows/build.yml` builds Linux
both ways, Linux arm64, macOS and Windows on every push.

## Building and testing

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
./build/tools/g1boot Roms/NORD-MODULAR-RACK-VER-3.03.BIN 60     # 60 M instructions
./build/tools/g1boot Roms/NORD-MODULAR-RACK-VER-3.03.BIN dis C800 C900   # disassemble
```

`g1boot` reports where the PC is, which loop it is stuck in, the programmed chip-selects and every
access to hardware that is not emulated yet. Note: the OS runs from RAM (`$100000`), copied from
the ROM at `$C800`, so RAM address X is at ROM offset `X - $100000 + $C800`.

`g1patchtest` (needs `../Nomad2026`) uploads a `.pch` exactly like NME, plays a note and measures
the four outputs and the links between DSPs; it also has probes for the panel (see `NOTES.md`).

| Path | What it is |
| --- | --- |
| `g1Lib/g1mc.*` | The CPU (68331) with its memory map: ROM, RAM, flash and the panel. |
| `g1Lib/g1flash.h` | The AMD Am29F080 flash at `$300000`. |
| `g1Lib/g1duart.h` | The PC PORT: SCN2681 DUART on a parallel bus (GP port + port E). |
| `g1Lib/g1dsp.*` | A DSP56303 with its HI08 boot ROM, attached to the CPU host port. |
| `g1Lib/g1lcd.h` | The display (HD44780). |
| `g1Lib/g1thread.*` | Precise sleeps (Windows' `sleep_for` takes 15.6 ms) and keeping the emulator's threads out of EcoQoS (#63, #65). |
| `g1Lib/g1knobs.h` | What each panel knob is assigned to (module, parameter, names, value), read from the OS's tables. |
| `cmake/Dsp56300.cmake` | DSP core fixes, applied to a build copy of upstream's DSP56300 core (`G1_DSP56300_DIR`, default `~/src/dsp56300-upstream`; the 68k core still comes from `GEARMULATOR_DIR`): the ESSI clock keeping time in busy patches and never serving the link receivers (`Dsp::writeLinkRing` fills their ring), DMA and interrupt fixes, `x:aa` cycles, and a JIT failure that returns instead of recursing (#17). `tools/overlaycheck.py` says which patches apply to a given core tree. |
| `cmake/Mc68k.cmake` | 68331 timer (GPT) fixes: the prescaler and compares that match once (#22), applied to a build copy of `gpt.cpp`. |
| `app/engine.*` | `Engine`: the G1 with nothing around it (ROM in, factory OS in the flash), and its user state as a difference against the factory flash, which never carries the OS. |
| `app/hostlog.*`, `app/diaglog.*` | The shared log file; the `system:` line and the `health` line (output and link peaks, link misses) written a moment after MIDI comes in (#65). |
| `app/hostconfig.*` | What every front end shares: the per-user directory, the settings file (`HostOptions`) and the status figures (`HostStats`). |
| `app/emuhost.*` | The running G1 (flash, MIDI, audio, real time) on its own thread; used by `g1run` and `g1gui`. |
| `app/g1run.cpp`, `g1.sh` | The console front end. |
| `app/gui/`, `g1gui.sh` | `g1gui`: the window with the panel. |
| `app/gui/Settings.*` | The settings window: ROM, audio driver and device, level, raw MIDI card. |
| `app/plugin/` | The VST3 (`g1plugin`): `Processor`, `Editor` (the same `Panel`, through `PanelHost`), and `runner.*`, which paces the engine by the host's blocks. `g1runnertest` tests the runner without a DAW. |
| `app/slotkeeper.*` | `SlotKeeper`: what each slot holds, read from the OS over the PC Port and uploaded back after a restore (#25). `g1slotkeepertest` tests it against an emulated G1. |
| `app/romfinder.*`, `g1Lib/g1rom.h` | Where the ROM comes from, and whether a file is the right one. |
| `app/audiobridge.h` | Rate conversion and the lock-free queues between the emulator and the card. |
| `app/miditransport.*` | `MidiTransport`, the interface every way of carrying the G1's MIDI bytes implements, and `makeMidiTransport()`, the one place that picks which. `EmuHost` knows none of them by name. |
| `app/alsamidi.h`, `app/alsaaudio.h`, `app/jackaudio.h` | The native Linux backend: ALSA MIDI (a `MidiTransport`), ALSA audio, JACK audio. |
| `app/juceaudio.h`, `app/jucemidi.h` | The JUCE backend: every system, and the only one off Linux. `JuceMidi` is a `MidiTransport`. |
| `app/windowsmidi.*` | The experimental Windows MIDI Services `MidiTransport`, opt-in. |
| `packaging/windows/G1-Emu.iss` | The Windows installer (Inno Setup 6), built by CI from the same files as the zip: replaces the previous version and removes hand-made copies of the plugins. |
| `tools/g1boot.cpp` | Headless boot and disassembler. |
| `tools/patchtest/` | `g1patchtest`: the test bench. |
| `tools/battery/` | `battery.py`: one patch per module type, played and measured (`docs/module-battery.md`). |
| `tools/vst3check/` | `g1vst3check`: loads the built `.vst3` through JUCE's VST3 host, two instances at once, and checks sound, state, editor, that each PC Port answers NME's greeting on its own port, the knobs as parameters and the host's program.; `--fx dir` checks G1-Emu FX's input through AudioIn. |
| `tools/dspdis.cpp` | DSP56300 disassembler (hex words on stdin). |
| `tools/jitdiff.cpp` | Runs DSP instructions on the JIT and on the interpreter (or as one block against one per block) from random states and prints what differs. |

**The plan and the pending ideas are in `ROADMAP.md`.**

## The ROM

G1-Emu ships no ROM and never will, so finding one is part of the program. `app/romfinder.*` looks,
in order, at the path on the command line (an order: if it does not serve, it stops and says so),
`rom = ...` in the settings file, `<Documents>/Animatek/G1-Emu/roms`, `roms/` next to the flash, and
`Roms/` in the current directory and in the source tree — which is why a clone works with no setup.
`g1Lib/g1rom.h` says whether a file serves and, when it does not, why: not 512 KB, no Nord Modular
OS inside, or the keyboard model's instead of the rack's (the model byte at `$7FF`, which the OS
itself reads). With none found, the window offers to open the ROM folder or to pick a file, and
starts as soon as it has one; the console prints where to put it. `G1_ROM` overrides everything.

## Rules

- **ROMs never go into the repo** nor into a release. `Roms/` is ignored by Git: the rack OS 3.03,
  the official updater and the Mac editor.
- **License:** linking Gearmulator makes it GPLv3. NME only speaks MIDI and is a separate program.
- Before touching the real synth from here, check which device NME is connected to.

## Changelog rule

**Every change that goes into the repo gets its line in `CHANGELOG.md`, in the same commit.** No
exceptions: code, documentation, tools, small fixes. Newest first, under the date, with who did
it, what changes and how it was verified. No hash needed: the entry lands in the same commit as the
change. Anything left uncommitted is marked "local change, not committed". The repo is public: the
changelog is what people read.

## Maintainer's workspace changelog

In the maintainer's workspace, every change also goes into the global changelog
`/mnt/SPEED/CODE/CHANGELOG.md` (rule in `/mnt/SPEED/CODE/AGENTS.md`, section Global Changelog; that
one is written in Spanish). It is a link to an Obsidian note: edit its target, never replace it.

**Write entries with `cambios apuntar "<project>" "<what changed, verification, commit>" -a <agent>`**, not by editing the file: it files them under the date and project without rewriting anything or breaking the symlink (`cambios proyectos` lists the names; if the link is already broken, `cambios reparar`). Never `sed -i`, `>` or a whole-file write on it: that is how the symlink broke three times.
