# Patch upload timeouts (issues #3 and #4)

Written on 2026-09-24 as a handover: the diagnosis so far and the steps to finish it on a machine
with the build environment, a ROM and NME. The procedure below has not been run yet; what has
been run is in "Already checked on Linux" at the end.

## What is reported

- **#3 (macOS, alpha.4):** the NME handshake works, but every patch upload ends in
  `Upload timeout at packet 0` (once packet 2), whatever the patch size (861 B, 1020 B, 4960 B).
  The emulator stays healthy.
- **#4 (Linux, Windows and macOS):** no saw from MultipleOscA/B, and an upload timeout when the
  patch has several modules. The timeout on **Linux** matters: Linux uses the native ALSA backend,
  not `app/jucemidi.h`.

## Why the fix proposed in #3 would not work

#3 blames the empty `handlePartialSysexMessage` override in `app/jucemidi.h` and proposes filling
it. With the JUCE our builds use (8.0.12, `.github/workflows/build.yml`), that callback is never
reached from a `MidiInput`:

- On current macOS JUCE creates the virtual port with `MIDIDestinationCreateWithProtocol`
  (`juce_CoreMidi_mac.mm`). CoreMIDI hands it UMP packets, not byte fragments.
- `MidiInput::Impl::consume` (`juce_MidiDevices.cpp`) runs every packet through
  `ump::ToBytestreamConverter`, whose `SingleGroupMidi1ToBytestreamTranslator`
  (`juce_UMPMidi1ToBytestreamTranslator.h`) collects SysEx7 packets until the end packet. It then
  calls `handleIncomingMidiMessage` once, with the whole message. A SysEx that does not end in
  `F7` is dropped inside JUCE, silently.
- `handlePartialSysexMessage` is only called by `MidiDataConcatenator`, which `MidiInput` does not
  use on this path.

So filling the override is harmless but changes nothing. **Do not ship it as the fix for #3.**
Timing is ruled out too: NME waits 5 s for each ACK (`uploadAckTimeoutMs` in NME's
`source/midi/ConnectionManager.h`).

## The test that tells the three cases apart

Upload one small patch that fails, with both programs logging:

```bash
G1_MIDI_LOG=1 ./g1run          # or ./build/app/g1run; prints a [midi] line per chunk in and out
```

