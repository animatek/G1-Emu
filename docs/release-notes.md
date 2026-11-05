# G1-Emu v0.1.0-alpha.30

**Fixes for the plugin in DAWs.** Dropouts in Nuendo and Cubase, and patches the synth rejected at
random while uploading. Please use it with **Animatek NME 0.26** or later.

**No more dropouts in Nuendo and Cubase (#86).** Their ASIO-Guard, like REAPER's anticipative
processing, runs a plugin ahead of real time: it rests, then asks for several blocks back to back.
G1-Emu only ran a few milliseconds ahead, so from the second block of each burst the sound was not
ready yet and came out as silence, however idle the CPU was: dropouts at 20-35 % load, never in the
standalone. Now a block asked for early waits for the emulator instead. Timing and latency do not
change, and with an ordinary sound card nothing waits. The log says when the host processes ahead of
real time.

**Uploads are no longer rejected at random (#81).** A MIDI driver hands a long SysEx over in pieces,
and the plugin's own background requests (reading the slots and the synth settings to keep them with
the project) could land between two pieces of the editor's message: the synth then answered
"Unfinished bubble error" (code 6) or "Error in synth" (code 3). They now wait until the editor's
message is whole, in the plugin and in the standalone.

**Smaller things.** The status bar's "CPU n cores" now works on Windows and macOS (it read 0). The
emulator is a little lighter at rest, with the same output to the bit.

Your ROM, banks and settings are kept.

## Please test

- **Nuendo / Cubase with ASIO-Guard on:** play a project for a few minutes. Any dropouts? The log's
  `dropout` lines now also say how many blocks waited.
- **Sending patches from NME:** send several patches in a row, to the plugin and the standalone. Is
  any rejected?
- **G1-Emu FX (#85):** an audio track through AudioIn while you add modules to the patch. Does it
  keep working? We could not reproduce the error on Windows.

If something breaks up, stays silent or is rejected, attach the log (`%APPDATA%\Animatek\G1-Emu\g1-emu.log`
on Windows, `~/.local/share/Animatek/G1-Emu/g1-emu.log` on Linux and macOS), with your CPU and DAW.

## Known

- In CLAP, when the last instance in a project closes, new instances have no PC Port of their own
  (a JUCE limit); NME still finds them through the direct link. VST3 is not affected.
- The Micro's display shows `--` after a project reopens, though the right patch plays.
- Notes sent over the PC Port (an editor's keyboard) do not follow the pitch on the Micro: play from
  the MIDI port or the DAW.
- On a computer that cannot run the Expanded in real time it breaks up: use the Regular there.
