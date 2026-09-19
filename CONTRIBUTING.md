# Contributing to G1-Emu

**You are warmly invited to join.** G1-Emu is a collaborative project: it was started by Animatek,
but it is far too big for one person, and it will only become a complete emulator if many people
build it together. We love collaboration and we are very happy to welcome new contributors, whatever
your experience: reverse engineering, C++, DSP, testing patches, recordings from a real G1,
documentation or builds for other systems. Every contribution, big or small, makes the emulator
bigger.

G1-Emu is pre-alpha and there is plenty to do. Everything in the repo is in English.

## Before you start

- **There is no support and no ROMs.** Issues are for bug reports with details (what you did, what
  you expected, what happened, ideally with the `.pch`) and for concrete proposals. Requests for
  help, builds or ROMs will be closed without an answer. The project is not affiliated with Clavia
  DMI.

- **Never commit ROMs, firmware or dumps.** You need your own Nord Modular rack OS 3.03 ROM in
  `Roms/`, which Git ignores. Pull requests that include copyrighted firmware will be closed.
- Read `NOTES.md` (what is known about the hardware and the OS) and `ROADMAP.md` (what is next).
- Build it following `README.md`.

## Where to help

- **The panel:** identify the remaining buttons and LEDs (see `ROADMAP.md`, item 1). The test
  bench can press any button and report what changes on the display and the LEDs.
- **Modules:** test modules and patches with `g1patchtest` and report what sounds wrong, ideally
  with the `.pch` and a comparison against a real G1.
- **Recordings from real hardware:** the same patch recorded on a real G1 and on the emulator is
  the best reference we can get.
- **Portability:** macOS and Windows builds (audio and MIDI through JUCE, CI).

## Changes

- One topic per pull request, with a clear description of what changes and how you checked it.
- **Every change gets its line in `CHANGELOG.md`, in the same commit**: newest first, under the
  date, saying what changes and how it was verified.
- Keep the style of the surrounding code; comments explain the why, and hardware findings go to
  `NOTES.md`.
- Do not modify the Gearmulator clone: core fixes go into `cmake/Dsp56300.cmake`, which patches a
  build copy.

## License

By contributing you agree that your work is released under the GPLv3, like the rest of the project.