- NME prints `[UPLOAD] packet N/M size=... hex: ...` on stdout for every packet it sends.
- Everything that arrives on the PC Port is also appended to `pcport-in.bin` next to the flash
  (`~/.local/share/Animatek/G1-Emu/` on Linux and macOS, `%APPDATA%\Animatek\G1-Emu\` on Windows).
  Delete it first so it holds only this session.

Then compare the three logs:

| What the logs show | Where the fault is | What to do next |
| --- | --- | --- |
| NME prints `[UPLOAD] packet 0`, but there is **no** `[midi] in  PC Port` line with those bytes | Between NME and the emulator: JUCE/CoreMIDI drops the packet | Build `-DG1_BACKEND=juce` on Linux and repeat. If it also fails there, it is our JUCE input path; if not, it is macOS-only. Check that no byte of the packet between `F0` and `F7` is `>= 0x80` (any status byte other than real-time ends a SysEx, and JUCE then drops it). |
| The packet **does** arrive (`[midi] in  PC Port ... f0 33 ...`), but **no** `[midi] out PC Port` ACK follows | The emulated G1 does not answer | Same cause as #4 on Linux, most likely. Reproduce on Linux with `g1patchtest` and the reporter's `.pch`, replay `pcport-in.bin`, and look at what the OS does with the packet (`NOTES.md`, `G1_WATCH`). |
| The ACK **is** sent (`[midi] out PC Port`), but NME still times out | Between the emulator and NME | Check what NME's input receives (its MIDI monitor), and the framing in `JuceMidi::send` (`port.pending`, `messageLength`). |

Whatever the result, post a short note on #3 with it. The reporter offered to test a patched build
on their Mac, and can also send the three logs above.

## Already checked on Linux (2026-09-24)

- Built with `-DG1_BACKEND=juce` on Linux and sent SysEx of 5005 and 3005 bytes to the virtual PC
  Port: each arrived whole, in one chunk (`[midi] in  PC Port 5005 bytes`). So the JUCE input
  `Collector` does not lose data by itself. Linux reaches it through ALSA, not CoreMIDI, so this
  says nothing about the macOS path.
- `g1patchtest` uploads a 4.2 KB patch in 8 packets and gets every ACK, with the native backend.
  That is the emulated G1 with no MIDI in between, which points the timeouts at the MIDI path or at
  what NME sends, not at the OS on a plain upload.
- With `G1_MIDI_LOG=1` the JUCE backend now also prints `truncated SysEx, N bytes dropped by JUCE`
  when a message reaches JUCE without its `F7`. That is the line to look for in the first row of the
  table above. Not seen firing yet: no tool here sends a SysEx without `F7`.

## Second pass on Linux, over real ALSA ports (2026-09-24)

`g1patchtest --dump-packets DIR` writes the exact SysEx NME would send, one file per packet, and
`tools/upload-e2e.sh patch.pch` replays them with `aseqsend` against a running `g1run` (native
backend, a copy of the flash), so the upload goes through `app/alsamidi.h` like NME's does.

- The 71 `.pch` files under `../Nomad2026` were uploaded with `g1patchtest`: 68 finish. Packets are
  197 bytes and the biggest patch is 17 packets. `future303` and `progger` seemed to lose packets 8
  and 9 only because the bench waits 300 emulated ms for a reply; with `G1_ACKMS=15000` they finish,
  and the slowest packet takes 812 ms (the OS loading code into the DSPs). NME waits 5 s per packet,
  so the OS being slow is not what times out.
- `D Trama Viva` (5 packets) over ALSA: all five arrive whole (197, 197, 197, 197 and 19 bytes) and
  the OS answers each. The native backend does not lose a 197-byte SysEx.
- **`nmedit/jMod/src/patches/korg.pch` deadlocked the emulator: found and fixed.** After packet 3
  the OS answers `f0 33 50 06 01 05 0b 00 00 00 0a f7` (a `VoiceCount` NMInfo, 11 voices, not an
  error) and packet 4 never got an answer. It happened in `g1patchtest`, and in `g1run` over ALSA,
  which then ignored SIGINT and SIGTERM. Under gdb the main thread sat in `Hdi08::read8` →
  `Dsp::readIsr` → `Dsp::runUntil` → `Essi::execTX` → the core's TX write callback, waiting on a
  semaphore, while the three workers idled in `Microcontroller::workerLoop`. `G1_THREADS=0` hung
  the same way, so it was not the threading. The TX ring (32768 frames) really was full: logging
  the cycle count of every TX frame showed bursts of frames at one cycle count, after gaps of
  335 thousand and then 434 million cycles in which DSP 0's ESSI emitted nothing. The clock's
  catch-up loop paid all of that debt at once when the port woke up. Fix: `cmake/Dsp56300.cmake`
  (`NOTES.md`, "DSP core fixes"), plus `g1dspcheck` "idle link port", which fails without it.
  Checked after the fix: `korg.pch` uploads without hanging, `g1run` over ALSA exits on SIGINT,
  the other patches measure the same (SimpleOSC −61.8 dBFS at 262 Hz, `A Nucleo Duro` −78.4 dBFS at
  25 Hz, `Conexiones encadenadas` −59.5 dBFS at 262 Hz), and `ctest` passes.
- **`korg.pch`'s last packet got no ACK: a host command was being dropped (found and fixed).** The
  real G1 accepts this patch: Javier loaded it from NME into slot A of the real synth on 2026-09-25
  and all four packets were ACKed (the packets NME sent are byte for byte the ones
  `--dump-packets` writes, checked for three of the four). It then reports `VoiceCount 9 1 1 0`:
  slot A got 9 voices because slots B and C already held patches; alone in the emulator it gets 11.
  So the fault was ours. The display showed the patch loaded and the 68k spun in `$10c354`, polling
  bit 0 of DSP 0's HI08 status (`$200000`, `RXDF`) after sending it host command `$76`. DSP 0 never
  serviced it because `Dsp::hostCommand` had thrown it away: it waits up to 200,000 cycles for
  `hasPendingInterrupts()` to go false and drops the command if it does not, and that predicate is
  also true while the DSP is inside an interrupt. A DSP whose sample routine fills the block never
  leaves it (the idle loop `$016c`/`$016e` takes 95 % of PC samples for a small patch, about 6 % for
  `progger`, under 5 % for `korg`). Logging showed exactly one drop, `$76` on DSP 0, and none for
  the healthy patches. Fix: wait only for the external interrupt queue (the 32 entries the wait was
  there to protect) to empty, with `hasPendingExternalInterrupts()`.
  Checked after the fix: all 71 sample patches upload with no missing reply (68 before), `korg.pch`
  included; over ALSA against `g1run` its four packets get their ACKs (one `0x36`, three `0x7f`, as
  on the real G1) and `g1run` exits on SIGINT; the five reference patches measure the same
  (SimpleOSC −61.8 dBFS at 262 Hz among them); `ctest` passes.
  It also explains what looked like a threshold: removing one module at a time (with its cables,
  parameters and names; morphs untouched) made the last packet answer for 10 of the 14 voice-area
  modules and for none of the six in the FX area, which fits a DSP that is only just full and gets
  a gap when most modules go. Ruled out on the way, with copies of `korg.pch`: the voice count
  (asks for 32, gets 11; 4, 8 and 10 fail the same way) and the size of the last packet. **Not
  known:** whether a real DSP is also that close to full with this patch or the emulator charges
  more cycles per module than the `cycles` in `modules.xml` says; the host command now gets through
  either way. #4 (several modules) could be this, the deadlock above, or neither: the reporter's
  patch is not known.
- Still not reproduced anywhere: #3 (a Mac, packet 0, so not a DSP problem). NME was not running in
  this pass, so nothing here used the editor itself.

## On a real Mac (2026-09-25)

Javier's Intel Mac, macOS 13.7.8, the prebuilt `v0.1.0-alpha.4` universal `g1run` with a scratch
flash, and the Animatek NME 0.18.0 app. **Uploads work there.** Two patches: one packet to slot A
(`f0 33 7c …`, 141 bytes, first and last) and two packets to slot B (`f0 33 75 …` and
`f0 33 79 …`, 197 bytes each). Each arrived whole on `G1-Emu PC Port` and got its ACK (`36`, then
`7f`), and NME then re-sent the knob assignments (cc `$25`), which it only does after "All packets
sent and ACKed" (`replayPanelAssignments`). NME's own console was not captured; the MIDI log is
enough. So CoreMIDI does not split or drop a 197-byte SysEx on Intel with macOS 13.

What still separates this from #3: Apple Silicon (M4 Pro), macOS 26.7, and NME built from source
(`94874fc`). Asked the reporter for their logs and for one of the failing patches (issue comments of
2026-09-24 and 2026-09-25).

Aside: `g1run` came up with no sound (`audio: no sound: No such device: default`). It appears to
have read Javier's `settings.conf`, whose `default` device is the Linux one. Not related to #3.
- Read in JUCE 8.0.12: the virtual-port input path (`juce_CoreMidi_mac.mm`, `MidiInput::Impl::consume`)
  returns without a word if its spin lock is contended (`ScopedTryLockType`). Nothing takes that
  lock in steady state, so it is noted and not a suspect.


## #4's WavetableSynth.pch (2026-09-26)

- With alpha.5 its last packet got no ACK. Cause: a DSP whose sample routine fills the block never
  took the host command (see `NOTES.md`, "A busy DSP starved its host commands"). Fixed in
  `v0.1.0-alpha.6`. The real G1 loads the patch with 11 voices and its envelopes gate.
- Afterwards the emulated G1 showed `Error` and 1 voice with this patch, but only under
  `g1patchtest`: the bench sent each packet as soon as the OS answered anything, and after the
  fifth packet the OS first reports the voice count, then resets the PC Port receiver, then ACKs.
  The sixth packet was thrown away by that reset, the upload stayed open (`$160caa` = 2), and the
  next message was taken for a new upload (error 6, which prints `Error`). NME waits for the ACK,
  and so does the bench now: the patch loads (`WavetableSnth`, 11 voices) and sounds.
- `tools/upload-e2e.sh` still sends with a fixed pause, not waiting for ACKs, so a patch that makes
  the OS reload the DSPs can lose a packet there too; read its result with that in mind.
