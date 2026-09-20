# Changelog

Everything that changes in G1-Emu, newest first. **Rule: every change that goes into the repo gets
its line here, in the same commit** (see `CLAUDE.md`). Each entry says who made it, what changes
and how it was checked; the commit is the one that brings the entry (`git log -- CHANGELOG.md`).
Older entries cite their commit by hand.

## 2026-09-20

- **The ROM stops being a hard-coded path: G1-Emu looks for one, says what is wrong with what it
  finds, and offers the folder (Claude).** Until now both front ends took the ROM as an argument and
  checked only its size, which is no way to hand the thing to anyone else: G1-Emu ships no ROM and
  never will, so a new user's first screen is this one. New `g1Lib/g1rom.h` decides whether a file
  serves and, when it does not, why — not 512 KB (with the size it does have), 512 KB but no Nord
  Modular OS inside, or a Nord Modular OS that is the keyboard model's and not the rack's, told
  apart by the model byte at `$7FF` that the OS itself reads at boot (`NOTES.md`, "The panel"). New
  `app/romfinder.*` looks, in order, at the path on the command line, `rom = ...` in the settings
  file, `<Documents>/Animatek/G1-Emu/roms` (honouring the user's XDG document folder, which is not
  called "Documents" in every language), `roms/` next to the flash, and `Roms/` in the current
  directory and in the source tree, so a clone still works with no setup. A ROM named on the command
  line is an order: if it does not serve the emulator stops and says so, instead of starting on a
  different one, which would look like it worked. The one in the settings is a preference and falls
  back to the search. The window offers **Open the folder** and **Choose a ROM file...**, starts as
  soon as it has one, and the settings window gains a **ROM** section on top with the file in use, a
  picker that refuses a file with the reason and a button to the folder; the console prints the
  whole story and exits. `G1_ROM` overrides everything, and the ROM argument of `g1run` and `g1gui`
  is now optional. Also: the window's startup log was never flushed, so it only appeared on exit.
  Verification: found in the repo with no argument at all; and the four ways it goes wrong, each
  giving its own line — a 100 KB file, a 512 KB file of noise, a copy with the model byte set to 0,
  and a path that does not exist. The settings window was checked on screen with its ROM section,
  and the picker opens on the ROM folder filtering `*.bin`. Release build and CTest 1/1. Two labels
  left over from the snd-virmidi days reworded.

