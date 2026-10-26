# G1-Emu

![G1-Emu: the panel of the emulated Nord Modular G1, with a patch loaded](docs/g1-emu-panel.png)

**An emulator of the Nord Modular G1** (rack, OS 3.03) built on the
[Gearmulator](https://github.com/dsp56300/gearmulator) core: the hardware's original operating
system runs on an emulated Motorola 68331 and four emulated DSP56303s, and
[Animatek NME](https://github.com/animatek/Animatek-NME) edits it as if it were the real synth.

**Status: alpha.** The OS boots, editors connect over the PC Port and upload patches, and it
sounds clean in real time: oscillators, filters, envelopes, clocks, effects (chorus, overdrive…),
four outputs and two inputs. It has a window with the panel (display, knobs, buttons and LEDs), and
it is also a **VST3 and CLAP plugin**. **Runs on Linux, macOS and Windows**, tested on real
machines and in several DAWs. Builds in [Releases](https://github.com/animatek/G1-Emu/releases)
are free, unsigned and contain no ROM ([release notes](docs/release-notes.md)): **to install one,
see [Getting started](#getting-started)**, and for common questions the [FAQ](#faq). Still
missing: module-by-module testing against a real G1. The technical details are in
[`NOTES.md`](NOTES.md), the plan in
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

The window shows this same notice the first time it runs. After that it stays out of the way: it
is always readable in **Settings**, which is also where to put it back at startup.

## Getting started

**1. Download** the archive for your system from
[Releases](https://github.com/animatek/G1-Emu/releases) (the newest is at the top):

| System | Archive |
| --- | --- |
| macOS (Apple Silicon and Intel) | `G1-Emu-macos-universal.tar.gz` |
| Windows | `G1-Emu-windows-x86_64-setup.exe`, the installer (from alpha.20), or `G1-Emu-windows-x86_64.zip` |
| Linux | `G1-Emu-linux-x86_64-native.tar.gz` (or `-juce`); Raspberry Pi and other arm64: `G1-Emu-linux-arm64-native.tar.gz` |

Each one has the **standalone** (`G1-Emu`, `G1-Emu.app` or `G1-Emu.exe`: the synth in its own
window) and the **plugin** (`G1-Emu.vst3` and `G1-Emu.clap`: the synth inside your DAW). Use
either, or both.

**On Windows, the installer does steps 1 and 2 for you**: run `G1-Emu-windows-x86_64-setup.exe`,
choose what to install (the standalone, the VST3, the CLAP) and press Next until it is done. It
puts the plugins where every DAW looks, **replaces the previous version**, and removes old copies
you put by hand in `AppData\Local\Programs\Common` (a DAW that finds two may load the old one).
Your ROM, banks and settings are not touched, also not when you uninstall it (Settings → Apps). The
zip is still there for those who prefer copying by hand.

**2. Install the plugin**, if you want it, by copying it to your plugin folder, and let the DAW
rescan:

| System | VST3 | CLAP |
| --- | --- | --- |
| macOS | `~/Library/Audio/Plug-Ins/VST3` | `~/Library/Audio/Plug-Ins/CLAP` |
| Windows | `C:\Program Files\Common Files\VST3` | `C:\Program Files\Common Files\CLAP` |
| Linux | `~/.vst3` | `~/.clap` |

Nothing is signed yet. **On macOS**, if the app will not open or the DAW does not list the
plugin, run in Terminal (then rescan):

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/G1-Emu.vst3
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/CLAP/G1-Emu.clap
xattr -dr com.apple.quarantine /path/to/G1-Emu.app
```

**On Windows**, SmartScreen may warn: choose **More info** and **Run anyway**. Read the
[Windows first-run guide](WINDOWS.md) too.

**3. The first start** asks for the ROM (see [Please read this first](#please-read-this-first)
and [Using it](#using-it)). Then the synth runs, **with empty slots**: a G1 needs patches, and
they do not come with it (see [the next section](#two-things-a-nord-modular-needs-and-neither-comes-in-the-box)).

**4. Load sounds.** Three ways, from the easiest:

- **The Presets page**, in the standalone and the plugin: press **Presets** (right of slot D). It
  lists the synth's banks 1 to 9, and **a click loads a patch** into the active slot. **Load
  .pch...** sends a patch file from your disk to the synth, with no editor: stored in a bank and
  position you pick, or **Load only** into the slot. Press Presets again, or Escape, to go back.
- **An editor**, to load, make and edit patches: [Animatek NME](https://github.com/animatek/Animatek-NME)
  0.21 or later **finds every running G1-Emu by itself** (window or plugin, on every system, with
  no MIDI port to set up: the direct link). Other editors connect through the PC Port (see
  [Using it](#using-it) and, on Windows, [`WINDOWS.md`](WINDOWS.md)).
- **Program Change** from a MIDI track (and Bank Select), or **Patch/Load** and the dial on the
  panel, as on the hardware: they choose among the patches already stored in the banks.

**5. Play it** from your DAW track or a MIDI keyboard. Slot A listens on MIDI channel 1 (B on 2,
C on 3, D on 4) unless the synth settings say otherwise.

**6. Check that it works, with the two test patches.** If something does not sound, this is also
the first thing to try, and what to tell us in an issue. Download them (they are not in the
release archives):
[**Test Audio.pch**](https://github.com/animatek/G1-Emu/raw/main/test-patches/Test%20Audio.pch) and
[**Test MIDI.pch**](https://github.com/animatek/G1-Emu/raw/main/test-patches/Test%20MIDI.pch)
(more in [`test-patches/`](test-patches/)).

1. **Turn your speakers down first**: the test tone is loud. Select **slot A** on the panel.
2. **Presets → Load .pch...**, choose `Test Audio.pch` and **Load only**.
3. **Without playing any note**, you should hear a steady tone, and the status bar's
   `output 1/2` should read a level, not `silence`. In a DAW, check its meter too.
   - Silent? Then the problem is the audio, not MIDI or the patch: press **Report issue** and
     attach `g1-emu.log` (it opens the log's folder for you).
4. Now load `Test MIDI.pch` the same way. It is silent until you **play a note** on MIDI
   channel 1: then it sounds while the note is held.
   - The `MIDI in` counter in the status bar should count up and the **MIDI LED** flash with
     each note. If they do not, the notes are not reaching the synth: arm the track or turn its
     monitoring on in your DAW.
   - They do, but no sound? Then slot A is not listening on that channel: check its MIDI channel
     in the synth settings.

If both sound, the emulator works: any silence left is in a patch or your routing. For a patch
that does not sound, or sounds strange, see
[the FAQ](#some-patches-do-not-sound-or-sound-strange).

## Building

Linux (tested on Arch/CachyOS with PipeWire). You need:

- A clone of [dsp56300](https://github.com/dsp56300/dsp56300), the DSP56300 core, with its
  submodule (`git clone --recurse-submodules`), in `~/src/dsp56300-upstream` (or
  `-DG1_DSP56300_DIR=...`). CI pins the commit it tests (`.github/workflows/build.yml`).
- A clone of [gearmulator-md-mm](https://github.com/joelanders/gearmulator-md-mm) in
  `~/src/gearmulator-md-mm` (or `-DGEARMULATOR_DIR=...`), for the 68k core. The version tested in
  CI is **`mdmm-v0.1.0-alpha.13`**.
- Neither clone is modified: the core fixes the G1 needs are applied to a copy at build time
  (`cmake/Dsp56300.cmake`, `cmake/Mc68k.cmake`).
- ALSA, and JACK (pipewire-jack) for the four outputs and the inputs.
- JUCE for the window and the test bench: by default the one inside Animatek NME next to this repo
  (`../Nomad2026/JUCE`), or `-DG1_JUCE_DIR=...`. Without JUCE only the console version is built.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Two things a Nord Modular needs, and neither comes in the box

**A G1 makes no sound on its own, and it starts empty.** It is a modular: a patch is what turns it
into an instrument, and without one there is nothing to hear. Real hardware left the factory with
a bank of patches in its flash; G1-Emu cannot give you those, because the ROM carries the
operating system and nothing else — the first time it runs, its flash is built from the OS alone
and every slot says `Empty patch`. **The patches are yours to find or to make.** There are
thousands of `.pch` files out there from twenty-five years of the Nord Modular community: the old
Clavia patch libraries, the Electro-Music archives, and whatever a search turns up. Put them
wherever you like and open them from your editor.

**And to make or load a patch you need an editor.** The G1's own panel edits parameters, not
patches: on the real instrument the patch comes down the PC Port from a computer, and the emulator
is no different. **Any editor that speaks the G1's protocol works** — G1-Emu emulates the
instrument, not one particular editor, and there is nothing in it that prefers one over another:

- **[Animatek NME](https://github.com/animatek/Animatek-NME)** — a modern editor, open source, and
  the one this emulator is developed against, which only means it is the one that gets tested
  first.
- **The original Clavia editor v3.03** — the real thing. It still works, and
  [Stage Engine](https://www.stage-engine.com/) packages it for current macOS with the Wine parts
  bundled in, free with an optional donation, for the G1 and the Micro Modular.
- **[Nomad / NMEdit](https://sourceforge.net/projects/nmedit/)** — the long-running open-source
  Java editor, GPLv2, which also reads and writes `.pch`.
- **[nordmodulareditor.com](https://nordmodulareditor.com)** — another editor project.

If yours is not on the list and it works, say so in an issue and it goes in. If it speaks the
protocol and G1-Emu does not answer it properly, that is a bug here, not in your editor, and we
want to hear about it.

## Using it

```bash
./g1gui.sh   # with the panel in a window
./g1.sh      # in the console (Ctrl+C saves and quits)
```

- **MIDI** (ALSA): client **G1-Emu** with two ports, like the hardware: **PC Port** (the editor:
  choose it in NME as input and output) and **MIDI** (notes and controllers, e.g. from a DAW).
  Programs that read **raw MIDI devices** instead (Bitwig on Linux) see no sequencer port at all:
  for them the emulator takes over a card of its own — a one-port USB MIDI gadget — and links it
  by itself, with nothing to run or route by hand. See [raw MIDI](docs/bitwig-midi.md).
- **Audio** (JACK): `G1-Emu:out_1..out_4` and `in_L`/`in_R`; `out_1`/`out_2` connect themselves to
  the sound card. Without JACK, outputs 1/2 go through ALSA.
- **The ROM** is yours to provide and is looked for in `<Documents>/Animatek/G1-Emu/roms`, in
  `roms/` next to the flash, and in `Roms/` of the current directory and the source tree. With none
  found, the window says what is needed, opens that folder for you, or lets you pick the file;
  `Settings` shows which one is running and changes it. A file that is not the right ROM is told
  apart from a missing one, and the reason is given.
- **Regular or Expanded:** Settings picks the model. The **Expanded** is the G1 with the expansion
  board: eight DSPs instead of four, and the same OS gives each patch about twice the voices
  (BellsMorph 8 → 16, 4VoiceChoir 16 → 32). It costs more CPU, and the change takes effect on the
  next start or Restart (`model =` in the settings file, `G1_MODEL=expanded`).
- **Micro Modular:** the third model, one DSP and its own OS, light on the CPU (about a third of
  a Regular's) for small patches and slower computers. G1-Emu does not ship that OS: point
  Settings' **Micro OS** at Clavia's Micro Modular OS updater (the `.exe`; it is read as it is, nothing to
  extract) or leave it in the ROM folder. It has its own box (volume, three edit knobs and the
  two-digit display; its buttons are not there yet) and its own flash (`flash-micro.bin`). Start or
  Restart to switch (`model = micro`, `G1_MODEL=micro`). The plugin has it too, in its Settings, so one plugin is a G1 or a Micro.
- **Settings** (in the window): the ROM, the model, audio driver and device, output level, whether outputs 1/2
  connect themselves, the raw MIDI card, and the notice above. The console reads the same settings;
  the `G1_*` environment variables win over them.
- The flash (installed OS and stored patches) is saved beside it. On Windows both live in
  `%APPDATA%\Animatek\G1-Emu`; on Linux and macOS the existing path is
  `~/.local/share/Animatek/G1-Emu`.
- The level is low because the OS itself caps the master volume at −36 dB; it is compensated with
  +36 dB (`G1_GAIN_DB`). More settings in [`CLAUDE.md`](CLAUDE.md).

## Optional: the OS a real G1 runs

The ROM carries a factory OS, and that is what G1-Emu runs unless told otherwise. **It works**:
patches load, play and edit, and the plugin keeps its state in your projects. But it is not quite
the OS an updated G1 runs from its flash, Clavia's 3.03b update, and they differ: the factory one
does not read the stored synth settings at power-on, so **Shift+Store's slots do not come back**
when G1-Emu starts again (every slot says `Empty patch`), where a real G1 starts with them. More
differences may turn up.

You can install the real one into G1-Emu the way you would into the hardware, with Clavia's own
updater. It is optional, done once per machine, and needs:

- **Clavia's free Windows updater for the Nord Modular, OS 3.03b** (`Nord Modular OS v3.03b
  Update.exe`), which you find and download yourself. The Mac version of it is for Mac OS 8/9 and
  runs on no current Mac: use the Windows one everywhere.
- **On Linux or macOS, Wine** to run it (on macOS also CrossOver or Whisky). On Windows it runs as
  it is, but G1-Emu's PC Port needs a loopback driver there for now (see Windows notes).

Then:

1. Put G1-Emu in update mode: **Settings → OS... → Update mode**, which restarts it as a real G1
   started with Shift + Store held (or start it with `G1_UPDATE=1 ./g1gui.sh`). The display shows
   `Update utility`: the G1 waits for an OS. Your banks are not touched.
2. Run the updater (`wine "Nord Modular OS v3.03b Update.exe"`) and choose **G1-Emu's PC Port** as
   its MIDI output and input. Follow its steps. The G1's display counts the percentage received
   and asks `Update flash?`: press **Store** on the panel, and it ends with `Update completed`. Tick **Safe (Slower)** on the ports page: without it the
   updater sends faster than the G1 takes it in, and ends with "Dump error" while the G1 is still
   working (harmless, but confusing). If the updater complains before the G1 is done, let the G1
   finish: what counts is its display.
3. Restart the G1 with the power button at the bottom right, beside About (or close G1-Emu). The OS that came in is kept in the data folder (`os/received-….bin`) and set as
   `os =` in the settings, so the window, the console and the plugin all run it from then on.

To go back to the factory OS, **Settings → OS... → The ROM's own OS** (or empty `os =` in the
settings file). Two things to know: a DAW project
saved with one OS does not apply its G1 state under the other (it is kept as it is, nothing is
lost), and the received OS is Clavia's, like the ROM: keep it to yourself.

## The VST3 and CLAP plugin (beta)

The same G1 as an instrument inside a DAW: **notes from the track, audio back to the track**, with
no JACK client and no virtual MIDI card. Its one MIDI port is the PC Port, for the editor (below).
Used on Linux, macOS and Windows, in Bitwig, Ableton Live, Reaper, Cubase and Nuendo among others.
**To install it from a release, see [Getting started](#getting-started)**; to build it:

```bash
cmake --build build --target g1plugin_VST3 g1plugin_CLAP g1pluginfx_VST3 g1pluginfx_CLAP -j$(nproc)
cp -r build/app/plugin/g1plugin_artefacts/Release/VST3/G1-Emu.vst3 ~/.vst3/
cp build/app/plugin/g1plugin_artefacts/Release/CLAP/G1-Emu.clap ~/.clap/
cp -r "build/app/plugin/g1pluginfx_artefacts/Release/VST3/G1-Emu FX.vst3" ~/.vst3/
cp "build/app/plugin/g1pluginfx_artefacts/Release/CLAP/G1-Emu FX.clap" ~/.clap/
```

The CLAP build needs a clone of [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions)
with its submodules in `~/src/clap-juce-extensions` (or `-DG1_CLAP_DIR=...`); without it, only the
VST3 is built.

- **Outputs 1/2 and 3/4** as two stereo buses, and **In L/R** as an optional input.
- **Regular, Expanded or Micro Modular** in the plugin's Settings, saved with the project (the Micro needs Clavia's Micro OS updater in the ROM folder, as in the standalone; it has its own box and its own flash, `flash-micro.bin`): changing it restarts
  that instance's G1, which keeps its patches. A new instance takes the last model chosen.
- **G1-Emu FX** is the same G1 as an effect, to put on an audio track: its **In L/R** is on from the
  start and feeds the G1's two inputs, so a patch with the Audio In module processes the track. It
  still takes notes and the editor like the instrument. Most DAWs never offer an instrument's
  input, which is why there are two.
- **The ROM** is found exactly like the standalone finds it; with none, the plugin's window says
  what it needs and lets you pick the file (remembered in the same settings file).
- **Each instance has its own G1.** A new one starts from a *copy* of the standalone's flash, so
  your banks are already there; from then on its patches and synth settings live in the DAW
  project, never in the standalone's files. What is saved is only what differs from the factory
  flash — the OS, which is the ROM's, never goes into your project.
- **The 18 knobs are parameters** the DAW can automate. Each one is named after what it moves in
  the current patch (`Knob 3: OscA Freq coars`) and shows the value the OS gives it; turning one
  on the panel moves the parameter too.
- **Choosing a patch:** a MIDI Program Change (and Bank Select) from the track, or Patch/Load on
  the panel. VST3 carries no Program Change as MIDI, so the plugin also has 128 programs: a host
  that sends Program Changes to a VST3 sets those, and they reach the G1 on channel 1. The G1 does not remember what each slot had, so the plugin remembers the last Program
  Change of each channel and sends it again when the project opens.
- **The G1's MIDI OUT is the plugin's MIDI output:** what the G1 sends there (the CC of a knob
  turned on the panel, and whatever the OS sends) comes out of the track, on the sample where the
  G1's own sound has it, for the DAW to record or route to another instrument. SysEx is not passed
  on. The standalone sends the same to its `MIDI` port.
- **Latency** is one host block plus about 5.5 ms, and it is reported to the DAW, which
  compensates for it: notes land on the exact sample the DAW put them on, whatever the block size.
- **The Presets page** works here too: the banks, a click to load, and Load .pch (see
  [Getting started](#getting-started)).
- **Editing from Animatek NME:** NME 0.21 or later finds each instance by itself over the direct
  link ([#8](https://github.com/animatek/G1-Emu/issues/8)), on every system. Each instance also
  opens a virtual MIDI port for its PC Port, **G1-Emu PC Port** (the second instance **G1-Emu 2 PC
  Port**, and so on), for any other editor: choose it as input and output, as with the hardware.
  Linux and macOS; on Windows JUCE cannot create that port. `G1_PLUGIN_PC_PORT=0` leaves it out.
- Loading a patch mutes the G1 for a moment, as on the hardware.

## FAQ

The questions that come up most, with their answers. If yours is not here, look in the
[issues](https://github.com/animatek/G1-Emu/issues) or open one.

**Is it free? Where do I get it?**
Yes, free and open source (GPLv3). The ready builds are in
[Releases](https://github.com/animatek/G1-Emu/releases); [Getting started](#getting-started) says
which one to take and how to install it.

**macOS will not open it, or my DAW does not show the plugin.**
It is not signed yet, so macOS quarantines it: run the `xattr` lines in
[Getting started](#getting-started) and rescan. Also check that it is in the right folder, and that
your DAW takes that format: **Ableton Live has no CLAP**, use the VST3 there.

**Does it run on Apple Silicon? And under Rosetta?**
The macOS build is universal and runs natively on Apple Silicon and Intel. **Run your DAW natively,
not under Rosetta**: under Rosetta the emulator is translated on top of the emulation and cannot
keep up (no sound, load at 100 %).

**How much CPU does it use? Can all four slots play at once?**
Yes, four slots at once, like the real G1. The emulator always runs the whole synth, its 68331 and
its four DSPs, so using all four slots costs about the same as one. Each instance is a whole G1. The
status bar shows **speed** (100 % means real time) and **load**; if the speed stays below 100 %,
this computer cannot keep up and you will hear dropouts.

**It makes no sound.**
A G1 starts with empty slots: load a patch first ([Getting started](#getting-started), step 4).
Then go through the two test patches, step by step, in
[Getting started, step 6](#getting-started): **Test Audio** sounds with no notes at all, **Test
MIDI** only while a note is held, so which one stays silent says where the problem is. Then check that the notes reach it: the **MIDI LED** on the panel flashes with each note, and
`MIDI in` in the status bar counts up. If they do and it is still silent, check the master volume
knob and that the DAW routes the plugin's outputs. If the test patches sound and yours do not, see
the next question.

<a id="some-patches-do-not-sound-or-sound-strange"></a>
**Some patches do not sound, or sound strange.**
`.pch` files come in two formats. The usual one is **3.0**, written by Clavia's editor 3.x and by
Animatek NME. The older **2.10**, from Clavia's editor 2.x, is still common: many of Clavia's
factory patches and many in the community archives are 2.10 (open the file in a text editor: its
first lines say `Version=Nord Modular patch 2.10`). The G1 itself only takes 3.0, so **a 2.10
patch is converted when it is loaded**, by Load .pch and by Animatek NME alike. Since G1-Emu
alpha.19 (and in the Animatek NME release after 0.21) the conversion follows the rules of Clavia's
own editor 3.03, morphs, knobs and MIDI controllers included, and these patches sound as they
should ([#65](https://github.com/animatek/G1-Emu/issues/65)). A converted patch can still differ
from what its author heard. If one is silent or odd:

1. **Look at the patch in an editor first.** Open it in Animatek NME (or Clavia's editor) and
   check what it needs: an output module that is connected and not muted, the outputs it goes to
   (3/4 instead of 1/2 is silent in most setups), the audio inputs (an effect patch is silent with
   nothing coming in), a keyboard or velocity range, the MIDI channel. **Sequencer patches often
   wait for a clock**: one with a MIDI Global module needs the G1's MIDI clock running (in a DAW,
   press play; standalone, set the clock to internal in the synth settings), and some start only
   when a knob is turned, which their notes usually say. Many "silent" patches are one of these.
2. **Send it from Animatek NME** instead of Load .pch. If it sounds from NME and not from Load
   .pch, tell us: that is our bug.
3. **Open an issue and attach the `.pch`** (zip it if GitHub refuses the file), the very file you
   loaded, and say where it comes from. We need that file and not just the patch's name: copies of
   the same patch differ, and the one you have may be damaged, in the old format, or sent to other
   outputs. **Report issue** opens the issue for you.

**The plugin gets no notes from my DAW.**
Watch `MIDI in` in the status bar while you play. If it stays at 0, the notes are not reaching the
plugin: arm the track or turn its monitoring on, and check the DAW's MIDI input settings. If it
counts up, the G1 gets them but no slot listens on that channel: slot A is on channel 1 by default,
set in the synth settings (the **Settings** page button on the panel, next to Presets, or your
editor). We are looking into a case
on Windows in [#53](https://github.com/animatek/G1-Emu/issues/53): if it happens to you, tell us
there.

**How do I load patches?**
The Presets page, Load .pch, an editor, or Program Change: all four in
[Getting started](#getting-started), step 4.

**Where is Animatek NME? There is no program on its GitHub page.**
GitHub has its source code. The ready-to-run builds for macOS, Windows and Linux are for
[Patreon](https://www.patreon.com/c/animatek) members, which is how its development is paid for.
How to install and use it: [its manual](https://github.com/animatek/Animatek-NME/tree/main/manual).
Any other G1 editor works too ([the list](#two-things-a-nord-modular-needs-and-neither-comes-in-the-box)).

**How does the editor connect?**
Animatek NME 0.21 or later finds every running G1-Emu by itself, the window and each plugin
instance, on every system. Other editors use the PC Port: a virtual MIDI port on Linux and macOS,
and two loopMIDI cables on Windows for now ([`WINDOWS.md`](WINDOWS.md)).

**The MIDI channels and other synth settings are gone when I reopen my project.**
On the G1 they are kept only by **Shift+Store**, and the ROM's factory OS does not load them at
power-on at all (the 3.03b update does: see
[the OS a real G1 runs](#optional-the-os-a-real-g1-runs)). **From the release after alpha.14 the
plugin keeps the settings in use in the project**, stored or not, and gives them back when the
project opens, under either OS ([#46](https://github.com/animatek/G1-Emu/issues/46)). The
standalone still behaves like the hardware: Shift+Store, and the 3.03b update.

**Can I use the original Clavia editor?**
Yes: G1-Emu emulates the instrument, and any editor that speaks its protocol works. See
[the list of editors](#two-things-a-nord-modular-needs-and-neither-comes-in-the-box).

**Where do I get the ROM?**
Not here: see [Please read this first](#please-read-this-first).

**How do I report a bug?**
With the **Report issue** button in the window's status bar: it opens a new issue with the
version, system, audio and MIDI details already filled in. Say what you did, what you expected
and what happened. **If a patch is involved, attach its `.pch`** (zipped if GitHub refuses it):
the file you used, not its name ([why](#some-patches-do-not-sound-or-sound-strange)).

## Support the project

G1-Emu is free and open source, built in spare time, and the builds in Releases cost nothing. If it
is useful to you and you want it to keep going:

[![Patreon](https://img.shields.io/badge/Patreon-support-f96854?logo=patreon&logoColor=white)](https://www.patreon.com/c/animatek)
[![Donate](https://img.shields.io/badge/Donate-PayPal-0070ba?logo=paypal&logoColor=white)](https://www.paypal.com/donate/?hosted_button_id=YATW56VC46ENE)

A donation is a thank-you: nothing is locked behind it.

## License and credits

**Panel skin and GUI design by Mike Fiction.**

The **About** button in the extras drawer shows all of this inside the program.

**Licenses.** G1-Emu's code is GPLv3 (see [`LICENSE`](LICENSE)), because it links Gearmulator. The
panel artwork in [`app/gui/skin`](app/gui/skin) and the standalone's icon are © 2026 Mike Fiction,
under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) (see
[`app/gui/skin/LICENSE.md`](app/gui/skin/LICENSE.md)). Inside the program as well: Gearmulator's
dsp56300 and mc68k (GPLv3), the Musashi 68000 core by Karl Stenerud (MIT), AsmJit (zlib), JUCE 8
(AGPLv3), the VST 3 SDK by Steinberg (MIT; VST is a trademark of Steinberg Media Technologies GmbH),
and CLAP with clap-juce-extensions (MIT).

**Credits.** Made by Javier Melgar, Animatek ([animatek.net](https://animatek.net)), who also makes
[Animatek NME](https://github.com/animatek/Animatek-NME), the editor that connects to G1-Emu by
itself. Mike Fiction designed the panel skin and the GUI. Thanks to The Usual Suspects for
[Gearmulator](https://github.com/dsp56300/gearmulator) and to joelanders for the fork with the
Monomachine and Machinedrum, which this work builds on; to Tuth for the Windows fixes in PR #5; to
Psychlist1972 for the help with Windows MIDI Services; and to everyone who tests and reports:
AlphasiaIndustries, artqcid, Garrincha568, JuliusLC, masc4ii, ModGod222, msavery123, nirsu1,
psy-dub and Waltercalling.

Nord and Nord Modular are trademarks of Clavia DMI AB, which neither makes, endorses nor supports
G1-Emu. G1-Emu ships no ROM.
