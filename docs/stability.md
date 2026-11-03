# Stability release: checklist

Decided with Javier on 2026-11-03, after the live stream of 2026-11-02 where the plugin was close to
unusable: **the next release is a stability release.** No new features go in until every line
marked **gate** below passes. Each item says where it comes from, what to do first, and when it
counts as done. Tick a box only with the check that proves it, written in `CHANGELOG.md`.

## What the stream showed (log of 2026-11-02, Bitwig, CLAP, Ryzen 7 5700X)

Read from `~/.local/share/Animatek/G1-Emu/g1-emu.log`, 17:27 to 17:57:

- **The ROM was not the original.** Every start of the day loaded
  `G1-Mods/Roms/rom-all.bin` (`rom =` in `settings.conf`), the G1-Mods build with the new module
  types: 7,272 bytes differ from `NORD-MODULAR-RACK-VER-3.03.BIN`. The Micro runs at 14:47 used
  `G1-Mods/Roms/os-all.bin`. So nothing seen on the stream can be blamed on the emulator until it
  is seen again on the clean ROM.
- **The load jumped to 99 % and stayed there**: at 17:53:12, after 26 quiet minutes, ~190
  dropouts a second, steady, until the instance was closed. After a reload it came back within
  15 s (17:54:53) and again at 17:56. Not a burst: the emulator stopped keeping real time.
- **The PC Port vanished after a reload.** CLAP: once the last instance in the process closed,
  JUCE's MIDI was shut down, and the new instances logged `PC Port: none in this instance`
  (known JUCE 8 limit, `CLAUDE.md`). The editor lost the synth; only the direct link stayed.
  Those instances also logged themselves as **instance 0**, twice at the same time.
- Symptoms Javier saw, not in the log: Store into bank 6 ("Kick") did not store and the editor
  hung; a `.pch` opened in NME and sent to slot 1 did not load; copy and paste in NME gave a
  "syntax error" and the plugin stopped answering; the Micro was worse.

## Gate: reproduce on the clean ROM

- [ ] **S1. Redo the stream on the original ROM** (`rom =` back to the 3.03 BIN, `os =` empty,
  then again with the 3.03b update OS). Same host (Bitwig), CLAP and VST3, two instances, NME
  connected. The steps: build a patch, Store into a bank, re-read the bank, open a `.pch` in NME
  and send it to slot A, copy/paste modules, leave it 30 min. Record which symptoms stay.
  Done when each symptom below is marked "seen on the clean ROM" or "G1-Mods only".
