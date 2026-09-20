# Raw MIDI: one G1 in the DAW's list, and nothing else

G1-Emu publishes its two ports, **PC Port** and **MIDI**, as an ALSA **sequencer** client called
`G1-Emu`, the same way the hardware has two DIN sockets. NME, `aconnect` and anything else that
speaks the sequencer API sees them straight away and needs nothing from this page.

Some programs never look there. Bitwig Studio on Linux reads **raw MIDI devices**
(`snd_rawmidi_*`, `/dev/snd/midiC*D*`): its engine loads `libasound` only for that, and even with
its native PipeWire audio backend it takes no MIDI from the graph. A sequencer port, whoever
creates it, is invisible to it, and **only the kernel can make a raw MIDI device**.

So the G1 gets a card of its own, and **the emulator takes it over itself** at startup: it finds
the card whose ID is `G1`, links its first port to the G1's MIDI and, if the card has a second
one, the PC Port as well, in both directions. It says so in its log:

```
raw MIDI: MIDI <-> f_midi
```

There is no helper to run and nothing to route by hand, and it is tried again every two seconds,
so the card may be loaded after the emulator. `G1_RAWMIDI=<id>` or the **Settings** window takes
over another card instead; `G1_RAWMIDI=0` leaves raw MIDI alone.

## The card: a USB MIDI gadget, not snd-virmidi

**Do not use `snd-virmidi` for this.** It hard-codes sixteen subdevices per device and the name
"Virtual Raw MIDI", and neither can be changed by any module parameter: one card puts sixteen
`Virtual Raw MIDI/1..16` entries in the DAW's MIDI list, and `midi_devs=2` puts thirty-two. That
is what the G1's bridge used until 2026-09-20 and it was unusable.

Use the USB MIDI gadget instead. `dummy_hcd` emulates a USB host and a device on the same machine,
and `g_midi` presents a MIDI instrument on it — both are stock in-tree kernel modules, nothing is
compiled here, and `g_midi` takes the port count and the name as parameters:

```sh
sudo modprobe dummy_hcd
sudo modprobe g_midi id=G1 iProduct=G1 in_ports=1 out_ports=1
```

To have it at every boot:

```sh
printf 'dummy_hcd\ng_midi\n' | sudo tee /etc/modules-load.d/g1emu.conf
echo 'options g_midi id=G1 iProduct=G1 in_ports=1 out_ports=1' | sudo tee /etc/modprobe.d/g1emu.conf
```

## The two sides, and which is which

The gadget is plugged into an emulated host, so ALSA ends up with **two cards, one per side of the
virtual cable**. That is how the MIDI crosses over, and each side belongs to one program:

| Card | `amidi -l` | Who uses it |
| --- | --- | --- |
| `G1` — `MIDI Gadget - f_midi` | `hw:5,0` `f_midi` | **The emulator.** This is the card ID to put in Settings. |
| `G1_1` — `USB-Audio - G1` | `hw:6,0,0` `G1 MIDI 1` | **The DAW.** This is what Bitwig lists, as `G1 MIDI 1`. |

The card numbers depend on what else is plugged in; the IDs do not. In Bitwig, a **HW Instrument**
sending to `G1 MIDI 1` on the slot's channel (1 for slot A) plays the G1. Audio comes back on its
own way: G1-Emu's JACK outputs `out_1..out_4`.

Two entries is the floor for a card made of stock modules. One entry with the two ports named
"PC Port" and "MIDI" needs a driver of the G1's own — one was tried on 2026-09-20 and faulted on
insertion, so it was withdrawn, and any new attempt gets validated in a VM first, never on the
maintainer's host. The way out that needs no card at all is the plugin: see
[instance-hosting.md](instance-hosting.md), where the DAW hands the MIDI over directly.

Diagnostics: `amidi -l` for the raw devices, `aconnect -l` for the sequencer ports and their
subscriptions, and the emulator's own log, which counts the bytes each port receives. To check the
cable itself, without the emulator: `amidi -p hw:5,0 -d` in one terminal and
`amidi -p hw:6,0 -S "90 3C 64"` in another.
