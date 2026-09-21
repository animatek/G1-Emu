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
startup only on the first run (no settings file yet) and lives in that window afterwards. It writes `~/.local/share/Animatek/G1-Emu/settings.conf`, a
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
flash (OS + stored patches) lives in `~/.local/share/Animatek/G1-Emu/flash.bin`; if missing, it is
created with the factory OS from the ROM.

Audio goes through JACK (pipewire-jack): client **G1-Emu** with `out_1..out_4` and `in_L`/`in_R`,
like the back panel; `out_1`/`out_2` connect themselves to the sound card (`G1_JACK_CONNECT=0`
disables that). Without JACK, or with `G1_AUDIO=alsa`/`G1_AUDIO=device`, outputs 1/2 go through
ALSA; `G1_AUDIO=no` disables audio. Also: `G1_GAIN_DB` (default +36 dB, which undoes the −36 dB cap
the OS puts on the master volume); `G1_THREADS=0` runs the DSPs serially; `G1_RECORD=seconds`
records a 4-channel WAV. The memory map, the loader and the findings are in `NOTES.md`. The
template is Gearmulator's Nord Lead 2X emulation (`source/nord/n2x`).

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
| `cmake/Dsp56300.cmake`, `g1Lib/dsp56300.cpp` | DSP core fixes (JIT and DMA), applied to a build copy. |
| `app/emuhost.*` | The running G1 (flash, MIDI, audio, real time) on its own thread; used by `g1run` and `g1gui`. |
| `app/g1run.cpp`, `g1.sh` | The console front end. |
| `app/gui/`, `g1gui.sh` | `g1gui`: the window with the panel. |
| `app/gui/Settings.*` | The settings window: ROM, audio driver and device, level, raw MIDI card. |
| `app/romfinder.*`, `g1Lib/g1rom.h` | Where the ROM comes from, and whether a file is the right one. |
| `app/audiobridge.h` | Rate conversion and the lock-free queues between the emulator and the card. |
| `app/alsamidi.h`, `app/alsaaudio.h`, `app/jackaudio.h` | The native Linux backend: ALSA MIDI, ALSA audio, JACK audio. |
| `app/juceaudio.h`, `app/jucemidi.h` | The JUCE backend: every system, and the only one off Linux. |
| `tools/g1boot.cpp` | Headless boot and disassembler. |
| `tools/patchtest/` | `g1patchtest`: the test bench. |
| `tools/battery/` | `battery.py`: one patch per module type, played and measured (`docs/module-battery.md`). |
| `tools/dspdis.cpp` | DSP56300 disassembler (hex words on stdin). |

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
