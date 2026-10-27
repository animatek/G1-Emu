# G1-Emu v0.1.0-alpha.27

**The Micro Modular, finished off**, and **G1-Emu as an Audio Unit on macOS** (#7).

**The Micro's box, as the real one.** Shift, Volume and the three edit knobs with what Shift makes of
them (Master Tune, MIDI Channel, Patch Select), the display, and the two buttons that select patches.
**The buttons work now**: G1-Emu read them the wrong way round, so the Micro thought all three were
held from power-on and ignored them. The upper button goes to the next patch and, with Shift, plays
a note while it is held (Note Trig); the lower one goes back and, with Shift, is the switch a patch
can assign. Shift is also your computer's Shift key, as on the G1's panel. Knobs 1 and 3 were
swapped: fixed.

**Below the box**, what the hardware never had: **Presets** (the Micro's one bank of 99, click one to
load it), **Restart**, **About**, and the tooltips. Changing the model in Settings now offers to
restart at once.

**The plugin no longer falls asleep.** It told the host its sound ends when the notes do, so a host
that puts silent plugins to sleep (Bitwig) stopped it while you edited, and an upload from the editor
stopped halfway: "Upload timeout", a frozen synth, and a project that reopened broken. A G1 sounds
without notes, and now says so. It uses a little CPU even when nothing plays, as the standalone does.

**Audio Unit on macOS** (asked for in #7): `G1-Emu.component` (instrument) and `G1-Emu FX.component`
(effect), in the macOS package next to the VST3 and the CLAP. Copy them to
`~/Library/Audio/Plug-Ins/Components`. They pass Apple's `auval -strict`; we have not played them
in a Mac DAW yet, so please tell us how they go.

Your ROM, banks, settings and projects are not affected. The Micro sets up its own flash again on its
first start with this version (it skipped part of its start before): patches stored in the Micro
with alpha.26 may be gone.

## Please test

- **macOS:** the AU in Logic, GarageBand or any AU host: does it load, play, and keep its state in a
  project? Tell us in #7.
- **The Micro:** its buttons, Presets, and editing it from the editor inside your DAW.

If something breaks up or stays silent, attach the log (`%APPDATA%\Animatek\G1-Emu\g1-emu.log` on
Windows, `~/.local/share/Animatek/G1-Emu/g1-emu.log` on Linux and macOS), with your CPU and DAW.

## Known

- **Micro and the editor:** Animatek NME 0.23 shows four slots and nine banks for a Micro; use slot A
  and bank 1. The next NME knows the Micro's one slot.
- Notes sent over the PC Port (an editor's keyboard) do not follow the pitch on the Micro: play from
  the MIDI port or the DAW.
- A project saved with alpha.26 while a Micro's upload was stuck may reopen frozen: remove that
  instance and insert a new one.
- On a computer that cannot run the Expanded in real time it breaks up: use the Regular there.
- On Windows the plugin has no PC Port of its own (it uses the direct link); the standalone still
  needs loopMIDI for the editor when the direct link is not used.
