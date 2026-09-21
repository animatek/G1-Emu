**This is a pre-alpha test build, and its only job is to tell us whether the emulator runs on a
machine that is not the developer's.** Everything here compiles and passes the DSP tests on Linux
(x86-64 and arm64), macOS (Apple Silicon) and Windows in CI, but nobody has yet run the macOS or
Windows builds on real hardware. That is exactly what we are asking for.

## It brings no ROM, and it never will

G1-Emu emulates the hardware, not Clavia's software. You need a Nord Modular **rack** OS 3.03 ROM
image of your own (512 KB). The program looks for one when it starts and tells you where to put
it; the README explains the search order. Without a ROM it will not run, and that is by design.

**It also starts empty.** The ROM carries the operating system and nothing else, so every slot
says `Empty patch`. To make a sound you need a patch, and to make a patch you need an **editor**
talking to the emulator over the PC Port — any editor that speaks the G1's protocol.

## Nothing is signed

- **macOS:** Gatekeeper will say the developer is unidentified. Right-click the app and choose
  Open, or run `xattr -dr com.apple.quarantine G1-Emu.app`.
- **Windows:** SmartScreen will warn. "More info" then "Run anyway".

Signing and notarisation are a job of their own and are not done yet.

## What we expect on each system, and what to report

| | Audio | Virtual MIDI ports | Confidence |
| --- | --- | --- | --- |
| Linux | JACK/PipeWire or ALSA | ALSA sequencer | used daily |
| macOS | CoreAudio | CoreMIDI, nothing to install | should work, never run |
| Windows | WASAPI/ASIO | **only with Windows MIDI Services** | the real unknown |

**The macOS build is a universal binary (Apple Silicon and Intel) and needs macOS 11 Big Sur or
newer.** The Apple Silicon half is what CI compiles and tests; the Intel half is built from the
same source and has never been run, so a report from an Intel Mac is especially welcome.

On Windows, JUCE can only create a virtual port through Windows MIDI Services; with the older
WinRT or WinMM backends it cannot, and the status line will say so plainly. If that happens the
emulator still runs and makes sound, but no editor can reach it. Tell us what the status line
says — that answer is worth as much to us as a success.

Useful reports: whether the window opens, whether the audio device is found and sounds, whether an
editor and a DAW see `G1-Emu PC Port` and `G1-Emu MIDI`, and whatever the status bar says.
Open an issue with the system, its version, and the log the program prints at startup.

## Files

`G1-Emu` is the window with the panel; `g1run` is the same emulator in a console. The licence and
the README travel inside each archive.

GPLv3, because it links Gearmulator. The source of this build is the tag this release points at.
