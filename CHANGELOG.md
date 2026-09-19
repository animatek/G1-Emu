# Changelog

Everything that changes in G1-Emu, newest first. **Rule: every change that goes into the repo gets
its line here, in the same commit** (see `CLAUDE.md`). Each entry says who made it, what changes
and how it was checked; the commit is the one that brings the entry (`git log -- CHANGELOG.md`).
Older entries cite their commit by hand.

## 2026-09-19

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
