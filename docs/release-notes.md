# G1-Emu v0.1.0-alpha.24

The Expanded, steadier: it no longer goes silent on a computer that cannot keep up, and it drops
out less. And one settings file for the standalone and the plugins.

**Fix: the plugin went silent for good when the computer could not keep up.** Switched to the
Expanded (8 DSPs) on a machine that cannot quite run it in real time, the sound stopped and never
came back until you went back to the Regular. It now keeps playing: on such a machine it breaks
up, which tells you it is too much for it, instead of going quiet. Thanks to @Waltercalling for
the report.

**Fewer dropouts with heavy patches.** The emulator's threads now ask the system for a high
priority, as Gearmulator's own plugins do, so the DAW's other work takes their turn less often.
And when one DSP's thread is late, the main thread now does that DSP's work itself instead of
waiting for it. The sound is the same, bit for bit; only the waiting changes.

**One settings file.** The plugin kept its model (Regular or Expanded) and its panel switches
apart from the standalone's, so the two could run different models without anyone noticing. Both
now share `settings.conf`; the plugin keeps only its window size apart. A project still keeps its
own model and settings.

**Banks...** (Emulator Settings): restore your banks from a saved copy, or start with empty banks.
Either puts the banks in use aside first, as `flash-before-reset-<date>.bin`. Note that the ROM
holds no factory patches: those came in each synth's flash and as Clavia's files. **ROM's own
OS...** goes back to the ROM's OS if you had picked an OS update.

**Fix: a ROM given for one run** (on the command line) no longer becomes the plugins' ROM too, and
the Settings card fits a small panel.

Your ROM, banks, settings and projects are not affected otherwise.

## Please test

If the Expanded still breaks up or goes silent, tell us in #75 and attach the log
(`%APPDATA%\Animatek\G1-Emu\g1-emu.log` on Windows, `~/.local/share/Animatek/G1-Emu/g1-emu.log`
on Linux and macOS), with your CPU and DAW.

## Known

- Switching to the Expanded restarts the G1: wait a few seconds until its eight DSPs have booted.
- On a computer that cannot run the Expanded in real time it breaks up: use the Regular there.
- On Windows the plugin has no PC Port of its own (it uses the direct link); the standalone still
  needs loopMIDI for the editor when the direct link is not used.
