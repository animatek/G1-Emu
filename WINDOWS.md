# G1-Emu on Windows: first-run guide

G1-Emu runs on Windows 11, but its two application-owned MIDI ports are temporarily blocked by
[a Windows MIDI Services bug](https://github.com/microsoft/MIDI/issues/1047). Until Microsoft's
fix reaches Windows, use [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html) as the
two virtual cables between G1-Emu and the editor.

G1-Emu includes no ROM. You need your own 512 KB ROM dump from a Nord Modular **rack** running OS
3.03. Do not distribute the ROM with the emulator or upload it to the project.

## 1. Install the programs

1. Extract the G1-Emu Windows ZIP to a normal folder.
2. Install and start loopMIDI from its official site.
3. Install or extract a G1 editor. These steps use
   [Animatek NME](https://github.com/animatek/Animatek-NME).

The executables are unsigned. Windows SmartScreen may show an unidentified-developer warning;
choose **More info**, verify that the file is the one downloaded from the G1-Emu GitHub release,
then choose **Run anyway**.

## 2. Make the editor cables

In loopMIDI, create these two ports with the names exactly as shown:

- `G1→NME`
- `NME→G1`

Keep loopMIDI running while using the emulator. Two ports are intentional: using one loopback
port for both directions can feed a program's own output back into its input.

## 3. Start G1-Emu and choose the ROM

1. Run `G1-Emu.exe`.
2. When asked for a ROM, choose your own rack OS 3.03 `.bin` file.
3. Open **Settings**.
4. Choose an audio driver and device. **System default** is the simplest first test; ASIO is also
   available when an ASIO driver is installed.
5. In **MIDI ports**, set:
   - **PC Port out:** `G1→NME`
   - **PC Port in:** `NME→G1`
6. Leave **MIDI out** and **MIDI in** on **Automatic (owned port)** for now. They are the separate
   performance-MIDI connection for a keyboard or DAW, not the editor connection.
7. Close G1-Emu and open it again. MIDI-device changes take effect on the next start.

Settings and flash are stored in `%APPDATA%\Animatek\G1-Emu`. Removing the application folder does
not remove them.

## 4. Connect Animatek NME

1. Start Animatek NME and open **MIDI Settings**.
2. Set **MIDI INPUT** to `G1→NME`.
3. Set **MIDI OUTPUT** to `NME→G1`.
4. Press **Connect**.

NME should identify the synth and enable the editor. In G1-Emu's status line, **PC Port in** rises
when NME sends its 8-byte request, and **PC Port out** rises when the emulated G1 answers.

## 5. Load a patch and hear it

The emulated synth starts with empty slots. Open or create a `.pch` patch in NME and upload it to a
slot. A simple oscillator connected to a 2 Output module is enough for the first audio test. Play
notes from NME's keyboard.

## If NME says "No response from synth (timeout)"

Check these in order:

1. loopMIDI is still running and both ports exist.
2. The arrows are not crossed: NME input and G1-Emu output both use `G1→NME`; NME output and
   G1-Emu input both use `NME→G1`.
3. G1-Emu was restarted after changing its MIDI dropdowns.
4. G1-Emu's **In use now** section says that the manual MIDI devices opened successfully.
5. In NME's SysEx Monitor, a connection attempt shows an 8-byte `TX` message. If **PC Port in** in
   G1-Emu stays unchanged, the NME-to-G1 cable is wrong or closed. If input rises but output does
   not, report the G1-Emu status and byte counters in a GitHub issue.

The console program `g1run.exe --list-devices` lists the MIDI and audio devices that G1-Emu can
currently see.
