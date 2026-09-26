# G1-Emu v0.1.0-alpha.6

**This pre-release fixes an upload timeout that alpha.5 still had with heavier patches.** It is
built and DSP-tested in CI on Linux x86-64 (native and JUCE backends), Linux arm64, macOS universal
and Windows x86-64. The fix was found and verified on Linux, with the patch attached to
[issue #4](https://github.com/animatek/G1-Emu/issues/4); it is in the DSP emulation shared by every
platform, but has not been tried on a real Mac or Windows machine yet. If uploads still time out
for you, please say so on #4 or [issue #3](https://github.com/animatek/G1-Emu/issues/3) with the
patch attached.

## What alpha.6 fixes

- **Uploads of patches that keep a DSP busy.** When a patch fills a DSP's time, the emulated DSP
  never got round to the editor's commands, so the last packet of the upload was never
  acknowledged and the editor reported `Upload timeout at packet N`. The patch from #4 now uploads
  completely, like the 71 sample patches used for testing.

## Also in this build (from alpha.5)

- Two other upload timeouts fixed: a deadlock after a DSP had been idle for a long time, and a
  command to a busy DSP being thrown away.
- The sawtooth of `OscA`, `OscB` and `OscSlvC` sounds again on every platform.
- With `G1_MIDI_LOG=1` the JUCE MIDI backend (macOS and Windows) reports a SysEx that reached it
  truncated.

## Windows quick start

Read `WINDOWS.md` inside the ZIP for the complete step-by-step version. In short:

1. Install and run [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html).
2. Create `G1→NME` and `NME→G1`.
3. In G1-Emu Settings choose **PC Port out = G1→NME** and
   **PC Port in = NME→G1**, then restart G1-Emu.
4. In Animatek NME choose **MIDI INPUT = G1→NME** and
   **MIDI OUTPUT = NME→G1**, then press **Connect**.

The two cables must remain separate. One shared loopback port can return a program's own output to
its input and make the traffic counters misleading.

## It brings no ROM, and it never will

G1-Emu emulates the hardware, not Clavia's software. You need your own 512 KB ROM dump from a Nord
Modular **rack** running OS 3.03. The program asks for it on first start. Do not distribute the ROM
with G1-Emu or upload it to the project.

The emulated synth also starts with empty patch slots. Use a compatible editor, such as
[Animatek NME](https://github.com/animatek/Animatek-NME), to create or upload a `.pch` patch over
the PC Port.

## Nothing is signed

- **macOS:** Gatekeeper may say the developer is unidentified. Right-click the app and choose
  **Open**, or run `xattr -dr com.apple.quarantine G1-Emu.app`.
- **Windows:** SmartScreen may warn. Choose **More info**, verify that the file came from this
  GitHub release, and choose **Run anyway**.

Signing and notarisation are not done yet.

## Platforms

| System | Audio | Editor MIDI | Status |
| --- | --- | --- | --- |
| Linux | JACK/PipeWire or ALSA | ALSA sequencer ports | used daily |
| macOS 11+ | CoreAudio | CoreMIDI virtual ports | verified on real hardware |
| Windows 11 | WASAPI/DirectSound/ASIO | two loopMIDI cables | verified on real hardware |

The macOS package is universal (Apple Silicon and Intel). Windows' loopMIDI step is temporary:
[Microsoft/MIDI issue #1047](https://github.com/microsoft/MIDI/issues/1047) is fixed upstream but
awaiting the November 2026 Windows release.

## Files

`G1-Emu` / `G1-Emu.exe` is the panel application. `g1run` / `g1run.exe` is the console front end.
Every archive contains the README and GPLv3 licence; the Windows ZIP also contains `WINDOWS.md`.
The source of this build is the tag attached to the release.
