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
inputs over JACK. The panel window (`g1gui`) shows the display, knobs, buttons and LEDs; a few
buttons are still unidentified (see `ROADMAP.md`).

## Language

**Everything in this repo is in English**: code, comments, messages, documentation, changelog and
commit messages.

## Using it

```bash
./g1.sh      # runs the emulated G1 in the console; Ctrl+C saves the flash and quits
./g1gui.sh   # the same with its panel in a window (JUCE); closing it saves the flash
```

The window (`app/gui`) shows the display, the 18 knobs and the master volume, the identified
buttons and LEDs, and a status bar with speed, emulator load and CPU cores. JUCE comes from
`../Nomad2026/JUCE` (or `G1_JUCE_DIR`); without it only the console and the tools are built.

It creates the ALSA client **G1-Emu** with two ports, like the hardware: **PC Port** (the editor
port: in NME, choose it as input and output) and **MIDI** (the regular MIDI IN/OUT). The flash (OS
+ stored patches) lives in `~/.local/share/Animatek/G1-Emu/flash.bin`; if missing, it is created
with the factory OS from the ROM.

Audio goes through JACK (pipewire-jack): client **G1-Emu** with `out_1..out_4` and `in_L`/`in_R`,
like the back panel; `out_1`/`out_2` connect themselves to the sound card (`G1_JACK_CONNECT=0`
disables that). Without JACK, or with `G1_AUDIO=alsa`/`G1_AUDIO=device`, outputs 1/2 go through
ALSA; `G1_AUDIO=no` disables audio. Also: `G1_GAIN_DB` (default +36 dB, which undoes the −36 dB cap
the OS puts on the master volume); `G1_THREADS=0` runs the DSPs serially; `G1_RECORD=seconds`
records a 4-channel WAV. The memory map, the loader and the findings are in `NOTES.md`. The
template is Gearmulator's Nord Lead 2X emulation (`source/nord/n2x`).

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
| `app/alsamidi.h`, `app/alsaaudio.h`, `app/jackaudio.h` | ALSA MIDI, ALSA audio and JACK audio (4 outputs, 2 inputs). |
| `tools/g1boot.cpp` | Headless boot and disassembler. |
| `tools/patchtest/` | `g1patchtest`: the test bench. |
| `tools/dspdis.cpp` | DSP56300 disassembler (hex words on stdin). |

**The plan and the pending ideas are in `ROADMAP.md`.**

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