- **One G1 in the DAW's MIDI list instead of thirty-two entries: the card is a USB MIDI gadget now
  (Claude).** `snd-virmidi` was the wrong card: it hard-codes sixteen subdevices per device and the
  name "Virtual Raw MIDI", and no module parameter changes either, so it filled Bitwig's list with
  `Virtual Raw MIDI/1..16` — and `midi_devs=2`, suggested here earlier to get a second port, doubled
  it to thirty-two. Replaced by `dummy_hcd` + `g_midi`, stock in-tree kernel modules that take the
  port count and the name as parameters: `modprobe g_midi id=G1 iProduct=G1 in_ports=1 out_ports=1`
  gives one port with a name of our own. The gadget is plugged into an emulated host, so ALSA gets
  two cards, one per side of the virtual cable: `G1` (`f_midi`) is the emulator's and `G1_1` (`G1
  MIDI 1`) is the DAW's. Two entries is the floor for stock modules; one would need a driver of the
  G1's own. `AlsaMidi::findPorts` becomes `findCardPorts`, which matches **by sound card instead of
  by client name** — the name was `snd-virmidi`'s and no other card has it — and returns each port's
  name, so the log says what it linked. `bindRawMidi` now gives the card's first port to the MIDI,
  which is all a DAW wants, and the second one, if there is one, to the PC Port. The default card ID
  goes from `G1Emu` to `G1`. `docs/bitwig-midi.md` rewritten around the gadget, with the table of
  which side is whose. Verification, live: the cable on its own (`amidi -p hw:6,0 -S "90 3C 64"` on
  the host side comes out of `amidi -p hw:5,0 -d` on the gadget side), then the emulator reporting
  `raw MIDI: MIDI <-> f_midi` and its MIDI input counter moving by 6 bytes for two notes sent the
  way Bitwig sends them. Release build and CTest 1/1.

- **The notice stops stopping every startup: it moves into the settings window (Claude).** Javier
  asked for it. It is shown at startup **only on the first run** — when there is no settings file
  yet — and answering it writes the file, so it does not come back; the "Don't show this again" tick
  is gone, because closing it is the answer. In the settings window there is now a **Notice**
  section with the text always readable and a "Show the notice below at startup" switch to put it
  back. The flag lives in `settings.conf` as `showDisclaimer`, so the window no longer keeps a
  second settings file of its own (`juce::ApplicationProperties`, which was writing to
  `~/.config/.G1-Emu/` and is why ticking the old box never seemed to work): everything is in one
  place. `Options::load` now says whether the file was there, which is what "first run" means.
  Verification: a round-trip of `save`/`load` through a scratch build (every field back, a missing
  file reports false and leaves the defaults standing); with `showDisclaimer = 0` the window opens
  straight into the panel, with no settings file at all it shows the notice once. The settings
  window was checked on screen and its layout fixed twice: the notice box was cut off and there was
  dead space under it.

- **Settings window, and the options stop being environment variables only (Claude).** New
  `app/gui/Settings.*`, opened from a **Settings** button next to the status bar: audio driver
  (JACK/PipeWire, ALSA, none), ALSA device, output level, whether outputs 1/2 connect themselves to
  the sound card, and which `snd-virmidi` card is taken over for raw MIDI, plus a live read-out of
  what is actually in use. `EmuHost::Options` holds them and is read from
  `~/.local/share/Animatek/G1-Emu/settings.conf`, a plain `key = value` file that `g1run` reads too,
  so the window and the console agree. Order: defaults, file, then the `G1_*` variables, which still
  win — the scripts and `g1patchtest` keep working untouched. The level applies while it plays (both
  backends read the gain from an atomic now, and `JackAudio` takes auto-connect as an argument
  instead of reading the environment); the rest, on the next start, because the audio callback runs
  on the DSP thread and swapping a driver under it is not worth the race. `G1_THREADS` and
  `G1_INTERP` stay environment-only: they are core debugging knobs. Verification: Release build and
  CTest 1/1; with `audio = no` in the file the console reports no audio, with `audio = alsa` it
  opens ALSA "default", with `audio = hw:2,0` it reports the device is busy, and `G1_AUDIO=jack
  G1_GAIN_DB=30` over the same file gives JACK at +30 dB. The window was seen running with the
  Settings button in place; the panel's own layout has not been looked at on screen yet. Also
  shortened the raw MIDI text in the status bar, which wrapped it onto two lines; the full hint
  stays in the log.

- **The emulator takes over its own raw MIDI card; the helper script is gone (Claude).** Bitwig on
  Linux reads raw MIDI devices and never looks at ALSA sequencer ports (checked: its engine has
  `PipeWireAudioHostApiPlugin.so` loaded for audio and `libasound` open on `/dev/snd/midiC5D0` for
  MIDI), so `G1-Emu:PC Port` and `G1-Emu:MIDI` are invisible to it and a kernel-made device is
  unavoidable. Instead of an external helper plus `aconnect`, `EmuHost` now finds the `snd-virmidi`
  card whose ID is `G1Emu` itself and links its device 0 to the PC Port and device 1 to the MIDI, in
  both directions, retrying every two seconds so the card may be loaded afterwards (`G1_RAWMIDI`
  picks another card, `0` disables it). New `AlsaMidi::findPorts` and `AlsaMidi::link`; the status
  bar and the log say which devices are linked. Removed `tools/bitwig-midi.py` and rewrote
  `docs/bitwig-midi.md`. Verification: Release build; with the card as it is loaded now
  (`midi_devs=1`) the emulator reports `raw MIDI: MIDI <-> hw:5,0`, `aconnect -l` shows `36:0`
  subscribed both ways to `128:1` with no helper run, and `amidi -p hw:5,0 -S "90 3C 64"` reached
  the emulator (its MIDI input counter moved). The two-device case needs the card reloaded with
  `midi_devs=2`, which Bitwig was holding open: not verified yet.

- **The emulator does sound: what went silent was the routing (Claude; no code change).** Javier
  reported no sound since the MIDI work. Checked in three ways: `g1patchtest` with `SimpleOSC.pch`
  gives 261.5 Hz at -61.8 dBFS on outputs 1 and 2 with the links between the four DSPs carrying
  signal; a live `g1run` on a copy of the flash boots with all four DSPs on at 100% speed and
  auto-connects `out_1`/`out_2` to the Komplete Audio 6; and replaying the captured PC Port session
  (`pcport-in.bin`, NME's own traffic) into it brings the outputs to -25 dB. Two things did explain
  silence: the `aconnect` subscription from the virtual card dies every time the emulator exits and
  had to be re-run by hand (it was not there at the start of this session), and after boot with no
  editor connected the active slot holds no patch, so a note plays nothing.

- **Post-reboot recovery and build repair (Codex).** Moved the existing PC-trail size declaration
  before the array that uses it, fixing compilation of the pending diagnostic changes without
  removing them. Verification: full Release build, CTest (1/1), and `git diff --check` passed. A
  12-second run with temporary factory flash and audio disabled exposed both ALSA ports, ran all
  four DSPs at 99.9–100% speed, and exited cleanly. The current boot has no matching kernel oops and
  the experimental driver is not loaded. Audio playback, NME and the single-endpoint Bitwig
  requirement remain unverified in this session; no user flash or physical MIDI connections were
  changed.

- **Failed single-port kernel bridge experiment withdrawn (Codex).** A modified Linux virtual MIDI
  bridge built successfully with Clang for 7.2.5-1-cachyos but faulted during insertion on the
  maintainer's host: the kernel logged a null-pointer page fault in `dev_driver_string`, followed by
  another insertion fault. No G1Emu card was registered. Build success was not a sufficient
  validation; this should have been tested in an isolated VM first. Advised saving work and
  rebooting, without forced unload or another insertion; no boot-time installation was made. Removed
  the experimental driver/build from the repo and restored the helper to the previously tested stock
  snd-virmidi bridge. A single named Raw MIDI endpoint remains unresolved. Verification: live ALSA
  enumeration, kernel journal, helper syntax and `git diff --check`; recovery after reboot has not
  yet been verified.

- **Instance and VST3 hosting plan (Codex).** Javier confirmed the Bitwig bridge appears and plays,
  then requested one named G1Emu performance port per instance and a shared standalone/VST3
  direction. Added `docs/instance-hosting.md`, linked from the roadmap and bridge guide, covering
  engine/host separation, independent state, optional editor endpoints, native plugin MIDI/audio and
  acceptance gates. Verification: inspected EmuHost's shared default flash/temp/log paths and device
  ownership; Linux driver source confirms hard-coded 16 input/output substreams and the Virtual Raw
  MIDI name. This is a design proposal; endpoint reduction/renaming and VST3 are not implemented.
  Documentation links and `git diff --check` verified; no runtime code changed.

- **Bitwig Raw MIDI bridge for issue #1 (Codex).** Added `tools/bitwig-midi.py` and
  `docs/bitwig-midi.md`: a dedicated `snd-virmidi` card exposes a Raw MIDI device to Bitwig and the
  helper connects only its output to `G1-Emu:MIDI`. It discovers current client/card numbers,
  accepts an existing connection and refuses missing or ambiguous clients. NME keeps its separate PC
  Port. Corrected the ALSA header's claim that every DAW sees sequencer ports. Verification: Python
  compilation and `git diff --check`; live ALSA test with the user's newly loaded G1Emu card
  (hw:5,0), two helper runs creating one subscription, and Note On/Off sent through Raw MIDI: the
  temporary-flash, audio-disabled emulator reported MIDI input increasing from 0 to 6 bytes while PC
  Port input stayed 0. Missing-emulator refusal checked after stopping it. Bitwig's device picker
  and audible playback are still awaiting user verification.

- **A module battery worth the name: 80 of the 109 types give a signal (Claude).** New
  `tools/battery/battery.py`: it builds a patch per module type with what each one needs to show
  signs of life — an oscillator on its audio inputs, an LFO on its control ones, a running clock
  on its logic ones and a sine into the G1's inputs — and sends its first four outputs to the four
  outputs, which `g1patchtest` measures. Two things it learned the hard way: a clock on a reset or
  a sync input freezes the module (so those are left alone), and a parameter that `modules.xml`
  leaves without a default is uploaded as 0, which mutes a level or an amount (so the battery
  opens those up and says which). `g1patchtest` now reports the mean and the drift of each output,
  which is what makes a slow signal visible at all: the OS caps the volume at −36 dB, so full
  scale is around −62 dBFS. **61 sound, 19 move, 12 hold a level, 17 give nothing** (was 56 of 101
  before), in `docs/module-battery.md`. Silence is a list to look into, not a verdict: of the
  first ones looked at, Constant is bipolar and 64 is its zero, DrumSynth with its levels open is
  the loudest module measured (−51.7 dBFS against the −62 of an oscillator), AudioIn only needed a
  signal in the inputs and MasterOsc has nothing but a master-slave output.

- **What each panel button does, and the System menu (Claude).** Watched on the emulator, each
  against a control run. The **navigator is row 1**: right and left walk a menu line, down goes
  into the item, and in the Edit pages they walk the morph groups and a module's parameters; row 2
  does none of that. **Shift** is the second function of another key: Shift + Store opens
  `Store settings` (the panel's "Save Synth. Settings") and Shift + a slot shows and changes that
  slot's voices. **Find**, held, puts `Find` on the display. **Assign/Morph is the only key with
  no known effect**: alone or with Shift, on the patch screen, the Morph page, a parameter page or
  the System menu, before or after moving a knob or the dial, nothing changes on the display, the
  LEDs or the traffic to the editor, though the OS does take the key. Also written down: the whole
  System menu, from the OS's table at `$1442EE`. `g1patchtest` grew a gesture language — a step of
  `G1_PRESS` can now be a knob (`k5=200`) or the dial (`d3`), and what the OS says to the editor
  during the gesture is printed — plus `G1_HOLD_END`. Check: `1.2,1.6,k5=200,2.6` and its control
  without the key give identical output; audio and CTest as before.

- **Oct Shift belongs to the keyboard, and how Panel Split shares out the knobs (Claude).** The OS
  keeps an octave shift per slot (`$1C3AB8 + slot`, −2 to +2) which travels in the patch header,
  and lights one of the five LEDs for it: 0.0 = −2, 1.0 = −1, 2.0 = 0, 3.0 = +1, 3.1 = +2. On the
  rack it does none of that: the routine is gated on the model byte, read at boot from `$7FF` of
  the ROM, which is `$01` in the rack's. Panel Split (flag `$18C0E4`, 0 = on) gives knobs 1–6 to
  slot A, 7–12 to B, 13–15 to C and 16–18 to D, each renumbered from 1, through two tables at
  `$145A94` and `$145AA6`. Two new probes in `g1patchtest`: `G1_MIDINOTE=channel` (the note through
  MIDI IN, not the PC Port) and `G1_PEEK=addr,...`. Check: uploading a patch with `OctShift` 0, 2
  or 4 leaves `$FE`, `$00` or `$02` in `$1C3AB8`, so the value arrives, but the note comes out at
  262 Hz in all three, from the editor and from MIDI IN alike, and no code outside the front
  panel's module reads the variable; with the split on, only knobs 1–6 still reach the patch in
  slot A and 7–18 go silent.

- **Every panel button identified, and the dial works (Claude).** The OS only reads **bits 2 to 7**
  of each of the three matrix rows: 18 buttons, not 24. Bits 0 and 1 are the **dial**, a quadrature
  encoder the OS decodes in its main loop (`$104DC6`) with four edges per step and its own
  acceleration; `Microcontroller::turnDial()` emulates it and the window's dial turns it by dragging
  or with the wheel. The six buttons that were left (matrix row 2) are Panel Split, Find, Oct down,
  Oct up, Assign/Morph and Shift, in that order, and they are wired in `g1gui`. The names come from
  the factory test's tables, which sit in the flash before the OS ($9962 the key codes, $9986 the
  names, $9A16 the 32 LEDs, $9A56 the 20 ADC channels with their names): its codes are the OS's plus
  six, which the twelve buttons already known confirm. `$18` is the **pedal** input, no longer a
  guess. New probes in `g1patchtest`: `G1_PREPRESS`, `G1_HOLD` (a held modifier) and `G1_DIAL`.
  Check: holding 2.3 puts `Find` on the display and 2.2 lights LED 3.2; turning the dial on the
  Morph screen moves its value up and down, and faster turns move it further, as the OS intends;
  audio, CTest and the rest of the panel behave as before.

## 2026-09-19

- **An open invitation to collaborate (Claude).** New section at the top of the README ("You are
  invited: this is a collaborative project") and a warmer opening in `CONTRIBUTING.md`: the project
  is meant to be built together and everyone is welcome, whatever their experience. Check: both
  files reviewed.

- **Disclaimer at startup and in the README (Claude).** `g1gui` shows a notice when it opens: an
  independent project not affiliated with Clavia DMI, no ROMs now or ever, and no support. It has a
  "Don't show this again" box, saved in the user settings (`~/.config/G1-Emu.settings`). The same
  text opens the README ("Please read this first") and is summarised in `CONTRIBUTING.md`; the
  missing-ROM error now says that no ROM is provided. Check: built, the dialog opened and reviewed.

- **The whole repo in English (Claude).** Documentation, code comments, program messages and the
  changelog translated; `NOTAS.md` → `NOTES.md` (rewritten as a technical reference, organised by
  topic) and `SIGUIENTES-PASOS.md` → `ROADMAP.md` (updated). New `CONTRIBUTING.md`. The language
  rule is now in `CLAUDE.md` and `AGENTS.md`. Check: full build, CTest, `g1patchtest` and `g1run`
  behave as before; no Spanish left in tracked files.

- **Panel screenshot in the README (Claude).** `docs/g1gui.png`, the `g1gui` window cropped, at the
  top of `README.md`. Check: image reviewed (only the window, no background).

- **Panel like the hardware, knobs in place and repo ready to go public (Claude).** The ADC returned
  the selected channel instead of the previous conversion: every knob was shifted by one and the
  runtime volume was read from another channel; fixed. Identified with `g1patchtest` (new
  `G1_KNOBS`, `G1_ADCSWEEP`, `G1_LEDSTATE`, `G1_PRESS`): the 18 knobs and their LEDs, the slot and
  mode LEDs, Edit, Patch/Load and the navigator. The window, redone from photos of the hardware:
  display with the HD44780 dot font, red and black knobs with the number under the LED, Panel
  Split, Find/Panic, Oct Shift, Assign/Morph, Shift, the dial and a MIDI LED; the raw matrix strip
  is gone. `LICENSE` (GPLv3), `README.md` and the changelog rule in `CLAUDE.md` and `AGENTS.md`.
  Check: ADC sweep with the 18 knobs assigned, 18 LED probes and 24+36 button probes, audio as
  before, window screenshots; history reviewed (no ROMs).

- **Patreon announcement drafts (Codex; private, not in the repo).** A bilingual draft (English
  first) of the announcement, including the planned multi-G1 editing in NME. The folder is excluded
  through `.gitignore`. Check: `git check-ignore -v` confirms the exclusion and `git ls-files` does
  not list the draft.

- **The window: first panel in JUCE (Claude, commit `46c7b8d`).** `EmuHost` moves `g1run`'s loop
  (flash, MIDI, JACK/ALSA, real time and statistics) into a class with its own thread; `g1run`
  becomes a thin console and now reports the load (~55%) and the cores (~2.9). `g1gui`
  (`./g1gui.sh`): display with the CGRAM custom characters, 18 knobs and volume (ADC), the
  identified buttons and LEDs, a raw matrix view and a status bar. JUCE is included once in the
  main CMake. Check: built, `g1run` 10 s over JACK as before, window open with the G1 display
  ("Empty Patch", voices per slot) and the slot A LED.

- **The panel, emulated (Claude, commit `f1e7573`).** HD44780 LCD (`$202006/7`), 32 LEDs in 4 rows
  and a 24-button matrix (`$202004/5`, `$201800`), with an API for a front end. Identified A–D,
  Store and System. Check: `g1patchtest` shows the G1 display (patch name and voices per slot) and
  reacts to buttons (System menu, Store, slots).

- **Control-rate modules, audio inputs and four outputs (Claude, commit `f1e7573`).** Gearmulator's
  JIT did not follow changes of the LA register, and that is how the OS extends the main loop when
  it loads control-rate modules: envelopes, clocks, master/slave, the chorus LFO and the overdrive
  amount stood still. Now it resynchronises (`onLaChanged`). Two missing DMA modes (fixed→fixed and
  block per request without clearing DE) bring the audio inputs. The outputs were swapped in pairs
  (1↔2, 3↔4). `g1run` over JACK with `out_1..out_4` and `in_L`/`in_R`. New test bench
  `g1patchtest`. Check: battery of the 101 module types, A/B of the chorus (L≠R) and the overdrive
  (follows its knob), AudioIn with different sines on L and R, 4Output with four signals, `g1run`
  over JACK for 16 s without dropouts, CTest.

- **The level, explained (Claude, commit `f1e7573`).** The rack OS caps the master volume at
  −36 dB: it takes it from a 128-entry table (`$153CAC`) indexed with ADC ÷ 2, at boot and when the
  knob moves. The −62 dBFS of an OscA → 2Output is what the OS computes; the emulator loses no
  level, and `g1run`'s +36 dB undo that cap. New `G1_FINDTX` and `G1_ADCALL` in `g1boot`. Check:
  CPU trace down to DSP 3's `Y:$5F`, table read from the ROM, and a replay with the 20 ADC channels
  at maximum (same level).

- **Clean sound: real DSP clock and 9-word links (Claude, commit `79e16aa`).** The DSPs run at
  82.944 MHz (864 cycles per sample, from the OS's `PCTL`) with IRQD on a fixed common grid; the
  ESSIs at the rate derived from their CRA (96 cycles per word on the links). The link between DSPs
  works by position (each word goes to the receive ring slot the DMA will write, from the block 8
  blocks ago) and the output is read from DSP 3 block by block. The steps and clicks came from
  there: the link moved 2 of the 9 words per sample and the channels shifted. `g1run` adds +36 dB
  by default. New `G1_BLOCKS` in `g1boot`. Check: built, CTest, replay with FFT (C at 261.6 Hz on
  1/2, harmonics at −82 dB, a single jump in the whole run) and `g1run` at 100% real time. Tested
  with NME by the maintainer: no noise.

- **Real-time audio through the sound card (Claude, commit `0723e40`).** `g1run` plays outputs 1/2
  through ALSA (`app/alsaaudio.h`, 48 kHz; `G1_AUDIO`, `G1_GAIN_DB`). The four DSPs run on their own
  threads, and their audio goes from one to the next on the CPU thread when all have stopped: 100%
  real time (was ~79%), replay 2.2× faster, same audio as serial (`G1_THREADS=0`). Check: built,
  CTest, replay in both modes with FFT and dropout counts, `g1run` for 15 s.

- **It sounds at the output (Claude, commit `59287f1`).** The audio goes through the chain
  DSP0→1→2→3 and DSP 3 plays the note's C at 261 Hz. Three fixes: the DMA with dual counters on
  source and destination (missing in Gearmulator, it copied nothing), immediate block transfers (the
  copy arrived late and overwrote the voice) and the master volume, which the OS reads from the
  panel ADC (code `$30`) and the emulator returned as 0. Check: everything built, CTest, a 60 M
  instruction replay and FFT of the four DSPs' output.

- **First audio from a patch (Claude, commit `5450cd8`).** Codex's replay was silent because of the
  replay itself: in the recorded session NME reconnected to a rebooted G1, so the second upload got
  PID 1 again; in the replay the OS gives PID 2 and drops the following messages (modules and
  note). `g1boot ... replay` now rewrites the PID with the one the OS assigns (ACK `$36`) and redoes
  the checksum. With that, DSP 0 loads the modules, links their code at `P:$197` and sends a
  periodic 261 Hz wave out of ESSI0. `G1_TAP=file` dumps what leaves each DSP's ESSI0. Check:
  everything built, CTest passes, 60 M instruction replay with 19 rewritten messages, FFT.

- **JIT extensions validated (Codex).** Short MOVEM and DO FOREVER in the build copy, with one DO
  iteration per dispatch. Fixed SR initialisation and accumulator reading in `g1dspcheck`,
  registered in CTest. CMake includes only the needed cores and the MIDI bridge, so nothing is
  generated inside the external Gearmulator clone. Check: `g1boot`, `g1run`, `dspdis` and
  `g1dspcheck` built; CTest passes MOVEM, JIT invalidation, DO FOREVER, IRQD and nested DO
  (blocks of 1/32).

- Plan for the next session (getting audio out, performance) and the maintainer's ideas (use the
  emulated G1 to improve NME, recreate modules from their DSP code, a patch as a plugin).
  `AGENTS.md` for Codex/opencode (commit `fd85567`).
- `g1run` no longer always records a WAV: only with `G1_RECORD=seconds`. One night without a limit
  had reached 35 GB.

- **The OS loads the patch code into the DSPs and the oscillator computes (commit `fe4c24d`).**
  Five chained problems fixed: the system clock (the SIM's PIT, not emulated by Gearmulator), an
  excessive wait in HI08 status polls, host-command arbitration (incompatible with the G1's fast
  interrupts), the interrupt queue filling up in a single thread, and IRQD ignoring the IPRC. The
  ESSI slot masks start as on the chip (all enabled).
- `g1boot`: the replay sends messages spaced out like NME; new `diff` mode, and dumps of the ESSIs,
  serviced vectors, processing mode and memory changes of each DSP.

- **NME connects to the emulated G1 and builds patches** (OscA → 2Output; the OS confirms
  everything and even reports the DSP load). No sound yet.
- Audio: 96 kHz ESSI clock, IRQD as the processing clock, meters and WAV recording of DSP 3's
  output in `g1run`, and a log of everything coming in through the PC Port (`pcport-in.bin`) to
  replay sessions.
- Fixed DSP 3's double boot: its sound program was left incomplete. Pending words now go to the
  boot ROM, and when the CPU polls the status with a pending word, the DSP runs until it takes it,
  so the OS does not drop it.
- `g1boot ... replay FILE`: replays an NME session without NME and dumps DMA, buffers and the
  output of each DSP. `G1_WATCH`: watchpoints in the OS code.

## 2026-09-18

- **`g1run` / `g1.sh`: the emulated G1 in real time with virtual MIDI ports (commit `e852a34`).**
  The ALSA client "G1-Emu" has two ports, "PC Port" (editor) and "MIDI". The flash is saved in
  `~/.local/share/Animatek/G1-Emu/flash.bin` on exit and whenever the OS writes to it. Tested with
  `aseqsend`/`aseqdump`: an IAm on the PC Port gets its answer. ~94% of real time.

- **The emulated G1 answers NME's handshake (commit `5e84273`).** Two ports, like the hardware:
  MIDI IN/OUT is the CPU's SCI (connected with `SciMidi`), and the editor's PC PORT is an external
  SCN2681 DUART on a parallel bus built from the GP port and port E. `g1Lib/g1duart.h` emulates it,
  and the byte-received notification (RxRDY → PAI → PAOV interrupt, vector IVBA+`$A`) is emulated in
  the CPU, because Gearmulator's GPT lacks the pulse accumulator. To the IAm
  `F0 33 00 06 00 03 03 F7` it answers `F0 33 00 06 01 03 03 3F 7F 7F 01 F7`.

- **The four DSP56303s boot with the OS program (commit `10f108f`).** There are 8 HI08 ports and
  the four on the main board have a DSP behind them; the HF flags go back and forth, host commands
  work and the jump to `$FF0000` returns to the boot ROM. DSP 3 boots twice (loader + OS), like the
  hardware. ~88% of real time. New tool `dspdis`.

- **OS 3.03 boots on the emulated 68331 and reaches its main loop (commit `0b2829c`).** `g1Lib`
  (CPU with ROM, RAM and flash) and `tools/g1boot` (headless boot, log of accesses to unknown
  hardware, chip-selects and disassembler). Found on the way: the loader picks the mode from the
  keys held at power-on, the OS is copied from the flash at `$300000` to RAM, and the four DSPs are
  at `$200000`/`08`/`10`/`18` over HI08. The flash is emulated as an AMD Am29F080, one of the three
  chips the OS accepts; on first boot it formats the patch area.

- New project, separate from `Elektron-Emu` (commit `6fa939e`). Analysis of the rack OS 3.03: the
  CPU is a 68331 (confirmed by the GPT/SIM/QSM accesses), the DSPs are 56303s and `$50000`–`$5FFFF`
  looks like the DSP code. The template is Gearmulator's Nord Lead 2X.
