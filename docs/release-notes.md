# G1-Emu v0.1.0-alpha.31

**For Mac users, and the Micro's knobs in the DAW.** A fix for the dropouts on Apple Silicon that
needs your testing, the Micro's knobs as automation, and smaller things you asked for. Please use it
with **Animatek NME 0.27** or later.

**macOS: the plugin's threads join the DAW's audio work (#81, #85), please test.** On Apple Silicon,
macOS puts the threads a plugin starts on its own onto the efficiency cores whenever the DAW looks
idle, and the DAW then waits for audio that comes late: dropouts with the load well below 100 %, in
the plugin and not in the standalone, worse on macOS 27. The emulator's threads now join the DAW's
audio workgroup, the same fix Gearmulator made for its plugins. We could not try it on a Mac
ourselves, so tell us whether the dropouts, and the uploads rejected with "Unfinished bubble"
(code 6), are gone. Nothing changes on Windows and Linux.

**G1-Emu Micro: its knobs are automation parameters (#91).** Knob 1-3 and Master Volume are the
Micro's own knobs, both ways: what you turn on the Micro's box reaches the DAW, and the DAW's
automation turns the Micro's knobs. Knob 4-18 still show up but do nothing on a Micro, which has only
three.

**A new instance keeps the last output level (#88).** Set the level once (on the Settings card or from
the DAW) and the next instance you add starts there instead of at 48 dB. A project's own level still
wins.

**A MIDI keyboard in the standalone (#84).** Settings has a **MIDI keyboard** row: pick your keyboard
and it plays the G1 at once, next to NME, and it is opened again on the next start.

**OCT SHIFT off looks off (#92).** With Octave Shift Disabled (the default since alpha.24) the OCT SHIFT
keys are now drawn in grey, and hovering them says how to turn them on (right-click, or the Synth
Settings page).

**README:** the Micro Modular model needs the rack ROM too: the Micro's OS starts on the rack's boot ROM.

Your ROM, banks and settings are kept.

## Please test

- **On a Mac with Apple Silicon (#81, #85):** play a project with G1-Emu for a few minutes and send
  patches from NME. Any dropouts? Any upload rejected? G1-Emu FX on an audio track while you add
  modules: does it keep working?
- **G1-Emu Micro in your DAW:** do Knob 1-3 and Master Volume record and play back as automation?

If something breaks up, stays silent or is rejected, attach the log (`%APPDATA%\Animatek\G1-Emu\g1-emu.log`
on Windows, `~/.local/share/Animatek/G1-Emu/g1-emu.log` on Linux and macOS), with your CPU and DAW.

## Known

- In CLAP, when the last instance in a project closes, new instances have no PC Port of their own
  (a JUCE limit); NME still finds them through the direct link. VST3 is not affected.
- The Micro's display shows `--` after a project reopens, though the right patch plays.
- Notes sent over the PC Port (an editor's keyboard) do not follow the pitch on the Micro: play from
  the MIDI port or the DAW.
- On a computer that cannot run the Expanded in real time it breaks up: use the Regular there.
