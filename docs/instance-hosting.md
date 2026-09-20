# Standalone instances and VST3 hosting

Design proposal, 2026-09-20. Requested by Javier; researched by Codex.
This is a delivery plan, not implemented multi-instance or plugin support.

## User-facing contract

- Standalone: one performance MIDI destination per running instance, named
  `G1Emu`, `G1Emu 2`, etc. All four slots share that destination and use MIDI
  channels, as on the hardware. Closing an instance removes its destination.
- Keep the editor connection distinct: `G1Emu Editor`, `G1Emu 2 Editor`, etc.
  It is not another performance destination. NME needs a way to select the
  intended instance; simultaneous editing of several instances is separate NME work.
- VST3: the track supplies notes/controllers and receives audio. Creating a plugin
  instance must not create a system performance MIDI device, ALSA/JACK audio client,
  or require a virtual MIDI kernel module. An external NME editor connection remains
  an explicit optional service, separate from the track's MIDI input.
- Instance identity must outlive transient ALSA client numbers. Display numbering
  is not sufficient to identify saved state or reconnect a DAW route.

## Why the current Bitwig bridge cannot deliver that contract

Javier confirmed that the snd-virmidi bridge appears in Bitwig and plays the emulator.
It remains a Linux compatibility workaround, not the intended instance lifecycle.

`midi_devs=1` creates one Raw MIDI device, but `snd_virmidi_new()` creates **16 input
and 16 output substreams** with hard-coded counts. The card driver also hard-codes
the Raw MIDI name to `Virtual Raw MIDI`. `id=G1Emu` changes the card ID, not that name.
Renaming our sequencer client cannot fix either property in Bitwig's Raw MIDI picker.

Sources: Linux [virmidi.c](https://github.com/torvalds/linux/blob/master/sound/drivers/virmidi.c)
and [seq_virmidi.c](https://github.com/torvalds/linux/blob/master/sound/core/seq/seq_virmidi.c).
The local `amidi -l` result also reports 16 subdevices for the dedicated card.

For this Bitwig backend, a genuinely single named Raw MIDI endpoint requires a
different bridge/driver implementation or a change in backend support. There is no
documented snd-virmidi parameter that provides it. Do not claim it is fixed by
changing ALSA sequencer names, reserving 15 substreams, or hiding duplicate rows.
Do not make a custom kernel driver a prerequisite for the VST3 or the portable core.
Evaluate that optional Linux backend separately, including install/uninstall,
kernel updates, hotplug and ownership of endpoints when an instance crashes.
An initial custom-driver experiment on 2026-09-20 faulted during insertion and was
withdrawn. It must not be loaded again on the maintainer's host. Any replacement
requires isolated VM validation; the single named Raw MIDI endpoint is not delivered.

## Shared core and host adapters

`g1Lib` already separates much of the chip emulation. `app/EmuHost` currently owns
MIDI devices, audio devices, wall-clock pacing, files and the emulation together.
Extract a shared engine before wrapping it as a plugin:

1. **Engine:** ROM/flash initialization, timestamped MIDI input (performance and
   editor separately), audio input/output, panel commands and state snapshots.
   No device enumeration, UI or automatic user-directory writes.
2. **Standalone host:** audio/MIDI backends, instance identity, endpoint lifetime,
   storage selection and pacing. Keep the current standalone working throughout.
3. **VST3 host:** JUCE AudioProcessor adapter, sample-rate conversion from the G1's
   96 kHz engine, event sample offsets, four output channels and two input channels,
   state save/restore and offline rendering. Assess the existing DSP worker model
   before choosing synchronous rendering or a bounded buffered worker design.
   Report any buffering latency to the host; avoid file I/O, unbounded waits and
   allocation on the audio callback.
4. **Panel:** share the existing panel presentation, but send commands through
   the engine boundary instead of exposing a mutable CPU reference to host/UI threads.

## State belongs to the instance

Today `defaultFlashPath()` returns the same flash file for every process and
`saveFlash()` uses the same `.tmp` name. Logs and recordings also share the flash
directory. Automatic multi-instance launch must wait until these are isolated.

- Standalone profiles need independent flash, temporary files, logs and recordings,
  plus exclusive ownership of writable profiles. Opening a second instance must
  never overwrite the first instance's banks or silently reuse its writable profile.
- Plugin state belongs to the DAW project/instance; never autosave into the standalone
  flash. Define patch/bank persistence and migration explicitly.
- Full flash contains installed OS bytes: do not blindly serialize it into a plugin
  preset. Define a state format that separates user state from firmware; load the
  user's ROM separately and identify/validate it. No ROM in Git or releases.

## Acceptance gates

- Two standalone instances receive different notes and retain independent banks;
  closing either leaves the other and its connections intact. Crash recovery does
  not steal an active identity or profile.
- With a supported single-endpoint backend, Bitwig shows exactly one named
  performance destination per instance, with no leftover destination after close.
  Validate this in Bitwig, not just `aconnect`.
- Two VST3 instances play independently without external performance MIDI ports;
  save/reopen a DAW project restores each one's user state without shared files.
- Validate MIDI note timing, note-off handling, variable block sizes, sample rates,
  offline bounce, reported latency, and all audio channels.
- External NME traffic reaches only its selected engine; editor uploads cannot block
  the DAW audio callback. Multi-instance GUI/ROM/DSP-core assumptions need an audit.

Recommended sequence: isolate state and engine boundaries, deliver a minimal VST3
instrument, then complete portable standalone backends. Keep the tested Bitwig
bridge available while the dedicated single-endpoint Linux backend is evaluated.
