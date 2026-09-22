# Building on Windows

Use Visual Studio 2022 Build Tools with the C++ x64/x86 tools and a Windows SDK,
CMake 3.22 or newer, Git, and JUCE 8.0.12. WSL is not required. The commands below
run from the G1-Emu repository in PowerShell and keep dependencies and outputs in
ignored build directories. They use the same Gearmulator tag as CI.

```powershell
git clone --branch mdmm-v0.1.0-alpha.13 --depth 1 --recurse-submodules --shallow-submodules https://github.com/joelanders/gearmulator-md-mm.git build-windows-deps/gearmulator-md-mm
# Reuse ../Nomad2026/JUCE if it already contains JUCE 8.0.12.
cmake -S . -B build-windows -G "Visual Studio 17 2022" -A x64 -DGEARMULATOR_DIR="$PWD/build-windows-deps/gearmulator-md-mm" -DG1_JUCE_DIR="$PWD/../Nomad2026/JUCE" -DG1_BACKEND=juce
cmake --build build-windows --config Release --parallel 4
ctest --test-dir build-windows -C Release -L g1 --output-on-failure
```

The panel is `build-windows/app/gui/g1gui_artefacts/Release/G1-Emu.exe`.
The console is `build-windows/app/Release/g1run.exe`.
Neither contains a ROM. Existing Linux build directories cannot be reused on Windows.

List audio and MIDI devices without loading a ROM or opening any MIDI connection:

```powershell
& ./build-windows/app/Release/g1run.exe --list-devices
```

If MSBuild reports duplicate `Path` and `PATH` environment keys, launch CMake with
a deduplicated child-process environment; no permanent system PATH change is needed.
This was necessary in the agent's Windows shell during the first local build.

## Audio and MIDI

Settings separates audio driver and device selection. The Windows build enables
WASAPI, DirectSound and ASIO. ASIO lists only drivers registered on this computer;
enabling ASIO in the emulator does not install the sound card manufacturer's driver.
Device changes apply after restarting the emulator. This is an application using
the sound card's driver, not an installed virtual audio driver.

The emulator must publish its own two MIDI ports, like the instrument it emulates.
It does not open loopMIDI cables or physical MIDI devices as a fallback. A default
Windows build reports that owned endpoints are unavailable; it must not silently
change the device model.

## Experimental own MIDI ports (local development only)

The optional `WindowsMidi` backend uses Windows MIDI Services to create one owned
G1-Emu device with two bidirectional MIDI 1.0 function blocks, `PC Port` and `MIDI`.
It
converts MIDI 1 byte streams to/from UMP, including fragmented SysEx. SDK setup and
teardown run in a dedicated COM MTA, independently of the JUCE GUI message loop.
This is an experimental x64 integration, not yet a verified release feature.

For the local experiment, extract Microsoft's official
[Windows.Devices.Midi2 0.99.81-devpreview.9 package](https://github.com/microsoft/MIDI/releases/tag/inbox-dev-preview-9)
into `build-windows-deps/windows-midi-sdk`, and Microsoft.Windows.CppWinRT
3.0.260520.1 into `build-windows-deps/cppwinrt`. Add these configure options:

```powershell
cmake -S . -B build-windows -DG1_WINDOWS_MIDI_SDK="$PWD/build-windows-deps/windows-midi-sdk" -DG1_CPPWINRT="$PWD/build-windows-deps/cppwinrt/bin/cppwinrt.exe"
cmake --build build-windows --config Release --parallel 4
& ./build-windows/app/Release/g1midicheck.exe
```

CMake generates the C++/WinRT projection and copies the SDK DLL/PRI beside local
executables. The preview license is for development/testing, not production
redistribution: do not publish these binaries or bundle this runtime in releases.
Normal CI builds leave `G1_WINDOWS_MIDI_SDK` empty and do not use the preview.

`g1midicheck` creates only its own temporary test endpoints and checks WinMM
visibility, independent routing, notes and fragmented SysEx in both directions.
It never opens a physical MIDI device. Run it as a separate live test, not CTest.
On Windows 11 25H2 build 26200.9457, whose in-box MIDI component is 26100.8875,
the owned endpoint is not projected as MIDI 1.0 ports and teardown leaves the
service stuck. This exactly matches Microsoft issue #1047, fixed only in the
November 2026 Windows release. The one-device/two-function-block design compiles,
but cannot be validated on this affected service. Do not repeatedly rerun the test:
recovering the shared MIDI service requires a Windows restart.

For audio/UI diagnosis while the shared service is unavailable, launch with
`$env:G1_WINDOWS_MIDI='0'`. This disables this backend for that process and its
children, reports the disabled state, and leaves audio available. Remove that
environment variable before testing native MIDI again. This is not a MIDI fix.
