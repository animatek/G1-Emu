# G1-Emu v0.1.0-alpha.26

**The Micro Modular**, as a third model next to the Regular and the Expanded (#82).

**Micro Modular.** One DSP and the Micro's own OS: about a third of a Regular's CPU, for small
patches and slower computers. Pick it in Settings (**Model → Micro**), in the standalone and in the
plugin, which saves it with the project: one plugin is a G1 or a Micro. G1-Emu does not ship the
Micro's OS: point Settings' **Micro OS** at Clavia's Micro Modular OS updater (the `.exe`, read as it
is) or leave it in your ROM folder. It has a box of its own (volume, the three edit knobs and the
two-digit display) and a flash of its own (`flash-micro.bin`), so your G1's patches are not
touched. Connect the editor to it as to a G1: one slot, A. Thanks to @bacizone for asking.

**Update mode from Settings.** **OS... → Update mode (install an OS)** restarts the G1 into its
boot ROM's update utility, as holding Shift + Store at power-on does on the synth, to install an OS
over MIDI. Press Store when it asks; after the next restart G1-Emu keeps the OS that came in.

**A MOD sticker** on the panel when the OS running is not Clavia's as it came.

**Under the hood.** Every plugin instance runs its DSPs on threads again (serial was tried for the
second and later ones and dropped: real patches need more than one core).

Your ROM, banks, settings and projects are not affected.

## Please test

The Micro: switch to it, load small patches from the editor into slot A, play them from your DAW
or keyboard, and tell us in #82 how it goes, with your CPU, system and DAW. If something breaks up
or stays silent, attach the log (`%APPDATA%\Animatek\G1-Emu\g1-emu.log` on Windows,
`~/.local/share/Animatek/G1-Emu/g1-emu.log` on Linux and macOS).

## Known

- **Micro:** its panel buttons do not work yet (the knobs and the display do). Notes sent over the
  PC Port (an editor's keyboard) do not follow the pitch: play from the MIDI port or the DAW.
- **Micro and the editor:** Animatek NME 0.23 shows four slots for a Micro; use slot A. The next NME
  recognises the Micro and shows one.
- Switching the model restarts the G1. If a plugin stays silent after switching under a playing
  patch, remove it and insert it again, already on the new model.
- On a computer that cannot run the Expanded in real time it breaks up: use the Regular there.
- On Windows the plugin has no PC Port of its own (it uses the direct link); the standalone still
  needs loopMIDI for the editor when the direct link is not used.
