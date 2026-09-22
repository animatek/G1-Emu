# G1-Emu v0.1.0-alpha.4

**This is the first pre-release verified end to end on Linux, macOS and Windows.** The same tag is
built and DSP-tested in CI on Linux x86-64 (native and JUCE backends), Linux arm64, macOS universal
and Windows x86-64. The window, audio and Animatek NME handshake have now also been exercised on
real macOS and Windows machines; macOS has additionally completed patch uploads.

## What alpha.4 adds

- Windows can manually pair each half of the **PC Port** and regular **MIDI** port with an existing
  system MIDI device. This is the practical bridge to loopMIDI while Microsoft issue #1047 blocks
  G1-Emu's application-owned Windows MIDI endpoints.
- The Windows ZIP includes `WINDOWS.md`, a complete first-run guide: install loopMIDI, create the
  two directional cables, configure G1-Emu and NME, restart after device changes, upload a patch
  and diagnose a timeout from the byte counters.
- Windows settings and flash now use `%APPDATA%\Animatek\G1-Emu` instead of accidentally following
  the process's working directory. Opening the same executable from Explorer and a terminal no
  longer creates two unrelated configurations.
- The Windows audio settings list the real JUCE device types and devices, including WASAPI,
  DirectSound and installed ASIO drivers.
- The JUCE MIDI backend preserves fragmented SysEx replies, so NME receives the complete handshake
  and larger PC Port messages.

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
