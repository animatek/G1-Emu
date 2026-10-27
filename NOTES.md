# Emulating the Nord Modular G1 — technical notes

Goal: run the G1's original system on emulated chips (Gearmulator) and play it from
**Animatek NME**, exactly like the real synth. This file collects what is known about the
hardware and the OS and how each part is emulated. Addresses are hexadecimal (`$`); CPU
addresses are in RAM unless stated otherwise.

## The ROM

The rack OS 3.03: 512 KB, sha256 `d9b199f2…c997e`, file `Roms/NORD-MODULAR-RACK-VER-3.03.BIN`.
It never goes into Git. The official updater carries the same OS linked differently (see "The
official OS update").

| Range (ROM) | Contents |
| --- | --- |
| `$00000`–`$08xxx` | 68k vectors (stack `$1FFF00`, reset `$000008`) and the loader. `MIDI BOOT`, *Update utility* and the factory test menu: *DSP Memory test*, *Expansion Board*, *Exp Boot Failure*, DAC/AD, knobs, keyboard. |
| `$0A000`–`$0C3xx` | Garbage: Windows COM headers (`objidl.h`) that slipped into the image. Not code. |
| `~$0C400`–`$4FFFF` | The main system (68k code, very few strings). |
| `$50000`–`$5FFFF` | DSP code and data (24-bit words stored in 32). `NORD MODULAR` at `$50311`. |
| `$60000`–`$68xxx` | More code and data. |
| `$6C000`–`$7FFFF` | Empty (`$FF`). |

The OS runs from RAM at `$100000`, copied from the ROM at `$C800`: RAM address X is at ROM offset
`X − $100000 + $C800`.

## The official OS update

Clavia's updaters (`Nord Modular OS v3.03b Update`, `Nord Micro Modular OS v3.03b Update`) each
carry an OS image that runs from RAM at `$100000`. Getting it out of the updater is not described
here; what was found in it is:

**Rack: the same OS as the ROM's, linked differently.** Both say 3.03 and hold the same strings,
tables and DSP programs; with the absolute addresses masked out, ~94% of the image matches the
factory OS at ROM `$C800` byte for byte. The rest is moved references: most of the code sits `$20`
higher (the DSP-port table is at `$15BD88` instead of `$15BD68`, the DSP count at `$1AB93C` instead
of `$1AB91C`), some tables a little more. Whether any small fix hides in the difference has not
been checked. **Everything that reads the OS by address breaks on it** (`g1Lib/g1knobs.h`: slot
base, type table, morph, active slot, panel split), so booting from the update (#16) needs those
addresses per OS build.

**The ROM's factory OS is not the OS a G1 runs (2026-10-04).** A G1 updated to 3.03 runs the
update's OS from its flash; the emulator installed the factory OS kept in the boot ROM. They
differ in more than addresses. Right after the flash filesystem is set up (`$125C12` in the update),
the update's boot calls the routine that loads the stored synth settings (object `$138A`, written
by Shift+Store: `$12875C`) and stores default ones if there are none; the factory OS has neither
call, and its loop that loads each slot's patch at power-on is behind a flag that nothing sets.
So Shift+Store did write its record (with each slot's bank and patch number), and the factory OS
never read it back: after a restart every slot was "Empty patch". Checked on Javier's real G1:
Shift+Store with 207 in slot A, power off and on, 207; load 201 (or edit 207) without
Shift+Store, power off and on, 207 again. With the update's OS in the emulator's flash it does
the same (Shift+Store with 108, restart, 108). `HostOptions::os` (`os =` in the settings file,
or `G1_OS`) names an OS image to run instead of the ROM's (as it runs from RAM at `$100000`);
`Engine` installs it, keeps it as the factory flash the plugin's state is a difference against,
and puts it back over whatever OS a loaded flash file had. `g1Lib/g1knobs.h` tells the two
apart by where the DSP host-port table is and adds `$20` for the update. Getting the image out of
the updater is not described here; `g1patchtest` takes `G1_OS` too.

**Synth settings written by an editor are not stored (2026-10-09, `g1synthsettingstest`).** The
section an editor sends ($1F, NME's Synth Settings dialog) changes the settings in use and leaves
the flash as it was, as the panel's System menu does: only Shift+Store writes them into the flash.
So a G1 booted from that flash has its old ones (under the factory OS, which loads no stored
settings at all, always the defaults). The OS builds its answer to RequestSynthSettings as the
section itself at `$160CCA` (`$20` higher under the update), but only when asked: it is a reply
buffer, not where the settings live (after boot it holds `05 01 00...`). The settings in use are
spread elsewhere in other layouts (the name at `$15BC50`, the tempo at `$1A84A5`, each slot's
channel in its slot block at `$1AEE1C` + `$6000` per slot, factory OS), not mapped further.

**Micro Modular: the same code base with one DSP.** It keeps the eight-port HI08 table, but where
the rack checks for the expansion and sets 4 or 8 DSPs (`$10BB12`), the Micro sets 1 (`$1050E4`)
and boots only DSP 0. Its DSP program is the rack's DSP 0 program (codec input) carrying DSP 3's
duties too (output to the codec with the master volume at `X:$5F`, slot mask `$FFFFF9`), with the
same PLL (`$3C001A`) but another ESSI clock towards the codec (CRA `$181808`, PM=8, against the
rack's `$181801`/`$181802`), so its codec framing differs. No LCD (`$202004`–`$202007` are never
touched); the knobs are still read through `$202000`/`$202800`. The update holds only the OS: the
Micro's boot ROM is not in it.

## Micro Modular in G1-Emu

(2026-10-26, issue #82.) `G1_MODEL=micro` builds one DSP (`g1::g_microDsps`) and, with
`G1_OS=Roms/Micro/micro-os.bin`, the Micro's OS boots on the rack's boot ROM: it loads its program
into DSP 0, runs at 100 % speed, answers NME's IAm with the last byte `02` where the rack says `01`,
and **sounds like the rack** (SimpleOSC at -61.8 dBFS, Test Audio at -54.1 dBFS and 349 Hz, the same
figures). The DSP's block routine is the rack's DSP 3 output stage in the same program: it computes
`out = X:$5F + Y:$5F x signal`, with `Y:$5F` the master gain, and the OS writes that gain from the
volume knob.

**The Micro's panel is not the rack's.** It has a volume knob, three more, two buttons and a
two-digit 7-segment display (photo in `Roms/Imagenes`). The OS never touches the rack's ADC at
`$202000`/`$202800`: it drives the display by multiplexing (`$201800` = segments, `$202000` = digit
select) and **reads its knobs from a serial ADC on the 68331's QSPI** (SPCR0 `$AA40`, SPCR2 `$4300`
wrapping over four queue entries, command bytes `$D0`, transmit words `$033C $02BC $03BC $023C`;
the OS stores each received word as `RX >> 1` at `$15737E`+). With no one answering the QSPI the
volume read zero, `Y:$5F` was never written and the output was silence: that was the whole problem
(the patch ran, the same code and data as the rack's DSP 0). `Microcontroller::setQspiAdc` answers
each queue entry with a value; **entry 3 is the volume** (found by trying them), entries 0-2 are
the other knobs and do nothing to the sound so far (`G1_QSPIADC=ff,00,00,00` is the test hook).

**The Micro's buttons** (Shift, and the two that select patches) are not on the QSPI: they are on
the 68331's port F (`$FFFA19`), **bits 1-3, active low** (corrected 2026-10-27; read as active high
before, which made the OS see all three held from power-on). The OS's is-held test (`$102CA2`,
`$102CB6`, `$102CCA`: `andi #mask; subq #1; addx`) answers yes for a clear bit. Its poll (`$102C0A`)
turns a change of bit 1 into event `$40E`, bit 2 into `$410` and bit 3 into `$405`, handled at
`$1113B2`: `$405` is **Shift** (it keeps `$17BF8C`, Shift held), `$40E` the **upper button** (the
next patch; with Shift a C4 at velocity 96, on while held, off on release: Note Trig, as Clavia's
manual says) and `$410` the **lower button** (the previous patch; with Shift, a switch a patch can
assign). At power-on the OS asks whether `$410` is held (`$100A8A`) and, if so, skips part of its
start (`$120D0A`, `$120B48`); released, it sets up its flash on a first start (10 sectors erased in
`g1boot`). With a rack's flash loaded into a Micro, that start never ends: each model keeps its own
flash (`flash-micro.bin`), and the tests do the same. The display is **active low**, a-g on bits
0-6 and the dot on bit 7 (`$F9` is `1`); the OS lights digit 1 (the right one) and digit 2 (the
left one) in turn.

Open: notes through the PC Port's play-note path do not track pitch on the Micro while MIDI notes on
channel 1 do (`G1_MIDINOTE=1`: 524 Hz for note 72, the rack's 522); the 7-segment display and the
buttons are emulated since 2026-10-27; where an end user gets the Micro's OS image (the rack's `os =` setting
has the same question).

## Boot and memory map

**The loader** (`$000000`–`$0007FF`) reads the panel at power-on (selects rows in `$202005`,
reads `$201800`) and chooses:

| Keys held | What it does |
| --- | --- |
| none | Copies the OS from the flash at `$300000` to RAM (length at `+8`, data from `+$20`) and jumps to `$100000`. If the flash is empty (`$FFFFFFFF`), it enters update mode. |
| D1=`$7F`, D2=`$BF` (**Shift + Store**) | MIDI update mode: runs the *Update utility* from `$0800`. |
| D1=`$7F`, D3=`$F7` (Shift + Patch/Load) | Boots the **factory OS stored in the ROM itself** (`$C800`, `$1CE01` long words). |
| D3=`$EF`, D1=`$F7` (Navigator up + Find) | The utility with mode 2: "Production test". |
| D3=`$EF`, D1=`$FB` (Navigator up + Panel Split) | RAM test. |

**Which buttons (2026-10-19).** The loader selects row 2 (`$3F`), row 0 (`$6F`) and row 1 (`$5F`)
and compares each whole byte, so only those keys may be held: D1, D2 and D3 are the panel's button
rows 2, 0 and 1 (the table in "The panel"). Tried by Javier on his real G1: **Shift + Store**
enters the update mode, Navigator up + Find shows "Production test", Navigator up + Panel Split
runs the RAM test, and **Shift + Patch/Load** (row 1 bit 3, the factory test's "Play") boots the
ROM's factory OS: it also says 3.03, and shows only in not loading the stored settings (every slot
"Empty patch"); the next power-on without keys runs the flash's OS again, as before.

**Memory map** (chip-selects programmed by the OS):

| Address | CS | What |
| --- | --- | --- |
| `$000000` | BOOT | 512 KB ROM (loader, update utility, factory OS) |
| `$100000`–`$1FFFFF` | 8/9/10 | 1 MB RAM (the OS runs here) |
| `$200000`, `$200008`, `$200010`, `$200018` | 0 | **The four DSP56303s over HI08**, 8 registers each: host command at `+1` (CVR), status at `+2` (ISR), 24-bit word at `+4/+6` |
| `$200020`–`$20003F` | 0 | The expansion board (four more DSPs) |
| `$201000` | 1 | Output of an 8-row matrix read through CPU port F (probably the keyboard of the keyboard model) |
| `$201800` | 4 | Button matrix input |
| `$202000` | 2 | ADC multiplexer (which knob) |
| `$202004`, `$202005` | 2 | LED data and LED/button row select |
| `$202006`, `$202007` | 2 | LCD data and control |
| `$202800` | 3 | ADC reading |
| `$300000` | 5/7 | **1 MB flash** (8 bits): the installed OS and the patches |

**The flash:** the OS accepts an Intel 28F008 (`$89/$A6`), a Fujitsu MBM29F080 (`$04/$D5`) or an
AMD Am29F080 (`$01/$D5`), and otherwise hangs. The AMD is emulated (`g1Lib/g1flash.h`). On first
boot the OS formats the patch area: it erases 10 sectors and writes 64 KB.

**System clock:** the OS uses the SIM's PIT (`PICR=$0140`: level 1, vector `$40`; `PITR=$0002`:
every 244 µs; routine `$1008A4`). Gearmulator's SIM does not emulate it; it is done in `g1mc.cpp`.
The CPU runs at 20.97 MHz after the OS programs SYNCR.

**The master clock** (what MIDIGlobal's clock and sync outputs carry, and so every sequencer or
arpeggio synced to the synth's tempo). Found 2026-09-27 with nmedit's `progger.pch`, which is
loud on a real G1 and was silent here. It runs on the GPT's output compare 2, not the PIT:
`$117d9a` starts it with `TOC2 = TCNT + period` (`$fff916`, the period at `$1a84a6`) and
`bset #4, $fff920` (OC2I), or stops it; the interrupt (`$117e3c`) clears OC2F and adds the period.
Two things kept it still:
- **The synth setting.** The OS starts the internal clock only when the synth settings' MIDI clock
  source bit is **1**; with 0 it waits for MIDI clock. A new flash comes up with 0. Animatek NME's
  Synth Settings dialog shows 0 as "Internal", which is the other way round (to be confirmed on the
  real G1's own display). `g1patchtest` sets it with `G1_CLOCKSRC=1` and sends MIDI clock with
  `G1_MIDICLOCK=bpm`.
- **An emulator fault, fixed in `g1mc.cpp`.** On the 68331 an output-compare interrupt is requested
  while its flag and its enable are both set, so enabling OC2I with OC2F already up interrupts at
  once. Gearmulator's GPT only interrupts on a new compare match, and not while the flag is up, and
  OC2F is up from an earlier match when the OS starts the clock: the interrupt never came and the
  clock never ticked, even with the setting at 1. `Microcontroller::gptMaskWritten` now injects the
  interrupt when TMSK1 gains an OCxI bit whose flag is set.

**The clock's rate (issue #22, 2026-10-03).** MIDIGlobal's clock output gives 24 pulses per beat
(NME's help for the Clock Divider): 48 Hz at 120 BPM, 8 Hz after a divider set to 6. With MIDI
clock (`G1_CLOCKSRC=0 G1_MIDICLOCK=120`) it always did; the internal clock gave 192 Hz, 4 times too
fast. The OS's tempo table (`$145B4C`, a word per BPM) holds the period for 24 ticks per beat with
the GPT counting the system clock /32 (TMSK2 = `$23`, CPR 3): `$3556` at 120 BPM. Each tick its
handler (`$117E3C`) clears OC2F, adds the period to TOC2 and sends host command `$C8` to every DSP;
the PIT routine sends `$C7` about 2 ms later (a countdown at `$1C3AAE`), and the DSPs make the
pulse. Two GPT faults in Gearmulator, fixed in a build copy of `gpt.cpp` (`cmake/Mc68k.cmake`):
the prescaler was ignored (always /4: 8 times too fast) and a compare matched by level, so the
handler, clearing OC2F before moving TOC2, got a second interrupt at once (two ticks together and
a gap of two periods: half the pulses). With both fixed, `tools/patches/ClockTest.pch` (MIDIGlobal
into a Clock Divider set to 6, both to the outputs) gives 48.00 Hz and 8.00 Hz with the internal
clock, as with MIDI clock, and the OS counts 2 beats a second. Of the bench patches only `progger`
changes (it is clocked); the rest render byte for byte as before.

With both, `progger.pch` measures in the emulator what a real G1 gives (a real-vs-emulator
comparison: the patch loaded in the real synth through NME, a note over MIDI, both outputs
recorded, against `g1patchtest --wav`). Checked against the real G1 the same way and matching in
level and shape: `ButohDrone`, `04-007-007` and `Clasico` from Javier's bank 9, and the test patch.
Note that `g1patchtest`'s printed `output` peaks are before `g1run`'s +36 dB and its `--wav` after.

## The CPU and the hardware

- **CPU: Motorola 68331.** The code writes to the SIM (`$FFFAxx`), the QSM (`$FFFCxx`) and above
  all the **GPT** (`$FFF9xx`), which is what tells the 68331 from the 68332. Gearmulator emulates
  exactly this chip (`source/mc68k`) for the Nord Lead 2X, whose emulation (`source/nord/n2x`:
  same CPU, same flash size, DSP563xx over HI08) is the template for this project.
- **DSPs: four Motorola DSP56303s** on the main board (eight with the 32-voice expansion).
- VBR = `$1AB4E0` (vector table in RAM).

## MIDI and the PC PORT

The G1 has **two independent serial ports**:

- **MIDI IN/OUT = the 68331's SCI** (internal UART, `SCCR0=$15`, receive interrupt at level 3,
  vector `$42`).
- **PC PORT = an external SCN2681/68681 DUART** on a parallel bus built from CPU ports:
  - data: the timer's GP port (`$FFF906/7`), DDRGP `$FF` to write and `$00` to read;
  - control: port E (`$FFFA11`). Bit 0 = /CS, bit 1 = /RD, bit 2 = /WR, bits 3/6/7 = A0/A1/A2;
  - channel A setup: CR `$0A,$10`, MR1/MR2 `$13/$07` (8N1), CSR `$EE`, ACR `$FF`, IMR `$00`,
    CR `$20,$30,$50,$C0,$90`, and finally CR `$05` (RX and TX on);
  - receive notification: RxRDY → PAI pin. The OS leaves PACNT at `$FF` with PAOVI enabled, and
    the overflow jumps to vector **IVBA+`$A`** (`$5A`, level 2). The handler (`$117364`) reads RHR,
    rebuilds the SysEx and dispatches it. Gearmulator's GPT lacks the pulse accumulator; it is
    emulated in `g1mc.cpp`.

At boot the emulated G1 announces itself on the PC PORT (`F0 33 50 06 00 07 08 08 F7` and
`F0 33 50 06 00 05 01 00 00 00 7F F7`) and answers NME's *IAm* (`F0 33 00 06 00 03 03 F7`) with
`F0 33 00 06 01 03 03 3F 7F 7F 01 F7`: sender 1, version 3.3, serial number and device ID.

**The editor's keyboard is one key** (found 2026-09-26, #4's "the keyboard floater stacks
notes"). The editor's note message (cc `$17`, `56 onOff note`, handled at `$11b434`) does not
play a note: it sets **one** virtual key, down with its note (`$1c3ab5`, `$1c3abc`) or up, without
looking at which note is released. A routine run from the panel scan (`$117aae`) compares that
key with its last state (`$1c3aed`) and only on a change queues a note-on or note-off of the one
stored note (`$115c06`, the same queue MIDI IN uses). So two editor notes that overlap stick: the
second press only changes the stored note, and the releases send a note-off for the wrong note or
none. Two that arrive within one scan play as a single note. **The real G1 does the same**:
checked on 2026-09-26 with a gate-only test patch (`tools/patches/PolyGateTest.pch`, 8 voices) and
its ADSR gate LED, which stayed lit after 60 (10 s) and 64 (0.5 s, on top) were both released,
and went out after a lone note. MIDI IN is polyphonic and not affected. The emulator reproduces it
faithfully; the fix belongs in the editor, which must never send a second note-on before the
first note-off. `g1patchtest` shows it with `G1_SEQ=+60,+64,-64,-60 G1_CHORD=1`.

## Booting the DSPs

- **8 HI08 ports** at `$200000 + 8·n`: DSPs 0–3 on the main board, 4–7 on the expansion. The OS
  keeps the pointer table at `$15BD68` and the number of DSPs at `$1AB91C`.
- **The loader** sends a 52-word program only to DSPs 3 and 7. It sets up the PLL and both ESSIs,
  sends `$155` twice through them (initialises the codec), raises HF2 ("I am here"), waits for HF0
  and returns to its boot ROM (`jmp $FF0000`). That is how the OS detects the expansion.
- **The OS** loads each DSP with a standard HI08 boot (length `$205`, address 0). There are three
  programs: DSP 0 (`$144644`), the middle ones (`$144C18`) and the last one (`$1451EC`). Then it
  sends 128-word tables, each followed by a host command. The send routine (`$10C0E0`) waits for
  TXDE and **silently drops the word** after 10 tries.
- **DSP 3 boots twice** (loader + OS): whatever is left in the port when it jumps back to
  `$FF0000` is handed to the boot ROM.
- **The expansion board, tried on 2026-10-18 (#39)** by building with eight DSPs on the eight
  ports and nothing else changed. The OS boots them all: `$1AB91C` holds 8 instead of 4 and the
  eight reach their main loop. **One chain, 0 → 7 → codec:** DSPs 1-6 get the middle program
  (`CRA=$181801` on both ESSIs, receivers on) and the codec moves to **DSP 7** (`CRA=$181802`);
  DSP 3 is then a middle one. The inputs still enter at DSP 0. So the outputs are read from the
  last DSP, whichever it is. **The voices double**: asked for 32, the bench patches get
  WavetableSynth/Pad 11 → 23, 4VoiceChoir 16 → 32, FM303 and BellsMorph 8 → 16, Grainalizzer,
  DungeonDub and WindowLicker 4 → 8 (SimpleOSC 32 either way, progger 1). The OS fills the lower
  DSPs first: a light patch never reaches DSPs 4-7. And the voices placed there sound: BellsMorph
  with 8 notes and with 16 gives −38.3 and −38.5 dB RMS on four DSPs (16 notes steal), −37.7 and
  −33.3 dB on eight. SimpleOSC sounds as on four (262 Hz at −61.8 dBFS) down the eight links.
- **Its cost.** Every DSP runs its program whether it has voices or not, so eight cost twice four:
  serially 1.25x real time against 0.67x; threaded, 4.7x against 2.7x (Ryzen 7 5700X, 8 cores).
  One thread per DSP made nine spinning threads with the CPU's, and in real time (`g1runnertest
  --realtime`) that took 5.8 cores and dropped out 1-4 times in 8 s; four threads with two DSPs
  each run as fast offline (2.5x), take 2.75 cores and drop nothing, so four it is for either
  model. The longer chain also delays the sound (eight links), so an Expanded's WAV is not a
  Regular's; threaded and serial are still identical, bit for bit.
- Emulation details (`g1dsp.cpp`): HF0/HF1 from the CPU's ICR go to the DSP's HSR and HF2/HF3 back
  to the ISR; the jump to `$FF0000` re-arms the boot ROM; host commands are fast interrupts (one
  `movep` in the vector, no `RTI`), so Gearmulator's host-command arbitration is disabled; the ESSI
  slot masks start at their reset value (`$FFFFFF`), because the voice DSPs never write them.

## Loading a patch

- **Voice allocation:** `$1226F0` → `$123E94` (voice allocation) → `$1239E8` ("does another voice
  fit?", adds up each slot's resources: cycles and X/Y/P memory) → per-DSP voice lists
  (`$1A84A8 + DSP×$602`) → `$12287E` (memory layout) → `$122DE2` (per-voice load) → the module
  loader `$122F72`.
- **The module loader** (`$122F72`) sends, per module, `$BF` (address, vector `$7E`: `r0`),
  `$B2`/`$B3` (X/Y data), one word with `$B7` (vector `$6E`: `movep` to `p:(r0)` without
  increment) at `base + offset`, and the rest with `$B4` (`p:(r0)+`) from `base`. The offset
  (signed byte, `−$C`) belongs to the previous module: each module overwrites the `NOP` before the
  previous module's epilogue. The first one patches `P:$197` in the block routine; the last one
  ends with its own epilogue and `RTI`. Per-type resource tables: `$1C3B0C`, `$1C3B24`, `$1C3B28`,
  in steps of `$30`.
- **Reload stop:** to reload, the OS raises HF0; each DSP disables IRQD (`IPRC=$FF0800`), waits
  for its DMAs, clears its buffers, raises HF2 and waits in `$C2`. Then it points the IRQD vector
  to `$CA`, which on the next IRQD re-arms the receive DMAs and restores the vector from DOR0
  (`X:$FFFFF3`), where the OS stored the block routine address.
- Earlier emulator problems that blocked patch loading (all fixed): no PIT; `readIsr` letting the
  DSP run 200 000 cycles per status poll; host-command arbitration; a full interrupt queue; and
  IRQD injected while disabled in the IPRC (the emulator only checked SR).
- **A host command must not wait for the DSP to be idle** (`Dsp::hostCommand`). It used to wait up to
  200 000 cycles for `hasPendingInterrupts()` to go false and to drop the command otherwise, but
  that predicate is also true while the DSP is inside an interrupt, and a DSP whose sample routine
  fills the block never leaves it. With nmedit's `korg.pch` the `$76` ("send me a word") sent to
  DSP 0 was thrown away, and the 68k polled `RXDF` of `$200000` forever: the upload's last packet
  got no ACK. It now waits only for the external interrupt queue (`hasPendingExternalInterrupts()`),
  the 32 entries the wait was protecting. The real G1 accepts `korg.pch` (loaded into slot A on
  2026-09-25); the emulated DSPs sit almost without an idle gap with it, which may or may not be
  how full a real one is.
- **A busy DSP starved its host commands, and piled up IRQDs** (2026-09-26, #4's
  `WavetableSynth.pch`). The core moves external interrupts (host commands) into its queue only in
  `execPeripherals`, which runs only when no internal interrupt can be serviced; a DSP whose sample
  routine fills the block finds the next IRQD pending at every RTI, so a host command waited for
  ever, and `Dsp::hostCommand`, after 200 000 cycles, dropped the next one (`$76` behind a `$7e`).
  And every IRQD injected while the last was still pending was queued: 83 071 on DSP 0 in a ring
  of 1024. Two fixes: the core now collects external interrupts when it picks the next interrupt
  (`cmake/Dsp56300.cmake`, `DSP::execInterrupts`), and `runUntil` injects an IRQD only when the last
  one has been serviced, as a pin would (`m_irqdPending`, cleared in the serviced callback and in
  `armBoot`). The latch alone was tried and did not get the ACK; the core fix alone was not tried.
- **DMA interrupts piled up the same way, and cost samples** (2026-10-14, `BellsMorph.pch` from the
  1.10 factory presets, reported on YouTube as crackles that go away without its two Delays). DSP 0
  ends a DMA3 transfer of its audio input twice per sample (vector `$1E`, a one-`movep` fast
  interrupt). The core queues every interrupt in order and services none while a long interrupt
  runs, so with the sample routine taking ~850 of the 864 cycles the queue held up to six `$1E`,
  and the next IRQD waited behind them: with the DSP idle at `$16C`, interrupts unmasked, the IRQD
  had been pending 835–870 cycles when the next sample clock found it and was lost (~60 a second).
  It looked like the emulator charging too many cycles, and counting one input word per sample
  instead of two hid it, but the OS programs two (CRA `$181801`: DC = 1). On the chip a DMA
  channel's interrupt is a status bit, so a second request while one waits is the same request:
  the core now queues a DMA interrupt only if it is not queued already (`cmake/Dsp56300.cmake`,
  `DSP::isInterruptQueued`). BellsMorph: 251 lost samples in 4 s → 0. Found by logging, at each
  lost sample clock, the PC, SR, processing mode and the queue (length and head).
- **PIDs:** each upload gets a patch ID from the OS (ACK `$36`). `g1boot ... replay` rewrites the
  PID of Parameter (cc `$13`), PatchModification (cc `$17`, except `$41`) and PatchPacket (cc
  `$1C–$1F` without the command bit) messages with the one the OS gave, and redoes the checksum
  (sum from `F0` to the payload, `& $7F`).

## Reading a slot back (issue #25)

- **RequestPatch** (`F0 33 5C+slot 06 41 35`) is answered with an ACK `$36` carrying the slot's
  pid; then the 13 **GetPatch** requests editors send (`pid sc [payload]`, cc `$17`), each answered
  by one section in patch packets (cc `$1C`–`$1F`, 7-bit packed, MSB first). An empty slot answers
  too, with pid 0 and a patch of no modules.
- **The replies are the upload sections**, byte-aligned, with three differences: the header
  request (`$20 $28`) brings the Header (11 bytes, no padding) and then PatchName2 (39) in one
  reply, where an upload starts with PatchName (55: the same name after three zero bytes); the
  padding bits of a section's last byte are whatever the OS had (an upload ignores them); and the
  OS may list cables in another order. The Header's last 3 bits (`unknown4`) change with every
  upload. **No CustomDump (91) comes back**: what it carries in a `.pch` is editor-only (frequency
  display units, a zoom, a slider position, per `modules.xml`), so an empty one goes up.
- `g1patchtest` with `G1_FETCHCHECK=1` reads the patch back, uploads what it read over the same
  slot and reads it again: on the 97 patches of the maintainer's banks every upload was accepted
  and every patch came back the same (sections equal apart from the above).
- **While an upload waits for its next packet the DSPs are stopped** (reload stop), so in the
  plugin, where audio paces the emulator, the G1's time runs far ahead of the wall clock until
  the upload ends: a pause of 0.1 s between an editor's packets became 10 s of G1 time. Anything
  that times out on G1 time between an editor's packets will fire; the keeper does not.

## A MIDI CC assigned from the panel, and the editor

- The knob menu's Assign MIDI CC and MIDI Learn send what Animatek NME sends (`app/presetslink.*`):
  RequestPatch for the slot's pid, then `$22` (MidiCtrlAssignment: section, module, parameter, CC)
  and/or `$23` (the previous CC changed or removed), then GetPatch `$61` to read the ControlMap back.
  The OS takes all of it for the editor's own and **tells the editor nothing**.
- So the link tells it: a **NewPatchInSlot** Info message (cc `$14`, sc `$38`), which makes NME read
  the whole patch again and so see the new CC. **It is sent twice**, as the OS itself does after a
  load from the panel. A single one is often lost: NME skips some NewPatchInSlot notices as echoes
  of its own actions, and those skips can go stale:
  - after an upload, NME arms a one-shot skip (`suppressNextAutoFetch`, `ConnectionManager.cpp`)
    that only clears when a notice arrives; if the G1 never announces the uploaded patch, the skip
    stays armed and eats the next real notice;
  - after a module or cable edit, `expectSyncEcho()` (`PatchSynchronizer.cpp`) counts one expected
    echo; the G1 confirms these only on some paths, so the count can build up and swallow later
    notices. With more than one stale echo, two notices are still not enough.
- The second notice usually arrives while the first read is going on; NME marks that read as
  superseded and reads again once it ends. The cost is a second read of the patch (13 sections, a
  fraction of a second) and a second redraw in NME. Either way NME takes it as a new patch in the
  slot, and forgets which bank position it came from.
- **What would let one notice do, or none:** on NME's side, the skips expiring after a second or
  two (each edit refreshing its own), so a notice after that window is always read. Matching the
  skip on the pid is not enough: an assignment keeps the patch's pid, the same as the upload's.
  The same stale skips also hide a patch loaded from the G1's own panel after an upload or an
  edit, so it is worth fixing in NME on its own terms. Later, NME could also take a lighter
  message from G1-Emu for an assignment (the CC change alone), with no full read and no lost
  bank position.

## How each DSP processes audio

- **Clocks, from the OS itself.** All four programs write `PCTL=$3C001A`: MF+1=27, PD+1=4. With a
  12.288 MHz crystal (128 × 96 kHz) that is **82.944 MHz = 864 cycles per sample** at 96 kHz. The
  emulator runs the DSPs at 2025/512 × the CPU clock.
- **IRQD is the sample clock.** The vector `$16` is `jsr` to the block routine (`$175`, or later
  if control code was inserted): one block, one sample, per IRQD. The emulator raises IRQD on a
  fixed 864-cycle grid shared by the four DSPs (they stay in lock-step), and only when the IPRC
  enables it.
- **The main loop** is a `do forever` whose end is `$174` (a `NOP`). It polls the host port and,
  every 4 blocks (24 kHz, when `X:$1` passes 3), runs the control-rate code.
- **The block routine:** DMA0 copies the received links (`X:$6C0`, 18 words) into the output
  buffer (`$6C0`/`$6E0`, alternating), the voices add on top, and DMA4/DMA5 send the buffer out
  through ESSI0/ESSI1. A DSP without voices just passes on what it receives.

## The links between DSPs

- **Chain: DSP0 → DSP1 → DSP2 → DSP3 → codec.** DSPs 0–2 transmit on ESSI0 and ESSI1 with
  `CRA=$181801` (PM=1, PSR, 2 words per frame, 24 bits), internal clock, network mode. Word period
  = 2·(PM+1)·24 = **96 cycles: 9 words per sample per ESSI**, exactly what DMA4/5 send per block.
  That is 18 channels per link.
- DSPs 1–3 receive with DMA2/3 into two 9-word rings (`X:$6C0` and `X:$6C9`, 2D with DOR2=−8,
  continuous). DSP 3's `CRA=$181802` (PM=2, 144 cycles) is the codec clock; its receiver, in
  asynchronous mode, runs on DSP 2's clock.
- **Why it sounded stepped:** the emulator moved 2 words per sample on each link, while the DMA
  expected 9. Even at the right speed, the channels arrived shifted: the receiver re-enables its
  DMA after each reload and the first word to arrive becomes channel 0. On the hardware the common
  clock guarantees that; in the emulator the words wait until the threads synchronise.
- **How it is emulated** (`g1dsp.cpp`): **by position.** At each IRQD every DSP publishes what it
  just sent (`Y:[X:$5]` and `Y:[X:$6]`, 9 + 9 words) with its block number. When a receiver's ESSI
  asks for a frame, the emulator looks at which ring word its DMA will write next (DDR) and hands
  it that channel from the block 8 blocks ago (a fixed ~83 µs delay per DSP, so it never runs ahead
  of the threads). If that block is missing (the previous DSP is stopped reloading), silence.
- **Since 2026-10-14 the receivers are not run at all** (`Dsp::writeLinkRing`): at each IRQD the
  whole block from 9 samples ago (the 8 of the delay plus the one the DMA spends receiving it) is
  written into both rings at once, base+i for word i, while the receive DMA points at its ring. The
  receive side has no interrupt (`RIE=0`, `DIE=0` on DMA2/3) and the OS reads only the ring, so
  this is what the DMA left there anyway, minus the moments where a busy DSP read a ring half
  rewritten with the next block: BellsMorph loses its last crackle (energy above 20 kHz −44 →
  −67 dB). A quarter of a DSP thread's time went into those words; ~18 % faster.
  `G1_ESSI_RX_PER_WORD=1` brings back the receivers and their DMA, word by word.
- The ESSIs run at the rate derived from their CRA, set from `drainAudio` on the core's ESSI clock
  (a 96-cycle tick and a divider per direction; see "On upstream's core" below); the receivers of
  DSPs with an upstream DSP are clocked at 96 cycles per word. A receiver takes each word from the
  block whose slot was due, by the clock's count, not by the DSP's cycle count.

## Outputs, inputs and level

- **Outputs:** at each IRQD the emulator reads the 2 + 2 words DSP 3 just sent to the codec. Each
  ESSI carries its pair reversed: word 0 = output 2, 1 = output 1, 2 = output 4, 3 = output 3
  (checked with a 4Output and a different signal on each output). The headphones are a copy of 1/2.
  DSP 3 computes `out = X:$5F + in × Y:$5F`; `X:$5F = $155` is a small offset (removed by the
  output capacitor on the hardware, subtracted by the emulator).
- **Inputs:** they reach DSP 0 through the codec (it is the only DSP with nothing upstream): **R**
  on ESSI0 (DMA2 → `X:$6C4`) and **L** on ESSI1 (DMA3 → `X:$6C5`, whose interrupt, vector `$1E`,
  copies the other channel). From there they travel down the links in channels 4 and 5, where the
  AudioIn module reads them. In the emulator, DSP 0's codec receivers are clocked at **432 DSP
  cycles per word** (two words per 864-cycle, 96 kHz sample), independently of its 96-cycle-per-word
  transmitters to DSP 1. The RX period is explicit and survives repeated CRA writes.
- **Master volume:** the CPU writes DSP 3's `Y:$5F` with a generic helper (`$10C268`), ramping the
  sent value (`$162CE2`) towards the target (`$162CDE`) by 1/32 of the difference each time. The
  target comes from `$11030C`: a 128-entry table at `$153CAC`, from −113 dB to **−36.1 dB** at index
  `$7F` (`$01FEAA`). The index is always ADC ÷ 2 (at boot via `$10427E`, at runtime via the
  `$100|channel` event from `$104226`, handled by `$102E46`). So with the knob at full the master
  volume is −36 dB, and an OscA → 2Output comes out at −62 dBFS. The emulator loses no level; the
  front ends add +36 dB by default. Whether the real G1 has more gain after the DSP (analog stage)
  is still to be measured.

## Control-rate modules and the moving loop end

- **Symptom (fixed):** everything at control rate stood still: MasterOsc → OscSlv did not
  oscillate, Keyboard → ADSR was silent, ClkGen was dead, and chorus and overdrive "did nothing"
  because their internal LFO and amount are control rate.
- **How the OS does it:** if the patch has control-rate modules, the OS writes their code from
  `$174` on, moves the block routine after it (and the IRQD vector, stored in DOR0), and **extends
  the main loop by changing the LA register** with a host command (vector `$7C`: `movep
  x:$FFFFC6,la`), without touching the DO instruction. The control code's pointers (`r3`, `r4`)
  come from DOR1 and DCO1, which the OS uses as storage.
- **The bug, in Gearmulator:** the JIT records the loop end when it compiles the DO (from the
  instruction) and cuts its blocks there; a later change of LA goes unnoticed, so only the first
  instruction of the control code ran.
- **Fix** (`g1dsp.cpp`, `onLaChanged`): after each JIT block the emulator checks LA; if it changed,
  it moves the loop end in the JIT (`removeLoop`/`addLoop`) and discards the blocks compiled at the
  old and new ends. `G1_NO_LA_FIX=1` disables it (for comparisons).

### A cached block at the loop end

- **Symptom (fixed):** after a run of uploads (in one NME session, 18 patches loaded one after
  another into slot A) the emulator stopped answering: `Upload timeout at packet 3`, then every
  request timed out and NME still showed the synth as connected. Deterministic: the same patches in
  the same order hang it at the same upload, in `g1run` and headless in `g1patchtest`
  (`G1_BEFORE`). The real G1 takes the same sequences without trouble.
- **What happened:** DSP 0 ran past the end of its main loop. The OS had moved LA to `$28F` (see
  above) and the JIT knew it, but the one-instruction block at `$28F` (a `nop`) was not compiled
  with the loop-end check: execution fell into the sample routine at `$290` without an interrupt.
  That routine saves registers on a stack of its own in R6 and the modules borrow R6 in between, so
  every pass through it off an interrupt shifted the stack; after enough of them an `rti` returned
  to itself for ever. The OS, waiting for DSP 0 to answer a host command (`$10C354`), stopped
  answering the editor.
- **The bug, in Gearmulator:** with `cacheSingleOpBlocks` (on by default) a destroyed block of one
  instruction is kept, keyed by its opcode, and reused at that address; the cache does not look at
  whether the address has become a loop end since. With the G1 moving LA on every patch, a plain
  `nop` compiled earlier came back at the new loop end.
- **Fix** (`g1dsp.cpp`): the cache is off. Measured cost: 1-2 % (20 emulated seconds of
  `ButohDrone`: 12.5 s with it, 12.7 s without). `G1_SINGLEOP_CACHE=1` turns it back on, and the
  hang comes back.
- **How it was found:** a PC histogram after the upload (`G1_PCHIST`: the CPU polling DSP 0, and
  DSP 0 repeating one block 100 million times in 200 ms), then a ring of DSP 0's last blocks
  (`G1_DSPTRACE`) back from the stuck `rti` to the first pass that left the loop. Two wrong turns,
  for the record: the interpreter cannot be used to compare (see Tools), and IRQDs serviced while
  the OS had disabled them in the IPRC do happen on DSP 0 during reloads but were not the cause
  (dropping them changed nothing).

## DSP core fixes (`cmake/Dsp56300.cmake`)

Applied to a build copy; the clones are never modified.

### On upstream's core (branch `core-upstream`, 2026-10-14)

The DSP56300 core now comes from upstream (`dsp56300/dsp56300`, `G1_DSP56300_DIR`), not from
joelanders' fork inside gearmulator-md-mm, which still gives the 68k core. The fork had left
upstream on 2026-08-29, and G1-Emu used API only the fork has (one commit of 1,600 lines: the
ESSI "fine link" mode, host commands with arbitration, cycle deadlines for the peripherals, a
callback when an interrupt is serviced). On upstream, G1-Emu uses upstream's own means instead,
and keeps 23 small patches. What changed, and what the move turned up:

- **The ESSI clock.** Upstream's clock ticks at one rate and serves each direction of a port every
  (divider + 1) ticks. The tick is the link's word, 96 cycles (`g1dsp.cpp`, `g_essiTickCycles`).
  DSP 0's codec receivers take a word every 432 cycles, 4.5 ticks: they run every 4 ticks and once
  per sample their counter is set one tick back (`Dsp::codecTick`, through the clock's public tick
  callback), so two words per sample, 5 and 4 ticks apart. AudioIn still measures 440 Hz at
  −62 dBFS. A 48-cycle tick, which divides 432 exactly, cost 20 % of the speed.
- **It fell behind in busy patches, and the links went wrong.** Upstream serves one slot per call,
  so that the DSP can take each request before the next one, and calls the clock between JIT
  blocks. Once a patch fills the DSPs (BellsMorph), the blocks last longer than 96 cycles on
  average and the clock ran at ~72 % of the DSP: millions of cycles behind after a few seconds.
  It now serves up to 8 slots per call while it is behind. It also starved while interrupts kept
  coming: the core runs the peripherals only when no unmasked interrupt is pending, so the
  overlay's call in `execInterrupts` now runs them too, as it already collected host commands.
- **Which block a link word belongs to** is chosen by when its slot was due
  (`EsxiClock::getSlotClock`, added by the overlay), not by the DSP's cycle count when it got round
  to it, which is a few dozen cycles off. Near a block's edge the cycle count picked the next or
  previous block: a sample out of place now and then. That was in `main` too, under the fork: its
  heavy patches carried energy above 20 kHz (BellsMorph −21 dB, WavetableSynth −42 dB of the
  total) that the branch does not (−67 and −75 dB), with the same partials and level.
- **The clock's cycles-to-instructions ratio** was rounded up to a whole number (2.1 cycles per
  instruction taken as 3): every wait ended two thirds of the way and the clock was asked again.
  Now in fixed point.
- **`x:aa` / `y:aa` moves:** upstream charges 1 cycle for the read and 2 for the write; we keep 1
  for both. With 2, BellsMorph loses ~6,000 sample clocks in 3 s on DSP 0.
- **DMA:** upstream completes a DE-triggered block two cycles per word after it moves the data
  (the data moves at once, which is what mattered to the G1); dual counters on both sides and fixed
  to fixed are upstream's now; block per request without clearing DE (DTM=100) is still ours.
- **Upstream now covers:** short `MOVEM`, `DO FOREVER` and FV in nested loops (decided when the
  block is compiled), CMPM, GT/LE on both architectures, the volatile-P marks (`JitConfig::
  trackVolatilePMemory`, off for the G1), and the idle link port that deadlocked on `korg.pch` (its
  clock moves for every port at once, so a port that was off owes nothing).
- **The transmitters are served once per sample**, not once per word (`drainAudio`): what they
  send is never used, since the links and the outputs are read from the DSP's memory. ~30 % of
  the emulator's time, with byte-identical output. `G1_ESSI_TX_PER_WORD=1` brings back every word.
- **Neither are the link receivers** (see "How it is emulated" above): their divider is all ones,
  which a core patch takes as "never serve" (a huge divider would overflow the signed tick counter
  after ~41 minutes).
- **The JIT's limits stay** (unlinked blocks of 32, one loop iteration per block): longer or linked
  blocks serve the peripherals too late for the G1 and lose sample clocks; see
  `docs/core-upgrade-plan.md`, step 4.
- **A REP and its instruction** are one unit upstream, but the REP still skipped the per-block
  limit, so with one instruction per block the next instruction joined it: the cycle profile
  charged it to the REP (`g1dspcheck`).

The fork-era items below are kept for the history; the ones upstream covers are no longer patched.

- JIT: short `MOVEM`, `DO FOREVER` (FV flag), nested DO/ENDDO saving FV. One form for both
  architectures. `g1dspcheck` exercises finite, forever and nested forms with JIT block sizes 1
  and 32 on every CI platform.
- DMA with dual counters on source and destination at once (DAM `011 011`): not implemented, and
  in Release it reported the block done without copying.
- Immediate block transfers (the delayed ones let the voices add before the copy and overwrote
  them).
- DMA from a fixed address to a fixed address (DAM `100 100`): no branch at all; used for the
  audio inputs.
- DMA "block per request, DE not cleared" (DTM=100): ignored; DMA3 of DSP 0 uses it.
- ESSI on the fine schedule, idle: the clock's catch-up loop (`EsxiClock::exec`,
  `while(ic - fineLastClock >= finePeriod)`) counts from `fineLastClock`, which only moves when the
  port is served or its CRA is rewritten. A DSP with nothing to do can leave both ESSIs off for
  seconds; when it woke up the loop emitted every frame of the gap in one call. With nmedit's
  `korg.pch` DSP 0 woke after 434 million cycles (4.5 million frames) into a TX ring of 32768; the
  core's write callback (`Audio::Audio`, `m_audioOutputs.push_back`, a semaphore) waits for room,
  and the thread that would make room is the one waiting: a deadlock, with all workers idle. A
  real port does not queue the frames of the time it was off, so an anchor more than 64 periods
  behind is put back at 64. Found by logging each TX frame's cycle count: a burst is many frames
  at the same one. `G1_THREADS=0` did not avoid it, which is what ruled the threading out.
- **CMPM left |S| in its operand** (`jitops_alu.cpp`, `op_Cmpm_S1S2`). `alu_cmp` takes both absolute
  values in place, and when the operand is an accumulator `decode_JJJ_read_56` returns the JIT's
  cached register for it, not a copy. The rest of the block then read |a| for a, and if a was
  already dirty |a| was written back. It silenced every sawtooth: `OscA`/`OscB` wave 2 and
  `OscSlvC`. The waveform parameter rewrites a `jmp` in the oscillator (`P:$223` on DSP 0: `$26f`
  for saw, `$2aa` for square), and the saw branch does `sub x1,b a1,a`, `cmpm a,b`, `tgt a,b`, so
  its output froze at one value. Blocks of one or two instructions (`maxInstructionsPerBlock`)
  made the saw sound, three did not, which is what pointed at state carried inside a block; the
  core is shared by x86-64 and AArch64, so the Mac had it too. The operand is now copied first.
- **GT and LE on x86-64** (`jitops_decode_x64.cpp`) tested the parity of Z, N and V, which is right
  except when Z = 1 and N != V. Now (N ^ V) | Z. **LE on AArch64** (`jitops_decode_aarch64.cpp`)
  had the same blind spot another way: it added (N ^ V) and Z and tested for exactly 1, so the sum
  2 made LE false; now "not zero", like its GT. The x86 fix was first written believing AArch64
  was right; `g1dspcheck`'s GT/LE test failing on the arm64 and macOS runners is what showed it
  was not. It did not
  cause the silent saw (fixing it alone changed nothing there), but it is wrong, and `jitdiff`
  found it first.
- The core's **interpreter** has the same LE mistake on IFcc and Bcc (Z = 1, N != V). Not fixed:
  G1-Emu never runs it (`G1_INTERP` is unusable with the G1's main loop), and `jitdiff` says so.
- **"Volatile" program memory that never went away** (`jit.cpp`, `Jit::checkPMemWrite`, #6). When
  DSP code writes to P memory where a block is compiled, the core destroys the block and also puts
  the address in `m_volatileP` for good: every later block stops there, so the code around it
  runs one instruction per block. The OS writes module code into P memory on every patch load and
  every module or cable added from the editor, over the previous patch's compiled code, so the marks
  piled up for the whole session. After 30 uploads DSP 0 ran `$36e`–`$37f` and beyond one
  instruction per block on every sample: the same instructions, to the cycle, in 3.3 times as many
  blocks, and the emulator 25 % slower (3.5× → 2.6× real time offline, where it levelled off). A
  heavy patch then no longer fit in real time and crackled until a restart, which is what Javier
  heard with fast audio-rate modulation. The marks are not needed for correctness: a block ends
  right after a P write, so it never runs code it has just changed, and G1-Emu does not link blocks,
  so the destroyed block is recompiled on the next entry. The mark is now skipped;
  `G1_VOLATILE_P=1` brings it back, to compare. Found with `G1_BEFORE` (many uploads) plus
  `--bench` (blocks per second rising while instructions stay the same) and `G1_PCHIST` (the
  one-instruction blocks). It changes when some interrupts land by a few cycles, so the golden WAV
  of `WavetableSynth.pch`, which uses S&H, is no longer byte-identical: same pitch, same spectral
  peaks, both tonal. The other eight are identical, and the reference was recorded again.
- `tools/jitdiff` finds this kind of bug: it runs instructions from a dump of P memory on the JIT
  and on the interpreter from random states, or runs windows of K instructions as one JIT block
  against the same JIT one instruction per block, and prints every register or memory word that
  ends up different.
- `g1dspcheck` tests the JIT extensions, CMPM, GT/LE and this idle port with synthetic programs (no
  ROM). The
  idle-port test counts frames in a callback that never blocks, so without the fix it fails
  instead of hanging.

### When the JIT cannot generate a block (issue #17)

When asmjit cannot take a block (out of JIT memory, typically, with several plugin instances of
four DSPs each) or code generation fails, the core's `create()` left the "create" stub for that PC
and executed it, which called `create()` again with no end: a stack overflow that took the host
down. The overlay makes `create()` return, gives the JIT a sticky failure with its reason
(`Jit::setFailure`/`hasFailed`/`failReason`/`clearFailure`) and stops a parent from linking a
child that was not generated. `Dsp::runUntilImpl` checks it after each block: the first time it
empties that DSP's JIT cache (`destroyAllBlocks`, which frees its code memory) and goes on, at most
once per second of DSP time; if it fails again the DSP is given up: it runs no more, and
`runDead` puts out silent blocks on its sample clock, so the output and the next DSP get silence
on time and the plugin's runner never waits. The CPU's waits on that DSP's host port return at
once. The status bar shows the DSP as `x` with the reason, and a report includes it.
`G1_JIT_FAIL_AT=n` fails the n-th block emitted (over all DSPs) and `G1_JIT_FAIL_FROM=n` every one
from the n-th on; `g1jitfailtest recover|fail` (ctest) uses them through the plugin's runner.

### The DSP JIT on ARM

**Cause found, and it was never the G1's `FV` extension.** `g1dspcheck` raised an illegal
instruction on the Apple Silicon runner, always in the `finite DO` case. Routing the core's log to
stderr, flushed, printed the one line that settles it:

```
handleError@14: Error: 50 - InvalidImmediate: tst w5, 0, block at PC 000102, P mem size 1
```

`jitblock.cpp` closes a loop body with `test_(lc, Imm(maxDoIterations - 1))` + `jz`. G1-Emu runs
**one iteration per block** (`g1dsp.cpp` and `g1dspcheck` both set `maxDoIterations = 1`), so that
mask is **zero**, and zero is not an encodable AArch64 logical immediate — TST is `ANDS WZR, Wn,
#imm`, and the immediate encoding has no way to say "no bits". asmjit refuses the instruction
(error 50). In Release `AsmJitErrorHandler` only logs — its `assert` is compiled out — so the rest
of the block is never emitted and the DSP runs into whatever is there: `ILLEGAL`. On x86 the same
`test r32, 0` encodes perfectly, which is why it never showed here.

With that mask the test is always true, so the overlay emits an unconditional jump instead
(`cmake/Dsp56300.cmake`). The `#ifdef HAVE_ARM64` paths written for the `FV` flag were removed:
they were treating a symptom, and every form they tried — a complemented mask, `BFC`, `BFI` with
the zero register — encodes without error anyway. That last point is worth keeping as a method:
**asmjit builds its arm64 backend on any host**, so a suspect sequence can be assembled on this
x86 machine and checked without a Mac.

```
bfi(w0, wzr, SRB_FV, 1)              err=0 (Ok)   0x331003e0
and_(w0, w0, Imm(~SR_FV))            err=0 (Ok)   0x120f7800
and_(x0, x0, Imm(~SR_FV))            err=0 (Ok)   0x926ff800
and_(w0, w0, Imm(~(SR_LF | SR_FV)))  err=0 (Ok)   0x120f7400
tst(w5, Imm(0))                      err=50 (InvalidImmediate)
```

CI also gained a `Linux arm64` job (`ubuntu-24.04-arm`): the same AArch64 JIT on a machine that is
not Apple's, so a fault in the code generated is told apart from one in what macOS does with it.
Both pass since the fix ([CI run 35570337853](https://github.com/animatek/G1-Emu/actions/runs/35570337853)), with the DSP test gating all five jobs.

## The panel

- **Display:** HD44780 character LCD, 2 × 16, 8-bit bus. Data at `$202006`; control at `$202007`:
  bit 0 = RS (0 command, 1 character), bit 1 = E; the byte is latched when E falls. The OS
  initialises it with `$30` ×3 and `$38` and never reads the busy flag. Lowercase letters with
  descenders (p, y...) are custom CGRAM characters (codes `$08–$0F`): "Empty patch" is stored as
  `Em\x0Bt\x0D \x0Batch`. Emulated in `g1Lib/g1lcd.h`. With a patch loaded it shows the name and
  the voices per slot: `( 1) --  --  --`.
- **LEDs:** 32, in 4 rows of 8, multiplexed (`$104000`). The OS puts the row byte in `$202004` and
  selects the row with the low nibble of `$202005` (bit 3 = row 0 ... bit 0 = row 3). Active low.
  Identified: slots A–D = bit 7 of rows 0–3 (the active slot blinks); Store/System/Edit/Patch-Load =
  row 3 bits 3/4/5/6; knob *k* (1–18) = row (k−1) mod 3, bit 1 + (k−1)/3; Panel Split = 3.2
  (it lights when 2.2 is pressed). The five left are the Oct Shift ones, and the OS lights them in
  order: **0.0 = −2, 1.0 = −1, 2.0 = 0, 3.0 = +1, 3.1 = +2** (`$100B74`, indexing on the octave of
  the active slot). On the rack they never light, see below.
- **Buttons: 18, not 24.** 3 rows of 8, but the OS only reads **bits 2 to 7** of each row: bits 0
  and 1 are the dial (below). Bits 4–6 of `$202005` select the row (active low) and `$201800`
  returns it (pressed = 0). The scan (`$1040CE`) posts an event with the code
  `$400 + row × 6 + (bit − 2)`, so the whole panel is `$400`–`$411`:

  | row.bit | code | button | row.bit | code | button | row.bit | code | button |
  | --- | --- | --- | --- | --- | --- | --- | --- | --- |
  | 0.2 | `$400` | A | 1.2 | `$406` | Edit | 2.2 | `$40C` | Panel Split |
  | 0.3 | `$401` | B | 1.3 | `$407` | Play (Patch/Load) | 2.3 | `$40D` | Find |
  | 0.4 | `$402` | C | 1.4 | `$408` | Navigator up | 2.4 | `$40E` | Oct down |
  | 0.5 | `$403` | D | 1.5 | `$409` | Navigator left | 2.5 | `$40F` | Oct up |
  | 0.6 | `$404` | Store | 1.6 | `$40A` | Navigator down | 2.6 | `$410` | Assign/Morph |
  | 0.7 | `$405` | System | 1.7 | `$40B` | Navigator right | 2.7 | `$411` | Shift |

  **Where the names come from.** The flash keeps the factory test's tables, before the OS: the 18
  key codes at `$9962`, their names at `$9986` (18 strings of 8 bytes: Split, Find, Oct down, Oct
  up, Store, System, Edit, Play, A, B, C, D, Shift, Assign, Left, Up, Right, Down), the 32 LEDs as
  (row, mask) at `$9A16` and the 20 ADC channels at `$9A56` with their names at `$9A7E`. Its codes
  are the OS's plus 6, wrapping around `$411` → `$400`: the test scans the rows in the order 2, 0,
  1. That is what the twelve buttons already known confirm (A–D, Store, System, Edit, Play and the
  four navigator keys all land where pressing them had shown), and the six new ones are the row
  left over. Two of them are confirmed directly on the emulator: 2.2 lights LED 3.2 (Panel Split)
  and **holding 2.3 puts `Find` on the display**. Nothing is visible on the patch screen for Oct
  down/up, Assign or Shift, as one would expect from an octave shift with no keyboard, a modifier
  and a mode that needs its own screen.
- **The dial:** a quadrature encoder on **bits 0 and 1 of `$201800`**, not multiplexed (the same
  two bits whichever row is selected). The OS decodes it in its main loop (`$104DC6`, ~3 000 times
  a second, against the 185 of the button scan): it XORs the two bits with the previous reading
  (`$15EC1F`), ignores a step where both changed, and moves a counter at `$15EC72` that starts at 4
  and sends an event (class `$500`, 1 = clockwise) when it reaches 8 or 0. It accelerates: turned
  fast, one detent moves the value by more than one. `Microcontroller::turnDial(detents)` emulates
  it by handing out one edge every ~2 ms of CPU time, well apart for the OS to catch each one.
- **What each button does**, watched on the emulator (display, LEDs and what the OS sends to the
  editor). Slots A–D pick the slot, Store asks `Store?`, System opens the `SYSTEM MENU`, Edit opens
  the patch's pages and Play goes back to the patch screen.
  - **The navigator is row 1**: right and left walk along a menu line (`<SYNTH>  PATCH` →
    ` SYNTH <PATCH>`), down goes into the item (`MASTER TUNE`), and in the Edit pages left and
    right walk the four morph groups and, one level down, the parameters of a module
    (`Freq coarse` → `Freq fine`). Row 2 does none of that, which settles the two rows.
  - **Shift is the second function of another key**: **Shift + Store** opens `Store settings`
    (the panel's "Save Synth. Settings", a different screen from Store's `Store?`), and **Shift +
    a slot** shows and changes that slot's voices (`( 1) --  --  --` → `( 1)  1  --  --`). The OS
    keeps the modifier in a block at `$1C39D4` and each shifted action branches on it.
    **Shift counts for the next key only** (3.03 update, 2026-10-06, `g1patchtest` with
    `G1_PRESS=h2.7,...`): held through another key (Find, a slot, System), the next Store is a
    plain `Store?`; let go and pressed again, it is `Store settings` again. And Shift still held
    about half a second after Shift + Store turns `Store settings` into `Store?`; let go sooner, the
    display goes back to the patch. So the window's panel lets a latched Shift go after the key.
    The slots are the exception: Shift held through A, B and C changes each one's voices
    (`( 1)  1   1  --`), though the Store after them is still a plain `Store?`. Let go and pressed
    again between the slots, they change the same and Store is `Store settings`: after a slot the
    panel keeps Shift down and presses it again for the OS. **The navigator in Edit mode** is the
    other: on a module's page, Shift held + Right steps to the next module (`ADSR` → `Out` →
    `Amp`) as many times as pressed, with no need to press Shift again (2026-10-07, `g1patchtest`);
    so in Edit mode the panel keeps Shift down after a navigator key. The manual says the same,
    for all four navigator keys (v3.0, pp. 49 and 59).
  - **The mode keys (Store, System, Edit, Patch/Load)** act on the press: each held through the
    navigator, the dial, a slot, Assign, Find, a knob turn or another mode key, every display ends
    as with it pressed and let go (2026-10-07, `g1patchtest`), and the manual has no use for them
    held (Store and Patch/Load confirm on a second press, not a hold, and in Shift + Store it is
    Shift that is held; v3.0, pp. 50, 51 and 59). So the panel does not latch them.
  - **Find**, held down, puts `Find` on the display and goes back when released. Its second
    function, printed in red on the panel, is Panic: Shift + Find works in the emulator
    (Mike Fiction, 2026-10-05).
  - **The rotary dial** (2026-10-07, `g1patchtest`): on the patch display a turn opens `Load?`;
    with Shift held it sets the active slot's requested voices (`( 1)` → `( 7)` in four steps, as
    the manual says); at `Load?` and `Store?` it picks the patch or the location; in Edit mode it
    changes the value in focus, on the morph page the group's value, and with Shift + Assign held
    the morph range; in the System menu it does nothing on the top page and changes a setting
    inside one, except CTRL SNAP SHOT. Shift changes nothing in Edit or System mode.
  - **The System pages, one by one** (2026-10-07, `g1patchtest`: Down from the menu's SYNTH or
    PATCH, then the dial turned both ways and Right pressed, against the page as it was):

    | Page | Dial | Right |
    | --- | --- | --- |
    | SYNTH: MASTER TUNE | the tuning in cents | nothing |
    | MIDI CLOCK | EXT / INT, or the tempo | to the tempo |
    | GLOBAL SYNC | quarter notes | nothing |
    | LOCAL | ON / OFF | nothing |
    | PROGRAM CHANGE | SEND AND RECEIVE / RECEIVE / ... | nothing |
    | KEYBOARD MODE | ACTIVE SLOT / SELECTED SLOTS | nothing |
    | PEDAL POLARITY | NORMAL / INVERTED | nothing |
    | KNOB MODE | IMMEDIATE / HOOK | nothing |
    | LEDS ACTIVE | YES / NO | nothing |
    | MIDI VEL SCALE | the field in brackets (min, max) | to the next field |
    | MIDI CHANNELS | the slot's channel in brackets | to the next slot |
    | SYNTH NAME | the letter in brackets | to the next letter |
    | MEMORY PROTECT | OFF / ON | nothing |
    | PATCH: VOICES | the requested voices | nothing |
    | CTRL SNAP SHOT | **nothing** | **sends the snapshot** (`[--] SEND` → `-- [DONE]`) |
    | VOICE RETRIG | Y / N of the field in brackets (POLY, COMM) | to the next field |
    | PATCH NAME | the letter in brackets | to the next letter |
    | BEND RANGE | the range | nothing |
    | KEYB RANGE | the field in brackets (low, high) | to the next field |
    | VEL RANGE | the field in brackets (low, high) | to the next field |
    | PORTAMENTO | the field in brackets (TIME, NORM) | to the next field |
    | PEDAL MODE | SUSTAIN / ON/OFF SWITCH / ... | nothing |

    MEMORY PROTECT is the last Synth page and PEDAL MODE the last Patch page: Down stays there.
  - **Store** (2026-10-07, `g1patchtest`; the manual, p. 50): Store asks `Store?` in Patch mode
    only, and does nothing in System or Edit mode ("You can not store any patches if Nord Modular
    is in System or Edit mode"). Shift + Store opens `Store settings` (Save Synth. Settings) in
    Patch and System mode, and does nothing in Edit mode.
  - **Assign/Morph works in Edit mode only, held** (2026-10-07, `g1patchtest`, as the manual
    says, v3.0 pp. 41 and 49). With a parameter in focus in Edit mode, **Assign held + a knob
    turned** assigns that parameter to the knob: on another module afterwards, turning the knob
    jumps the display back to it (`ADSR … Attack 13.4m`). The same in Patch mode assigns nothing.
    **Shift + Assign held** shows the focused parameter's morph end value, with `|` in place of
    `:` (`Attack |13.4m`), and the dial changes it (`|16.3m`); let go, the display is back to the
    value (`:3.23m`); a parameter with no morph shows `| --`. Pressed and let go on its own, or
    outside Edit mode, it does nothing, which is what the earlier tries saw. The OS takes the key
    through the jump table at `$1255F4`, which sends it to `$106168` along with Shift and the two
    Oct keys (Oct up = 0, Shift = 1, Oct down = 2, Assign = 3).
  - Moving a knob that the patch has on a morph group jumps the display to the Morph page with
    that group's value, and the OS sends the editor a `$2F` message.
- **The System menu**, from the OS's table at `$1442EE` (the letter closing each line is the
  section shown in the display's corner; the Edit pages use `T`):
  - **S**: MIDI CHANNELS, MIDI VEL SCALE, MIDI CLOCK, LEDS ACTIVE, MASTER TUNE, KEYBOARD MODE,
    GLOBAL SYNC, PEDAL POLARITY, LOCAL, PROGRAM CHANGE, MEMORY PROTECT, KNOB MODE, SYNTH NAME.
  - **P**: VOICES, PORTAMENTO, PEDAL MODE, BEND RANGE, KEYB RANGE, VEL RANGE, PATCH NAME,
    VOICE RETRIG, CTRL SNAP SHOT.
  - **D**: DUMP ALL, DUMP ACTIVE, RECIEVE ALL (the OS's own spelling).
- **Selecting several slots does not layer them for MIDI on the rack** (Mike Fiction, 2026-10-05).
  Holding A and B (both pressed in the matrix) and trying the System menu's settings, notes from a
  MIDI keyboard on channel 1 still played only slot A; slot B played only once its MIDI channel was
  set to 1 as well. As with Oct Shift below, selected slots are the keyboard model's (its own keys
  play them); on the rack each slot answers its own channel, and slots are layered by giving them
  the same one. **G1-Emu does it itself** with KEYBOARD MODE on Selected Slots: the hosts copy a
  note on any selected slot's channel to every selected slot's (`app/midirouting.*`).
- **Local Off stops nothing on the rack** (Mike Fiction, 2026-10-06). With LOCAL set to Off,
  nothing was found that stopped working. Like KEYBOARD MODE, it is the keyboard model's: it
  would part its own keys and wheels from the sound. Whether the knobs then also go out on MIDI is
  not checked (the standalone's `MIDI` port and the plugin's MIDI output carry the MIDI OUT). The window's Synth Settings leave LOCAL out;
  KEYBOARD MODE is there, since G1-Emu layers the selected slots itself.
- **Oct Shift is the keyboard model's, not the rack's.** The OS keeps an octave shift per slot at
  `$1C3AB8 + slot`, signed −2 to +2, with a setter (`$101E0E`, taking 0–4) and a getter
  (`$101E28`). It travels **in the patch**: the deserializer writes it and the serializer reads it
  (3 bits of the header, `octaveShift` in NME, which already has it in its patch settings).
  Uploading a patch with `OctShift` 0, 2 or 4 leaves `$FE`, `$00` or `$02` in `$1C3AB8`, so the
  value does arrive. But **nothing in the note path reads it**: all 25 references to the variable
  are in the front panel's module, and a note plays at the same pitch whatever the value, whether
  it comes from the editor or from MIDI IN. The LEDs are not refreshed either: the routine begins
  with `cmpi.b #$1,$1C3AAC; beq` and `$1C3AAC` is the model byte, read at boot from **`$7FF` of
  the ROM, which is `$01`** in the rack's, and sent to the editor in a SysEx. Which fits the
  hardware: the octave shift moves the local keyboard, which the rack does not have. The buttons
  (2.4 and 2.5) and the five LEDs are wired in `g1gui` because the panel there is drawn from a
  photo of the keyboard model; on the rack they do nothing. **G1-Emu applies it itself:** the
  hosts move each note by its slot's shift on its way to the MIDI IN (`app/midirouting.*`), and
  the window lights the five LEDs from the active slot's shift.
- **Panel Split** (button 2.2, LED 3.2, flag `$18C0E4`, **0 = split on**) hands each group of knobs
  to a different slot (`$115EA8`), with two tables in the OS: `$145A94` gives the slot of each
  knob and `$145AA6` its number inside that slot.

  | Panel knobs | Slot | They become |
  | --- | --- | --- |
  | 1–6 | A | knobs 1–6 |
  | 7–12 | B | knobs 1–6 |
  | 13–15 | C | knobs 1–3 |
  | 16–18 | D | knobs 1–3 |

  That is, the panel's four groups, one per slot, each renumbered from 1. Slots C and D only reach
  their first three knobs. Checked on the emulator: with the split on, only knobs 1–6 still send
  parameter changes for the patch in slot A, and 7–18 go silent (their slots are empty).
- **Knobs:** ADC channels. `$202000` selects, `$202800` reads. Knob *n* = entry *n* of the table at
  `$14420A`: `$31` = 1, `$37` = 2, `$2D`, `$32`, `$28`, `$2E`, `$33`, `$29`, `$2F`, `$34`, `$2A`,
  `$1A`, `$35`, `$2B`, `$1B`, `$36`, `$2C`, `$1C` = 18; `$30` = master volume; `$18` = **the
  pedal**. Checked by assigning a knob to each module and moving each channel: the OS tells the
  editor which knob moved; the factory test's table (`$9A56`) names the same twenty in the same
  order, master volume first (`VR1 (Mstr)`, so knob *n* is `VR`*n*`+1` on the board) and `Pedal`
  last.
- **The knob assignments** (`g1Lib/g1knobs.h`, #13), found by diffing RAM dumps (`G1_RAMDUMP`) of
  runs that only differ in one assignment, then reading the OS's own Edit-page code. `$110F9A(slot,
  section, module, param, &moduleName, &paramName)` is the lookup the display uses:
  - Each slot has a block at `$1AB988 + slot × $6000`. In it, **the knob table** at `+$5C36`: 23
    entries of 4 bytes, `{section, module, param, -}` (knobs 1–18, then the pedal and the rest, in
    NME's order); section 0 = common, 1 = poly, 2 = morph; module 0 = nothing assigned.
  - **Module pointers**, one long per module index: `+$467A` (common) and `+$4C66` (poly). In a
    module: its type at `+$0E`, the name given in the patch at `+$13`, and its parameters from
    `+$25`, 8 bytes each, the current value in the fourth.
  - **Parameter names:** `$1C3B1C + type × 48` holds a pointer to the type's list, 12 bytes per
    parameter: 11 characters (`Freq coars`, `Pitch MA 1`) and the maximum value. The OS builds it in
    RAM at boot from the ROM, so the names come from the user's ROM, not from us. Morph (section 2)
    is named `Morph` (`$15BDD8`) with its groups at `$15BDDE + group × 8`. **The four morph
    values** (0–127) are at `+$5D64` in the slot's block, and again at `+$5D68`; both follow a
    morph knob as it turns.
  - The active slot is `$1C3ABE`; with Panel Split on, the two tables at `$145A94` / `$145AA6` say
    which slot and which of its knobs each panel knob is.
  - **A knob's position to a value:** value = position × (max + 1) / 256 (measured: 100 → 50, 254 →
    127 on a 0–127 parameter, 128 → 2 on a 0–3 one). Positions 0 and 255 change nothing, so the
    window keeps to 1–254.
- **The ADC returns the previous conversion.** Each read of `$202800` returns the result of the
  previous conversion and starts a new one on the selected channel. At runtime the OS selects the
  next channel, reads, and stores the value in the previous one (`$1041BE`); at boot it selects and
  reads twice. The emulator used to return the currently selected channel, which shifted every
  knob by one and read the runtime volume from `$18`.
- **`$201000` + port F:** another 8-row matrix the OS scans ~19 000 times per second (`$1177CA`),
  probably the keyboard of the keyboard model. Nothing on the rack.

## Real time and cost

- Where the time goes: the DSPs, in equal parts. One spinning thread per DSP (`g1mc.cpp`), synced
  every ~1 000 CPU cycles (about 20 000 times per second, too often to sleep) and at every CPU access
  to a host port. **Pipelined** (2026-09-28): a periodic sync launches the four DSPs towards that
  instant and the CPU runs on; the next sync waits for them first. A host-port access waits for that DSP
  (and the one upstream of it, whose link it reads 8 blocks late) and brings it up to that exact
  instant, so the CPU never sees it ahead or behind; the others keep running. What leaves
  each DSP (audio, the word for the CPU's side of the HI08, which the CPU reads at every instruction)
  is handed on by the CPU thread at the join. Output is byte-identical to the serial run
  (`G1_THREADS=0`); `tools/bench/` measures it.
- Real time with ~45% headroom on a Ryzen 7 5700X (the emulation thread is busy ~55% of the time;
  the process uses ~2.9 cores because the DSP threads spin while they wait).

## Tools

- `g1boot ROM N`: boots for N million instructions and reports where the PC is, the chip-selects
  and every unemulated hardware access. `dis A B`: disassembles. `replay FILE`: replays a recorded
  NME session (spaced like NME, with PID rewriting). `diff A.bin B.bin`: code executed after a
  message. Variables: `G1_WATCH=addr,...`, `G1_TAP`, `G1_BLOCKS`, `G1_TRACE`, `G1_FINDTX=word`
  (which CPU code sends that word to a DSP), `G1_ADCMUX`, `G1_ADCALL`.
- `g1patchtest ROM patch.pch [--note N] [--seconds S] [--wav f.wav] [--input-sine Hz]`: boots
  headless, greets like NME (IAm and the 16 connection messages), uploads the `.pch` with NME's own
  code (`PchFileIO` → `PatchSerializer` → `UploadPacketizer`), packet by packet waiting for each ACK,
  plays the note through the PC Port and measures the four outputs and the 18 channels of each
  link. Needs `../Nomad2026`. Variables: `G1_VERBOSE`, `G1_DUMP=dir` (DSP P/X/Y memory),
  `G1_PCWATCH=addr,...`, `G1_INTERP=mask` (DSPs on the interpreter; unusable with the main loop,
  because the interpreter runs a whole `do forever` without returning), `G1_NO_LA_FIX`,
  `G1_KNOBS=knob:module:param,...`, `G1_ADCSWEEP`, `G1_LEDSTATE`, `G1_PRESS=row.bit,...`,
  `G1_PREPRESS=row.bit,...` (pressed before the note, to hear what they change),
  `G1_HOLD=row.bit` (held down meanwhile, for modifiers such as Shift), `G1_DIAL=detents`,
  `G1_MIDINOTE=channel` (the note through MIDI IN instead of the PC Port) and `G1_PEEK=addr,...`
  (bytes of the CPU's memory, to read the OS's own variables). `G1_BEFORE="a.pch|b.pch"` uploads
  those patches to the same slot first, `G1_BEFORE_MS` apart (default 500), as a session in NME
  does; `G1_PCHIST=ms` prints where the CPU and each DSP spend the next ms after the upload;
  `G1_DSPTRACE=dsp,pc,arg` keeps a ring of that DSP's last JIT blocks (PC, R6, N6, SR, processing
  mode, SP, LA) and prints it when it reaches pc with R6 other than arg, or, with pc 0, when one
  block at or above arg repeats 1000 times; `G1_JITBLOCK=n` limits JIT blocks to n instructions. A step of `G1_PRESS` /
  `G1_PREPRESS` can be a knob (`k5=200`) or the dial (`d3`) instead of a button, so a whole panel
  gesture fits in one line (`1.2,1.6,k5=200,2.6`), and what the OS sends to the editor during it
  is printed. `G1_HOLD_END=1` keeps the held key down until every probe is over.
  `G1_KNOBINFO=1` lists what each panel knob moves, as the window's displays show it;
  `G1_RAMDUMP=file` writes the CPU's 1 MB of RAM once the patch and its knobs are in.
  `G1_REALTIME=seconds` plays the note in real time with `EmuHost`'s pacing and `AudioBridge`
  against a simulated 48 kHz card, printing dropouts and lag once per second; `G1_RT_NOTES=60,64,...`
  adds notes through MIDI IN and `G1_RT_KNOB=k,ms,from,to` sweeps knob k every ms between those
  seconds.
- **Connector indices:** in a `.pch`, connectors go by their `index` in `modules.xml`, which is not
  always the list order (in the Overdrive, `in` is input 0 and `overdrive mod` is 1).
- **Module battery** (`tools/battery/battery.py`, results in `docs/module-battery.md`): every
  module type in a patch of its own, with an oscillator on its audio inputs, an LFO on its control
  ones, a running clock on its logic ones and a sine into the G1's inputs; its first four outputs
  go to the four outputs, which `g1patchtest` measures. Of the 109 types, **61 sound, 19 move**
  (a control signal under 20 Hz), 12 hold a fixed level and 17 give nothing. Silence is a list to
  look into, not a verdict: the first ones looked at were the test's fault, not the emulator's —
  Constant is bipolar and 64 is its zero, DrumSynth with its levels open is the loudest module
  measured (−51.7 dBFS), and MasterOsc only has a master-slave output, which carries no signal of
  its own.
- `g1run` records everything NME sends in `~/.local/share/Animatek/G1-Emu/pcport-in.bin`, to replay
  it. `G1_RECORD=10` records 10 s of output as a WAV (without the variable nothing is recorded).

## A patch's name cut short (open, 2026-10-08)

`LfoAdditive05NET.pch` (James Clark's, in the community archive) comes back named **"LfoAdd"**
whatever its file is called (`LfoAddTen1`, `LfoAdditive05NE`...), on the display and in the bank
once stored: through the Presets page's Load .pch and through `g1patchtest`, which uploads as NME
does, so not an upload of ours. The name goes out whole (the first packet carries `LfoAddTen1`).
`future303` keeps a 16-character name. Not seen on the real G1 yet: to check there with NME before
calling it the OS's or the emulator's.
