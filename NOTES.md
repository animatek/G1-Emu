# Emulating the Nord Modular G1 — technical notes

Goal: run the G1's original system on emulated chips (Gearmulator) and play it from
**Animatek NME**, exactly like the real synth. This file collects what is known about the
hardware and the OS and how each part is emulated. Addresses are hexadecimal (`$`); CPU
addresses are in RAM unless stated otherwise.

## The ROM

The rack OS 3.03: 512 KB, sha256 `d9b199f2…c997e`, file `Roms/NORD-MODULAR-RACK-VER-3.03.BIN`.
It never goes into Git. The official updater (`Nord Modular OS v3.03b Update`, 1999) should carry
the same OS in another format and can be used to cross-check it.

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

## Boot and memory map

**The loader** (`$000000`–`$0007FF`) reads the panel at power-on (selects rows in `$202005`,
reads `$201800`) and chooses:

| Keys held | What it does |
| --- | --- |
| none | Copies the OS from the flash at `$300000` to RAM (length at `+8`, data from `+$20`) and jumps to `$100000`. If the flash is empty (`$FFFFFFFF`), it enters update mode. |
| D1=`$7F`, D2=`$BF` | MIDI update mode: runs the *Update utility* from `$0800`. |
| D1=`$7F`, D3=`$F7` | Boots the **factory OS stored in the ROM itself** (`$C800`, `$1CE01` long words). |
| D3=`$EF` and D1=`$F7`/`$FB` | Factory and RAM tests. |

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
- **PIDs:** each upload gets a patch ID from the OS (ACK `$36`). `g1boot ... replay` rewrites the
  PID of Parameter (cc `$13`), PatchModification (cc `$17`, except `$41`) and PatchPacket (cc
  `$1C–$1F` without the command bit) messages with the one the OS gave, and redoes the checksum
  (sum from `F0` to the payload, `& $7F`).

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
- The ESSIs run at the rate derived from their CRA (Gearmulator fork's "fine link" mode); the
  receivers of DSPs with an upstream DSP are clocked at 96 cycles per word.

## Outputs, inputs and level

- **Outputs:** at each IRQD the emulator reads the 2 + 2 words DSP 3 just sent to the codec. Each
  ESSI carries its pair reversed: word 0 = output 2, 1 = output 1, 2 = output 4, 3 = output 3
  (checked with a 4Output and a different signal on each output). The headphones are a copy of 1/2.
  DSP 3 computes `out = X:$5F + in × Y:$5F`; `X:$5F = $155` is a small offset (removed by the
  output capacitor on the hardware, subtracted by the emulator).
- **Inputs:** they reach DSP 0 through the codec (it is the only DSP with nothing upstream): **R**
  on ESSI0 (DMA2 → `X:$6C4`) and **L** on ESSI1 (DMA3 → `X:$6C5`, whose interrupt, vector `$1E`,
  copies the other channel). From there they travel down the links in channels 4 and 5, where the
  AudioIn module reads them.
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

## DSP core fixes (`cmake/Dsp56300.cmake`)

Applied to a build copy; the Gearmulator clone is never modified.

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
- `g1dspcheck` tests the JIT extensions with synthetic programs (no ROM).

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
  - **Find**, held down, puts `Find` on the display and goes back when released. Its second
    function, printed in red on the panel, is Panic.
  - **Assign/Morph: nothing found yet.** Pressed or held, alone or with Shift, on the patch
    screen, on the Morph page, on a parameter page and in the System menu, before and after
    moving a knob or the dial: display, LEDs and the traffic to the editor come out the same as
    without it (checked against a control run each time). The OS does take the key: the jump
    table at `$1255F4` sends it to `$106168` along with Shift and the two Oct keys, numbered
    Oct up = 0, Shift = 1, Oct down = 2, Assign = 3. So either it needs a screen not reached yet,
    or it is another thing the rack does not use.
  - Moving a knob that the patch has on a morph group jumps the display to the Morph page with
    that group's value, and the OS sends the editor a `$2F` message.
- **The System menu**, from the OS's table at `$1442EE` (the letter closing each line is the
  section shown in the display's corner; the Edit pages use `T`):
  - **S**: MIDI CHANNELS, MIDI VEL SCALE, MIDI CLOCK, LEDS ACTIVE, MASTER TUNE, KEYBOARD MODE,
    GLOBAL SYNC, PEDAL POLARITY, LOCAL, PROGRAM CHANGE, MEMORY PROTECT, KNOB MODE, SYNTH NAME.
  - **P**: VOICES, PORTAMENTO, PEDAL MODE, BEND RANGE, KEYB RANGE, VEL RANGE, PATCH NAME,
    VOICE RETRIG, CTRL SNAP SHOT.
  - **D**: DUMP ALL, DUMP ACTIVE, RECIEVE ALL (the OS's own spelling).
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
  photo of the keyboard model; on the rack they do nothing.
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
- **The ADC returns the previous conversion.** Each read of `$202800` returns the result of the
  previous conversion and starts a new one on the selected channel. At runtime the OS selects the
  next channel, reads, and stores the value in the previous one (`$1041BE`); at boot it selects and
  reads twice. The emulator used to return the currently selected channel, which shifted every
  knob by one and read the runtime volume from `$18`.
- **`$201000` + port F:** another 8-row matrix the OS scans ~19 000 times per second (`$1177CA`),
  probably the keyboard of the keyboard model. Nothing on the rack.

## Real time and cost

- Where the time goes: the DSPs, in equal parts. One thread per DSP (`g1mc.cpp`): DSP 0 runs on
  the CPU thread and DSPs 1–3 on three spinning threads that synchronise every ~1 000 CPU cycles
  (about 20 000 times per second, too often to sleep). What leaves each DSP is queued and handed on
  by the CPU thread when all have stopped. `G1_THREADS=0` runs everything serially.
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
  (bytes of the CPU's memory, to read the OS's own variables). A step of `G1_PRESS` /
  `G1_PREPRESS` can be a knob (`k5=200`) or the dial (`d3`) instead of a button, so a whole panel
  gesture fits in one line (`1.2,1.6,k5=200,2.6`), and what the OS sends to the editor during it
  is printed. `G1_HOLD_END=1` keeps the held key down until every probe is over.
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
