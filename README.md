# G1-Emu

![G1-Emu: the panel of the emulated Nord Modular G1](docs/g1gui.png)

**An emulator of the Nord Modular G1** (rack, OS 3.03) built on the
[Gearmulator](https://github.com/dsp56300/gearmulator) core: the hardware's original operating
system runs on an emulated Motorola 68331 and four emulated DSP56303s, and
[Animatek NME](https://github.com/animatek/Animatek-NME) edits it as if it were the real synth.

**Status: pre-alpha.** The OS boots, NME connects over the PC Port and uploads patches, and it
sounds clean in real time: oscillators, filters, envelopes, clocks, effects (chorus, overdrive…),
four outputs and two inputs. It has a window with the panel (display, knobs, buttons and LEDs).
Still missing: a few panel buttons, the dial, module-by-module testing and builds for macOS and
Windows. The technical details are in [`NOTES.md`](NOTES.md), the plan in
[`ROADMAP.md`](ROADMAP.md) and what changes in [`CHANGELOG.md`](CHANGELOG.md). Contributions are
welcome: see [`CONTRIBUTING.md`](CONTRIBUTING.md).

## You are invited: this is a collaborative project

G1-Emu was started by [Animatek](https://animatek.net), but it is a big project and it is meant to
be built together. **Everyone is warmly invited to take part** — reverse engineering, C++, DSP
code, testing patches against a real G1, recordings, documentation, builds for macOS and Windows,
or simply reporting what sounds wrong. We love collaboration, and every person who joins makes the
emulator bigger and better. There is room for all skill levels: see
[`CONTRIBUTING.md`](CONTRIBUTING.md) and the open tasks in [`ROADMAP.md`](ROADMAP.md), and say
hello in an issue.

## Please read this first

- **Not affiliated with Clavia.** G1-Emu is an independent, open-source project. It is not
  affiliated with, endorsed by or connected to Clavia DMI in any way. "Nord" and "Nord Modular" are
  trademarks of Clavia DMI; they are used here only to say which instrument is emulated.
- **No ROMs, now or ever.** No ROM or firmware is included, and none will be provided. Please do
  not ask for them in issues, e-mails or messages: you will not find them here. You need the 512 KB
  ROM of a Nord Modular rack with OS 3.03 (`Roms/NORD-MODULAR-RACK-VER-3.03.BIN`), dumped from
  your own unit. `Roms/` is ignored by Git and must stay that way.
- **No support.** This is a pre-alpha community project, made in spare time by its maintainer and
  whoever wants to join. There is no support: please do not ask for help, builds or ROMs. Bug
  reports with details, and contributions, are welcome (see [`CONTRIBUTING.md`](CONTRIBUTING.md)).

The window shows this same notice when it opens, until you tick "Don't show this again".

## Building

Linux (tested on Arch/CachyOS with PipeWire). You need:

- A clone of [gearmulator-md-mm](https://github.com/joelanders/gearmulator-md-mm) in
  `~/src/gearmulator-md-mm` (or `-DGEARMULATOR_DIR=...`). It is not modified: the core fixes the G1
  needs are applied to a copy at build time (`cmake/Dsp56300.cmake`).
- ALSA, and JACK (pipewire-jack) for the four outputs and the inputs.
- JUCE for the window and the test bench: by default the one inside Animatek NME next to this repo
  (`../Nomad2026/JUCE`), or `-DG1_JUCE_DIR=...`. Without JUCE only the console version is built.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Using it

```bash
./g1gui.sh   # with the panel in a window
./g1.sh      # in the console (Ctrl+C saves and quits)
```

- **MIDI** (ALSA): client **G1-Emu** with two ports, like the hardware: **PC Port** (the editor:
  choose it in NME as input and output) and **MIDI** (notes and controllers, e.g. from a DAW).
- **Audio** (JACK): `G1-Emu:out_1..out_4` and `in_L`/`in_R`; `out_1`/`out_2` connect themselves to
  the sound card. Without JACK, outputs 1/2 go through ALSA.
- The flash (installed OS and stored patches) is saved in `~/.local/share/Animatek/G1-Emu/flash.bin`.
- The level is low because the OS itself caps the master volume at −36 dB; it is compensated with
  +36 dB (`G1_GAIN_DB`). More settings in [`CLAUDE.md`](CLAUDE.md).

## License and credits

GPLv3 (see [`LICENSE`](LICENSE)), because it links Gearmulator. Thanks to The Usual Suspects for
[Gearmulator](https://github.com/dsp56300/gearmulator) and to joelanders for the fork with the
Monomachine and Machinedrum, which this work builds on. Made by Animatek
([animatek.net](https://animatek.net)).
