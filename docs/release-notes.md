# G1-Emu v0.1.0-alpha.29

**A stability release.** No new instruments, but the things that broke in daily use: patches that
were not stored, projects that did not come back as they were saved, and the Micro switching inside
one plugin. Please use it with **Animatek NME 0.26** or later.

**One memory, as on a synth.** The banks are now shared by the standalone and every plugin instance
(`flash.bin` for the G1, `flash-micro.bin` for the Micro). A patch you store, from the editor, the
panel or the Presets page, is there in a new instance, a reopened project and the standalone. Before,
each instance started from a copy and kept its banks inside the DAW project, so a patch stored in a
plugin was gone once you loaded the plugin again. Several instances can store at once: one that
started before another stored puts its patch in without overwriting the other's. A project keeps what
each slot holds, the knobs and the synth settings, so it still sounds as it was saved.

**Storing works, and the project knows.** Storing a patch changes nothing the DAW can see, so the
project was not marked as changed and a reopened project had lost the stored patch. G1-Emu now tells
the DAW. A store or a bank list asked for while the plugin was reading a slot could also get lost, so
the editor waited seconds or said nothing was stored: fixed.

**G1-Emu Micro is a plugin of its own.** The Micro Modular is no longer a model inside G1-Emu: put
**G1-Emu Micro** on the track. Switching between the rack and the Micro inside one plugin lost the
editor's connection and, back on the rack, could hang with the load at 100 %. G1-Emu is a Regular or
an Expanded (that switch stays). A project saved with G1-Emu set to the Micro does not open in
G1-Emu: load G1-Emu Micro there again.

**The Micro comes back as you left it.** Reopening a project now brings back the patch in the Micro's
slot (its OS takes patches only a moment after it starts, and the plugin sent it too early) and its
four knobs. Its display shows `--` after that, as for any patch sent from an editor.

**Store slot... on the Presets page** stores what the active slot holds at a bank and position, with
no editor. The Micro has no Store button, as the real one; this is how you store on it. The Micro has
one bank of 99 places, and only bank 1 is offered.

**Two plugins in one project** (G1-Emu and G1-Emu Micro) no longer take the same number, which gave
two PC Ports the same name.

Your ROM, banks and settings are kept. The first time, every instance and the standalone read the
banks from the standalone's `flash.bin`.

## Please test

- **Store, then reopen:** store a patch from NME or the Presets page, save the project, close it,
  open it again. Is it in the bank? Does each slot sound as before?
- **Two instances storing:** two G1-Emu on two tracks, store a patch in each, then load a third
  one: are both patches in its banks?
- **G1-Emu Micro:** reopen a project; does slot A play what it played, with the knobs where they were?

If something breaks up, stays silent or is not stored, attach the log (`%APPDATA%\Animatek\G1-Emu\g1-emu.log`
on Windows, `~/.local/share/Animatek/G1-Emu/g1-emu.log` on Linux and macOS), with your CPU and DAW.

## Known

- In CLAP, when the last instance in a project closes, new instances have no PC Port of their own
  (a JUCE limit); NME still finds them through the direct link. VST3 is not affected.
- The Micro's display shows `--` after a project reopens, though the right patch plays.
- Notes sent over the PC Port (an editor's keyboard) do not follow the pitch on the Micro: play from
  the MIDI port or the DAW.
- On a computer that cannot run the Expanded in real time it breaks up: use the Regular there.
