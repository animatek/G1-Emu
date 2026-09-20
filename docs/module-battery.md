# Module battery

Every module type of the G1 in a patch of its own, played and measured on the emulator. It is
the quickest way to see what the emulation already does and where to look next; it is not a proof
that a module is right, only that it is alive.

Made with `tools/battery/battery.py`, which builds one `.pch` per module with what it needs to
show signs of life — an oscillator on its audio inputs, an LFO on its control inputs, a running
clock on its logic ones, a sine into the G1's audio inputs — and sends its first four outputs to
the four outputs of the G1, which `g1patchtest` measures. Reset and sync inputs are left alone,
because a clock on them freezes the module. A parameter that `modules.xml` leaves without a
default is uploaded as 0, which mutes a level or an amount, so the battery opens those up and
says which ones in the report.

```bash
tools/battery/battery.py --seconds 3          # the whole battery
tools/battery/battery.py --only DrumSynth --verbose --param 4=100
```

Run of 2026-09-20, 3 s per module: **61 sound**, **19 move** (a slow signal: LFOs, envelopes, sequencers), 12 hold a fixed level and 17 give nothing, out of 109.

## What the verdicts mean

- **sounds** — something at 20 Hz or more comes out.
- **moves** — the signal travels but slowly, under 20 Hz: control-rate modules doing their job.
- **fixed level** — a steady level and nothing else. For the keyboard and the MIDI modules that
  is right: one note is held for the whole measurement, so their output does not move.
- **silent** — nothing. It does not mean broken: it is the list to look into one by one, and the
  first three looked at were all the test's fault, not the emulator's (below).

## Followed up by hand

- **Constant** is bipolar, so its default of 64 is zero. At 127 it gives a solid level.
- **DrumSynth is not broken: it is born inaudible, and the fault is in the module description.**
  Its level law is an ordinary exponential one, measured on the emulator with `MLevel` and
  `SLevel` together: 0 → −107 dBFS (the 24-bit floor: silence), 25 → −102, 40 → −89.6, 60 → −76.4,
  80 → −66.3, 100 → −58.6, 127 → −50.9, about 0.45 dB per step over most of the range. An
  oscillator measures −62, so at 100 the DrumSynth is already the loudest module in the machine
  and at its default of 25 it sits 40 dB under an oscillator, which is nothing.
  **And 25 is a placeholder, not a default Clavia chose:** in the whole of NME's `modules.xml` the
  value 25 appears twelve times and all twelve are this module's, while `MTune` and `STune` have
  no default at all and go up as 0. Every other module that defaults all its parameters to one
  value uses one that means something — OscA 64 (centre), FilterBank 127 (open), Mixer (8) 100.
  So a DrumSynth created in NME comes up silent, and the fix belongs in `Nomad2026/data/modules.xml`,
  not here.
- **The same trap catches six more modules.** A parameter with no default goes up as 0, and when
  it is a level that means mute: `4-1Switch` (its four levels), `1-4Switch` (`level`), `Multi-Env`
  (`level 4`), `OscC` (`pitch mod amount`), `EqShelving` (`gain`) and `RingMod` (`ringmod depth
  mod`). The two switches are born completely silent. 255 of the 515 parameters in the file have
  no default; these are the ones where it is audible.
- **AudioIn** needed a signal in the G1's inputs, which the battery now sends (440 Hz).
- **MasterOsc**'s only output is a master-slave one, which carries no signal of its own: it can
  only be judged through a slave module.
## The table

