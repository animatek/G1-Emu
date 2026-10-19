# G1-Emu v0.1.0-alpha.23

A G1 that starts faster, Octave Shift you can switch off, and a fix for projects.

**Faster start (Mike Fiction).** While the G1 boots, and while a project's slots go back in, the
emulator now runs as fast as the computer allows, then carries on in real time. Nothing can be
heard during those moments anyway. The OS answers in about 0.4 s instead of 2.5 s, and a project's
notes are let in sooner: a slot's channel opens as soon as the slots on it are back.

**Octave Shift: Enabled or Disabled (Mike Fiction).** The rack G1 ignores the octave shift a patch
carries; the keyboard model plays it. **Disabled is the default**, as the rack plays: the notes go
in as sent, and the OCT SHIFT keys and LEDs are greyed out (the LEDs still show the patch's shift,
so a keyboard patch that sounds an octave away shows why). **Enabled** moves each slot's notes by
its shift, as before. Set it on the Synth Settings page (OCT SHIFT) or with a right click on OCT
SHIFT. A project from before opens Disabled: if it relied on the shift, enable it and save the
project again.

**Fix: a reopened project keeps its octave shifts.** Since alpha.22 a project could come back with
slot A's octave shift at 0. It now keeps the shifts its patches carried.

Your ROM, banks, settings and projects are not affected otherwise.

## Known

- On Windows the plugin has no PC Port of its own (it uses the direct link); the standalone still
  needs loopMIDI for the editor when the direct link is not used.
- Very heavy patches on the Expanded can still drop out on some machines. We are working on it.
