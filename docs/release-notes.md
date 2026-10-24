# G1-Emu v0.1.0-alpha.25

Uploads that no longer fail at random, and some quiet work under the hood.

**Fix: "upload rejected by synth (code 6: Unfinished bubble error)".** Sending a patch from the
editor to the plugin could fail now and then, with any patch. While an editor uploads a patch,
the plugin's own background request (it re-reads the synth settings every couple of seconds)
could slip in between two packets when the acknowledgements were slow, and the G1's OS drops an
upload another message comes into. The plugin now keeps out of an upload until it ends. We
reproduced it here with long waits between packets: it failed before, it goes through now.
Thanks to @sovietpop for the report. We have not seen it on a Mac, so please tell us in #81 if
it still happens.

**Under the hood.** A review of the audio path (bad input values no longer reach the DSPs, a
change of the sound card's sample rate is applied in one step, and a queue of late frames is
dropped in one go), and small fixes in the core (the system clock when it is written a byte at a
time, the DSP threads' wake-up on ARM, about 3 MB less memory per instance). The sound is the same,
bit for bit, on the eight test patches.

Your ROM, banks, settings and projects are not affected.

## Please test

If you saw the `Unfinished bubble` error, send a few patches to the plugin and tell us in #81 if
it is gone. If something else breaks up, attach the log (`%APPDATA%\Animatek\G1-Emu\g1-emu.log` on
Windows, `~/.local/share/Animatek/G1-Emu/g1-emu.log` on Linux and macOS), with your CPU and DAW.

## Known

- Switching to the Expanded restarts the G1: wait a few seconds until its eight DSPs have booted.
- On a computer that cannot run the Expanded in real time it breaks up: use the Regular there.
- On Windows the plugin has no PC Port of its own (it uses the direct link); the standalone still
  needs loopMIDI for the editor when the direct link is not used.
