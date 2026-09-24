# Patch upload timeouts (issues #3 and #4)

Written on 2026-09-24 as a handover: the diagnosis so far and the steps to finish it on a machine
with the build environment, a ROM and NME. Nothing here has been run yet.

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
