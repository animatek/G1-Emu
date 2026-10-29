# G1-Emu v0.1.0-alpha.28

**Picking a ROM now restarts G1-Emu with it**, **the Micro no longer makes a harsh sound on outputs 3/4**, and a
note on where a **MIDI keyboard** goes in the Windows standalone.

**A new ROM takes effect at once.** G1-Emu read its ROM once, at start, so choosing another one in
**Settings > ROM > Choose...** only wrote it down and the G1 went on with the old one until you closed
G1-Emu and opened it again. Now Choose... asks **Restart with this ROM**; accept and the G1 switches off
and on with the new ROM, your banks untouched. Cancel keeps the choice for the next start, as before. If
the file does not serve, it keeps the ROM in use and says so in the log. It is what you want when you try
a ROM you built yourself (the **MOD** badge of a modified one shows as soon as it restarts).

**The Micro's box and its OS.** Pick the Micro's OS in **Settings > Micro OS > Choose...**; if you pick it in the ROM
box by mistake, G1-Emu now offers **Use it as the Micro OS** instead of only saying it is not a ROM. A Micro OS
whose modules were changed shows a **MOD** badge on the box.

**The Micro's outputs 3/4 are silent** (#83). The Micro has one stereo output, but its OS also puts a tone
on two more slots when a patch's Output module is set to **3/4** (36 dB louder than outputs 1/2, and a
copy of the right channel on output 4). In the plugin's Out 3/4 bus that was a harsh, louder sound. The
Micro model now sends nothing to outputs 3/4, in the standalone and in the plugin: the sound stays on 1/2
whatever the Output module says. The Regular and the Expanded are unchanged.

**A MIDI keyboard in the Windows standalone** (#84). Nothing was wrong, but nothing said where it goes: on
Windows the standalone cannot create a MIDI port, so pick your keyboard in **Settings > MIDI in** and
restart G1-Emu. `WINDOWS.md` has the steps now, and the README points to them.

**Inside:** the DSP core moves to the commit the builds here were already on. Three fixes come with it: the
HDI08 takes its pending host flags in one exchange (a flag stored between the load and the reset was lost,
which the G1's reload handshake could hit), the JIT's serial status poll fast-forward covers the
two-instruction form, and a JIT block at the last address of an untouched region no longer writes into a
shared page.

Your ROM, banks, settings and projects are not affected.

## Please test

- **The Micro on outputs 3/4** (#83): a patch with its Output module on 3/4 should sound as on 1/2, with
  nothing on the Out 3/4 bus.
- **Choose a ROM and restart** in Settings, standalone and plugin: does it come back with the new one?
- **A keyboard on Windows** (#84): Settings > MIDI in, then play. Does the `MIDI in` counter count?

If something breaks up or stays silent, attach the log (`%APPDATA%\Animatek\G1-Emu\g1-emu.log` on
Windows, `~/.local/share/Animatek/G1-Emu/g1-emu.log` on Linux and macOS), with your CPU and DAW.

## Known

- **Micro and the editor:** Animatek NME 0.25 knows the Micro's one slot and three knobs; the Slot-select
  dialog still shows four buttons and the bank list nine: use slot A and bank 1.
- Notes sent over the PC Port (an editor's keyboard) do not follow the pitch on the Micro: play from
  the MIDI port or the DAW.
- On a computer that cannot run the Expanded in real time it breaks up: use the Regular there.
- On Windows the plugin has no PC Port of its own (it uses the direct link); the standalone still
  needs loopMIDI for the editor when the direct link is not used.
