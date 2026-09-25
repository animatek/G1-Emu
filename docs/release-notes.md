# G1-Emu v0.1.0-alpha.5

**This pre-release fixes patch uploads that timed out and oscillators whose sawtooth was silent.**
It is built and DSP-tested in CI on Linux x86-64 (native and JUCE backends), Linux arm64, macOS
universal and Windows x86-64. The fixes were found and verified on Linux; they are in the DSP and
68k emulation shared by every platform, but they have not been tried on a real Mac or Windows
machine yet. If uploads still time out for you, please say so on
[issue #3](https://github.com/animatek/G1-Emu/issues/3) or
[issue #4](https://github.com/animatek/G1-Emu/issues/4) with the patch attached.

## What alpha.5 fixes

- **Uploads that timed out.** Two emulator bugs ended as `Upload timeout at packet N` in the
  editor. A DSP that had been idle for a long time could deadlock the emulator when it woke up, and
  a command to a DSP with a full sample routine was thrown away, so the last packet of some patches
  was never acknowledged. Every one of 71 sample patches now uploads; one of them that failed
  before is accepted by a real Nord Modular too.
- **The sawtooth.** `OscA` and `OscB` on the saw waveform, and the sawtooth slave `OscSlvC`, gave
  a constant level instead of a wave, on every platform. It was a bug in the DSP emulator's
  just-in-time compiler, not in the patch: they now sound like the other waveforms.
- **The upload log.** With `G1_MIDI_LOG=1` the JUCE MIDI backend (macOS and Windows) also reports a
  SysEx that reached it truncated, which helps tell a MIDI problem from an emulator one.
- On Windows, the ROM folder falls back to `%USERPROFILE%` when `HOME` is not set.

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