| type | module | category | in/out | verdict |
| --- | --- | --- | --- | --- |
| 61 | Clip | Audio | 2/1 | sounds (-62 dBFS, 262 Hz) |
| 21 | Compressor | Audio | 3/2 | sounds (-62 dBFS, 262 Hz) |
| 78 | Delay | Audio | 2/2 | sounds (-62 dBFS, 262 Hz) |
| 118 | Digitizer | Audio | 2/1 | sounds (-50 dBFS, 262 Hz) |
| 82 | Diode | Audio | 1/1 | sounds (-62 dBFS, 262 Hz) |
| 105 | Expander | Audio | 3/2 | sounds (-62 dBFS, 262 Hz) |
| 57 | InvLevShift | Audio | 1/1 | sounds (-62 dBFS, 262 Hz) |
| 62 | Overdrive | Audio | 2/1 | sounds (-62 dBFS, 262 Hz) |
| 102 | Phaser | Audio | 3/1 | sounds (-62 dBFS, 262 Hz) |
| 54 | Quantizer | Audio | 1/1 | sounds (-50 dBFS, 262 Hz) |
| 117 | RingMod | Audio | 3/1 | sounds (-62 dBFS, 262 Hz) |
| 53 | Sample&Hold | Audio | 2/1 | sounds (-62 dBFS, 22 Hz) |
| 83 | Shaper | Audio | 1/1 | sounds (-62 dBFS, 262 Hz) |
| 94 | StereoChorus | Audio | 1/2 | sounds (-62 dBFS, 262 Hz) |
| 74 | WaveWrap | Audio | 2/1 | sounds (-62 dBFS, 262 Hz) |
| 43 | Constant | Control | 0/1 | silent |
| 66 | ControlMixer | Control | 2/1 | moves (-56 dBFS, 0.30 Hz) |
| 98 | KeyQuant | Control | 1/1 | silent |
| 75 | NoteQuant | Control | 1/1 | silent |
| 72 | NoteScaler | Control | 1/1 | silent |
| 115 | NoteVelScal | Control | 2/1 | fixed level (-62 dBFS) |
| 22 | PartialGen | Control | 1/1 | silent |
| 48 | PortamentoA | Control | 2/1 | moves (-62 dBFS, 0.30 Hz) |
| 16 | PortamentoB | Control | 2/1 | moves (-62 dBFS, 0.30 Hz) |
| 39 | Smooth | Control | 1/1 | moves (-62 dBFS, 0.30 Hz) |
| 84 | AD-Env | Envelope | 3/2 | moves (-62 dBFS, 15.30 Hz) |
| 20 | ADSR | Envelope | 4/2 | sounds (-62 dBFS, 48 Hz) |
| 46 | AHD | Envelope | 6/2 | moves (-62 dBFS, 5.00 Hz) |
| 71 | EnvFollower | Envelope | 1/1 | sounds (-62 dBFS, 523 Hz) |
| 23 | Mod-Env | Envelope | 8/2 | sounds (-62 dBFS, 48 Hz) |
| 52 | Multi-Env | Envelope | 3/2 | sounds (-62 dBFS, 48 Hz) |
| 103 | EqMid | Filter | 1/1 | sounds (-62 dBFS, 262 Hz) |
| 104 | EqShelving | Filter | 1/1 | sounds (-53 dBFS, 262 Hz) |
| 86 | FilterA | Filter | 1/1 | sounds (-63 dBFS, 262 Hz) |
| 87 | FilterB | Filter | 1/1 | sounds (-67 dBFS, 262 Hz) |
| 32 | FilterBank | Filter | 1/1 | sounds (-61 dBFS, 262 Hz) |
| 50 | FilterC | Filter | 1/3 | sounds (-66 dBFS, 262 Hz) |
| 49 | FilterD | Filter | 2/3 | sounds (-68 dBFS, 262 Hz) |
| 51 | FilterE | Filter | 4/1 | sounds (-72 dBFS, 262 Hz) |
| 92 | FilterF | Filter | 3/1 | sounds (-66 dBFS, 262 Hz) |
| 45 | VocalFilter | Filter | 3/1 | sounds (-57 dBFS, 262 Hz) |
| 108 | Vocoder | Filter | 2/1 | sounds (-60 dBFS, 262 Hz) |
| 5 | 1Output | In/Out | 1/0 | sounds (-62 dBFS, 262 Hz) |
| 4 | 2Output | In/Out | 2/0 | sounds (-62 dBFS, 262 Hz) |
| 3 | 4Output | In/Out | 4/0 | sounds (-62 dBFS, 262 Hz) |
| 2 | AudioIn | In/Out | 0/2 | sounds (-62 dBFS, 440 Hz) |
| 1 | Keyboard | In/Out | 0/4 | fixed level (-62 dBFS) |
| 63 | KeyboardPatch | In/Out | 0/4 | fixed level (-62 dBFS) |
| 100 | KeybSplit | In/Out | 3/3 | silent |
| 65 | MIDIGlobal | In/Out | 0/3 | fixed level (-62 dBFS) |
| 67 | NoteDetect | In/Out | 0/3 | fixed level (-62 dBFS) |
| 127 | PolyAreaIn | In/Out | 0/2 | silent |
| 68 | ClkGen | LFO | 1/4 | sounds (-62 dBFS, 48 Hz) |
| 33 | ClkRndGen | LFO | 1/1 | moves (-62 dBFS, 11.00 Hz) |
| 24 | LFOA | LFO | 2/2 | moves (-64 dBFS, 0.30 Hz) |
| 25 | LFOB | LFO | 3/2 | fixed level (-62 dBFS) |
| 26 | LFOC | LFO | 1/2 | moves (-64 dBFS, 0.30 Hz) |
| 80 | LFOSlvA | LFO | 2/1 | moves (-62 dBFS, 8.00 Hz) |
| 27 | LFOSlvB | LFO | 1/1 | moves (-62 dBFS, 8.00 Hz) |
| 28 | LFOSlvC | LFO | 1/1 | moves (-62 dBFS, 8.30 Hz) |
| 29 | LFOSlvD | LFO | 1/1 | moves (-62 dBFS, 8.30 Hz) |
| 30 | LFOSlvE | LFO | 1/1 | moves (-62 dBFS, 8.30 Hz) |
| 99 | PatternGen | LFO | 3/1 | moves (-62 dBFS, 12.70 Hz) |
| 110 | RandomGen | LFO | 1/1 | moves (-63 dBFS, 3.30 Hz) |
| 35 | RndPulsGen | LFO | 0/1 | silent |
| 34 | RndStepGen | LFO | 1/1 | moves (-68 dBFS, 1.00 Hz) |
| 69 | ClkDiv | Logic | 2/1 | sounds (-62 dBFS, 48 Hz) |
| 77 | ClkDivFix | Logic | 2/3 | moves (-62 dBFS, 8.00 Hz) |
| 89 | CompareAB | Logic | 2/1 | fixed level (-62 dBFS) |
| 59 | CompareLev | Logic | 1/1 | fixed level (-62 dBFS) |
| 37 | LogicDelay | Logic | 1/1 | silent |
| 70 | LogicInv | Logic | 1/1 | sounds (-62 dBFS, 48 Hz) |
| 73 | LogicProc | Logic | 2/1 | sounds (-62 dBFS, 48 Hz) |
| 64 | NegEdgeDelay | Logic | 1/1 | fixed level (-62 dBFS) |
| 36 | PosEdgeDelay | Logic | 1/1 | silent |
| 38 | Pulse | Logic | 1/1 | fixed level (-62 dBFS) |
| 88 | 1-4Switch | Mixer | 1/4 | sounds (-70 dBFS, 262 Hz) |
| 113 | 1to2Fade | Mixer | 1/2 | silent |
| 114 | 2to1Fade | Mixer | 2/1 | silent |
| 79 | 4-1Switch | Mixer | 4/1 | sounds (-70 dBFS, 262 Hz) |
| 81 | Amplifier | Mixer | 1/1 | sounds (-62 dBFS, 262 Hz) |
| 44 | GainControl | Mixer | 2/1 | sounds (-62 dBFS, 523 Hz) |
| 112 | LevAdd | Mixer | 1/1 | sounds (-62 dBFS, 262 Hz) |
| 111 | LevMult | Mixer | 1/1 | silent |
| 19 | Mixer (3) | Mixer | 3/1 | sounds (-52 dBFS, 262 Hz) |
| 40 | Mixer (8) | Mixer | 8/1 | sounds (-52 dBFS, 262 Hz) |
| 76 | OnOff | Mixer | 1/1 | sounds (-62 dBFS, 262 Hz) |
| 47 | Pan | Mixer | 2/2 | sounds (-68 dBFS, 262 Hz) |
| 18 | X-Fade | Mixer | 3/1 | sounds (-62 dBFS, 262 Hz) |
| 58 | DrumSynth | Oscillator | 3/1 | silent |
| 96 | FormantOsc | Oscillator | 3/2 | sounds (-50 dBFS, 14407 Hz) |
| 97 | MasterOsc | Oscillator | 2/1 | silent |
| 31 | Noise | Oscillator | 0/1 | sounds (-63 dBFS, 18222 Hz) |
| 7 | OscA | Oscillator | 5/2 | sounds (-62 dBFS, 262 Hz) |
| 8 | OscB | Oscillator | 4/2 | sounds (-62 dBFS, 262 Hz) |
| 9 | OscC | Oscillator | 3/2 | sounds (-62 dBFS, 694 Hz) |
| 106 | OscSineBank | Oscillator | 9/1 | sounds (-56 dBFS, 262 Hz) |
| 14 | OscSlvA | Oscillator | 4/1 | sounds (-62 dBFS, 262 Hz) |
| 10 | OscSlvB | Oscillator | 2/1 | sounds (-62 dBFS, 262 Hz) |
| 11 | OscSlvC | Oscillator | 2/1 | fixed level (-68 dBFS) |
| 12 | OscSlvD | Oscillator | 2/1 | sounds (-62 dBFS, 262 Hz) |
| 13 | OscSlvE | Oscillator | 3/1 | sounds (-64 dBFS, 523 Hz) |
| 85 | OscSlvFM | Oscillator | 3/1 | sounds (-62 dBFS, 262 Hz) |
| 95 | PercOsc | Oscillator | 3/1 | sounds (-92 dBFS, 273 Hz) |
| 107 | SpectralOsc | Oscillator | 4/2 | sounds (-60 dBFS, 330 Hz) |
| 91 | CtrlSeq | Seqencer | 2/3 | silent |
| 17 | EventSeq | Seqencer | 2/4 | silent |
| 15 | NoteSeqA | Seqencer | 2/4 | fixed level (-62 dBFS) |
| 90 | NoteSeqB | Seqencer | 2/4 | moves (-62 dBFS, 3.00 Hz) |
