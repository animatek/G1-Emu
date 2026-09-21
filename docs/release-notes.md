# G1-Emu v0.1.0-alpha.3

**This is a pre-alpha test build.** Everything here compiles and passes the DSP tests on Linux
(x86-64 and arm64), macOS and Windows in CI. This release also includes the fixes found during the
first run on a real Mac: an Intel MacBook Pro with macOS 13.7.8 now opens CoreAudio, starts the
window, publishes both CoreMIDI ports and connects to NME over the PC Port.

## What alpha.3 fixes

- CoreAudio no longer hangs when the default input and output are two different devices, as they
  are for the built-in microphone and output on Intel Macs. G1-Emu uses output only in that case.
- `g1gui.sh` starts the executable inside the macOS `.app` bundle.
- The JUCE MIDI backend keeps incomplete messages between calls. PC Port SysEx replies are no
  longer sent early and truncated; NME's handshake now receives all 12 bytes and connects.

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
| macOS | CoreAudio | CoreMIDI, nothing to install | verified on Intel, macOS 13.7.8 |
| Windows | WASAPI/ASIO | **only with Windows MIDI Services** | the real unknown |

**The macOS build is a universal binary (Apple Silicon and Intel) and needs macOS 11 Big Sur or
newer.** The Intel half has been run on macOS 13.7.8 and connects to NME. Apple Silicon compiles
and passes the DSP test in CI, but a report from a real Apple Silicon Mac is still welcome.

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
