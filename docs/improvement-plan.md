# Improvement plan (2026-10-19)

**Status (2026-10-24):** tasks 2a, 3a, 3b and 3c are done and merged, checked against the golden WAVs.
Task 1 was reproduced in `g1pchtest` and is **not a bug**: the OS takes NME's greeting and a
RequestPatch in the middle of a Presets upload, so step 1.2 is not needed. Tasks 2b, 4 and 5 are open.

A code review of the core (`g1Lib/g1mc.cpp`, `g1Lib/g1dsp.cpp`), the plugin's runner, the audio
bridge and the PC Port links found no serious faults, but a few things worth doing. This file is
the step-by-step plan, written to be carried out by someone (or a model) who did not do the review.
**Do the tasks in order, one commit per task, each with its line in `CHANGELOG.md` in the same
commit** (see `CLAUDE.md`, "Changelog rule"). Everything written into the repo is in English.

Branch: `claude/peaceful-clarke-tli8o7`. Push with `git push -u origin claude/peaceful-clarke-tli8o7`.
Do not open a pull request unless asked.

## Before starting

1. The build needs Gearmulator (`GEARMULATOR_DIR`, default `~/src/gearmulator-md-mm`) and the
   DSP56300 core (`G1_DSP56300_DIR`, default `~/src/dsp56300-upstream`). If they are missing
   (`cmake -B build` stops with "mc68k not found"), **you cannot build or test**: do only the tasks
   marked *no build needed*, say clearly in the changelog entry that it was not compiled ("not
   built here: Gearmulator missing in the session"), and tell the user which tasks still need a
   build on the maintainer's machine. Never claim a test passed if it did not run.
2. With the dependencies present:
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build -j$(nproc)
   ctest --test-dir build --output-on-failure      # baseline: note which tests pass/skip BEFORE any change
   ```
   Tests that need a ROM exit with 77 (skipped) when there is none; that is not a failure.
3. If a ROM is available, record the golden output once before touching the core:
   `tools/bench/golden.sh record` (then `tools/bench/golden.sh` checks against it). Check the
   script's header for the exact mode names before using them.

---

## Task 1: Presets uploads protected from the editor (possible bug)

**What.** Issue #75 found that a message from the editor arriving between two packets of an upload
makes the OS drop the upload half done. The runner holds the editor's bytes while the
`SlotKeeper` uploads (`app/plugin/runner.cpp`, the line
`if(!m_editorHeld.empty() && !(m_keeper && m_keeper->uploading()))`), but **not** while the
`PresetsLink` uploads (the Presets page's "Load .pch", `PresetsLink::upload`, several packets,
`State::Uploading`). The standalone (`app/emuhost.cpp`, the loop around `m_midi->poll(incoming)`)
never holds them. Animatek NME greets the synth (IAm) every few hundred ms, so an upload from the
Presets page with NME connected may fail. **Not reproduced yet**: step 1 does that.

### Step 1.1: reproduce in `app/pchtest.cpp` (needs build and ROM)

In `app/pchtest.cpp`, after the existing upload check (the block that ends with
`check(told, "the editor is told of the new patch in slot A (NewPatchInSlot)");`), add a **second
upload with an editor talking during it**, modelled on `app/slotkeepertest.cpp` (search for
`editorOnUpload`):

- A local `std::vector<uint8_t> editorHeld;` and a flag `bool hold = false;` that the `run` lambda
  uses: when the test injects the editor's bytes, if `hold && link.uploading()` they stay in
  `editorHeld`; otherwise they go `mc.getPcPort().receive(...)` + `link.editorSent(..., now)`.
  The `run` lambda delivers `editorHeld` as soon as `!link.uploading()`.
- Inject, once `link.uploadStatus().busy` is true and at least one packet has gone (poll every
  1 ms in `run`), NME's connect bytes, the same ones `slotkeepertest.cpp` uses:
  `{0xf0,0x33,0x00,0x06,0x00,0x03,0x03,0xf7, 0xf0,0x33,0x5d,0x06,0x41,0x35,0x7c,0xf7}`.
- Upload to another place (for example `link.upload(1, up.frames, up.abort, 8, 97)`, slot B,
  bank 9 position 98) and check `status.ok` and that bank 9 position 98 has the name afterwards.
- Run it twice: with `hold = false` (today's behaviour) and with `hold = true` (the fix).
  `G1_PCH` can name a bigger patch (several packets) so the injection lands between packets; use
  one with at least 3 packets (the test prints the packet count).

**Decision point.**
- If the `hold = false` run **fails** (upload not OK, or the name not stored): the bug is real.
  Keep both runs in the test, the `hold = false` one only printed as information (not a `check`),
  the `hold = true` one as a `check`, and go on with step 1.2.
- If it **passes**: the OS tolerates it for this upload; do **not** do step 1.2. Keep the test
  with `hold = false` as a `check` (it guards the behaviour), write that finding in the changelog
  and in `NOTES.md` near the #75 text, and move on to Task 2.

### Step 1.2: the fix

1. `app/presetslink.h`, public section, next to `uploadStatus()`:
   ```cpp
   // A patch going in packet by packet: anything else sent to the PC Port meanwhile makes the OS
   // drop it (#75), so the editor's bytes wait (runner, EmuHost). Worker thread.
   bool uploading() const { return m_state == State::Uploading; }
   ```
   (`m_state` is `std::atomic<State>`, so reading it is safe.) Same shape as
   `SlotKeeper::uploading()` in `app/slotkeeper.h`.
2. `app/plugin/runner.cpp`: change the hold condition to
   ```cpp
   const bool uploading = (m_keeper && m_keeper->uploading()) || (m_presets && m_presets->uploading());
   if(!m_editorHeld.empty() && !uploading)
   ```
   and extend the comment above it ("not while the keeper uploads a slot") to say "or the Presets
   page uploads a patch".
3. `app/emuhost.cpp` (standalone): it has no `m_editorHeld`. Add a member
   `std::vector<uint8_t> m_editorHeld;` in `app/emuhost.h` next to the other run-loop state, or a
   local next to `out, toEditor, toG1, linkIn` in the run function (local is simpler; that is where
   the other buffers live). Then:
   - Where the PC Port bytes arrive (`if(!incoming[m_pcPort].empty())`): keep the counting,
     `logMidi` and `pcLog` lines as they are, but replace the `receive` + two `editorSent` calls
     by appending the bytes to the held buffer.
   - The same for the direct link (`linkIn`): append instead of `receive` + `editorSent`.
   - Right after both, deliver when allowed:
     ```cpp
     if(!editorHeld.empty() && !m_presets.uploading())
     {
         mc.getPcPort().receive(editorHeld);
         m_synthSettings.editorSent(editorHeld, mc.ucCycles() / msCycles);
         m_presets.editorSent(editorHeld, mc.ucCycles() / msCycles);
         editorHeld.clear();
     }
     ```
   Do not change anything else in that loop.
4. Build, run `ctest`, `build/app/g1pchtest*` (find the binary with
   `find build -name g1pchtest -type f`), `g1presetstest`, `g1slotkeepertest`, `g1runnertest`.
5. Changelog: `- [Fix] **...** (Claude, from a code review)`: what could happen, what changed in
   the runner and the standalone, how it was checked (the new pchtest case before and after).

---

## Task 2: debug-only work out of the hot path (no build needed to write, build to verify)

### 2a. The host-port trace only when asked

`Microcontroller::traceHost` (`g1Lib/g1mc.cpp`) appends every HI08 access to `m_hostTrace`, up to
200 000 entries (about 3 MB per instance), in every front end. Only `tools/g1boot.cpp` reads it
(`mc.hostTrace()`).

1. `g1Lib/g1mc.h`: next to `hostTrace()`, add
   ```cpp
   // Off by default: only g1boot looks at it.
   void setHostTrace(const bool _on) { m_traceHostOn = _on; }
   ```
   and a private member `bool m_traceHostOn = false;` next to `m_hostTrace`.
2. `g1Lib/g1mc.cpp`, `traceHost`: first line `if(!m_traceHostOn) return;`.
3. `tools/g1boot.cpp`: right after `g1::Microcontroller mc(rom, ...);` (around line 62) add
   `mc.setHostTrace(true);`. **Important:** the trace must be on before `mc` runs any instruction,
   so put it immediately after the construction.
4. Verify: `./build/tools/g1boot Roms/NORD-MODULAR-RACK-VER-3.03.BIN 60` prints the same host-port
   report as before (save the output before the change and `diff` it after).

### 2b. Known panel addresses not logged as unknown

In `g1Lib/g1mc.cpp`:
- `read16`: the `logUnknown(addr, false, 0);` line runs before `if(addr == g_panelIn) return buttonRow();`.
  Move the `g_panelIn` check above `logUnknown`.
- `read8`: same, move both the `g_panelIn` and the `g_panelAdc` blocks above `logUnknown`.
- `write8`: `if(addr == g_panelOut) m_adcSelect = _val;` falls through to `logUnknown`. Add
  `return;` after it, inside braces, like the branches below it.

This only changes `g1boot`'s "unknown accesses" list (the panel addresses disappear from it) and
removes a `std::map` lookup per panel access. Verify with the `g1boot` run of 2a: the diff must show
only `$201800`, `$202000` and `$202800` gone from the unknown list. Grep `NOTES.md` for
"unknown" to see whether any text relies on those addresses being in that list; adjust it if so.

Commit 2a and 2b together or separately, each with its changelog line (`[Perf]` or `[Tools]`).

---

## Task 3: small robustness fixes in the core (needs build; golden check if a ROM is there)

### 3a. PIT registers written a byte at a time

`write16` captures PICR (`$FFFA22`) and PITR (`$FFFA24`) into `m_picr`/`m_pitr`; `write8` does not.
OS 3.03 writes them as words, but a byte write would stop the system clock silently. In
`g1Lib/g1mc.cpp`, `write8`, inside `if(isInternalPeripheral(addr))`, before `Mc68k::write8`, add
(68k is big endian: the even address is the high byte):
```cpp
if(addr == 0xfffa22) m_picr = static_cast<uint16_t>((m_picr & 0x00ff) | (_val << 8));
if(addr == 0xfffa23) m_picr = static_cast<uint16_t>((m_picr & 0xff00) | _val);
if(addr == 0xfffa24) m_pitr = static_cast<uint16_t>((m_pitr & 0x00ff) | (_val << 8));
if(addr == 0xfffa25) m_pitr = static_cast<uint16_t>((m_pitr & 0xff00) | _val);
```
Expected effect on today's OS: none. Check with `tools/bench/golden.sh` (identical WAVs) and
`ctest`.

### 3b. The DSP workers' wake-up, formally correct

In `Microcontroller::launchDsp` the CPU bumps `job.generation` with `memory_order_release` and then
reads `m_sleepers` (seq_cst); the worker increments `m_sleepers` and then reads `generation`. That
pattern is only guaranteed with **both** sides seq_cst; otherwise, on ARM, a worker can go to sleep
having missed the new job. It never hangs (`waitDsp` then runs that DSP on the CPU thread), but that
sync loses its parallelism. Change, in `launchDsp`:
```cpp
job.generation.fetch_add(1, std::memory_order_seq_cst);	// seq_cst: pairs with ++m_sleepers in workerLoop
```
Nothing else. Check with `ctest` and `tools/bench/golden.sh`.

### 3c. Tidying (no behaviour change)

- `g1Lib/g1dsp.cpp`: the includes at the top list `<algorithm>` and `<cstdlib>` twice; remove the
  second ones.
- `g1Lib/g1mc.cpp`, `Microcontroller::exec()`: the condition `(m_ucCycles & 0x3ff) < cycles` is
  tested twice in a row; join the two `if` blocks into one (PC trail first, then `catchUpDsps()`,
  same order as now).

3a, 3b and 3c can be one commit (`[Fix]`/`[Code]`), with the golden check named in the changelog.

---

## Task 4: measure before changing: host ports ticked on a Regular (needs build, ROM, bench)

`Microcontroller::exec()` runs `port.exec(cycles)` on all 8 HI08 ports after every 68k
instruction, also on a Regular, where ports 4-7 have no DSP. **Do not change it blind.**

1. Read `exec` in `${GEARMULATOR_DIR}/source/mc68k/hdi08.cpp`. If it changes anything the OS can
   read on a port with no DSP (ISR bits, interrupts), stop here: write the finding in `NOTES.md`
   and leave the code as it is.
2. If it does nothing useful there, try `for(uint32_t i = 0; i < m_dspCount; ++i) m_hostPorts[i].exec(cycles);`
   and measure with `tools/bench/bench.sh` (before and after, same machine, three runs each) and
   check `tools/bench/golden.sh` (identical output) and that the Regular still boots with
   `g1boot` (the OS probes ports 4-7 at boot and must still find them silent).
3. Keep it only if it is measurably faster and the golden check passes; report the figures in the
   changelog either way.

---

## Task 5 (ask the maintainer first): one coordinator for the PC Port links

`SlotKeeper`, `SynthSettingsLink` and `PresetsLink` share one interface
(`editorSent`, `g1Sent`, `tick`), and the rules between them and the editor (who may speak, whose
answers are hidden, when the editor's bytes wait) are written twice: with generic lambdas in
`app/plugin/runner.cpp` (`speak`, `keepOwn`) and by hand in `app/emuhost.cpp`. Task 1 exists
because the two copies had drifted. A small class (for example `app/pcportlinks.h`) holding
pointers to the links present, with `editorBytes(bytes, now)`, `fromG1(bytes, now, toEditor)` and
`tick(now, toG1)`, and the hold rule inside, would make both front ends behave the same by
construction, as `audiobridge.h` does for audio. **This is a refactor across the two front ends:
propose it to the user (Javier) and do it only if they say so, as its own commit, after Tasks 1-3.**
Verification: every PC Port test (`g1slotkeepertest`, `g1presetstest`, `g1synthsettingstest`,
`g1pchtest`, `g1runnertest`, `g1directlinktest`) and `g1vst3check plugin.vst3 --slots`.

## Not in this plan (noted for later)

- `app/gui/Panel.cpp` (~2 800 lines) and `app/plugin/Processor.cpp` (~1 300) could be split the
  next time they are worked on.
- About 45 distinct `G1_*` environment variables are read across the code; a table of them in
  `NOTES.md` would help contributors.

## When done

Delete this file in the last commit (with its changelog line), or leave a short "done/left" note
at its top if some task stays open, and tell the user what was done, what was verified and how,
and what could not be run.