- [ ] **S2. G1-Mods kept apart.** The window already shows a MOD badge when the OS is not
  Clavia's; the log line `ROM:` should say so too (`ROM: ... (modified: not Clavia's 3.03)`), and
  the issue report text should carry it, so a report from a modded ROM is never read as a plain
  one. Done when the log and the report say it.

## Gate: the plugin keeps real time

- [ ] **S3. The 99 % lock.** Find what keeps the emulator below real time once it starts:
  a DSP in a JIT recompile loop, the 68k spinning on a host-port sync, the runner catching up
  forever after a stall, or the patch itself. First: `G1_CYCPROF` and the `health` lines while it
  happens; a `perf top` of the host process. Then whether it comes back with the clean ROM.
  Same family: #81 (Mac, Bitwig, load bursts to 99 %, plugin only, standalone fine) and #85 (Mac,
  Ableton, FX). Done when a 30-minute run with the stream's patches has no stretch of dropouts.
- [ ] **S4. macOS: join the host's audio workgroup.** Gearmulator `78766b46` (BUG-10380): on Apple
  Silicon, threads outside the host's audio workgroup are put on the efficiency cores when the
  host looks idle, and the host then waits for late audio: dropouts in the plugin, not in the
  standalone, worse on macOS 27. That is #81 and #85 exactly. Our DSP core already has
  `dsp56k::AudioWorkgroup` (dsp56300 `4f15f23c`); the runner's worker and the DSP threads do not
  join it. Port `pluginLib::HostAudioWorkgroup` (looks up the IO workgroup of the audio device in
  the process once a second; works for VST3 and CLAP, not only AU). Done when a Mac tester's
  dropouts drop, or with a measure on a Mac in CI.
- [ ] **S5. Overload is visible.** #64: the status bar turns red; confirm under a real overload
  (S3 gives one) and close #64.

## Gate: patches go in and out

- [ ] **S6. Upload from NME to the plugin.** #81: `Unfinished bubble` (code 6) still after
  alpha.25 on Mac, plugin only. With S3/S4 out of the way, retest; if it persists, the upload
  must survive late frames (the runner drops late frames; a PC Port byte must never be among
  them). Test: `tools/upload-e2e.sh` with a forced stall in the runner. Done when 100 uploads in a
  row with stalls injected all ACK.
- [x] **S7a. The slot keeper ate the editor's answers on slot A** (found 2026-11-03): a patch list
  or a Store answered while the keeper was reading slot A was hidden from the editor, so a synth
  refresh took seconds and a Store looked lost. Fixed in `app/slotkeeper.cpp`; test:
  `g1slotkeepertest` step 7. Still to retry by hand in Bitwig with NME (S7, S8).
- [ ] **S7. Open a `.pch` in NME and send it to slot A** (stream). Repro on the clean ROM, plugin
  and standalone; if only the plugin, look at the `SlotKeeper` (does it speak in the middle of the
  upload?) and at the PC Port after a reload (S9). Done when it loads every time in 20 tries.
- [ ] **S8. Store into a bank.** Stream, and #48 (JuliusLC: assigning knobs then storing corrupts
  the patch or stores nothing). The emulator answered Store correctly in the capture of
  2026-10-24; repeat with the plugin and with knob/CC assignments in the patch. Done when Store,
  re-read and reload give the same patch, plugin and standalone.
- [ ] **S9. CLAP reload keeps the PC Port.** When the last instance in the process closes, keep
  JUCE's MIDI alive (do not let `MessageManager`/`MidiDeviceListConnection` shut down: hold a
  process-wide reference), or recreate the port without JUCE. Fix the **instance 0** numbering at
  the same time. Test: `g1vst3check` with open, close all, open again, and the PC Port answering.
- [ ] **S10. Copy and paste in NME.** "Syntax error" is NME's paste parser, not the G1; but the
  plugin stopping to answer after it is ours. Repro with the clean ROM; if the paste sends a
  stream the OS rejects, NME gets an issue and G1-Emu must recover (the OS stays answering).
- [ ] **S11. G1-Emu FX errors while editing** (#85, Ableton, Mac). Repro with the attached patch:
  audio in to out, add modules while audio plays. Get the error text first (it is not in the log
  attached).

## Gate: projects come back as they were

- [ ] **S12. Slots on/off and voices after a project reload** (#75). The slots come back; the
  voices allocated per slot still need the patch reloaded (Waltercalling's second gif).
  Done when `g1vst3check --slots` covers voices too, and #75 is retested in Cubase.

- [x] **S13. The Micro's slot after a project reopen** (2026-11-03): the keeper restored before the
  Micro took patches (it answers RequestPatch ~2.5 s after IAm). Fixed; `g1vst3check --slots` on
  G1-Emu Micro passes.
- [ ] **S14. The plain `g1vst3check` is stale**: it fails on PC Ports (JUCE in one process does not
  list its own ports; `--slots` moved to the direct link for that), silence, restore and octave
  shift, on the committed HEAD too. Bring it up to date so it can gate the release.

## To retest and close (fixed in main or waiting for the reporter)

- [ ] #83 Micro outputs 3/4: silent since `78f3cc5`; ask bacizone to confirm on alpha.28.
- [ ] #80 clipping-like noise with many voices (alpha.14): ask for a retest on the stability build.
- [ ] #65, #59, #53 no audio / no notes in the plugin on Windows (alpha.14-17): ask for the
  stability build with the Test Audio / Test MIDI steps; close if no answer in two weeks.
- [ ] #84 MIDI keyboard in the standalone: documented in `WINDOWS.md`; confirm and close.

## The Micro

- [ ] **M1. Duplicating a Micro track in Bitwig** (Javier, 2026-11-03): a simple kick patch
  (DrumSynth on a MIDI input) played fine on one track; notes arrived badly at first; after
  duplicating the track (for a hi-hat) both instances collapsed and stayed stuck until a restart;
  after changing the OS, heavy dropouts with the CPU full. The log of 14:47-15:05 shows the
  duplicates starting in the same second, an **instance 0** again, and the **99 % lock with the
  clean `micro-os.bin`** at 15:03:54 (11 s, then it recovered): the lock is not G1-Mods' nor the
  Micro's alone, the regular model had it at 17:53. Repro: one Micro instance, duplicate the
  track, play both; then the same on alpha.25 (before the Micro) to see whether it worked then.
- [x] **M1b. Decided 2026-11-03: two plugins.** G1-Emu (Regular/Expanded) and G1-Emu Micro, built from the same sources; done the same day, to be tried by hand.
- [ ] ~~Is the model switch inside one plugin worth keeping?~~ A duplicated or reloaded
  instance starts one model's engine and may swap it for the other when the project's state
  arrives: the riskiest path in the plugin. Proposal: a separate plugin target for the Micro
  from the same sources (as `g1pluginfx` is), its model fixed at build time, no switch at run
  time. Decide after M1's bisect.
- [ ] **M2. Notes through the PC Port do not follow the pitch** (`NOTES.md`, open since #82).
- [ ] **M3. The Micro without a rack ROM** (see below).

## Booting without a ROM image (#16), found 2026-11-03

The Micro's OS runs today on the **rack's** boot ROM, because the Micro's own was never dumped:
a Micro owner has to have a rack ROM as well, and the update mode for the Micro is a stand-in
we wrote. Tested today with `g1patchtest`: a copy of the rack ROM with **everything after the
2 KB loader (`$0000`-`$07FF`) set to `$FF`** boots, answers NME's greeting, takes Test Audio and
sounds byte for byte the same as with the whole ROM, both for the Micro's OS (`micro-os.bin`,
-54.1 dBFS, 349.3 Hz) and for the rack's 3.03b update OS (-54.1 dBFS, 277.0 Hz). So past boot the
OS reads nothing from the ROM's factory OS or the update utility.

That leaves only the loader. Replace it with **our own** loader (written from the behaviour in
`NOTES.md`, "Boot and memory map": set the chip-selects and SYNCR, copy the OS from the flash at
`$300000` to `$100000`, jump), or set that state from C++ and start the CPU at `$100000`. Then the
**Micro boots from Clavia's Micro updater alone**, and the rack from the 3.03b updater alone, the
way Gearmulator's G2 boots from Clavia's 1.62 updater executable. Steps:

- [ ] R1. Check whether the OS touches `$0000`-`$07FF` after the jump (exception vectors? VBR?).
  Run with that range unmapped after `$100000` is reached and see if anything faults.
- [ ] R2. Our loader (HLE), behind a switch; the ROM path stays as the reference.
- [ ] R3. Gate: golden WAVs, upload e2e and the module battery byte-identical to the ROM boot,
  rack and Micro.
- [ ] R4. Settings: the ROM box becomes "OS": the updater `.exe` (or `.hqx`/image) is enough.

Not part of the stability gate; it goes after it unless it turns out to explain a stream bug.

## Also seen today, not yet understood

- [ ] **Test Audio's pitch differs by OS**: 349.3 Hz on the factory OS and on the Micro, 277.0 Hz
  (four semitones lower) on the rack's 3.03b update OS, same patch, no note. Check on the real G1
  (it runs the update OS) before calling it a bug: a default in the synth settings may differ.

## Upstream (checked 2026-11-03)

- **dsp56300**: our pin `8b731966` is upstream's head; nothing new to take.
- **Gearmulator** (`b3796343`): nothing new in the 68k core. What is worth taking: the audio
  workgroup (S4); `46bbb720`, a block with more than 16,384 MIDI events hung (check our runner's
  queue with a burst of that size); the N2X's DSPs in parallel (`f8b0be2c`) we already do.

## The release check (all of it, on the clean ROM)

Automated, before tagging:

- [ ] `ctest --test-dir build --output-on-failure`
- [ ] `tools/bench/golden.sh check` (regular and expanded)
- [ ] `tools/upload-e2e.sh`, and the module battery (`tools/battery/battery.py`)
- [ ] `g1vst3check` on the built `.vst3`: plain, `--slots`, `--buffer-change`, `--clock`, `--fx`
- [ ] `clap-validator` on the `.clap`
- [ ] `g1runnertest` (bit-identical across block sizes)

By hand, per host (Bitwig CLAP and VST3 on Linux; Ableton and Bitwig on a Mac; Reaper and Cubase
on Windows, through testers):

- [ ] Two instances, NME connected to each; send, edit, Store, re-read banks.
- [ ] Close every instance, open one again: the PC Port is there (S9).
- [ ] Save the project, close, reopen: patches, slots, voices, knobs, synth settings.
- [ ] 30 minutes playing a busy patch: no dropout stretch, load figure sane.
- [ ] Micro: the same with one instance.
