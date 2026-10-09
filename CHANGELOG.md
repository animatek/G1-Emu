# Changelog

Everything that changes in G1-Emu, newest first. **Rule: every change that goes into the repo gets
its line here, in the same commit** (see `CLAUDE.md`). Each entry says who made it, what changes
and how it was checked; the commit is the one that brings the entry (`git log -- CHANGELOG.md`).
Older entries cite their commit by hand.

## 2026-10-09

- [GUI] **The display below the knobs lets a name linger (Mike Fiction, Claude).** When the mouse
  leaves a control, its name and value stay on that display for half a second, then fade out in
  60 ms, instead of going blank at once. Moving straight onto another control changes it at once.
  Checked: builds on Windows.

- [Docs] **Agent instructions: the shared CODE changelog is written with `cambios apuntar` (Claude, asked by
  Javier).** `AGENTS.md` now says to log in the maintainer's workspace changelog through that command and never
  by editing the file, which three times turned its symlink into a loose copy. Docs only, nothing to check.

- [Fix] **Random reaches every knob (Mike Fiction, Claude).** A few knobs at random kept their old
  values on each Random: Shift was let go and the knobs moved at the same moment, and the OS, which
  sees Shift's release only at its next scan of the buttons, took the knobs it read before that as
  Shift + knob and ignored them. The knobs now move 60 ms after Shift is let go; the reset to the
  patch's values does the same. Checked by Mike Fiction in the standalone, over many presses.

- [Fix] **A stepped knob stays where it was turned (Mike Fiction, Claude).** With Knob Follows
  Patch on, a knob on a parameter of few values (a waveform, a mode) jumped to the middle of its
  value's slice half a second after it was turned or set by Random. It now stays wherever it is
  while that position still gives the patch's value (give or take a position: the OS rounds a
  little differently at times), and moves only when the value differs (another patch or slot, an
  editor). Checked by Mike Fiction in the standalone.

- [GUI] **The knobs turn into place (Mike Fiction, Claude).** A knob the G1 moves (another patch or
  slot with Knob Follows Patch on, the host's automation) used to jump there; it now glides like
  an automated desk, as on Mike Fiction's Waldorf Wave: 30 % of the remaining distance each 60th
  of a second, on the screen's refresh, there in about 0.14 s. Random and its reset to the patch's
  values turn the knobs the same way: the G1 has each new position at once, and the knob is drawn
  turning to it. Only the picture moves, never the G1's knob position; a knob being turned by hand
  follows the mouse. Checked by Mike Fiction in the standalone.

- [Fix] **A patch stored from the panel shows on the Presets page (Mike Fiction, Claude).** The page
  only read its bank when opened or switched to, and the OS tells no one of a store made on the
  panel, so a stored patch appeared only after leaving the page and coming back. Now a press of
  Store (not Shift + Store) while the page is open has it read its bank again 1.5 s later; the
  confirming press is the one that counts. Checked by Mike Fiction in the standalone: a patch
  stored into the bank shown appears in the list without leaving the page.

- [Fix] **The Presets page's Hide empty is remembered (Mike Fiction, Claude).** It came back ticked
  at every start, whatever was left. The standalone now keeps it as `presetsHideEmpty` in the
  settings file; the plugin still starts with it on, as with the tooltips. Checked by Mike Fiction
  in the standalone.

- [GUI] **Panel tweaks for the Presets page (Mike Fiction, Claude).** The Presets and Settings
  buttons are drawn from Mike Fiction's new grey button (`skin/button_grey.png`, the wide button's
  layout), so the emulator's own pages stand out from the synth's functions. The Shift label is
  10 px higher in the background art, and the Shift button with it. The Presets page has its own
  frame by Mike Fiction (`skin/presets_panel.png`, its title lettered in), instead of the Synth
  Settings' frame blanked with the title drawn in code, and Hide empty and the count sit together
  in the middle of its title bar. Checked by Mike Fiction in the standalone.

- [GUI] **A Close button on the Synth Settings and Presets pages (Mike Fiction, Claude).** Each
  page had only Esc and its own button at the panel's far right to close it; Close now sits at its
  bottom right, its tooltip naming Esc. On the Presets page Load .pch moves left of it; on the
  Synth Settings page the note beside it is in two lines. Checked: the standalone builds and runs.

- [Fix] **The Presets page loads into the active slot even while its LED is dark (Mike Fiction,
  Claude).** The page took the slot from the slot LEDs, and the active slot's LED blinks: with B, C
  or D active, the footer swapped between that slot and A in time with the blink, and a click while
  the LED was dark loaded into A. The panel now reads the active slot from the OS, where the knobs
  read it (`$1C3ABE`, `$20` higher under the 3.03b update). Checked by Mike Fiction in the
  standalone: with B, C or D active the footer names that slot steadily and a click loads into it.

## 2026-10-08

- [Docs] **Release notes for v0.1.0-alpha.14 (Claude).** The Presets page (banks, a click to load,
  Load .pch), Mike Fiction's panel work (#36, #40, #41, #43), the gestures for the DAW (#42), and
  Animatek NME 0.21's direct link; the Windows known issue points to it, and #42 is listed as fixed
  but not yet tried in Nuendo.

- [Fix] **After a load from the Presets page, the editor's own answers reach it (Claude, reported
  by Javier).** NME still took seconds to show a patch loaded from the page. The link kept from the
  editor every ACK while a request of its own was open and for 300 ms after, so as to catch late
  answers; the moment NME heard of the new patch it asked for it, and the OS's answer to NME was
  kept as the link's: NME waited for its retry. Now the link keeps only the ACKs of the kinds its
  own requests get (a list's $13 and $15, a load's $38, an upload's $36 and $7F), and the 300 ms
  only after a request that got no answer, not after one that did. Checked: `g1presetstest` has the
  editor ask for the patch the moment it hears of the load, and every ACK the G1 sends it from then
  on reaches it (1 of 1 under the factory OS; the 3.03b update answers that request with none);
  the trace before the fix showed that ACK kept; all tests pass.

- [Fix] **A load from the Presets page reaches the editor at once, not seconds later (Claude,
  reported by Javier).** With the dial and Load, Animatek NME had the patch at once; from the
  Presets page, some seconds later. The link only speaks once the editor has been quiet for half a
  second, so as not to cut into an exchange of the editor's, and Animatek NME greets the synth every
  so often (IAm) to see it is still there: each greeting started the wait again. A greeting is a
  question and its answer, not an exchange to keep out of: it no longer counts, in the Presets
  link, the Synth Settings' link and the slot keeper (which a greeting also made abandon a slot it
  was reading). And a load or an upload, which the user waits for, waits for 100 ms of quiet, not
  500. Checked: `g1presetstest` now loads with an editor greeting every 300 ms, and the editor hears
  of the load 250 ms after it is asked for (the OS's own time to load it; before, not within 3 s);
  all tests pass.

- [Fix] **The editor follows a patch loaded or uploaded from the Presets page (Claude, reported by
  Javier).** Loaded with the dial and Load, or by a Program Change, a patch reaches Animatek NME:
  the OS tells the editor (NewPatchInSlot, `f0 33 50 06 01 38 00 01 33 f7` for slot A) and NME
  fetches it. Loaded from the Presets page it did not: the request goes in through the PC Port, the
  OS takes it for the editor's own and only answers it, with an ACK the link keeps. Now the link
  tells the editor itself, with the same message the OS sends and the patch's new id from that ACK;
  after a Load .pch too, with the id the upload's first ACK gives. In the plugin the slot keeper
  hears it as well and reads the slot again, so the project keeps the new patch. Checked:
  `g1presetstest` and `g1pchtest` find the message, byte for byte the OS's, reaching the editor;
  all tests pass.

- [Fix] **Knobs turned on the panel reach the host as real gestures, and never under a lock
  (Claude, issue #42, reported by Waltercalling).** Nuendo crashed when one of its modulators was
  set to learn a parameter ("Acquisition") and a knob was turned in G1-Emu's window. Two things of
  ours could do it: the plugin told the host of a turn as a whole begin-value-end at every tick of
  its timer, thirty touches a second to a host learning a parameter; and it called the host while
  holding two of its own mutexes, so a host calling back into the plugin from inside (to learn the
  parameter, to keep an undo step) met a mutex already held. Now a turn is one gesture: a begin
  when the knob starts to move, its values, an end once it has rested for 300 ms (`HostGestures`,
  `app/plugin/hostgestures.h`); the host is told after the locks are let go, and a knob on its way
  to the host is left alone by the audio thread until the host has its value; a turn still open
  when the plugin goes is ended. The same gestures make automation in Touch mode record a turn as
  one. Checked: `g1hostgesturestest` (new: one begin and one end per turn at any timer pace, two
  turns two gestures, knobs apart, an open turn closed); all tests pass. Not checked in Nuendo
  (nobody here has it), nor with `g1vst3check` (Bitwig was open with the plugin).

- [New] **Load .pch on the Presets page (Claude, requested by Javier; Mike Fiction's idea).** A
  **Load .pch...** button sends a patch file to the synth with no editor: it is read and made into
  the G1's upload with Animatek NME's own code, copied into `app/nme` (its README says from which
  NME commit and how to bring it up to date) with NME's `modules.xml` built in. A card then asks
  where it goes: the bank, and the position, each named after what it holds, the first empty one
  offered; a position that holds a patch is named in a warning and the button says **Replace**.
  **Store** uploads it into the slot lit on the panel and stores it there (StorePatch, `$41 $0B`),
  and the bank is read again, so it shows in the list; **Load only** sends it to the slot and
  stores it nowhere; **Cancel**. `PresetsLink` uploads as an editor does, each packet after the
  OS's acknowledgement, and ends a transfer cut short with the empty last packet, so the OS is
  never left waiting for the rest. Checked: `g1pchtest` (new) uploads `ClockTest.pch` (one packet),
  `future303.pch` (17) and others into slot A, stores them in bank 9 at 99, and finds them there
  and on the display; all tests pass; the snapshots show the card, before and after the bank is
  read; Javier loaded and stored patches with it in the window. Found on the way, not ours: one patch comes back named by its first six letters, through
  NME's upload too (`NOTES.md`, "A patch's name cut short").

- [New] **The Presets page lists the synth's banks, and a click loads one (Claude, requested by
  Javier).** Pick a bank (1 to 9) from the drop-down at the top right and its names show in three
  columns, scrolling; **Hide empty** (on by default) leaves out the positions with nothing stored,
  and the count beside it says how many are used. A click on a name loads it into the slot lit on
  the panel; the loaded one is marked in orange. The names come from the OS over the PC Port as an
  editor reads them (GetPatchList, `$41 $14`) and the load is the OS's own LoadPatch (`$41 $0A`),
  through a new `PresetsLink` (`app/presetslink.*`), which talks between the editor's messages and
  hides the answers to its own requests from it, like the Synth Settings' link; in the plugin the
  slot keeper, the settings and the presets each take the others' requests for an editor's, so they
  never talk at once. The bank is read again each time the page opens or the bank changes. The
  standalone may now start a second window for `G1_SNAPSHOTS`, which touches no device. Checked:
  `g1presetstest` (new) decodes hand-made answers, reads bank 1 from an emulated G1 (98 of 99 used)
  under the factory OS and under 3.03b, and loading its first patch into slot A puts its name on
  the G1's display; all tests pass; the snapshots show the page with the bank's names; Javier
  tried it in the window and it works. Not done yet: Load .pch and Edit in NME.

- [Docs] **ROADMAP: the expanded G1 is tracked in #39 (Claude).**

- [Merge] **Mike Fiction's pull requests #36, #40, #41 and #43 (Claude, requested by Javier).**
  Presets and Settings lettered in the panel's own font (#36), switching pages without the panel
  showing in between (#40), oscillator pitch as a note with a click for Hz (#41, fixes #38), and
  right-click menus on glass with a panel menu: GUI Scale, Settings, Info (#43); their entries are
  below. `skin/LICENSE.md` now says the panel is again all Mike's art. Only this changelog clashed.
  Checked: it builds, all tests pass, and the snapshots show the new lettering and both pages.

## 2026-10-07

- [Imp] **Presets and Settings lettered in the panel's own font (Mike Fiction).** The two labels
  right of slot D in `skin/background.png` are redrawn from Mike's master artwork in the font of
  the rest of the panel, in place of the stand-in lettering; the master now carries both keys, so
  later art updates keep them. Nothing else in the image changes. Saved as 8-bit RGB, like the
  previous file. Checked on Windows: it builds, and the PNG matches the master export pixel for
  pixel.
- [Fix] **Switching between Presets and Settings no longer flashes the main panel (Claude,
  reported by Mike Fiction).** Pressing one page's key while the other page was open faded the
  open page out to the main panel and then faded the new one in, so the main panel showed for a
  moment in between. Now the new page fades in on top of the open one, and the old page is hidden
  once it's covered. Fading in from the main panel and out to it are unchanged. Checked on
  Windows: the standalone and the VST3 build, and the switch shows no main panel in between, both
  ways.
- [New] **A click on an oscillator's pitch display toggles Hz (Claude, requested by Mike Fiction).**
  Found while fixing the entry below: in the editor, the display box of seven oscillators switches
  between Hz and notes with a click (the coarse pitch of OscA, OscB, OscC and the Master, Spectral,
  Formant and Percussion oscillators). Their knob displays now do the same: they start as notes,
  as the G1's own display, and a click toggles Hz (tooltip "Click: Toggle Pitch / Hz"); every
  knob on the same module's pitch follows, and so does the info display. NME's own choice never
  reaches the G1, so the panel keeps its own until the window closes. Checked on Windows:
  `g1formattest` passes with the Hz readings, and Mike switched them in the standalone.

- [Fix] **The oscillators' coarse pitch reads as a note on the knob displays (Claude, issue #38,
  reported by Waltercalling).** The Master, Formant and Percussion oscillators' Pitch knob showed
  its plain value (36), and the Spectral Oscillator's showed Hz, where the G1's own display reads
  a note (C2). All four now read as notes, as OscA/B/C already did, with 60 = C4. Checked on
  Windows: `g1formattest` passes with four new cases, and Mike compared each knob display with the
  G1's display in the standalone.
- [Imp] **The knob menu says what a click will do (Claude, requested by Mike Fiction).** On a knob
  excluded from Random its item now reads "Include in Random", where it read "Exclude from Random"
  either way; and "Include all knobs in Random" is only there when at least one knob is excluded,
  instead of greyed out. Checked on Windows: Mike tried both in the standalone.

- [New] **Right-click menus on glass, and a menu for the panel (Claude, requested by Mike
  Fiction).** A right click on the panel, or on anything with no right click of its own (the
  displays, the LEDs, the keys that do not latch), opens a menu with **GUI Scale** (75% to 250%,
  the current one ticked), **Settings** (the gear's: audio, MIDI...) and **About** (the drawer's).
  The knobs keep their own menu, and the keys that latch and JUCE's own controls (the drawer's
  buttons, the master volume, the Settings page's boxes) keep their right click. The menus are
  drawn on the tooltips' glass, whose blur is now much stronger for both (one blur, at a quarter
  size, so it stays quick), in the tooltips' text, with a light band under the mouse, thin rules
  between groups and the knob menu's heading in capitals on a darker band. They open right of the
  pointer with the first item level with it, or above it near the bottom. Only `app/gui` changes;
  in the plugin a new scale resizes the editor as dragging its corner does. Checked on Windows:
  the standalone and the VST3 build, and Mike tried the menus in the standalone and in the VST3 in
  Bitwig.

- [Docs] **ROADMAP: running without a ROM (#16) is a goal around the beta, with no date (Claude,
  decided with Javier).**

- [Docs] **ROADMAP: no hardware dumps (Claude, decided with Javier).** Nobody opens a synth to
  read its ROM; what only the ROM holds is written as G1-Emu's own boot, with the OS from Clavia's
  public updaters (#16), checked against the rack ROM we have. That is also the way to the Micro.

- [Docs] **A release plan in `ROADMAP.md` (Claude, decided with Javier).** alpha.14: Presets
  (the synth's banks, read over the PC Port and loaded with a click) and the first test of the
  expanded G1; alpha.15: two models in the settings, Regular and Expanded (eight DSPs, #24), told
  apart by Animatek NME (the Micro Modular, with its own OS, becomes its own emulation or part of
  AG1TEK); alpha.16: the first beta. With what is known and what is open for each.

- [Fix] **The host's clock ticks land on the nearest frame (Claude).** `HostClock` truncated each
  tick's offset, so a tick whose arithmetic came out a hair under a whole frame (79999.9999) went
  one frame early. Which ticks did depends on the compiler: CI failed `g1hostclocktest` on Windows
  and macOS and not on Linux. Now each tick goes to the nearest frame, or to the next block's first
  one. The test now allows half a frame instead of one, and `G1_SEED` gives it other block sizes.
  Checked: it passes, and with 2000 other seeds.

- [Docs] **Release notes for v0.1.0-alpha.13 (Claude).** Mike Fiction's new panel, the plugin
  following the host's tempo (#20), mono outputs (#27), projects across OSes (#25), the direct
  link (#8, with the next Animatek NME), Presets (in development) and Settings, About; the Windows
  known issue now points to the direct link.

- [Imp] **Presets and Settings, two more keys in the slots' row (Claude, requested by Javier).**
  The page buttons Mike Fiction put under the display (Main Panel and Synth Settings) are gone;
  instead, right of slot D, **Presets** and **Settings** sit as the slots do, the LED above at the
  left and the label at its right. Each is a switch: a press shows its page over the knobs, another
  goes back to the panel, and one page closes the other (Escape too). **Settings** is Mike's Synth
  Settings page. **Presets** will list the synth's banks and programs to load with a click; it is
  not done yet (next release), and its page says so ("In development"), in the same frame. The
  background (`skin/background.png`) loses the old labels and gains the new ones: "Settings" is
  Mike's own lettering, "Presets" is set to match; the change is noted in `skin/LICENSE.md`, as
  CC BY asks. `G1_SNAPSHOTS` also takes the Presets page and the way back. Checked: it builds,
  the tests pass, and the snapshots show both pages and the panel again.

- [New] **`G1_SNAPSHOTS=dir`: pictures of the window (Claude).** The standalone, once the G1 has
  booted, saves the panel, the extras drawer, About at its top and at its end, and the Synth
  Settings page as PNGs, clicking what a user would click, and quits; with `G1_AUDIO=no
  G1_RAWMIDI=0 G1_DIRECT_LINK=0` and its own `HOME` it touches no device and no settings of the
  user's. For a look at the GUI from elsewhere (a phone, a pull request). Checked: the five
  pictures come out as expected.

- [New] **About, in the extras drawer (Claude, requested by Javier).** A card over the panel, like
  Restart's question, with the version, what G1-Emu is, how Animatek NME connects to it by itself
  (with a button to NME and one to the source code; any Nord Modular editor, new or old, connects through
  the PC Port), the credits (Mike Fiction's panel skin and GUI
  design, Gearmulator, contributors and testers) and the license of every part inside the program:
  GPLv3 for G1-Emu, Gearmulator's dsp56300 and mc68k; CC BY 4.0 for Mike Fiction's artwork; MIT for
  Musashi, the VST 3 SDK and CLAP; zlib for AsmJit; AGPLv3 for JUCE 8; and that G1-Emu is not
  Clavia's and ships no ROM. The README's "License and credits" says the same, with the credit line
  Mike asked for: "Panel skin and GUI design by Mike Fiction". Checked: it builds and the tests
  pass; the licenses read in each part's own license file.
- [Merge] **Mike Fiction's pull requests #29 to #35.** Original artwork for the knobs and buttons
  under CC BY 4.0 (#29), Synth Settings as a page of the panel (#30), the standalone's icon (#31),
  knob values as the G1 shows them (#32), Shift and the mode keys in Edit mode (#33), tooltips' delay
  and switch (#34), and the panel saying what each control will do (#35); their entries are below.
  Only this changelog clashed. Checked: everything builds and all tests pass on Linux.

- [Fix] **`g1vst3check` no longer talks to another G1-Emu (Claude).** It finds the PC Ports by
  name, and a DAW with the plugin open has the same names: with Bitwig running, `--clock` uploaded
  its test patch to that instance's slot A. It now stops at once when a `G1-Emu` MIDI port is
  already there. `--clock` also prints how late MIDIGlobal's pulses are against the host's beats,
  beyond the reported latency. Checked: it refuses with Bitwig open. Javier checked the tempo
  sync in Bitwig by ear and it follows the metronome.

- [New] **The plugin follows the host's tempo (Claude, issue #20, asked by Waltercalling).** A VST3 host
  sends a plugin no MIDI clock, so with the G1's MIDI clock set to external nothing clocked moved
  in the DAW. The plugin now makes the clock from the host's transport (`app/plugin/hostclock.h`):
  a Start at the song's beginning or a Song Position Pointer and a Continue elsewhere, $F8 at 24
  per beat on the frame where each falls, and a Stop when the transport stops; a loop's jump just
  carries on from the new place. It goes in with the track's MIDI in frame order, and the track's
  own clock bytes are left out while the host gives a transport, so the G1 never gets two clocks.
  With the clock set to internal the G1 ignores it, as the hardware does. Checked:
  `g1hostclocktest` (new: ticks on their frames in blocks of 1 frame to 2048, Start, Song Position,
  Continue, Stop, loops); `g1vst3check --clock` (new) uploads `ClockTest.pch` over the PC Port and
  measures MIDIGlobal's pulses at 48.00 Hz with the host at 120 BPM, 36.00 Hz at 90 and none when
  stopped, where the build before gave none at all; all tests pass.

- [New] **Mono outputs in the plugin (Claude, issue #27, asked by Waltercalling).** Besides Out 1/2 and
  Out 3/4, the VST3 and the CLAP now have Out 1, Out 2, Out 3 and Out 4 as mono buses, off until
  the host turns them on (Cubase and Nuendo: Activate Outputs), so each of the G1's four outputs
  can go to its own mixer channel. A channel on both a pair and its mono bus gets the same signal
  on both. Projects saved before keep their two stereo buses. Checked: `g1vst3check` turns every
  bus on and finds each mono output identical, sample for sample, to its stereo channel (a patch
  playing at −18.9 dBFS); all tests pass; Javier saw the six outputs in Bitwig, as CLAP and as VST3
  (`docs/images/mono-outputs-bitwig-*.png`). Not checked in Cubase itself, nor with clap-validator.
- [Imp] **The mode keys do not latch (Claude, requested by Mike Fiction).** A right click no longer
  holds Store, System, Edit or Patch/Load down: the OS acts on the press, held they change
  nothing, and the manual never holds them. Checked on Windows with `g1patchtest`: each held
  through the other keys ends on the same displays as pressed once.

- [Fix] **Shift stays held for the navigator in Edit mode (Claude, reported by Mike Fiction).** In
  Edit mode, Shift with the navigator walks from module to module, as many steps as wanted with
  Shift held, but the panel let a latched or keyboard Shift go after the first navigator key. In
  Edit mode it now stays down for them; elsewhere Shift still counts for the next key only.
  Checked on Windows with `g1patchtest` (Shift held + Right, three times: three modules on).

- [Imp] **Tooltips wait the same each time, and can be turned off (Claude, requested by Mike
  Fiction).** A tooltip came after half a second the first time, then at once over the next
  controls (JUCE's way); now each waits for the mouse to rest half a second on its control. The
  extras have a Tooltips switch, on by default. The standalone keeps it in its settings file
  (`tooltips =`); the plugin starts with it on each time, since keeping it there is a change to
  the plugin's state. Checked on Windows: the standalone and the VST3 build.
- [Imp] **The info display says what the rotary dial will do (Claude, requested by Mike
  Fiction).** Over the dial it reads "Rotary Dial - " and what a turn does now: Select Patch
  (the patch display and `Load?`), Voices (with Shift on the patch display), Location (`Store?`),
  Change Value (Edit mode and the System settings), Morph Value (the morph page) or Morph Range
  (Shift + Assign held in Edit mode, on a parameter that has a morph); blank where it does
  nothing: the System menu's top page, CTRL SNAP SHOT (sent with Right, where the info display
  reads "Nav Right - Send Snapshot"), and Shift + Assign on a parameter with no morph. Every
  System page was tried for what the dial and Right do (`NOTES.md`, "The System pages").
  Checked on Windows with `g1patchtest` (each case on the G1's display), and the standalone and the
  VST3 build.

- [Imp] **Assign/Morph says when it works (Claude, requested by Mike Fiction).** It only works in
  Edit mode, held: with Assign held a knob turned is assigned the parameter in focus, and with
  Shift + Assign held the dial sets its morph (`NOTES.md`, which had it as not found yet). The info
  display now names it only in Edit mode and is blank over it elsewhere; its tooltip says how to
  use it. Checked on Windows with `g1patchtest` (an assignment made in Edit mode and none in Patch
  mode; the morph end value changed with the dial), and the standalone and the VST3 build.

- [Imp] **The info display says what the navigator will do (Claude, requested by Mike Fiction).**
  It names the keys "Nav Up", "Nav Down", "Nav Left" and "Nav Right" instead of "Navigator ...";
  with Shift held on a module's page in Edit mode it adds " - Next Module", and with Shift held on
  the morph groups, where the navigator then does nothing, it stays blank. Checked on Windows: the
  standalone and the VST3 build.

- [Imp] **Tooltips for Shift on the navigator and Store (Claude, requested by Mike Fiction).** In
  Edit mode on a module's page (P or C in the G1's display), the navigator keys' tooltip adds "Hold
  Shift: next module" under "Hold: repeats"; on the morph groups, where Shift does nothing to them,
  and in the other modes it stays as it was. Store's tooltip follows the mode, as the OS does
  (`NOTES.md`, "Store"): "Store Patch" and "Hold Shift: Save Synth. Settings" in Patch mode, only
  the second in System mode, and "Use in Patch or System Mode" in Edit mode, where it does
  nothing. Checked on Windows with `g1patchtest` (Store and Shift + Store in each mode), and the
  standalone and the VST3 build.

- [Fix] **A project saved under one OS opens under another with its banks (Claude, issue #25,
  reported by Garrincha568).** After switching to Clavia's 3.03b update (`os =`), a project saved
  with the factory OS came back with an empty G1: the state carries a hash of the factory flash,
  and since that flash is blank outside the OS, the hash only names the OS, so a new OS refused
  every older project. `Engine::setUserState` now takes such a state: its banks and synth settings
  go in and the OS stays the one in use, as `loadFlash` already does with the standalone's
  `flash.bin`. The plugin says so where it tells where its banks came from. Checked:
  `g1flashkeeptest` restores a state from the ROM's OS under the 3.03b image and back, with no
  sector erased and the patch storage identical; all tests pass; `g1vst3check` passes with 3.03b.

## 2026-10-06

- [New] **Exclude a knob from Random (Claude, requested by Javier).** A right click on any of the
  18 knobs opens a menu with "Exclude from Random" (ticked when it is) and "Include all knobs in
  Random". Shift + Patch/Load then leaves those knobs where they are: an output level, say, which
  many patches put on a knob. An excluded knob carries a small padlock at its lower right, and every
  knob's hover tooltip says what a right click does. The window keeps the list in `settings.conf`,
  the plugin in the project and in `plugin.conf` (`randomExclude = 1 7 18`, the knobs' numbers).
  Checked: it builds, the `g1` tests pass, and Javier tried it in the window.
- [Imp] **The notice in Settings folds away (Claude, requested by Javier).** Its text is hidden
  until "Read the notice" opens it, and the window is shorter while it is folded; the startup switch
  stays where it was ("Show the notice at startup"). Checked: it builds.
- [Change] **Original artwork for the knobs and buttons, and a license for the skin art (Mike
  Fiction).** The small knob, the black buttons (wide, tall, tilted) and the red held buttons are
  redrawn from Mike's own photographs of real hardware, replacing the few pictures that were based
  on other skins, so every image in `app/gui/skin` is now Mike's original work. The new
  `app/gui/skin/LICENSE.md` publishes that artwork under CC BY 4.0 (credit: Mike Fiction); the code
  stays GPLv3. Same file names, frame sizes and knob travel (−120° to +120° over 128 frames), so no
  code changes. Checked on Windows: everything builds, the nine `g1` tests pass, and the panel shows
  the new art in the standalone and the VST3.
- [New] **An icon for the standalone (Claude, artwork by Mike Fiction).** `G1-Emu.exe` shows Mike's
  icon (`app/gui/icon.png`, 256 x 256) in Explorer, on the taskbar and in its title bar, where it had
  Windows' blank one; JUCE makes the sizes Windows wants from it (`ICON_BIG`). Only the standalone:
  the file is outside `app/gui/skin`, so the plugin does not carry it. The artwork is Mike's, under
  CC BY 4.0 like the skin's. Checked on Windows: it builds and the icon is the one in the exe.
- [Fix] **A knob on a morph group shows its value (Claude, reported by Mike Fiction).** The knob
  display and the tooltips' display showed only the group's name; they now show its value, 0 to
  127, as the G1's display does in Edit mode. The OS keeps the four values in the slot's block
  (`NOTES.md`, "The knob assignments"). The knobs themselves still keep to their own position on a
  morph group, with Knob Follows Patch on or not. Checked on Windows with `g1patchtest`: the four
  values read are the patch's, they follow a morph knob as it turns, and the G1's display shows the
  same; the standalone and the VST3 build.

- [Fix] **ADSR and Mod-Env sustain read 0 to 64, as on the G1 (Claude, reported by Mike Fiction).**
  The knob displays showed the raw 0 to 127; the G1's display shows half that, in steps of 0.5,
  with the top value as 64. NME gives these two no reading of their own, so they now use the one
  Multi-Env's levels already had. Checked against the G1's display in Edit mode and with
  `g1formattest`.
- [Change] **Synth Settings as a page of the panel, in Mike Fiction's artwork (Claude, requested
  by Mike Fiction).** Two buttons under the tooltips' display, Main and Synth Settings, each with
  its LED, switch between the synth and its settings, as on Mike's Nord Lead 2x skin; the extras'
  Synth Settings button and the page's Close are gone, and the page fades in and out quickly. The
  settings sit in Mike's frame over the knobs' four sections (`skin/settings_panel.png`,
  with its title, headings and labels) and leave the rest of the panel working. The boxes are
  drawn as his Perf / Global page's, with their hover colour. Master tune is a knob in his disk
  (`skin/master_tune_bg.png`); the clock's rate and the velocity scale are number boxes, dragged,
  scrolled or typed into, and only take values in range. Global sync reads in quarter notes. Each
  setting shows its name and value on the tooltips' display, and the page asks the G1 again every
  second while open, so a change from an editor or the System menu shows. Local and Keyboard mode
  are left out: they do nothing on the rack (`NOTES.md`). The background gains the two buttons'
  labels. Only `app/gui` and the skin list change, no plugin or `EmuHost` code; the plugin shows it
  as well, since it draws the same panel. The halving image scaler is now in `SynthSettingsView.cpp`
  as well as in `Panel.cpp`'s `Sprite`. Checked on Windows: everything builds,
  the eight `g1` tests pass, and the page works in the standalone and in the VST3 in Bitwig. Not
  built on Linux.

- [Imp] **Mike Fiction's panel skin merged with the direct link (Claude, requested by Javier).**
  Mike's `panel-skin` branch and the direct link (#8) both added a PC Port talker beside the editor's
  (the synth settings and the link): the runner and `EmuHost` now take both, and an editor's bytes
  over the link reach the synth settings too, as the MIDI PC Port's do. `SynthSettings::tied()` is
  declared before the operators that use it, which GCC needs (MSVC took it as it was). Checked on
  Linux: everything builds, the nine `g1` tests pass, and `g1vst3check` fails on two points that
  fail on `main` as well (an instance silent after its note, the restored instance silent).
  Approved by Javier; the loadable skins that come next are #28.

- [Change] **Shift + Patch/Load is Random, instead of a button in the extras (Claude, requested by
  Mike Fiction).** It turns the 18 knobs to random positions, as the extras' Random did; that button
  is commented out for now (`Panel.cpp`), with its double click that put the patch's values back,
  and Synth Settings takes its place in the extras. The OS has nothing of its own for Shift +
  Patch/Load (it asks `Load?` as without Shift), so the window keeps that press and the G1 does not
  see it. The OS ignores the knobs while Shift is down, so the G1 sees Shift let go while they move
  and pressed again 300 ms later; Shift stays held, so Random can be pressed again and again. The
  tooltips' display reads "Random Knobs" while Shift is down and flashes it. With Knob Follows Patch
  on, a knob the window has just turned (Random, or by hand) keeps its own position for half a
  second instead of jumping back to the patch's old value until the OS has read it. Checked: it
  builds.

- [Imp] **Cleanup of the panel, the synth settings and the plugin work (Claude, requested by Mike
  Fiction).** No change in behaviour; long functions split and repeats folded together: the panel's
  timer, the tooltips' display, the icons, the synth settings' reply handling, the runner's two PC
  Port talkers, EmuHost's start and restart, the plugin's preferences in its state, and the editor's
  size limits (now `PanelView::applyLimits` for both). `g1vst3check` puts the user's `plugin.conf`
  back however it ends; its main run used to leave the size of its own editor there. **Known
  duplicate:** the PC Port SysEx helpers are now in `app/pcsysex.h` for the synth settings, but
  `app/slotkeeper.cpp` keeps its own identical copies (`frame`, `pack7`, `unpack7`,
  `forEachMessage`...), left as the author wrote them. Checked: everything builds, and
  `g1formattest`, `g1synthsettingstest`, `g1slotkeepertest`, `g1runnertest` and `g1vst3check` (with
  and without `--window`) pass.

- [Imp] **Knob values read as the editor shows them (Claude, requested by Mike Fiction).** The knob
  displays, the tooltips' display and the plugin's knob parameters show "Sine", "C#4", "1.25 kHz"
  and so on instead of the OS's raw number (a DAW also takes the text typed in). The readings are
  Animatek NME's value formatters, ported to `g1Lib/g1format.*` with the table of which parameter
  reads which way generated from NME's `data/modules.xml` (Nomad's module descriptions, GPL). The
  text is the editor's, so in places it can differ from the G1's own display. Checked with the new
  `g1formattest` and `g1vst3check`.

- [Fix] **Shift lets go after the next key, as on the hardware (Claude, reported by Mike Fiction).**
  The OS takes Shift for one key only: held on in the window (latched with a right click, or the
  keyboard's), the next shifted key did nothing (Shift, System, then Save Synth. Settings), and
  Shift + Store went on to `Store?`. Now a latched Shift lets go once the key it was used with is
  released, and the keyboard's Shift must be pressed again. After a slot button (A-D) Shift stays
  down, so several slots can be picked in a row, and the OS sees it pressed again for the next key.
  `g1patchtest`'s `G1_PRESS` steps can hold and let go a button (`h2.7`, `u2.7`). Checked with
  `g1patchtest` (NOTES.md, "Shift counts for the next key only") and it builds.

- [New] **The master volume, 0-127 and in the DAW (Claude, requested by Mike Fiction).** The panel's
  display shows it as the OS takes it, the knob's position halved (0-127). The plugin has it as a
  "Master Volume" parameter, so a DAW can automate it, and the knob on the panel follows. A new
  instance's project saved before it ever ran keeps it too. Checked with `g1vst3check`: the host
  sets it to 64, and it stays there and comes back in a project reopened.

- [New] **A display for the synth's controls (Claude, artwork by Mike Fiction).** The background has
  a one-line display at the bottom centre. Hovering a control of the synth's own (knob, button,
  dial, volume) names it there, in the knob displays' dot font, a little larger. A knob shows what
  it moves at the left and its value at the right, and the value keeps updating while the knob
  turns. Text too long for the display scrolls through. While Shift is down (mouse, latched or the
  keyboard's), Find, Store and Assign show their second function instead (Panic, Save Synth.
  Settings, Morph), and pressing Find or Store with Shift flashes that name twice. Store follows the
  G1's mode, read from its mode LEDs: "Store Patch" in Patch/Load mode, "Save Synth. Settings" with
  Shift in Patch/Load or System, and blank where the OS ignores it (Edit, or System without Shift).
  With Shift down, an assigned knob reads "Clear Knob" in Edit mode, which turning it then does (the
  manual, Assign/Morph), and the knobs read nothing in Patch/Load and System, where the OS ignores
  them. Hints such as "Right click: hold it down" and the tooltips of the status bar and the extras
  stay in the floating box. New artwork for the knob displays too. Checked: it builds.

- **The greeting also names the PC Port (#8; Claude).** JUCE gives its virtual ports no identifier
  on Linux, so the plugin's greeting carried `pcport=,`: empty ids are left out now, and a
  `pcname=<name>` field (`MidiTransport::portListName`) lets an editor match the port by name. The
  plugin as built and installed for Bitwig on 2026-10-06 listens and greets ("G1-Emu plugin 1").
  Its IAm over the link was not answered within 3 s under `g1vst3check`, which renders offline
  (the emulator only advances while the host pulls audio): to be confirmed in a DAW. Also seen:
  `g1vst3check` FAILS on three points (an instance silent after its note, the restored instance
  silent, B answering A's greeting) **with the 2026-10-04 build too**, so they are not this change;
  not looked into yet.

- **The direct link says which MIDI PC Port leads to the same G1 (#8; Claude).** The greeting is
  now `G1-Emu 1 <name>\tpcport=<id>,<id>`, with the ids other programs see for this instance's PC
  Port (`MidiTransport::portIds`: "client-port" on ALSA, the JUCE device identifiers elsewhere), so
  an editor already on that MIDI port does not connect the same G1 a second time over the link. An
  editor that only knows the old greeting still reads the name (it ends at the tab). Checked
  against `g1run`: `G1-Emu 1 G1-Emu\tpcport=130-0` for ALSA client 130, the id NME lists for it.
  `Correspondencia/` (private notes) is ignored.

- **The direct link in the VST3/CLAP plugin (#8, #7; Claude, requested by Javier).** Each plugin
  instance listens like the standalone (`G1-Emu plugin <n>`, the first free port from 47310) and
  its runner polls the link beside the PC Port's MIDI port, feeding the SlotKeeper the same way.
  It needs no MIDI endpoint, so it is there on Windows, where JUCE can make no virtual port
  (Windows MIDI Services, microsoft/MIDI#1047), and in a CLAP instance created after JUCE's MIDI
  shut down. The editor's info text says where the link is. `WINDOWS.md` tells Windows users that
  NME 0.21 needs no loopMIDI. Checked: builds, `g1runnertest` and `g1directlinktest` pass; **not
  yet tried in a DAW**, nor on Windows.

- **Direct link: an editor that knocks and leaves no longer locks out the next one (#8; Claude).**
  The link read its editor only after looking for new connections, so an editor that checked who
  was there and closed at once (what NME does to list the instances) was still counted when the
  real one arrived, and the real one was turned away. The current editor is read first now.
  Found end to end with NME's client against `g1run`; `g1directlinktest` gains the knock-then-
  connect case. Checked: the test, and NME's IAm answered over the link right after a discovery.

## 2026-10-05

- [Imp] **A held panel button lets go on a click (Claude, requested by Mike Fiction).** A button
  latched with a right click (Shift, say) is released by a left click too, which presses nothing,
  and it shows no hover highlight while held. Checked: it builds.

- [New] **A Restart button, in the window and the plugin (Claude, requested by Mike Fiction).** A
  power icon at the right end of the extras, after Synth Settings (now at the right too), switches
  the emulated G1 off and on after asking in a card over the panel in the Synth Settings' look
  (`app/gui/Overlay.*`, which both now share), so a G1 that hangs comes back without closing G1-Emu.
  In the window the flash is saved and a new G1 boots from it with the knobs where they were; the
  MIDI ports and the sound card stay open, so the editor keeps its connection, and a restart after
  an OS update boots the OS that came in (`EmuHost::restart`). In the plugin its state as it is goes
  back in as a project's would, so the new G1 boots with the same banks, slots, knobs and programs
  (`Processor::restart`). Checked: it builds, and `g1vst3check` passes.

- [New] **Synth Settings from the panel, a test version (Claude, requested by Mike Fiction).** A
  button in the extras opens an overlay over the synth with the slots' MIDI channels and the global
  settings (clock, global sync, master tune, knob mode, pedal, program change, local, velocity
  scale, name), as dropdowns. It reads and writes them through the PC Port with the same message
  NME's Synth Settings dialog sends (`app/synthsettings.h`), between the editor's messages, in the
  window and the plugin. **It is there to test the settings, not the final look or version:** the
  artwork is still to come. Checked with `g1synthsettingstest` (the OS takes what is written and
  reads it back the same) and in the window.

- [Imp] **The extras drawer slides open and closed (Claude, requested by Mike Fiction)** instead of
  appearing at once. Checked in the window.

- [Change] **The panel drawn from PNG artwork (Claude, artwork by Mike Fiction).** `app/gui/skin/`
  holds a 3000 x 1238 background with the faceplate, every label, the knobs' red rings and the
  displays' frames, and sprites for the knob caps and their shadows, the dial, the wide, tall and
  tilted buttons, the LEDs and the parameter displays, built into the window and the plugin
  (`g1Skin`). `Panel` lays them out in the background's pixels, each sprite at its exact place (the
  knobs measured ring by ring), and only a button's body takes the mouse. The big display's glass,
  painted by the code, follows the art; what is around the skin takes its dark purple
  (`Panel::FaceColour`). Checked against the artist's reference images pixel by pixel and in the
  window.

- [New] **The window and the plugin's editor are resizable (Claude, requested by Mike Fiction).**
  `PanelView` scales the panel, still laid out at 1200 pixels, from 50% to 250% in its proportions,
  which change with the extras drawer; they start at 1500 pixels wide. The standalone keeps its size
  in the settings file, the plugin in the project. Checked on Windows at several sizes and across
  restarts.

- [Fix] **The plugin remembers its window (Claude, reported by Mike Fiction).** A change to the
  editor's size or the extras' switches now marks the project as changed (`setDirty`), so hosts save
  it, and a new instance starts as the last editor was left (`plugin.conf`, beside the settings
  file; a project's own state still wins; listed in `CLAUDE.md`). `g1vst3check --window` checks
  both; confirmed by Mike Fiction in Cubase, Bitwig and Reaper.

- [New] **What the knobs move (Claude, requested by Mike Fiction).** The parameter displays (extras:
  "Parameter Displays") take the knobs' LEDs' place, write in the big display's dot font and go dark
  on a knob with nothing assigned. **Knob Follows Patch** (extras) shows each knob where the patch's
  value puts it, and turning it starts there. A knob's tooltip says what it moves and its value, so
  the displays can stay off. Checked with patches loaded from NME.

- [Imp] **The dial turns, with a thumb indent (Claude, artwork by Mike Fiction).** The dial's sheets
  could not show it turning (their frames were alike); it is now turned in code, the ridges a step
  at a time within one ridge so their light stays put, and Mike Fiction's painted thumb indent, cut
  out of the knob, slides round its face. 48 detents a turn; it starts with the indent at eight
  o'clock. Checked by putting the pieces back together (the painted knob) and in the window.

- [Imp] **The panel's buttons (Claude, requested by Mike Fiction).** A button held without the mouse
  (right click, or a key) pulses with Mike Fiction's red picture of it, all held buttons together;
  the navigator's keys repeat when held instead, and they and Panel Split no longer latch. The
  navigator, Shift, Assign/Morph, Panel Split and Find are centred on the art's labels. Tooltips sit
  on a dark glass that blurs the panel behind. Seen in the window.

- [Imp] **The standalone's window (Claude, requested by Mike Fiction).** Its title bar follows
  Windows' dark mode (`NativeTitleBarTheme`), it has the plugin's resize grip in its corner, and it
  remembers the master volume (`masterVolume` in the settings file). Checked in the window.

- [Imp] **What the panel's keys do on the rack (Claude, reported by Mike Fiction).** `NOTES.md`
  records that Shift + Find (Panic) works in the emulator, and that holding several slot buttons
  does not layer them for MIDI on the rack: slots are layered by giving them the same MIDI channel.
  Documentation only.

- **The direct link for Animatek NME, beside the PC Port (#8; Claude, requested by Javier).** The
  standalone listens on a local TCP socket (127.0.0.1, the first free port from 47310, so instance
  n is at 47310 + n) and carries the PC Port's raw bytes both ways, with no MIDI port, driver or
  loopMIDI in between. The MIDI PC Port stays as it was: the original editor and other tools keep
  using it. An editor that connects first reads one line, `G1-Emu 1 <name>\n`; one editor at a
  time. `directLink = 0` in the settings (or `G1_DIRECT_LINK=0`) turns it off. `app/directlink.*`,
  in `g1Core` so the plugin can use it next; plain sockets, no JUCE, no thread of its own (polled
  from the loop that moves the PC Port). Checked: `g1directlinktest` (two instances take
  consecutive ports, hello, bytes both ways, a second editor turned away, the link freed when the
  editor leaves), and end to end against the emulated G1 with `g1run`: NME's IAm over the link is
  answered with the G1's IAm (OS 3.03), twice in a row with a restart in between (the port is not
  lost to TIME_WAIT). Not in the VST3/CLAP yet, and NME's side is next.

## 2026-10-04

- **`v0.1.0-alpha.12` release notes (Claude, requested by Javier).** `docs/release-notes.md`
  presents #25 (slots in the DAW project), #17 (JIT failure), #22 (the internal clock's tempo,
  with the warning that clocked patches now play slower), the optional real OS with Clavia's
  updater and Wine, and the known issues. Documentation only.
- **Install the real OS with Clavia's own updater: `G1_UPDATE=1` (Claude, requested by Javier).**
  The standalone starts in the boot ROM's update mode (the OS length in the flash blanked, as on a
  G1 with no OS: the banks stay) and, when it closes, keeps an OS that came in as
  `os/received-<date>.bin` and sets it as `os =`. Checked by Javier on Linux with Wine: the 3.03b
  updater sent the OS to the PC Port (`Update completed` on the G1's display), and the OS kept is
  byte for byte the one taken from the same updater by other means; the banks were intact. The
  loader copies one long word more than the updater's length, which is now left out. README:
  "Optional: the OS a real G1 runs". Not reproduced since: two crashes of the window in the DSP JIT
  right after that first update (the console ran the same state 11 times, the window 3, cleanly).
- **A kept flash is no longer formatted at every start (Claude, reported by Javier).** The `os =`
  change made `Engine::loadFlash` put the whole OS area back from the factory flash, which also
  erased the OS's own "formatted" mark at +0 (`$0000000C`): every start showed INIT FLASH and
  formatted the patch storage, banks included. Now only the OS's length and image are replaced.
  Javier's banks were restored from the backup taken before the change. New `g1flashkeeptest`
  (ctest): a flash kept after a first boot comes back with nothing erased and the mark in place,
  on the ROM's OS and on the update's; `ctest` passes on both.
- **Run the OS a real G1 runs, not the ROM's factory one (Claude, reported by Javier).** On the
  real G1, Shift+Store keeps each slot's patch and the synth comes back with them at power-on; in
  the emulator every slot came back as "Empty patch". The emulator was running the factory OS kept
  in the boot ROM, which never loads the stored synth settings at boot; Clavia's 3.03b update,
  which the hardware runs from its flash, does. New `os =` setting (and `G1_OS`): an OS image
  to run instead of the ROM's, used by the window, the console and the plugin, kept out of the
  plugin's project state like the factory OS. `g1Lib/g1knobs.h` finds its tables on either OS
  ($20 higher on the update). Checked: with the update's OS, Shift+Store with patch 108 in slot A
  and a restart come back with 108, as Javier's G1 does with 207; SimpleOSC sounds the same; the
  knob names of a bank patch read the same on both OSes; `ctest` passes on both (`G1_OS`). Details
  in `NOTES.md`, "The official OS update". Getting the image is up to the user; without one,
  nothing changes.

## 2026-10-03

- **A JIT that cannot generate a block no longer takes the host down (Claude, requested by
  Javier; #17).** When the DSP JIT ran out of memory or failed to generate code, the core
  re-entered its "create" stub with no end, a stack overflow that crashes the DAW, more likely with
  several plugin instances. The core overlay now returns and records the failure; the DSP empties
  its JIT cache and goes on, and if the JIT keeps failing that DSP is given up: silence on time, no
  hang, and the status bar shows it (`x` and the reason). `G1_JIT_FAIL_AT` / `G1_JIT_FAIL_FROM`
  simulate it. Checked with the new `g1jitfailtest` (ctest) through the plugin's runner: one
  failure recovers and SimpleOSC keeps sounding (−25.8 dBFS); a JIT that always fails gives up
  all four DSPs and renders 8 s of silence in 1.5 s. `ctest` passes and the nine bench patches
  render byte for byte as before. Details in `NOTES.md`, "When the JIT cannot generate a block".
- **The internal master clock runs at the synth's tempo (Claude, requested by Javier; #22).**
  MIDIGlobal's clock, and every sequencer or divider it drives, ran 4 times too fast with the
  internal clock (192 Hz at 120 BPM instead of 48), while MIDI clock was right. Two faults in
  Gearmulator's 68331 timer, which the OS times the clock with: it ignored the prescaler the OS
  selects (/32; it always counted /4) and an output compare matched for as long as the counter was
  past it, so the OS's handler got a second interrupt at once. Both fixed in a build copy of
  `gpt.cpp` (`cmake/Mc68k.cmake`, the clone untouched). Checked with the new
  `tools/patches/ClockTest.pch` (MIDIGlobal into a Clock Divider at 6): 48.00 Hz and 8.00 Hz with
  the internal clock, the same as with MIDI clock and as the manual's 24 pulses per beat, and the
  OS counts 2 beats a second at 120 BPM. `ctest` passes; of the bench patches only `progger`
  (clocked) renders differently, the rest byte for byte as before (the references for DungeonDub
  and 4VoiceChoir were already out of date and were recorded again). Details in `NOTES.md`, "The
  master clock".
- **The plugin saves what each slot holds and brings it back with the project (Claude, requested
  by Javier; #25).** Until now a project kept the banks, the knobs and the last Program Change:
  a patch sent by an editor, loaded from the panel, or edited since was lost on reopening. The
  new `SlotKeeper` (`app/slotkeeper.*`) reads each slot from the OS over the PC Port the way an
  editor does, whenever the slot may have changed and the port is quiet, hiding its own traffic
  from a connected editor, and saves the slots in the project (`<Slots>`); on reopening it uploads
  them once the G1 has booted. Findings in `NOTES.md`, "Reading a slot back". Checked:
  `G1_FETCHCHECK` in `g1patchtest` reads, re-uploads and re-reads the 97 patches of the
  maintainer's banks, all accepted and identical; the new `g1slotkeepertest` (ctest) restores a
  patch, reads it back with a fresh keeper, and follows an editor's upload into another slot,
  with the factory flash and with a real `flash.bin`; `g1vst3check plugin.vst3 --slots` sends a
  patch to a VST3 instance through its PC Port, saves the project and reopens it in a new
  instance with no editor, which plays it at the same level (−25.8 dBFS); the usual
  `g1vst3check` run and `ctest` pass.
- **What the official OS updates hold, rack and Micro Modular (Claude, requested by Javier).**
  `NOTES.md`, "The official OS update": the rack's is the same OS 3.03 as the ROM's, linked `$20`
  higher in most places (~94% identical with addresses masked), so `g1Lib/g1knobs.h`, which reads
  the OS by address, will need its addresses per build for #16; the Micro's sets one DSP, boots
  only DSP 0 and drives the codec from it with another ESSI clock. The Micro entry in
  `ROADMAP.md` is updated. Checked by disassembling both images against the ROM. Documentation
  only.
- **`tools/filtersweep/frun.cpp` follows FILTERtek going stereo (Claude, requested by Javier).**
  It drives the left side (`IN_L`/`OUT_L`); with DRIVE at 0 the comparison against the emulator
  gives the same figures as before, case by case.
- **`tools/filtersweep`: FilterE measured end to end (Claude, requested by Javier).** Every
  cutoff and resonance step, both slopes, the four types, the gain control, the modulation inputs
  and the saturation, turned into the tables of FILTERtek in the Animatek VCV plugin. It is a
  Chamberlin state-variable filter whose damping follows the cutoff, 24 dB as two identical
  sections, band reject with a gentler damping of its own, gain control at the input (12 dB) or
  between the sections (24 dB), and every internal value saturating at full scale; findings and
  scripts in its README. `fcompare.py` runs the module against the emulator: 33-55 dB below the
  signal in nine of ten cases, and the tenth (24 dB at resonance 124 driven hard) matches in
  spectrum to half a dB in the median.
- **`g1patchtest --input-raw file.f32` (Claude, requested by Javier).** Feeds mono float32
  samples into both audio inputs from the first measured sample on, so impulses, noise and sweeps
  can go through a patch and input and output line up (33 samples from AudioIn to an output).
  Used to measure FilterE. It shows that **the audio input path runs at 48 kHz**: only even
  samples get in (an impulse on an odd sample vanishes, 40 kHz comes out as 8 kHz) and each one is
  held for two output frames, while an OscA comes out at the full 96 kHz. **That is a bug in the
  emulator**: the real G1 runs its inputs at 96 kHz (Javier), so it is in how DSP 0's codec
  receivers are clocked (NOTES.md, "Inputs"), not yet fixed. `tools/filtersweep` holds the FilterE
  measurements so far (a two-pole state-variable filter, 24 dB as two in cascade; see its README).
- **`tools/envsweep`: the G1's envelopes measured for a VCV Rack module (Claude, requested by
  Javier).** Records every step of the ADSR's attack (three shapes), decay and release, four
  envelopes per run, plus the AD envelope, retriggers and short gates, and turns them into the
  tables of ADSRtek in the Animatek VCV plugin, which is modelled on these recordings and not on
  the DSP code. Findings in `tools/envsweep/README.md`: times follow the editor's table to half a
  percent in the median and run long at the top; decay and release are one exponential timed to
  1%; Log and Exp attacks are level-driven, which is why a retrigger carries on from where it
  is; everything moves at 24 kHz. `compare.py` runs the emulator and the module side by side:
  0.06-1.1% worst error over eight ADSR cases and four AD ones, except one 0.46 ms attack that is
  off by one tick. To support it, **`g1patchtest` takes `--note-off-at S` and `--events
  +S,-S,...`**, which press and release the note at those times during the measurement (the
  first `--note-at` keeps working: it is now the first event). Checked by recording the same ADSR
  both ways: the curves agree to 0.0004, the grid of one 24 kHz tick.

## 2026-10-02

- **`v0.1.0-alpha.11` release notes (Claude, requested by Javier).** `docs/release-notes.md` now
  presents the VST3/CLAP plugin (with the PC Port for NME on Linux and macOS, the automatable knobs,
  where to install it on each system and how to unblock it on macOS), what else changed since
  alpha.10 (#6, keyboard Shift and slots, the extras drawer, the icons) and the known issues (#3,
  no PC Port in the Windows plugin, the CLAP PC Port caveat). Named alpha.11 and not beta 1: the
  plugin has not been tried in a DAW on macOS or Windows yet, and #3 is still open.
- **Each plugin instance's PC Port has a name of its own, also across processes (Claude, reported
  by Javier).** In Bitwig two instances both showed up in NME as "G1-Emu PC Port": Bitwig can host
  each instance in a process of its own, and the numbering only counted the instances of one
  process. A number is now held with a file lock every process sees (`juce::InterProcessLock`,
  let go by the system if the process dies), kept for the instance's life; JUCE's list of MIDI
  ports is checked too, but cannot be the only check, because another process's new port reaches
  it late. Checked: three `g1vst3check` processes started together gave six distinct ports
  ("G1-Emu PC Port" to "G1-Emu 6 PC Port"), where before the lock two names came out twice;
  `g1vst3check` alone, `clap-validator` (34 passed, 0 failed) and ctest as before.
- **The plugin's knobs are automatable, it takes Program Changes in VST3, and it is also a CLAP
  (Claude, requested by Javier; #14, #7).**
  - **The 18 knobs as parameters**, named after what each one moves in the patch (`Knob 3: OscA
    Freq coars`, from the OS's tables) and showing the OS's value. The host's changes reach the G1
    in `processBlock`; turning a knob on the panel (or Random) goes to the host from a timer, as a
    gesture; a new engine sets them from its state. The range is the knob's 254 positions. The
    panel's knobs now follow the G1's positions, so automation moves them on screen.
  - **Program Changes in VST3**, which carries none as MIDI: 128 programs, sent to the G1 as
    Program Changes on channel 1 and remembered in the project like one from the track.
  - **CLAP**: `g1plugin_CLAP` builds `G1-Emu.clap` with clap-juce-extensions (upstream, not
    Gearmulator's copy, which predates JUCE 8.0.11). `clap-validator` 0.4.1 found and we fixed:
    state saved before the plugin ever ran was empty (now the settings and knob positions, loaded
    back as a new instance; saved again as it came while the G1 has not run), knob text that did
    not read back, and a crash creating the PC Port after the last instance in a process had shut
    JUCE down (JUCE 8 never recreates its MIDI endpoints: that instance now has no PC Port and says
    so). The CI checks out clap-juce-extensions and packages `G1-Emu.vst3` and `G1-Emu.clap` on
    every system.
  - Checked: `clap-validator` 34 passed, 0 failed; `g1vst3check` all good, now also with the
    knobs (host sets knob 1 to 0.75, kept after playing, back in a reopened project) and the host's
    program reaching the project; ctest passes. Not yet tried in Bitwig.
- **The VST3 has a PC Port: editing an instance from Animatek NME inside a DAW (Claude, requested
  by Javier).** Each instance creates a virtual MIDI port,
  **G1-Emu PC Port** (`G1-Emu 2 PC Port` for the second live instance, a freed number is reused),
  through `JuceMidi` (`app/jucemidi.h`) in the processor, so it survives engine swaps and NME stays
  connected; the runner's worker polls it on every pass and sends what the G1 puts out on its PC
  Port, which until now was discarded. Linux and macOS; on Windows JUCE cannot create the port and
  the instance's Settings say so (the direct link of #8 is still the way there).
  `G1_PLUGIN_PC_PORT=0` leaves it out. `g1vst3check` now greets each instance's PC Port as NME
  does: with two instances playing and only A greeted, A answered IAm 5 times and B none; sound,
  state and editor as before, ctest passes. Not yet tried with NME in Bitwig.
- **Holding Shift and several slots at once (Claude, requested by Javier).** A mouse presses one button at a time, and the G1 needs two together (Shift + a key,
  A + B to select several slots). In the panel (window and plugin, `app/gui/Panel.*`): **Shift on
  the computer's keyboard holds the panel's Shift**, read from the system so it works with the
  mouse over the panel even without the focus, and stays held until released; **A-D hold the slot
  buttons**, as many as are pressed (with the panel focused, which a click on it gives); and a
  **right click (Ctrl+click on a Mac) latches any button** down until the next right click. A
  held button is drawn down and lit in amber. Checked: `g1gui` and `g1plugin_VST3` build, ctest
  passes; Javier confirmed the keyboard Shift works.
- **`g1patchtest --note -1` and `--note-at S` (Claude, requested by Javier).** `--note -1` plays no
  note, so what is measured is what the patch does by itself; `--note-at S` plays the note S
  seconds into the measurement, so one boot measures the patch alone and then with the note (it
  also works with `G1_MIDICLOCK`). Used by g1-taller to listen to the 29,639 readable patches of
  the community archive. Checked: a self-playing drone ("Suelo de bruma") sounds in both halves; a
  bass that needs a key (`LP-BP Bass02`) is silent before the note and sounds after it; a
  MIDI-clocked patch (`Iman-A02`) sounds the same with the note from the start and with
  `--note-at`.

## 2026-09-30

- **First beta of the VST3 instrument (Claude, requested by Javier; #7).** `G1-Emu.vst3` plays the emulated G1 from a DAW track: notes and controllers in,
  outputs 1/2 and 3/4 out as two stereo buses, In L/R as an optional input, and the same panel as
  the window for editor. It creates no MIDI port, no audio client and no virtual card.
  - **The engine is split out** (`app/engine.*`, library `g1Core` with `romfinder` and the new
    `hostconfig.*`): ROM in, factory OS in the flash, and the user state as the runs of bytes that
    differ from the factory flash, with a hash that refuses another ROM. The OS never goes into a
    project. `EmuHost` is built on it; `HostOptions`/`HostStats` moved out of it unchanged
    (`EmuHost::Options`/`Stats` remain as aliases). The panel takes a `PanelHost` instead of an
    `EmuHost`, and its knobs start where the G1's ADC is instead of forcing 0 (a reopened plugin
    editor would otherwise turn all 18). `disclaimerText()` moved to the panel and the missing-ROM
    message to `romfinder`, shared by the window and the plugin.
  - **Pacing** (`app/plugin/runner.*`): a worker keeps the emulator one host block + 5 ms ahead of
    what the host has taken; events are scheduled at a fixed latency (reported to the host) plus
    their offset, with a 0.5 ms guard so they are never behind the emulator. The audio callback
    only pulls from a lock-free ring (it waits only when rendering offline); an underrun is paid
    back by dropping the late frames, so timing stays locked. `AudioBridge` gains a pull mode.
  - **State in the project:** the flash difference (gzip + base64), the knob positions, the
    panel's preferences, and the last Bank Select/Program Change per channel, sent again at boot
    because the OS does not remember what each slot held. A new instance starts from a read-only
    copy of the standalone's flash. A state that cannot be applied is kept and handed back as it came.
  - **Tests:** `g1runnertest` (a ctest, skipped without a ROM) and `tools/vst3check`
    (`g1vst3check`, the built `.vst3` through JUCE's VST3 host).
  - Verified on Linux (Ryzen 7 5700X): `g1runnertest` renders 8 s offline with blocks of 256 and
    with random blocks of 1–512 **bit-identical**; a Program Change and note 60 give −19 dBFS, onset
    784 frames (the reported latency) + 53 frames (the G1's MIDI IN) after the note. In real time,
    blocks of 256: 0 dropouts over 12 s with the worker at 38 %; with a Program Change, ~0.3 s of
    dropouts while the patch loads, all over silence (the G1 is muted then). `g1vst3check`: two
    instances at once in one process, −18.9 and −18.3 dBFS with programs 1 and 2; A's state
    (357 KB) restored into a third instance plays its patch with no Program Change sent, same
    latency; the editor opens at 1200×440. `ctest -L g1` passes; `g1run` boots from the same flash
    and leaves it unchanged; `g1run`, `g1gui` and the plugin also build with `G1_BACKEND=juce`.
    Not yet checked inside Bitwig itself.

- **Parameter displays above the knobs, and double-click on Random (Claude, requested by Javier;
  #13).** `g1Lib/g1knobs.h` reads from the OS's own tables what each panel knob is assigned to: the
  module's name in the patch, the parameter's short name as the OS shows it in Edit mode (from the
  user's ROM, built into RAM at boot), its value and its range, following the active slot and
  Panel Split. The extras drawer gets a **Parameter displays** switch (remembered as
  `knobDisplays`) that puts a small LCD above each knob with module, value and parameter. Random
  now keeps the patch's values before its first use, and a **double click** turns each knob back
  to the exact position that gives that value (value = position × (max + 1) / 256, measured).
  `g1patchtest` gains `G1_KNOBINFO` and `G1_RAMDUMP`; the tables are in `NOTES.md`. Verified: the
  reader lists `WavetableSynth.pch`'s nine knobs as the `.pch` assigns them; Javier checked the
  displays on screen with a mixer patch, and that Random then a double click brings the original
  sound back.
- **Extras drawer with Random (Claude, requested by Javier; #12).** A chevron next to the icon
  buttons opens a drawer below the panel (the window grows 60 px; `extrasOpen` in `settings.conf`
  remembers it). Its first control, **Random**, turns the 18 knobs to random positions, as if by
  hand. Verified: builds; Javier checked the drawer on screen and that Random changes the sound.
  Next in the drawer: parameter displays (#13), and double-click on Random to go back to the
  patch's own values, which needs the same reading of the OS's knob assignments.
- **The window loses Oct Shift and gains Patreon, Report issue and Settings as icons (Claude,
  requested by Javier; #10, #11).** The Oct Shift buttons, their five LEDs and the label are gone:
  they belong to the keyboard model and the rack's OS does nothing with them. At the right of the
  status bar, three small icon buttons: Patreon (opens the Animatek page), Report issue (a new
  GitHub issue prefilled with the build's `git describe`, the system and CPU, the audio and MIDI
  in use, speed, load and dropouts) and Settings. The panel now has a tooltip window, so those
  three, the knobs and the dial show what they are. Verified: builds; Javier checked the panel,
  the icons and both links on screen.
- **The emulator no longer slows down with every patch load (Claude, requested by Javier; #6).**
  The DSP core marked program memory that the OS rewrote as "volatile" and never forgot it, so
  code the OS loaded over a previous patch ran one instruction per JIT block from then on. After
  30 uploads the same patch ran 25 % slower, and a heavy one (fast audio-rate modulation,
  `WavetableSynth.pch`) crackled until the emulator was restarted. `cmake/Dsp56300.cmake` now
  skips that mark (`G1_VOLATILE_P=1` restores it, to compare). Verified: after 30 uploads,
  3.42× real time against 2.40× with the old behaviour, the same as a fresh start (3.46×).
  `g1dspcheck` passes. The golden WAVs are byte-identical except `WavetableSynth`, whose S&H
  shifts with interrupt timing (same pitch and spectrum), so its reference was recorded again.
  Details in `NOTES.md`. Javier confirmed it by ear in `g1gui` (many heavy patches in a row, fast
  FM no longer sticking), and #6 is closed.
- **`g1patchtest` gains a real-time mode (Claude).** `G1_REALTIME=seconds` runs `EmuHost`'s pacing
  and `AudioBridge` against a simulated sound card and prints dropouts and lag, with
  `G1_RT_NOTES` and `G1_RT_KNOB` to load it. With it, and with `g1run` on JACK, the fast-modulation
  crackle did not reproduce on a fresh start, which is what pointed at state building up over
  the session.
- **Issues instead of memory (Claude, requested by Javier).** #4 closed: its reporter confirmed that
  heavy patches load clean, and `WavetableSynth.pch` is down to faint clicks. New issues: #6 (fast
  audio-rate modulation crackles and stays broken until a restart, the priority), #7 (VST3 and first
  beta), #8 (direct link with NME), #9 (several instances, original editor with four synths),
  #10–#14 (report button, remove Oct Shift, extras drawer with Random, parameter displays, mappable
  knobs), plus Animatek-NME#83 and #84 for the editor side. `ROADMAP.md` points at them.
  Documentation only.

## 2026-09-28

- **`v0.1.0-alpha.10` is out (Claude, requested by Javier).** Prerelease on GitHub with the five
  packages: Linux x86_64 native and JUCE, Linux arm64, macOS universal and Windows. Checked the CI run
  (all six jobs green) and the packages' contents; no ROM in any of them. #4's reporter was asked to try
  it; the issue stays open until they confirm.
- **Release notes for `v0.1.0-alpha.10` (Claude, requested by Javier).** `docs/release-notes.md`
  describes today's speed and CPU work in plain terms and lists alpha.5 to alpha.9 as included.
  Documentation only.
- **The DSP threads sleep while the real-time loop does (Claude, requested by Javier).** Ahead of
  the clock, `EmuHost` sleeps 500 µs; the DSP threads kept spinning through it (20,000 pauses before
  sleeping, and a new job every ~49 µs), so the emulator used ~3.2 cores whatever the patch.
  `Microcontroller::setIdle` now tells them the front end is sleeping, and they sleep straight away;
  during a burst of emulation they still spin. Verified with `g1run` in real time: 3.2 → 1.7 cores,
  speed 100 %, emulation thread 43 % → 34 % busy; the bench, which never idles, is unchanged.
- **The DSPs skip whole iterations of the OS's idle loop (Claude, requested by Javier).** With a
  light patch a DSP spends ~90 % of its instructions polling in `$16C`–`$172`, one JIT dispatch per
  short block. Once two iterations leave every register as it was and cost the same, `skipIdle`
  moves the DSP on by as many whole iterations as fit before the next point where anything could
  differ (the target, the next sample clock, the next peripheral deadline, and the next multiple of
  1024 cycles, where `drainAudio` anchors a new CRA; crossing that one was what first made three
  golden WAVs differ). A temporary checker ran the loop instead of skipping and compared: every one
  of hundreds of thousands of predictions per DSP landed on the exact registers, instructions and
  cycles. `G1_NO_IDLE_SKIP` turns it off. Verified: bench mean 2.71× → 2.95–3.12× (SimpleOSC
  2.66× → 3.2×), 0 overruns; the nine golden WAVs byte-identical; battery unchanged for all 109
  modules; `ctest` passes.
- **Link-time optimization in Release builds (Claude, requested by Javier).** `G1_LTO` (on by
  default, only where the compiler supports it; `-DG1_LTO=OFF` for quicker links while developing).
  Measured with `tools/bench/bench.sh`, two interleaved runs of each build on the same machine:
  +3–5 % on all nine patches; output byte-identical to the references; `ctest` passes.
- **A host-port access waits only for the DSP it touches (Claude, requested by Javier).** The OS
  touches the DSPs' host ports ~39,000 times per emulated second (mostly a helper at `$10C346` that
  streams parameters to DSP 0 and polls its status), and every access used to stop all four DSPs.
  Now it waits for that DSP and the one before it, hands on the latter's audio (each DSP reads its
  upstream link 8 blocks late, and without that it could run into blocks not handed on yet and take
  them as silence, which the first attempt did), and brings that DSP to the instant on the CPU thread;
  the others keep running. The serial run (`G1_THREADS=0`) has the same catch-up points, so it stays
  the reference. Ports 4–7 (the expansion board, which the OS probes) have no DSP and no longer sync
  anything. Verified: bench 2.28–2.97× → 2.54–3.32× real time, 0 overruns; the nine golden WAVs
  byte-identical threaded, serial and against the references; a five-upload session plays; battery
  unchanged for all 109 modules; `g1run` in real time at 100 % speed with the emulation thread 43 %
  busy.
- **The CPU no longer waits for the DSPs at every sync: 20–57 % faster (Claude, requested by
  Javier).** A periodic sync now launches the four DSPs towards that instant, each on its own thread
  (DSP 0 used to run on the CPU thread), and the CPU runs its next slice meanwhile; the next sync, or
  any CPU access to a host port, waits for them first, so the CPU never sees a DSP ahead or behind.
  Words for the CPU's side of the HI08 and the audio are handed over at that join on the CPU thread.
  `syncDsps()` is there for tools that read DSP memory (`g1boot`, `g1patchtest`). Verified:
  `tools/bench/bench.sh` 1.74–2.15× → 2.28–2.97× real time with 0 overruns; the nine golden WAVs
  byte-identical threaded, serial and against the references; a five-upload session plays; the full
  battery gives the same verdict for all 109 modules; `ctest` passes.
- **A cycle-table correction tried and rejected (Claude, requested by Javier).** Charging the long
  absolute (`lab`) or the long immediate (`lim`) penalty, not both, as Codex read the DSP56300 manual,
  keeps 0 overruns but makes `WavetablePad.pch` clearly noisier (spectral flatness ~0.03 → ~0.3). Not
  applied; the measurements and what to check next are in `docs/performance-plan.md`.
- **A per-PC cycle profile of the emulated DSPs (Codex and Claude, requested by Javier).**
  `G1_JITBLOCK=1 G1_CYCPROF=<dsp> g1patchtest ...` prints, after the note, every executed PC with its
  count, emulated cycles and disassembly, plus totals per mnemonic and per instruction form, with REP
  bodies and interrupt vectors attributed separately; `tools/cycprof.py` runs it over four patches and
  four DSPs. Unset, the execution loop is unchanged (one dispatch per catch-up). `g1dspcheck` gains
  synthetic cases for it and records that the core runs one whole JIT block after each RTI before the
  next pending interrupt (up to 32 instructions; the chip takes a few cycles). Written by Codex, whose
  quota ran out before the audit was finished; reviewed, split and committed by Claude. Verified: the
  nine bench WAVs byte-identical, bench speed unchanged, `ctest` passes, WavetableSynth's DSP 1 profiles
  at 1.058 cycles per instruction with no unaccounted cycles.
- **A benchmark and a golden-WAV gate for performance work (Codex and Claude, requested by Javier).**
  `g1patchtest --bench` prints the realtime factor, each thread's busy and waiting time, barriers per
  second (periodic and host-port) and overruns; `tools/bench/bench.sh` runs nine patches
  (`tools/bench/patches.txt`) and fails on any overrun; `tools/bench/golden.sh record|check` compares
  WAVs byte for byte against references kept outside the repo. Written by Codex; Claude removed its
  per-instruction timing of the CPU thread (it made `--bench` runs 65 % slower and understated the
  speed as 1.25–1.44×) and fixed the table's thread column, which showed the idlest thread instead of
  the busiest. Verified: 1.74–2.15× real time, the CPU thread (68k + DSP 0) is the busiest on all nine,
  0 overruns, nine WAVs byte-identical threaded, serial and against the references; `ctest` passes.
- **`v0.1.0-alpha.9` is out (Claude, requested by Javier).** Prerelease on GitHub with the five
  packages: Linux x86_64 native and JUCE, Linux arm64, macOS universal and Windows. Checked the CI run
  (all six jobs green) and the packages' contents; no ROM in any of them. #4's reporter was asked to
  try it, first load included; the issue stays open until they confirm.
- **Release notes for `v0.1.0-alpha.9` (Claude, requested by Javier).** `docs/release-notes.md`
  describes the fix for DSP 0's codec input (the first-load grit of #4) and lists alpha.5 to alpha.8 as
  included. Documentation only.

## 2026-09-27

- **DSP 0 codec RX uses its own frame clock (Codex, requested by Javier).** The ESSI receivers on
  DSP 0 now run at 432 DSP cycles per word (two words per 96 kHz sample), independently of the
  96-cycle DSP-link transmitters. The explicit RX period survives repeated CRA writes and keeps
  the TX/RX phases independently anchored. This removes the codec-input interrupt overload:
  WavetableSynth and WavetablePad both report `overruns=0` on all four DSPs, including after the
  `G1_BEFORE` upload sequence; WavetableSynth is ~261.5–262.3 Hz with spectral flatness 0.012,
  SimpleOSC is 261.7 Hz at −61.8 dBFS, AudioIn is 440 Hz at −61.8 dBFS, and the full battery is
  62 sounds / 19 moves / 11 fixed / 17 silent. Verified with `g1dspcheck`, `ctest --test-dir
  build`, and `cmake --build build -j16`; no ROMs or third-party files changed. Reviewed and
  committed by Claude (Codex's sandbox cannot write `.git`), who moved an orphaned overlay comment
  and re-checked WavetableSynth, WavetablePad and SimpleOSC with the same results.

- **`v0.1.0-alpha.8` is out (Claude, requested by Javier).** Prerelease on GitHub with the five
  packages: Linux x86_64 native and JUCE, Linux arm64, macOS universal and Windows. Checked the CI run
  (all six jobs green) and the packages' contents; no ROM in any of them. #4's reporter was asked to
  try it; the issue stays open until they confirm.
- **Release notes for `v0.1.0-alpha.8` (Claude, requested by Javier).** `docs/release-notes.md`
  describes the fix for heavy patches that came out as noise (#4), the upload hang fixed earlier today,
  the DSP 0 input interrupt still open as a known issue, and lists alpha.5 to alpha.7 as included.
  Documentation only.
- **Heavy patches ran out of emulated DSP time, not host CPU: short absolute moves now cost one
  cycle (Claude, requested by Javier).** #4's `WavetableSynth.pch` (11 voices) came out as white
  noise even headless and faster than real time. The OS fills each DSP to its own budget of 864
  cycles per sample; the core's table charged 2 cycles for `move x:aa` / `y:aa`, which the G1's code
  uses everywhere, so full DSPs went over, the next sample clock was dropped and the control-rate code
  starved. `cmake/Dsp56300.cmake` sets them to 1 (not checked against the manual, which was not at
  hand: the evidence is that the real G1 plays the patch with 11 voices). `g1patchtest` with
  `G1_VERBOSE=1` now prints dropped sample clocks (`overruns=`), cycles per instruction and the
  interrupt vectors each DSP serviced. Verified: WavetableSynth, DSPs 1 and 2 from ~6,200 overruns in
  3 s to 0, output from flatness 0.71 (noise) to a pitched signal; `SimpleOSC.pch` unchanged (261.7 Hz,
  −61.8 dBFS); `ctest` passes. **Not fixed yet:** DSP 0 still overruns on this patch and on
  `WavetablePad.pch`, because its codec-input DMA interrupt runs ~6.6 times per sample; an
  experiment clocking its receivers at 2 words per sample removed it, but it also touches its
  transmitter and the audio inputs and was left out (`docs/performance-plan.md`). Also
  `docs/performance-plan.md`: a work order for an agent, with this finding first, then a benchmark
  and golden-WAV gate and phased host-side optimisations.

- **The emulator no longer stops answering after a run of uploads (Claude, requested by Javier).**
  Testing Animatek NME against it, 18 patches loaded one after another into slot A left it deaf:
  upload timeouts, then no answer to anything. DSP 0 was running past the end of its main loop,
  because the DSP JIT reused a cached one-instruction block compiled before the OS moved the loop
  end there; its register stack crept on every pass until an `rti` looped for ever. The cache of
  single-instruction blocks is now off (`g1Lib/g1dsp.cpp`; `G1_SINGLEOP_CACHE=1` turns it back
  on). `g1patchtest` gains `G1_BEFORE` (a session of uploads), `G1_PCHIST` and, in the DSP,
  `G1_DSPTRACE` and `G1_JITBLOCK` (NOTES.md, "A cached block at the loop end"). Verified: the
  hanging sequence (9 factory patches, ENSODEF, Classic sawbass) and the 18-patch one load and
  sound headless, and hang again with the cache back on; in `g1gui` with NME, 80 random patches
  from the library uploaded and fetched back with no timeout (the only differences, two module
  names longer than 16 characters, are NME's). The real G1 took both sequences without trouble.
  Speed cost 1-2 %. `ctest` and `g1dspcheck` pass.
- **`v0.1.0-alpha.7` is out (Claude, requested by Javier).** Prerelease on GitHub with the five
  packages: Linux x86_64 native and JUCE, Linux arm64, macOS universal and Windows. Checked the
  CI run (all six jobs green) and the release's asset list; no ROM in any of them.
- **Release notes for `v0.1.0-alpha.7` (Claude, requested by Javier).** `docs/release-notes.md`
  describes the master clock fix, the clock setting a new flash needs (and NME's inverted label for
  it), and lists alpha.5's and alpha.6's fixes as included. Documentation only.
- **The internal master clock ticks: clocked patches are no longer silent (Claude, requested by
  Javier).** nmedit's `progger.pch` (110 modules, a sequencer run by MIDIGlobal's clock) is loud
  on a real G1 and was silent here, as were MIDIGlobal's clock and sync outputs in the module
  battery. The OS runs the master clock on the GPT's output compare 2, and starts it by enabling
  OC2I while OC2F is already set; the 68331 then interrupts at once, Gearmulator's GPT waits for a
  new match that never comes. `g1Lib/g1mc.cpp` now injects the interrupt when TMSK1 gains an OCxI
  bit whose flag is up. The clock also needs the synth settings' MIDI clock source at 1, which a
  new flash does not have (it comes up at 0, waiting for MIDI clock); Animatek NME's dialog labels
  the bit the other way round. `g1patchtest` gains `G1_CLOCKSRC` (rewrite the clock source through
  NME's own settings encoder, now linked in), `G1_MIDICLOCK` (send MIDI clock) and, in verbose
  mode, a status line per DSP. **Checked** on Linux by comparing with the real G1 (patch loaded
  through NME, a note over MIDI, both outputs recorded): `progger.pch` with the clock at 1 gives
  the real synth's level; without the fix it stays silent even with the clock at 1 (checked by
  removing it); `ButohDrone`, `04-007-007` and `Clasico` from Javier's bank, and
  `PolyGateTest`, match in level and shape; Javier played the three in the emulator's window and
  they sound. `ctest` passes. In `NOTES.md`, "The master clock".

## 2026-09-26

- **The editor keyboard's stuck notes are the G1's own, and a test patch that shows it (Claude,
  requested by Javier).** #4 reported that the keyboard floater
  stacks notes. The OS handles the editor's note messages as one virtual key with one stored note,
  so overlapping notes stick: reproduced in the emulator, and on the real G1 with the new
  `tools/patches/PolyGateTest.pch` (keyboard gate to an ADSR, 8 voices), whose gate LED stayed lit
  after two overlapping notes were released and went out after a lone note. MIDI IN is not
  affected. The emulator is faithful here; the fix belongs in the editor. Written up in `NOTES.md`,
  "The editor's keyboard is one key". `g1patchtest` gains `G1_CHORD`, `G1_SEQ`, `G1_CHORD_MS` and
  `G1_CHORD_MIDI` to press and release notes and measure what is left, and now waits for each
  packet's ACK as NME does: sending on any reply had lost the last packet of #4's
  `WavetableSynth.pch` to the receiver reset the OS makes after reloading the DSPs, which showed as
  `Error` on the display. **Checked** on Linux: `WavetableSynth.pch` loads with 11 voices and
  sounds; with the test patch a lone note leaves silence and two overlapping ones leave a note at
  full level through the editor's port, not through MIDI IN; `ctest` passes.
- **Published `v0.1.0-alpha.6` pre-release (Claude, requested by Javier).** Annotated tag on
  `a1c5074`, whose CI was green on all five jobs. Tagged run 36234756317 completed on the five
  build jobs and the release job and published five ROM-free packages. Downloaded the macOS archive:
  `G1-Emu.app` and `g1run` are universal (x86_64 and arm64), with README and licence, and no ROM.
  Release: <https://github.com/animatek/G1-Emu/releases/tag/v0.1.0-alpha.6>.
- **Release notes for `v0.1.0-alpha.6` (Claude, requested by Javier).** `docs/release-notes.md`
  now describes alpha.6: the busy-DSP upload fix, with alpha.5's fixes listed as also included,
  and says the fix was verified on Linux only. The rest (Windows quick start, ROM, signing,
  platforms) is kept. Documentation only.
- **A DSP with a full sample routine no longer ignores the editor (Claude, requested by Javier
  after #4).** #4's reporter still got an upload timeout with
  alpha.5 and attached `WavetableSynth.pch`; the last of its six packets got no ACK. Two causes,
  both where a busy DSP meets a host command. The core moves external interrupts into its queue
  only when it has no internal interrupt to service, and a DSP whose sample routine fills the
  block always has the next IRQD waiting, so the host command waited until G1-Emu dropped the next
  one. And IRQDs were queued, not latched: 83,071 of them on DSP 0, in a ring of 1024. The core
  now collects external interrupts when it picks the next interrupt (`cmake/Dsp56300.cmake`), and
  `g1Lib/g1dsp.cpp` injects an IRQD only when the previous one has been serviced. `g1dspcheck`
  gains "host command while busy", which fails without the core fix (checked) and passes with it.
  `tools/upload-e2e.sh` now finds its own `g1run` by pid: it had picked the G1-Emu window that was
  open, and sent it the upload. **Checked** on Linux: `WavetableSynth.pch` uploads over ALSA with
  all six ACKs (one `0x36`, five `0x7f`); all 71 sample patches upload with no missing reply; the
  module battery gives exactly yesterday's verdicts (62 sound, 19 move, 11 fixed, 17 silent);
  `ctest` passes. Not checked on macOS or Windows.

## 2026-09-25

- **Published `v0.1.0-alpha.5` pre-release (Claude, requested by Javier).** Annotated tag on
  `a70b046`, after the CI of that commit was green on all five jobs (the previous commit's had
  failed on arm64 and macOS, which is how the AArch64 LE bug was found). Tagged run 36127285397
  completed on all five build jobs and the release job, and published five ROM-free packages:
  Windows x86_64, macOS universal, Linux x86_64 native, Linux x86_64 JUCE and Linux arm64 native.
  Downloaded the macOS archive: it holds `G1-Emu.app`, `g1run`, README and licence, both
  executables are universal (x86_64 and arm64), and there is no ROM. Release:
  <https://github.com/animatek/G1-Emu/releases/tag/v0.1.0-alpha.5>.
- **LE on AArch64 was wrong in the same case as on x86-64 (Claude).** The CI of `c98886f` failed on
  Linux arm64 and macOS: `g1dspcheck`'s new "GT and LE with Z set" test. AArch64 computed
  `(N ^ V) + Z` and took LE only when that was exactly 1, so with Z = 1 and N != V (a sum of 2) it
  was false. `cmake/Dsp56300.cmake` now tests "not zero", as its GT already did. The earlier entry
  said AArch64 was right; it was right for GT only. **Checked:** the overlay applies and x86-64 is
  unchanged (`g1dspcheck` passes); the AArch64 code can only run in CI, which is what confirms it.
- **Release notes for `v0.1.0-alpha.5` (Claude, requested by Javier).** `docs/release-notes.md`,
  which the release job publishes, now describes alpha.5: the two upload fixes (idle DSP deadlock,
  dropped host command), the sawtooth (CMPM) and GT/LE fixes, the truncated-SysEx log line and the
  `%USERPROFILE%` fallback. It says plainly that the fixes were verified on Linux only. The Windows
  quick start, ROM, signing and platform sections are kept from alpha.4. Documentation only.
- **The sawtooth sounds again: the JIT's CMPM overwrote its operand (Claude, requested by Javier
  after #4).** `OscA` and `OscB` on wave 2 (saw), and `OscSlvC`, gave a
  constant instead of a wave. The saw branch of the oscillator does `cmpm a,b` then `tgt a,b`, and
  the JIT took the absolute value for CMPM in the cached register it holds for `a`, so the next
  instruction of the block read |a|. `cmake/Dsp56300.cmake` now copies the operand first. Found
  with the new `tools/jitdiff`, which runs instructions dumped from the DSP on the JIT and on the
  interpreter, or as one block against one per block, from random states; it also found that GT
  and LE on x86-64 were wrong when Z = 1 and N != V (parity instead of `(N ^ V) | Z`), fixed too.
  The same core runs on AArch64, so the Mac had the saw bug as well. **Checked** on Linux: all four
  OscA waves measure −62 dBFS at 262 Hz and the recorded saw is a clean ramp; the module battery
  gives 62 sound, 19 move, 11 fixed, 17 silent (61/19/12/17 before), the only change being
  `OscSlvC`; `g1dspcheck` gains "CMPM keeps its operand" and "GT and LE with Z set", each failing
  without its fix (checked by removing it) and passing with it; four reference patches measure the
  same and `korg.pch` still uploads. Not checked on macOS or Windows. Also in `NOTES.md` and
  `docs/module-battery.md`; the interpreter's own LE mistake is noted, not fixed.
- **The upload test on a real Mac, written down (Claude, run by Javier and a Claude session on his
  Mac).** On an Intel Mac with macOS 13.7.8, the `v0.1.0-alpha.4`
  `g1run` and the NME 0.18.0 app, two uploads (one and two packets) arrive whole and get every
  ACK, so #3 does not reproduce there; what differs from the reporter is Apple Silicon, macOS 26.7
  and a source build of NME. Read from the `G1_MIDI_LOG=1` log of that run; NME's console was not
  captured. In `docs/upload-timeouts.md`. Documentation only.
- **A host command is no longer thrown away when its DSP has no idle gap (Claude, requested by
  Javier).** The last packet of nmedit's `korg.pch` got no ACK, so the
  upload timed out. The real G1 accepts that patch (Javier loaded it into slot A of the real synth
  and all four packets were ACKed), so the fault was ours: `Dsp::hostCommand` waited up to 200,000
  cycles for `hasPendingInterrupts()` to go false and dropped the command otherwise, but that
  predicate is also true while a DSP is inside an interrupt, and here the DSP's sample routine
  fills the block. The `$76` sent to DSP 0 was lost and the 68k polled its HI08 status for ever.
  It now waits only for the external interrupt queue (`hasPendingExternalInterrupts()`), which is
  what the wait protected. **Checked** on Linux (native backend): logging showed exactly one drop
  (`$76` on DSP 0) for `korg.pch` and none for the healthy patches; after the fix all 71 sample
  patches upload with no missing reply (68 before), and over ALSA against `g1run` `korg.pch`'s four
  packets get their ACKs (one `0x36`, three `0x7f`, like the real G1) and `g1run` exits on SIGINT;
  five reference patches measure the same as before; `ctest` passes. `tools/upload-e2e.sh` now also
  counts the `0x7f` ACKs. Not checked on macOS or Windows; NME was not running. It may or may not be
  the reporter's case in #4, whose patch is not known. Detail in `docs/upload-timeouts.md` and
  `NOTES.md`.

## 2026-09-24

- **A DSP that sat idle no longer deadlocks the emulator, and a way to replay an upload over ALSA
  (Claude, requested by Javier).** Uploading nmedit's `korg.pch`
  hung `g1patchtest` and `g1run` (which then ignored SIGINT): the main thread waited in the core's
  TX write callback for room in a ring of 32768 frames, with every worker idle, also with
  `G1_THREADS=0`. The ring was really full: after a 434-million-cycle gap in which DSP 0's ESSI
  emitted nothing, the core's fine-schedule clock paid the whole debt in one call (4.5 million
  frames). `cmake/Dsp56300.cmake` now puts an anchor more than 64 periods behind back at 64, as a
  port that was off queues nothing; `g1dspcheck` gained "idle link port", which fails without it
  (checked by removing the fix) and never blocks, as its callback only counts. Also
  `g1patchtest --dump-packets DIR` and `tools/upload-e2e.sh`, which replays a patch's packets with
  `aseqsend` against a running `g1run` on a copy of the flash. **Checked** on Linux (native
  backend): `ctest` passes; `korg.pch` uploads without hanging and `g1run` exits on SIGINT; five
  other patches measure the same as before (SimpleOSC −61.8 dBFS at 262 Hz among them); the 71 sample
  patches upload, two of them (`future303`, `progger`) only needing more than the bench's 300
  emulated ms. **Not fixed:** `korg.pch`'s last packet still gets no ACK (its DSPs have almost no idle
  time and DSP 0 never services the host command; cause not established), and the macOS report (#3)
  is not reproduced. Not checked on macOS or Windows; NME was not running. Detail in
  `docs/upload-timeouts.md` and `NOTES.md`.
- **The MIDI backends behind one interface, `MidiTransport` (Claude, requested by Javier; branch
  `refactor/midi-transport`, merged into `main` as `12153eb`).** `AlsaMidi`, `JuceMidi` and `WindowsMidi` resembled each
  other by habit, with no declared interface, and `EmuHost` chose between them with `#ifdef`s and
  carried the status text of each. Now `app/miditransport.h` declares `MidiTransport` (`addPort`,
  `poll`, `send`, `describe`, and `linkRawCard` for the ALSA-only raw MIDI card), the three classes
  implement it, and `makeMidiTransport()` in `app/miditransport.cpp` is the only place that knows
  which one a build uses. What moved: the status-bar wording into each transport's `describe()`, and
  the snd-virmidi/USB-gadget card lookup and linking from `EmuHost` into `AlsaMidi::linkRawCard`.
  `EmuHost` lost the `Midi` alias, its MIDI `#ifdef`s and about 60 lines. No behaviour change is
  intended; the point is that a new way to reach the emulator (a local socket straight from NME) is
  one class and one line in the factory. Verified on Linux, both configurations: native (ALSA) and
  JUCE forced, `g1dspcheck` passes on both; with a scratch flash, SysEx of 4005 bytes and a
  note-on arrive on the PC Port and MIDI on both, the emulator answers, the status line is the same
  text as before, the raw card `G1` is still linked on the native one (`MIDI <-> f_midi`), and a
  manual device name that does not exist gives the same "could not open one of the chosen MIDI
  devices" message. **Not verified:** macOS and Windows builds (the CI does it on push), and
  `windowsmidi.cpp`, which only builds with the preview Windows MIDI Services SDK that neither CI
  nor this machine has: its change is small (inherit, `override`, a `describe()` that moves the old
  status string) but nothing has compiled it.

- **ROM folder on Windows: fall back to `%USERPROFILE%` when `HOME` is unset (Tuth in PR #5,
  ported by Claude, requested by Javier).** `romfinder`'s `home()`
  fell back to `.` when `HOME` was missing, which on Windows is always the case for a program
  started from Explorer or a DAW, so the announced ROM folder was the relative
  `./Documents/Animatek/G1-Emu/roms`, a path that exists for nobody. It now tries `USERPROFILE`
  before giving up. Taken from PR #5 on its own, because `main` already fixed the flash and
  settings location with `%APPDATA%` (alpha.4) and already has its own manual MIDI pairing, which
  the rest of that PR reimplements with a different API. Verified on Linux with `HOME` unset and
  `USERPROFILE` pointing at a folder holding the ROM under `Documents/Animatek/G1-Emu/roms`:
  `g1run` picks that ROM ahead of the one in the source tree. **Not verified on Windows**; Tuth's
  PR reports the same fix working there.

- **JUCE MIDI backend: a truncated inbound SysEx now shows in `G1_MIDI_LOG` (Claude, requested by
  Javier).** Triage of issue #3 (macOS: every patch upload ends in
  `Upload timeout at packet 0`). Its diagnosis, that the empty `handlePartialSysexMessage`
  discards fragments of a long SysEx, does not hold against JUCE 8.0.12, the version CI builds:
  `SingleGroupMidi1ToBytestreamTranslator` (UMP path) and `MidiDataConcatenator` (bytestream path)
  reassemble the packets themselves and call `handleIncomingMidiMessage` with the whole message,
  and call the partial callback only for a stream that ended without `F7`, whose data they then
  clear. So there is nothing to accumulate there, and filling it in would not fix the report.
  What was missing is a way to see it happen: `handlePartialSysexMessage` now prints one line
  under `G1_MIDI_LOG=1` saying a SysEx was cut short and how many bytes JUCE dropped. Verified on
  Linux with the JUCE backend forced (`build-juce`, scratch flash): SysEx of 5005 and 3005 bytes
  sent to the virtual PC Port arrive whole in one chunk (`[midi] in PC Port 5005 bytes`), and
  `g1patchtest` uploads a 4.2 KB patch in 8 packets. **Not verified:** the truncated case itself
  (`aseqsend` refuses to send a SysEx with no `F7`) and anything on CoreMIDI, since there is no
  Mac here. Issue #3 stays open until the reporter runs a build with `G1_MIDI_LOG=1`.

- **Upload timeout diagnosis written down, and two ideas noted (Claude, asked for by Javier).**
  New `docs/upload-timeouts.md` for issues #3 and #4: with JUCE 8.0.12, `MidiInput` rebuilds each
  SysEx from CoreMIDI's UMP packets and calls only `handleIncomingMidiMessage`. The empty
  `handlePartialSysexMessage` that #3 blames is never reached, so filling it would change nothing.
  #4 also reports timeouts on Linux, which does not use that code. The document gives one logged
  upload (`G1_MIDI_LOG=1`, NME's `[UPLOAD]` lines, `pcport-in.bin`) that tells whether the packet
  is lost before the emulator, gets no ACK from the emulated G1, or its ACK does not reach NME.
  It also says what to do in each case. `docs/next-steps.md` points to it (point 7). `ROADMAP.md`,
  "Ideas for later", gains a direct NME–emulator link over a local socket, beside the MIDI ports
  and not instead of them, and browsing nordmodular.com patches from NME, which depends on that
  site's maintainer. Verified by reading JUCE 8.0.12's `juce_MidiDevices.cpp`,
  `juce_UMPMidi1ToBytestreamTranslator.h`, `juce_MidiDataConcatenator.h` and
  `juce_CoreMidi_mac.mm`, and NME's `ConnectionManager.*` (5 s ACK timeout). The logged-upload
  procedure itself has not been run (it needs a real failure); the Linux-side checks made on
  `main` afterwards, with the JUCE backend forced, are recorded at the end of the document.

## 2026-09-22

- **Published `v0.1.0-alpha.4` pre-release (Codex, requested and approved by Javier).**
  Published the annotated tag and GitHub pre-release with five ROM-free packages: Windows
  x86_64, macOS universal, Linux x86_64 native, Linux x86_64 JUCE and Linux arm64 native.
  Tagged GitHub Actions run 35710146687 completed successfully across all five build jobs and
  the release job. Downloaded the published Windows archive and verified that it contains
  `G1-Emu.exe`, `g1run.exe`, `WINDOWS.md`, `README.md` and `LICENSE`. Release:
  <https://github.com/animatek/G1-Emu/releases/tag/v0.1.0-alpha.4>.

- **`v0.1.0-alpha.4` release preparation and stable Windows user data (Codex, requested and
  real-machine tested by Javier).** Prepared the first pre-release
  described as end-to-end verified on Linux, macOS and Windows after Javier confirmed the real
  Mac run and completed the Animatek NME handshake on Windows. Added a packaged `WINDOWS.md` with
  the exact two-cable loopMIDI setup (`G1→NME` and `NME→G1`), restart requirement, ROM/audio/
  patch steps and counter-based timeout diagnosis; rewrote the release notes for alpha.4 and
  updated the README, roadmap and handover to match the real platform status. Windows now stores
  settings and flash in `%APPDATA%\Animatek\G1-Emu` instead of a working-directory-relative
  `.local` tree, so Explorer and terminal launches share one configuration. The Windows CI
  package now includes the guide. Verification: Javier's live Windows test connected NME through
  the two directional ports; a clean local Windows Release build completed with the NMake
  generator, `g1dspcheck` passed (1/1), the rebuilt executable enumerated both directional ports,
  and `git diff --check` passed. Tagged cross-platform CI is the publication gate.

- **Manual MIDI device pairing, the loopMIDI patch for Windows (Claude Sonnet 5, requested by
  Javier).** Windows MIDI Services still cannot own ports here (Microsoft/MIDI issue #1047,
  fixed only in the November 2026 Windows release), and Javier already had loopMIDI running
  with `G1-PC Port`/`G1 MIDI` ports and AnimatekNME pointed at them, but nothing on the G1-Emu
  side was using them yet. Added four `EmuHost::Options` fields (`pcPortOutDevice`,
  `pcPortInDevice`, `midiOutDevice`, `midiInDevice`; matching `G1_PCPORT_OUT`/`G1_PCPORT_IN`/
  `G1_MIDI_OUT`/`G1_MIDI_IN` environment overrides), persisted in the settings file like every
  other option. Empty (the default) keeps the existing owned-port behaviour unchanged on every
  platform. `JuceMidi::addPort` can now open an existing system MIDI device by name instead of
  creating one, and exposes `availableOutputs()`/`availableInputs()`. Settings (Windows, plain
  JUCE backend only, not the experimental Windows MIDI Services one) grew four device dropdowns
  -- PC Port out/in, MIDI out/in -- defaulting to "Automatic (owned port)", with the window
  height and layout adjusted to fit them and the stale "This build needs external MIDI cables"
  message replaced with one naming the Microsoft issue and the patch. Documented in
  `docs/windows-build.md` ("Manual MIDI device pairing") that each direction is independent and
  wants its own loopMIDI port, the way a real MIDI cable pair would, since pointing both halves
  of one logical G1 port at the same loopMIDI port risks the emulator hearing its own output.
  Updated `ROADMAP.md` and `docs/next-steps.md`, which had recorded the opposite decision
  (owned ports only, no loopback fallback) two days ago; that stance stands as the default, this
  is the temporary patch on top of it. Verification: full `build-windows` Release build and
  `ctest -C Release -L g1` (1/1) still pass. The initial same-port counter check was only a smoke
  test and could include loopback traffic; the conclusive live test used separate `G1→NME` and
  `NME→G1` ports, restarted G1-Emu after selecting them, and Animatek NME then completed the
  handshake and connected.

- **Windows audio UI and experimental owned MIDI ports (Codex, requested by Javier).**
  Enabled ASIO in Windows builds, separated driver and
  device selection, removed Linux-only settings from the JUCE view, and fixed garbled
  status text caused by UTF-8 strings passed to Windows wide `printf`. Added an opt-in
  x64 Windows MIDI Services backend using SDK 0.99.81-devpreview.9, generated C++/WinRT
  projections, two bidirectional endpoints, byte-stream/UMP conversion, and a live
  `g1midicheck` test for WinMM notes, fragmented SysEx and independent port routing.
  Represented the two G1 ports as two MIDI 1.0 function blocks/groups on one owned device,
  sanitized internal device identifiers, and moved SDK lifetime operations to a dedicated
  COM MTA. Added `G1_WINDOWS_MIDI=0` for audio-only recovery and ignored workspace-local
  runtime state (`.local/`). Documented preview licensing and local-only build requirements.
  Verification so far: ASIO drivers enumerate, the DSP CTest passes (1/1), and a sanitized
  test port appeared once in WinMM. A one-device/two-function-block build reaches endpoint
  creation, but component 26100.8875 does not publish either MIDI 1.0 port and hangs during
  teardown, matching Microsoft issue #1047 (fixed for the November 2026 Windows release).
  This is not a working/release-ready native MIDI claim. No loopMIDI integration or system
  driver was used, and no physical MIDI port was opened.

## 2026-09-21

- **Native Windows build completed (Codex, requested by Javier).**
  Reused the installed Visual Studio 2022 C++ tools and Windows SDK 10.0.26100.0; downloaded
  Gearmulator `mdmm-v0.1.0-alpha.13` and its submodules into ignored `build-windows-deps`, using
  the existing JUCE 8.0.12. Built every target in `build-windows` as Release x64. Fixed the
  optional patch test's nonstandard `M_PI` constant for MSVC and initialized JUCE in `g1run`,
  which previously failed to open audio on Windows. Added `g1run --list-devices` for diagnostics.
  Verification: full build succeeds, `ctest -C Release -L g1` passes (1/1), and a 12-second
  scratch-flash run opens Komplete Audio 6 via WASAPI at 44.1 kHz with four outputs, runs at
  100% speed and reports zero dropouts. No editor/loopMIDI connection or audible patch playback
  was verified. Preserved the pre-existing local MIDI logging changes in `emuhost.cpp`.

- **Windows audio selection (Codex, requested by
  Javier).** The JUCE settings view lists the available driver and
  device pairs (WASAPI/DirectSound/ASIO when JUCE exposes them) instead of Linux-only JACK/ALSA
  choices, and `EmuHost` preserves the selected device type when opening it. The MIDI backend
  does not open loopback cables or physical devices when owned ports are unavailable.
  Corrected the driver-selection API (the initialise argument is a device-name pattern), stopped
  opening audio during enumeration, and kept the native ALSA default working. Windows build and
  MIDI status are documented in `docs/windows-build.md`. Verification: Windows Release build,
  DSP test and explicit WASAPI device opening pass (see above); GUI interaction remains unverified.

- **`v0.1.0-alpha.3` published with the fixes proven on the first real Mac run (Codex, asked for
  by Javier).** [The pre-release](https://github.com/animatek/G1-Emu/releases/tag/v0.1.0-alpha.3)
  carries all five packages built from the tag: macOS universal, Windows x86-64, Linux x86-64
  with both backends and Linux arm64. The tag's
  [CI run](https://github.com/animatek/G1-Emu/actions/runs/35609561152) is green in all five build
  jobs and in the release job. Verification after publication: downloaded
  `G1-Emu-macos-universal.tar.gz`, its SHA-256 matches GitHub
  (`03df3cee739eeb726e0ed243238f5702cc7a1862c4da9d7734559dd91f7403eb`), the archive contains
  `G1-Emu.app`, `g1run`, README and licence, and both executables are Mach-O universal binaries
  with x86-64 and arm64 slices.

- **Release notes prepared for `v0.1.0-alpha.3` (Codex, asked for by Javier).** They now describe
  the three fixes found on the first real Mac run -- the CoreAudio split-device hang, the `.app`
  launcher path and truncated PC Port SysEx replies -- instead of claiming that nobody has run the
  macOS build. They distinguish what was verified on Intel macOS 13.7.8 from Apple Silicon, which
  still has CI coverage only. Verification: compared with the two merged fixes and the live Mac
  results recorded immediately below; Markdown links and the release workflow's notes path checked.

- **The JUCE MIDI backend dropped bytes out of the PC Port's own replies, breaking NME's
  handshake (Claude, found live with NME on the same first Mac run).** With the two fixes above
  in place, NME could see and open `G1-Emu PC Port`, but "Connect" still ended in "No response
  from synth (timeout)" every time. An independent CoreMIDI probe (not NME, not the emulator's
  own report) sending the exact bytes NME's *IAm* uses, `F0 33 00 06 00 03 03 F7`, showed why:
  `g1run` answered with `F0 33 00 06 01 03 F7` (7 bytes) on the first try and
  `F0 33 00 06 01 F7` (6 bytes) on a retry ten seconds later -- both short of the 12-byte reply
  `NOTES.md` already documents (`F0 33 00 06 01 03 03 3F 7F 7F 01 F7`: sender, version, serial,
  device ID), and shorter each time, worse under the heavier CPU load a second attempt landed
  under. `EmuHost::run` drains the emulated DUART's transmit buffer every 2 ms of real time
  (`app/emuhost.cpp`); a 12-byte SysEx reply can take the OS longer than that to finish writing,
  so `JuceMidi::send` (`app/jucemidi.h`) could receive it in more than one call, mid-message. Its
  `messageLength` had no way to say "not done yet": short of a real terminator it invented one --
  returning what bytes had arrived as if they were the whole message ("unterminated: send what
  there is") -- so a partial SysEx went out as a short, wrongly-terminated one, and the bytes
  after it were misread as new messages starting on a stray data byte, which is not a valid
  status byte and got silently dropped. The native ALSA backend never had this: `alsamidi.h` feeds
  bytes one at a time through `snd_midi_event_encode_byte`, a decoder that already holds an
  incomplete message across calls, which is exactly what was missing here. `JuceMidi` now keeps a
  `pending` buffer per port and only calls `sendMessageNow` once `messageLength` reports a
  complete message (0 means wait for the rest); an unfinished message can still arrive at CoreMIDI
  split across more than one packet, which is ordinary SysEx transport and not this bug -- what
  changes is that every byte the OS wrote is now in it, and in order, once whole. A message that
  somehow never completes clears itself past 1 MB (the entire flash) rather than blocking a port
  forever. Verification: the DSP test still passes; the same probe sending
  `F0 33 00 06 00 03 03 F7` to a rebuilt `g1run` now gets back exactly
  `F0 33 00 06 01 03 03 3F 7F 7F 01 F7`, split as `F0 33 00 06 01 03 03` then `3F 7F 7F 01 F7`
  across two CoreMIDI packets but byte-for-byte and in order.

- **`g1gui.sh` did not start the window on macOS (Claude, same first Mac run).** It execs the
  binary at a fixed path next to the JUCE bundle, which is what Linux and Windows produce; on
  macOS `g1gui` is a `.app` bundle instead, so that path is a directory and the script failed
  with "No such file or directory". It now tries the bundle's own binary first
  (`G1-Emu.app/Contents/MacOS/G1-Emu`) and falls back to the bare path, so the same script starts
  the window on all three. Verification: ran on macOS 13.7.8 (Intel), where it now opens the
  panel; the bare-path branch is unchanged, so Linux and Windows keep working as before.

- **The default-device combiner deadlocked on an Intel Mac; `JuceAudio` no longer risks it
  (Claude, first source build and run on a real Mac).** Built from source and run for the first
  time on real hardware (a 2013-era Intel MacBook Pro, macOS 13.7.8): it booted, the DSP test
  passed, and CoreMIDI created `G1-Emu PC Port` and `G1-Emu MIDI` exactly as expected -- checked
  independently with a small CoreMIDI lister of its own, not just the emulator's self-report.
  Sound did not: `initialiseWithDefaultDevices` hung forever inside JUCE's
  `AudioIODeviceCombiner::start()`, because this Mac's default input and default output are two
  different CoreAudio devices ("Built-in Microphone" and "Built-in Output", not one "Built-in"
  device the way Apple Silicon Macs have it) and combining them deadlocks there -- a JUCE/CoreAudio
  bug, not something to patch from here. `JuceAudio` now checks whether the two defaults are the
  same device before ever asking for both; when they are not, it asks for the output alone and
  drops the two inputs rather than risk the hang. The explicit-device path (`G1_AUDIO=<name>`, and
  the settings window) had the mirror bug -- it asked for the same name on input and output, which
  fails outright on a split-device Mac -- and now falls back to output-only there too.
  Verification: built with a local CMake 3.31.9, Gearmulator `mdmm-v0.1.0-alpha.13` and JUCE
  8.0.12; before the fix `g1run` hung indefinitely at `initialiseWithDefaultDevices`, after it
  `audio: Built-in Output at 44100 Hz, 2 outputs, +36 dB` and the four DSPs run at ~100% real-time
  speed with the flash freshly installed from the ROM.

- **The window shows the PC Port byte counters (Claude, from the first macOS report).** The
  status bar named the two MIDI ports, which is the one thing you can already see in the editor.
  It now prints `PC Port in/out` and `MIDI in/out` live, because when an editor says "no response
  from synth" the question that splits the problem in two is whether its bytes ever arrived: `in`
  stuck at zero means they did not and it is a routing problem; `in` moving with `out` stuck means
  the emulator is not answering. `g1run` already printed these; the window did not, and the window
  is what somebody testing on a Mac has open. Verification: built on the JUCE backend on Linux.

- **alpha.1 carries the universal macOS build too (Claude, asked for by Javier).** Its broken
  `G1-Emu-macos-arm64.tar.gz` was deleted and the universal binary from alpha.2 attached in its
  place, so a link to alpha.1 that is already in somebody's hands now downloads something that
  starts. Its notes say what was replaced and when, and still point at alpha.2 as the one to
  prefer. Verification: the asset was downloaded from the alpha.1 URL and its Mach-O header read —
  two slices, `x86_64 minos 11.0` and `arm64 minos 11.0`.

- **`v0.1.0-alpha.2` published with the universal macOS build; alpha.1 marked superseded
  (Claude).** [The release](https://github.com/animatek/G1-Emu/releases/tag/v0.1.0-alpha.2) was
  checked after publishing, not before: its `G1-Emu-macos-universal.tar.gz` was downloaded and its
  two slices read out of the Mach-O header — `x86_64 minos 11.0` and `arm64 minos 11.0`. alpha.1
  keeps its files but its notes now open by saying the macOS build there does not start on
  anything older than macOS 26 and pointing at alpha.2.
  [Run 35578546697](https://github.com/animatek/G1-Emu/actions/runs/35578546697).

- **The macOS build is universal and runs on macOS 11 and up; it said macOS 26 (Claude, reported
  by Javier).** The first release's macOS binary would not start on Ventura, and not because of
  the architecture: with no `CMAKE_OSX_DEPLOYMENT_TARGET` set, CMake inherits the runner's own
  system, so `LC_BUILD_VERSION` said `minos 26.0.0` and macOS refused to launch it on anything
  older — on Apple Silicon too. It was also `arm64` alone, so no Intel Mac could run it either.
  The macOS job now builds `arm64;x86_64` with a deployment target of 11.0 (the floor for arm64),
  which the core takes without changes because it picks its JIT by preprocessor and not by CMake
  (`dsp56kBase/buildconfig.h`). CI checks what it is about to ship rather than assuming it:
  `lipo -archs` must list both slices and `vtool -show-build` must say `minos 11`, and the job
  fails if not. Release notes say the Intel half is built but never run. Verification: the run for
  this commit is what proves the flags; the broken binary was diagnosed by reading
  `LC_BUILD_VERSION` out of the published asset.

- **First public pre-release: `v0.1.0-alpha.1` (Claude, asked for by Javier).** The tag on
  `53ffad9` ran the five builds and published them:
  [the release](https://github.com/animatek/G1-Emu/releases/tag/v0.1.0-alpha.1) carries macOS
  (Apple Silicon), Windows x86-64, and Linux x86-64 both backends and arm64, with
  `docs/release-notes.md` as its text. Verification: the run is green on all five jobs and the
  release job attached the same packages the tests ran against
  ([35575952609](https://github.com/animatek/G1-Emu/actions/runs/35575952609)); the Windows asset
  was downloaded **with no authentication at all** and holds `G1-Emu.exe`, `g1run.exe`, the README
  and the licence, which is the point — an artifact needs a GitHub account and a release does not.
  README now points at Releases.

- **A tag now publishes a pre-release with the five packages, and Windows stops lying about its
  MIDI (Claude).** Pushing a `v*` tag runs the same matrix and a final job attaches what it built,
  so what people download is the binary the tests ran against, never a separate build. It is
  created as a **pre-release** on purpose: this is a test build and must not look like a finished
  one. The notes people will read are `docs/release-notes.md`, kept in the repo: no ROM is
  included and none ever will be, the flash starts empty so an editor is needed, nothing is signed
  (with the Gatekeeper and SmartScreen steps written out), and a table of what is expected to work
  on each system with the Windows virtual-port question marked as the real unknown — saying plainly
  that "it did not work" is as useful a report as a success. Fixed along the way: when
  `createNewDevice` returns nothing, the status line claimed "only real MIDI devices", which the
  program never opens, and pointed at `docs/bitwig-midi.md`, a Linux document about a USB gadget.
  It now says no editor can reach the G1, and on Windows names Windows MIDI Services and loopMIDI.
  Verification: both backends build and `g1dspcheck` passes on x86-64; `g1run` on the JUCE backend
  still reports `G1-Emu PC Port (editor) and G1-Emu MIDI` where the ports do get created; the
  workflow parses and its release job is gated on a tag, so it stays dormant until one is pushed.

- **Every CI run now leaves the binaries to download (Claude).** The workflow built macOS and
  Windows and threw the result away, so nobody with those machines could try anything without
  building it first. It now packages `g1run` and the `G1-Emu` window per platform and uploads them
  as run artifacts (`G1-Emu-macos-arm64`, `G1-Emu-windows-x86_64`, `G1-Emu-linux-x86_64-native`,
  `G1-Emu-linux-x86_64-juce`, `G1-Emu-linux-arm64-native`), with the README and the licence
  alongside, kept for 30 days. No ROM is included, here or anywhere else. Everything but Windows
  is tarred first because an artifact is a zip and a zip loses the executable bit, and the
  packaging step fails the run if a binary is missing rather than uploading an empty archive.
  Nothing is signed or notarised yet, so both systems will warn about an unidentified developer.
  Verification: the workflow parses, and the run this commit triggers is what proves the paths.

- **The five CI jobs are green with the DSP test gating every one (Claude).** [CI run 35570337853](https://github.com/animatek/G1-Emu/actions/runs/35570337853) builds
  and tests Linux both ways, Linux arm64, macOS and Windows after the zero-mask fix, with no
  `continue-on-error` anywhere: the Apple Silicon blocker is closed, this time with a run that
  actually proves it. `README.md`, `ROADMAP.md`, `NOTES.md` and `docs/next-steps.md` updated to
  say so.

- **The Apple Silicon illegal instruction was a mask of zero, and not the G1's loop flag
  (Claude).** With the core's log now reaching the CI log, macOS printed the reason in one line:
  `Error: 50 - InvalidImmediate: tst w5, 0, block at PC 000102`. `jitblock.cpp` closes a loop body
  with `test_(lc, Imm(maxDoIterations - 1))`, and G1-Emu runs one iteration per block
  (`maxDoIterations = 1`, in `g1dsp.cpp` and in the test), so the mask is zero — which AArch64
  cannot encode as a logical immediate. asmjit refused the instruction, and in Release
  `AsmJitErrorHandler` only logs, so the block was left unfinished and the DSP ran into it. On x86
  `test r32, 0` encodes fine, which is why it only ever happened on ARM, and why the three earlier
  attempts, all of them rewriting the `FV` extension's encodings, could not have helped.
  `cmake/Dsp56300.cmake` now emits an unconditional jump for that mask, which is what the test
  means when it can never be false. Verification on x86-64: `g1dspcheck` passes, and the audio is
  unchanged — `g1patchtest` with `SimpleOSC.pch` still gives 261.5 Hz at −61.8 dBFS on outputs 1
  and 2 with the DSP links carrying two channels, the same figures as before the change. The macOS
  and `Linux arm64` jobs are what confirm it, and until both are green with the test gating this
  is not closed.

- **Task 1 is not done: the Apple Silicon crash is reopened, and the encoding theory is wrong
  (Claude).** The entries below that closed it cite CI run 35566113403, which is green only
  because the macOS `Test` step still had `continue-on-error`; its log ends in
  `g1dspcheck (ILLEGAL)` like the two gating runs after it
  ([35566928286](https://github.com/animatek/G1-Emu/actions/runs/35566928286),
  [35567844751](https://github.com/animatek/G1-Emu/actions/runs/35567844751)). The failing case is
  always `finite DO`, the first one that runs both places the G1 extension patches (the DO entry
  and `do_end`). The three fixes tried — a complemented mask, `BFC`, `BFI` with the zero register —
  all assumed AsmJit could not encode the immediate, and that is false: asmjit builds its arm64
  backend on any host, so the sequences were cross-assembled here on x86 and every form encodes
  with no error (words in `NOTES.md`, "The DSP JIT on ARM"). So the `#ifdef HAVE_ARM64` paths are
  removed from `cmake/Dsp56300.cmake` and the portable form is back; `g1dspcheck` now sends the
  core's log to stderr, flushed and prefixed `CORE:`, so the JIT errors that `AsmJitErrorHandler`
  only logs in Release survive in a CI log; and the matrix gains `Linux arm64 (native ALSA/JACK)`
  on `ubuntu-24.04-arm`, the same AArch64 JIT on a system that is not Apple's, to separate the
  code generated from what macOS does with it. `README.md`, `ROADMAP.md`, `NOTES.md` and
  `docs/next-steps.md` corrected accordingly. Verification: Release build and `g1dspcheck` pass on
  x86-64 locally; the cross-assembly probe is quoted in `NOTES.md`; the four-platform run with the
  arm64 job is next.

- **The ARM flag clear now uses the JIT's proven `BFI` form throughout (Codex).** The first gating
  run exposed that replacing the complemented immediate with AsmJit's `BFC` alias still left the
  finite-DO block illegal. Clearing `FV` now inserts the zero register with `BFI`, the same
  instruction form already used throughout Gearmulator's AArch64 JIT; restoring `LF`/`FV` also
  uses `BFI`. Verification: local Release `g1dspcheck` passes on x86-64; the corrected ARM form is
  going to the pinned alpha.13 runner next.

- **Task 1 is complete and the macOS DSP test is a gate again (Codex).** Removed the temporary
  `continue-on-error`, marked the Apple Silicon blocker done in `docs/next-steps.md`, and updated
  the roadmap and technical notes with the AArch64 cause and fix. Gearmulator remains external
  and is pinned to `mdmm-v0.1.0-alpha.13`. Verification: local Release `g1dspcheck` passes on
  x86-64; [CI run 35566113403](https://github.com/animatek/G1-Emu/actions/runs/35566113403)
  built on Apple Silicon and its actual `Test` step passed before the exception was removed. The
  final four-platform gating run is next.

- **Apple Silicon now uses AArch64 bit-field instructions for the G1 loop flag (Codex).** The
  split regression test showed that the crash happens in the first finite `DO`, not only in a
  nested loop: the G1 extension cleared `FV` with a sign-extended complemented immediate that
  AsmJit cannot encode as an AArch64 logical instruction. The ARM overlay now clears `FV` with
  `BFI` and the zero register, and restores the adjacent `LF`/`FV` pair with `BFI`; x86 keeps its existing mask path.
  Verification: the Release DSP test passes locally on x86-64 and in the pinned alpha.13
  Apple Silicon CI job ([run 35566113403](https://github.com/animatek/G1-Emu/actions/runs/35566113403)).

- **Gearmulator is updated and pinned to `mdmm-v0.1.0-alpha.13` in CI (Codex).** The workflow had
  already picked up alpha.13 implicitly from the dependency repository's default branch; it now
  names the release tag so later upstream changes cannot silently alter a G1-Emu build. The README
  records the tested version. The external clone is untouched. Verification: alpha.12 → alpha.13
  was reviewed (12 upstream commits; its DSP-core change is the ESSI/DMA pin already exercised by
  current CI), and the pinned build will run on Linux, macOS and Windows with the final ARM fix.

- **The Apple Silicon DSP failure is isolated and the nested-loop restore was split for diagnosis
  (Codex).** CI annotations prove that short MOVEM, JIT invalidation
  and both DO FOREVER cases pass on the arm64 macOS runner; the illegal instruction is raised by
  `nested DO` with a one-instruction JIT block. That case uniquely restores the outer loop's `LF`
  and `FV` together when the inner loop ends. A first experiment cleared, tested and restored the
  two bits separately. The synthetic test now separates a finite DO, nested finite DO and a DO
  FOREVER with a nested DO, so the next ARM run can distinguish saving `FV` from closing any inner
  loop. Verification so far: the expanded Release DSP test passes locally on x86-64; the fix is
  going to the macOS ARM CI runner next.

## 2026-09-20

- **Apple Silicon's DSP crash can now be pinned to one synthetic program (Codex).**
  `g1dspcheck` prints and flushes the name and JIT block size before each case, so a fatal signal
  on the macOS ARM runner leaves the exact last case in the CI log instead of only `ILLEGAL`; on
  GitHub Actions it also emits each case as a check annotation, which remains visible through the
  public API even when anonymous access to the raw log is unavailable. A basic NOP/JMP case runs
  first to distinguish a general aarch64 JIT startup failure from the G1-specific instructions.
  Verification: the Release test still passes locally on x86-64; Apple Silicon diagnosis is the
  purpose of the branch CI run.

- **A handover for whoever picks this up next (Claude).** New `docs/next-steps.md`: the short list
  in order, written so nobody has to reconstruct an evening of work before starting. The blocker
  first — the DSP56300 JIT raising an illegal instruction on Apple Silicon — with what
  `g1dspcheck` actually tests, the suspects (our own MOVEM and DO FOREVER extensions in
  `g1Lib/dsp56300.cpp` and `cmake/Dsp56300.cmake`, written against the emitter's shared mnemonics
  so they compile for both architectures), how to bisect it on CI without owning a Mac, and what
  the interpreter fallback would cost. Then what only the real machines can say, the audio device
  list in the settings window, the module defaults that belong in `../Nomad2026`, the 29 silent
  modules, and signing. It ends with the two traps that cost time today: two emulators answering
  to the same port name, and the real flash being one missing argument away. Linked from
  `AGENTS.md` and `ROADMAP.md`.

- **CI is green on Linux both ways and on Windows; macOS builds but its DSP raises an illegal
  instruction (Claude).** After the one-line fix, Windows compiles and passes, and so do both
  Linux jobs. macOS builds everything and then `g1dspcheck` dies with `ILLEGAL` on the Apple
  Silicon runner. The core does have an aarch64 JIT and our own extensions to it are written
  against the emitter's shared mnemonics, so they compile for both architectures and one of them
  is wrong only at run time — whose fault it is, ours or the core's, is the next thing to find
  out, with the interpreter as the fallback if the JIT cannot be fixed. Noted in `ROADMAP.md`.
  The macOS test step is allowed to fail meanwhile, so a real regression on the other three still
  turns the run red; the macOS **build** is not excused and still gates.

- **The first CI run answers the question: macOS builds the whole thing, Windows needed one line
  (Claude).** Four jobs, four failures, and all of them useful. **macOS compiled everything** —
  68k core, DSP cores, the JUCE audio and MIDI backend, the window — which was the real unknown
  and is now behind us. **Windows stopped at one of our own lines**: `g1mc.cpp` used
  `__builtin_ia32_pause`, a GCC and Clang builtin, behind a guard that tested the architecture
  (`_M_X64`) and not the compiler, and MSVC defines that too. Replaced by a `cpuPause()` that uses
  `_mm_pause` where it exists and `yield` on ARM, which also covers the Apple Silicon runners.
  And the three that did build failed their **test step for the same silly reason**: Gearmulator
  registers its own tests from the tree we add with `EXCLUDE_FROM_ALL`, so their binaries are
  never built and `ctest` reported nine "Not Run". Our test carries the label `g1` now and CI runs
  `ctest -L g1`. Verification: local build and `ctest -L g1` pass; the rest is for the next run.

- **A second backend on JUCE, so the other two systems stop being a leap in the dark, and CI for
  the three (Claude).** Audio and MIDI had gone straight to ALSA and JACK, which is why macOS and
  Windows did not compile. Now `-DG1_BACKEND=juce` puts both on JUCE — CoreAudio, WASAPI/ASIO,
  CoreMIDI, and virtual ports through `MidiOutput::createNewDevice` — and it is the default off
  Linux, where `native` stays the default because the JACK graph with the back panel's port names
  is worth keeping. New `app/audiobridge.h` holds the rate conversion between the G1's 96 kHz and
  the card's, the two lock-free queues and the dropout cushion, taken out of `jackaudio.h` and now
  shared by both backends, so they sound alike by construction. New `app/juceaudio.h` and
  `app/jucemidi.h`. `EmuHost` picks one at compile time, and the two things that only Linux has —
  the `/proc` figures and the raw MIDI card, which exists because the ALSA sequencer hides
  application ports from raw MIDI programs — are gated out. New
  `.github/workflows/build.yml`: Linux both ways, macOS and Windows, building and running CTest on
  every push, with no ROM anywhere near it. **Verification, and this is the point: the JUCE
  backend was tested here, on Linux**, where JUCE uses ALSA and creates virtual ports exactly as
  the other two systems do. It opens the card with four outputs, publishes `G1-Emu PC Port` and
  `G1-Emu MIDI`, takes the 326 KB of a captured editor session on the PC Port, answers with 10 KB
  and reaches -16 dB on outputs 1 and 2. One bug found and fixed on the way: JUCE gives every
  virtual port the same identifier on Linux, so a map keyed by it sent every message to whichever
  port was created last — the PC Port's traffic was arriving on the MIDI port. It is keyed by the
  device now. The native backend was checked to be unchanged by the refactor: `g1patchtest` still
  gives 261.5 Hz at -61.8 dBFS, and a live run still sounds. Both trees build and CTest passes.

- **The README says what a Nord Modular needs and does not come with, and any editor is welcome
  (Claude).** Two things a newcomer has no way to guess. **It starts empty:** the ROM carries the
  operating system and nothing else, so a fresh flash is built from the OS alone and every slot says
  `Empty patch` — real hardware left the factory with a bank and G1-Emu cannot give you that one;
  the patches are the user's to find among twenty-five years of community `.pch` files, or to make.
  **And making one needs an editor**, because the G1's panel edits parameters and not patches: on
  the real instrument the patch comes down the PC Port, and the emulator is no different. Said
  plainly that **any editor speaking the G1's protocol works** and that nothing here prefers one:
  Animatek NME is only the one tested first. Listed the original Clavia v3.03 — with [Stage
  Engine](https://www.stage-engine.com/), which packages it for current macOS with the Wine parts
  bundled, free with an optional donation, for the G1 and the Micro Modular (checked on the site) —
  Nomad/NMEdit, and nordmodulareditor.com, with an invitation to add any that is missing and to
  report it here when an editor speaks the protocol and G1-Emu answers badly. The status paragraph
  now says **Linux only, and that macOS and Windows do not compile**, which is the truth:
  `alsamidi.h`, `alsaaudio.h` and `jackaudio.h` are included unconditionally, `EmuHost` reads
  `/proc`, and the CMake has no platform branch.

- **Roadmap: what the three systems really cost, checked in JUCE instead of assumed (Claude).** Item
  5 now separates the two halves. The audio is the easy one: every backend needed is already in the
  JUCE 8.0.12 in `../Nomad2026/JUCE` (CoreAudio, WASAPI, ASIO, DirectSound, ALSA, JACK), with the
  caveat that our own JACK client names its ports like the back panel and connects itself, which
  JUCE's does not. The virtual MIDI ports are the hard one: macOS has them natively through CoreMIDI
  with nothing to install, Linux has them through the ALSA sequencer, and **Windows only through
  Windows MIDI Services** — `juce_Midi_windows.cpp` has three backends and only that one implements
  a virtual output; the flag is off by default, needs a minimum Windows SDK, and JUCE's own comment
  says it only worked on a Canary insider build when written, so it has to be tried on a real
  Windows 11 before anything is promised. Noted that the raw-MIDI split that forced the USB gadget
  here is Linux's alone, and that the plugin settles all three at once.

- **The ROM stops being a hard-coded path: G1-Emu looks for one, says what is wrong with what it
  finds, and offers the folder (Claude).** Until now both front ends took the ROM as an argument and
  checked only its size, which is no way to hand the thing to anyone else: G1-Emu ships no ROM and
  never will, so a new user's first screen is this one. New `g1Lib/g1rom.h` decides whether a file
  serves and, when it does not, why — not 512 KB (with the size it does have), 512 KB but no Nord
  Modular OS inside, or a Nord Modular OS that is the keyboard model's and not the rack's, told
  apart by the model byte at `$7FF` that the OS itself reads at boot (`NOTES.md`, "The panel"). New
  `app/romfinder.*` looks, in order, at the path on the command line, `rom = ...` in the settings
  file, `<Documents>/Animatek/G1-Emu/roms` (honouring the user's XDG document folder, which is not
  called "Documents" in every language), `roms/` next to the flash, and `Roms/` in the current
  directory and in the source tree, so a clone still works with no setup. A ROM named on the command
  line is an order: if it does not serve the emulator stops and says so, instead of starting on a
  different one, which would look like it worked. The one in the settings is a preference and falls
  back to the search. The window offers **Open the folder** and **Choose a ROM file...**, starts as
  soon as it has one, and the settings window gains a **ROM** section on top with the file in use, a
  picker that refuses a file with the reason and a button to the folder; the console prints the
  whole story and exits. `G1_ROM` overrides everything, and the ROM argument of `g1run` and `g1gui`
  is now optional. Also: the window's startup log was never flushed, so it only appeared on exit.
  Verification: found in the repo with no argument at all; and the four ways it goes wrong, each
  giving its own line — a 100 KB file, a 512 KB file of noise, a copy with the model byte set to 0,
  and a path that does not exist. The settings window was checked on screen with its ROM section,
  and the picker opens on the ROM folder filtering `*.bin`. Release build and CTest 1/1. Two labels
  left over from the snd-virmidi days reworded.

- **One G1 in the DAW's MIDI list instead of thirty-two entries: the card is a USB MIDI gadget now
  (Claude).** `snd-virmidi` was the wrong card: it hard-codes sixteen subdevices per device and the
  name "Virtual Raw MIDI", and no module parameter changes either, so it filled Bitwig's list with
  `Virtual Raw MIDI/1..16` — and `midi_devs=2`, suggested here earlier to get a second port, doubled
  it to thirty-two. Replaced by `dummy_hcd` + `g_midi`, stock in-tree kernel modules that take the
  port count and the name as parameters: `modprobe g_midi id=G1 iProduct=G1 in_ports=1 out_ports=1`
  gives one port with a name of our own. The gadget is plugged into an emulated host, so ALSA gets
  two cards, one per side of the virtual cable: `G1` (`f_midi`) is the emulator's and `G1_1` (`G1
  MIDI 1`) is the DAW's. Two entries is the floor for stock modules; one would need a driver of the
  G1's own. `AlsaMidi::findPorts` becomes `findCardPorts`, which matches **by sound card instead of
  by client name** — the name was `snd-virmidi`'s and no other card has it — and returns each port's
  name, so the log says what it linked. `bindRawMidi` now gives the card's first port to the MIDI,
  which is all a DAW wants, and the second one, if there is one, to the PC Port. The default card ID
  goes from `G1Emu` to `G1`. `docs/bitwig-midi.md` rewritten around the gadget, with the table of
  which side is whose. Verification, live: the cable on its own (`amidi -p hw:6,0 -S "90 3C 64"` on
  the host side comes out of `amidi -p hw:5,0 -d` on the gadget side), then the emulator reporting
  `raw MIDI: MIDI <-> f_midi` and its MIDI input counter moving by 6 bytes for two notes sent the
  way Bitwig sends them. Release build and CTest 1/1.

- **Why the DrumSynth does not sound: it is born inaudible, and the fault is not the emulator's
  (Claude; documentation only).** Measured on the emulator by sweeping `MLevel` and `SLevel`
  together: 0 → −107 dBFS (the 24-bit floor), 25 → −102, 40 → −89.6, 60 → −76.4, 80 → −66.3, 100 →
  −58.6, 127 → −50.9 — an ordinary exponential level law of about 0.45 dB per step. An oscillator
  measures −62, so at 100 the DrumSynth is the loudest module there is and at its default of 25 it
  sits 40 dB below an oscillator: nothing. The default is the problem, and it is a placeholder: in
  NME's whole `modules.xml` the value 25 appears twelve times and all twelve are this module's,
  while `MTune` and `STune` have no default at all and go up as 0. Every other module that flattens
  its defaults to one value picks one that means something (OscA 64, FilterBank 127, Mixer (8) 100).
  Six more modules fall into the same trap — a level with no default goes up as 0, which is mute:
  `4-1Switch` (all four levels), `1-4Switch`, `Multi-Env`, `OscC`, `EqShelving` and `RingMod`; the
  two switches are born silent. The fix is in `Nomad2026/data/modules.xml`, not in this repo.
  Verification: seven runs of `tools/battery/battery.py --only DrumSynth --param 4=v --param 5=v`,
  plus a count of every `defaultValue` in the file (260 of 515 parameters have one).

- **The notice stops stopping every startup: it moves into the settings window (Claude).** Javier
  asked for it. It is shown at startup **only on the first run** — when there is no settings file
  yet — and answering it writes the file, so it does not come back; the "Don't show this again" tick
  is gone, because closing it is the answer. In the settings window there is now a **Notice**
  section with the text always readable and a "Show the notice below at startup" switch to put it
  back. The flag lives in `settings.conf` as `showDisclaimer`, so the window no longer keeps a
  second settings file of its own (`juce::ApplicationProperties`, which was writing to
  `~/.config/.G1-Emu/` and is why ticking the old box never seemed to work): everything is in one
  place. `Options::load` now says whether the file was there, which is what "first run" means.
  Verification: a round-trip of `save`/`load` through a scratch build (every field back, a missing
  file reports false and leaves the defaults standing); with `showDisclaimer = 0` the window opens
  straight into the panel, with no settings file at all it shows the notice once. The settings
  window was checked on screen and its layout fixed twice: the notice box was cut off and there was
  dead space under it.

- **Settings window, and the options stop being environment variables only (Claude).** New
  `app/gui/Settings.*`, opened from a **Settings** button next to the status bar: audio driver
  (JACK/PipeWire, ALSA, none), ALSA device, output level, whether outputs 1/2 connect themselves to
  the sound card, and which `snd-virmidi` card is taken over for raw MIDI, plus a live read-out of
  what is actually in use. `EmuHost::Options` holds them and is read from
  `~/.local/share/Animatek/G1-Emu/settings.conf`, a plain `key = value` file that `g1run` reads too,
  so the window and the console agree. Order: defaults, file, then the `G1_*` variables, which still
  win — the scripts and `g1patchtest` keep working untouched. The level applies while it plays (both
  backends read the gain from an atomic now, and `JackAudio` takes auto-connect as an argument
  instead of reading the environment); the rest, on the next start, because the audio callback runs
  on the DSP thread and swapping a driver under it is not worth the race. `G1_THREADS` and
  `G1_INTERP` stay environment-only: they are core debugging knobs. Verification: Release build and
  CTest 1/1; with `audio = no` in the file the console reports no audio, with `audio = alsa` it
  opens ALSA "default", with `audio = hw:2,0` it reports the device is busy, and `G1_AUDIO=jack
  G1_GAIN_DB=30` over the same file gives JACK at +30 dB. The window was seen running with the
  Settings button in place; the panel's own layout has not been looked at on screen yet. Also
  shortened the raw MIDI text in the status bar, which wrapped it onto two lines; the full hint
  stays in the log.

- **The emulator takes over its own raw MIDI card; the helper script is gone (Claude).** Bitwig on
  Linux reads raw MIDI devices and never looks at ALSA sequencer ports (checked: its engine has
  `PipeWireAudioHostApiPlugin.so` loaded for audio and `libasound` open on `/dev/snd/midiC5D0` for
  MIDI), so `G1-Emu:PC Port` and `G1-Emu:MIDI` are invisible to it and a kernel-made device is
  unavoidable. Instead of an external helper plus `aconnect`, `EmuHost` now finds the `snd-virmidi`
  card whose ID is `G1Emu` itself and links its device 0 to the PC Port and device 1 to the MIDI, in
  both directions, retrying every two seconds so the card may be loaded afterwards (`G1_RAWMIDI`
  picks another card, `0` disables it). New `AlsaMidi::findPorts` and `AlsaMidi::link`; the status
  bar and the log say which devices are linked. Removed `tools/bitwig-midi.py` and rewrote
  `docs/bitwig-midi.md`. Verification: Release build; with the card as it is loaded now
  (`midi_devs=1`) the emulator reports `raw MIDI: MIDI <-> hw:5,0`, `aconnect -l` shows `36:0`
  subscribed both ways to `128:1` with no helper run, and `amidi -p hw:5,0 -S "90 3C 64"` reached
  the emulator (its MIDI input counter moved). The two-device case needs the card reloaded with
  `midi_devs=2`, which Bitwig was holding open: not verified yet.

- **The emulator does sound: what went silent was the routing (Claude; no code change).** Javier
  reported no sound since the MIDI work. Checked in three ways: `g1patchtest` with `SimpleOSC.pch`
  gives 261.5 Hz at -61.8 dBFS on outputs 1 and 2 with the links between the four DSPs carrying
  signal; a live `g1run` on a copy of the flash boots with all four DSPs on at 100% speed and
  auto-connects `out_1`/`out_2` to the Komplete Audio 6; and replaying the captured PC Port session
  (`pcport-in.bin`, NME's own traffic) into it brings the outputs to -25 dB. Two things did explain
  silence: the `aconnect` subscription from the virtual card dies every time the emulator exits and
  had to be re-run by hand (it was not there at the start of this session), and after boot with no
  editor connected the active slot holds no patch, so a note plays nothing.

- **Post-reboot recovery and build repair (Codex).** Moved the existing PC-trail size declaration
  before the array that uses it, fixing compilation of the pending diagnostic changes without
  removing them. Verification: full Release build, CTest (1/1), and `git diff --check` passed. A
  12-second run with temporary factory flash and audio disabled exposed both ALSA ports, ran all
  four DSPs at 99.9–100% speed, and exited cleanly. The current boot has no matching kernel oops and
  the experimental driver is not loaded. Audio playback, NME and the single-endpoint Bitwig
  requirement remain unverified in this session; no user flash or physical MIDI connections were
  changed.

- **Failed single-port kernel bridge experiment withdrawn (Codex).** A modified Linux virtual MIDI
  bridge built successfully with Clang for 7.2.5-1-cachyos but faulted during insertion on the
  maintainer's host: the kernel logged a null-pointer page fault in `dev_driver_string`, followed by
  another insertion fault. No G1Emu card was registered. Build success was not a sufficient
  validation; this should have been tested in an isolated VM first. Advised saving work and
  rebooting, without forced unload or another insertion; no boot-time installation was made. Removed
  the experimental driver/build from the repo and restored the helper to the previously tested stock
  snd-virmidi bridge. A single named Raw MIDI endpoint remains unresolved. Verification: live ALSA
  enumeration, kernel journal, helper syntax and `git diff --check`; recovery after reboot has not
  yet been verified.

- **Instance and VST3 hosting plan (Codex).** Javier confirmed the Bitwig bridge appears and plays,
  then requested one named G1Emu performance port per instance and a shared standalone/VST3
  direction. Added `docs/instance-hosting.md`, linked from the roadmap and bridge guide, covering
  engine/host separation, independent state, optional editor endpoints, native plugin MIDI/audio and
  acceptance gates. Verification: inspected EmuHost's shared default flash/temp/log paths and device
  ownership; Linux driver source confirms hard-coded 16 input/output substreams and the Virtual Raw
  MIDI name. This is a design proposal; endpoint reduction/renaming and VST3 are not implemented.
  Documentation links and `git diff --check` verified; no runtime code changed.

- **Bitwig Raw MIDI bridge for issue #1 (Codex).** Added `tools/bitwig-midi.py` and
  `docs/bitwig-midi.md`: a dedicated `snd-virmidi` card exposes a Raw MIDI device to Bitwig and the
  helper connects only its output to `G1-Emu:MIDI`. It discovers current client/card numbers,
  accepts an existing connection and refuses missing or ambiguous clients. NME keeps its separate PC
  Port. Corrected the ALSA header's claim that every DAW sees sequencer ports. Verification: Python
  compilation and `git diff --check`; live ALSA test with the user's newly loaded G1Emu card
  (hw:5,0), two helper runs creating one subscription, and Note On/Off sent through Raw MIDI: the
  temporary-flash, audio-disabled emulator reported MIDI input increasing from 0 to 6 bytes while PC
  Port input stayed 0. Missing-emulator refusal checked after stopping it. Bitwig's device picker
  and audible playback are still awaiting user verification.

- **A module battery worth the name: 80 of the 109 types give a signal (Claude).** New
  `tools/battery/battery.py`: it builds a patch per module type with what each one needs to show
  signs of life — an oscillator on its audio inputs, an LFO on its control ones, a running clock
  on its logic ones and a sine into the G1's inputs — and sends its first four outputs to the four
  outputs, which `g1patchtest` measures. Two things it learned the hard way: a clock on a reset or
  a sync input freezes the module (so those are left alone), and a parameter that `modules.xml`
  leaves without a default is uploaded as 0, which mutes a level or an amount (so the battery
  opens those up and says which). `g1patchtest` now reports the mean and the drift of each output,
  which is what makes a slow signal visible at all: the OS caps the volume at −36 dB, so full
  scale is around −62 dBFS. **61 sound, 19 move, 12 hold a level, 17 give nothing** (was 56 of 101
  before), in `docs/module-battery.md`. Silence is a list to look into, not a verdict: of the
  first ones looked at, Constant is bipolar and 64 is its zero, DrumSynth with its levels open is
  the loudest module measured (−51.7 dBFS against the −62 of an oscillator), AudioIn only needed a
  signal in the inputs and MasterOsc has nothing but a master-slave output.

- **What each panel button does, and the System menu (Claude).** Watched on the emulator, each
  against a control run. The **navigator is row 1**: right and left walk a menu line, down goes
  into the item, and in the Edit pages they walk the morph groups and a module's parameters; row 2
  does none of that. **Shift** is the second function of another key: Shift + Store opens
  `Store settings` (the panel's "Save Synth. Settings") and Shift + a slot shows and changes that
  slot's voices. **Find**, held, puts `Find` on the display. **Assign/Morph is the only key with
  no known effect**: alone or with Shift, on the patch screen, the Morph page, a parameter page or
  the System menu, before or after moving a knob or the dial, nothing changes on the display, the
  LEDs or the traffic to the editor, though the OS does take the key. Also written down: the whole
  System menu, from the OS's table at `$1442EE`. `g1patchtest` grew a gesture language — a step of
  `G1_PRESS` can now be a knob (`k5=200`) or the dial (`d3`), and what the OS says to the editor
  during the gesture is printed — plus `G1_HOLD_END`. Check: `1.2,1.6,k5=200,2.6` and its control
  without the key give identical output; audio and CTest as before.

- **Oct Shift belongs to the keyboard, and how Panel Split shares out the knobs (Claude).** The OS
  keeps an octave shift per slot (`$1C3AB8 + slot`, −2 to +2) which travels in the patch header,
  and lights one of the five LEDs for it: 0.0 = −2, 1.0 = −1, 2.0 = 0, 3.0 = +1, 3.1 = +2. On the
  rack it does none of that: the routine is gated on the model byte, read at boot from `$7FF` of
  the ROM, which is `$01` in the rack's. Panel Split (flag `$18C0E4`, 0 = on) gives knobs 1–6 to
  slot A, 7–12 to B, 13–15 to C and 16–18 to D, each renumbered from 1, through two tables at
  `$145A94` and `$145AA6`. Two new probes in `g1patchtest`: `G1_MIDINOTE=channel` (the note through
  MIDI IN, not the PC Port) and `G1_PEEK=addr,...`. Check: uploading a patch with `OctShift` 0, 2
  or 4 leaves `$FE`, `$00` or `$02` in `$1C3AB8`, so the value arrives, but the note comes out at
  262 Hz in all three, from the editor and from MIDI IN alike, and no code outside the front
  panel's module reads the variable; with the split on, only knobs 1–6 still reach the patch in
  slot A and 7–18 go silent.

- **Every panel button identified, and the dial works (Claude).** The OS only reads **bits 2 to 7**
  of each of the three matrix rows: 18 buttons, not 24. Bits 0 and 1 are the **dial**, a quadrature
  encoder the OS decodes in its main loop (`$104DC6`) with four edges per step and its own
  acceleration; `Microcontroller::turnDial()` emulates it and the window's dial turns it by dragging
  or with the wheel. The six buttons that were left (matrix row 2) are Panel Split, Find, Oct down,
  Oct up, Assign/Morph and Shift, in that order, and they are wired in `g1gui`. The names come from
  the factory test's tables, which sit in the flash before the OS ($9962 the key codes, $9986 the
  names, $9A16 the 32 LEDs, $9A56 the 20 ADC channels with their names): its codes are the OS's plus
  six, which the twelve buttons already known confirm. `$18` is the **pedal** input, no longer a
  guess. New probes in `g1patchtest`: `G1_PREPRESS`, `G1_HOLD` (a held modifier) and `G1_DIAL`.
  Check: holding 2.3 puts `Find` on the display and 2.2 lights LED 3.2; turning the dial on the
  Morph screen moves its value up and down, and faster turns move it further, as the OS intends;
  audio, CTest and the rest of the panel behave as before.

## 2026-09-19

- **An open invitation to collaborate (Claude).** New section at the top of the README ("You are
  invited: this is a collaborative project") and a warmer opening in `CONTRIBUTING.md`: the project
  is meant to be built together and everyone is welcome, whatever their experience. Check: both
  files reviewed.

- **Disclaimer at startup and in the README (Claude).** `g1gui` shows a notice when it opens: an
  independent project not affiliated with Clavia DMI, no ROMs now or ever, and no support. It has a
  "Don't show this again" box, saved in the user settings (`~/.config/G1-Emu.settings`). The same
  text opens the README ("Please read this first") and is summarised in `CONTRIBUTING.md`; the
  missing-ROM error now says that no ROM is provided. Check: built, the dialog opened and reviewed.

- **The whole repo in English (Claude).** Documentation, code comments, program messages and the
  changelog translated; `NOTAS.md` → `NOTES.md` (rewritten as a technical reference, organised by
  topic) and `SIGUIENTES-PASOS.md` → `ROADMAP.md` (updated). New `CONTRIBUTING.md`. The language
  rule is now in `CLAUDE.md` and `AGENTS.md`. Check: full build, CTest, `g1patchtest` and `g1run`
  behave as before; no Spanish left in tracked files.

- **Panel screenshot in the README (Claude).** `docs/g1gui.png`, the `g1gui` window cropped, at the
  top of `README.md`. Check: image reviewed (only the window, no background).

- **Panel like the hardware, knobs in place and repo ready to go public (Claude).** The ADC returned
  the selected channel instead of the previous conversion: every knob was shifted by one and the
  runtime volume was read from another channel; fixed. Identified with `g1patchtest` (new
  `G1_KNOBS`, `G1_ADCSWEEP`, `G1_LEDSTATE`, `G1_PRESS`): the 18 knobs and their LEDs, the slot and
  mode LEDs, Edit, Patch/Load and the navigator. The window, redone from photos of the hardware:
  display with the HD44780 dot font, red and black knobs with the number under the LED, Panel
  Split, Find/Panic, Oct Shift, Assign/Morph, Shift, the dial and a MIDI LED; the raw matrix strip
  is gone. `LICENSE` (GPLv3), `README.md` and the changelog rule in `CLAUDE.md` and `AGENTS.md`.
  Check: ADC sweep with the 18 knobs assigned, 18 LED probes and 24+36 button probes, audio as
  before, window screenshots; history reviewed (no ROMs).

- **Patreon announcement drafts (Codex; private, not in the repo).** A bilingual draft (English
  first) of the announcement, including the planned multi-G1 editing in NME. The folder is excluded
  through `.gitignore`. Check: `git check-ignore -v` confirms the exclusion and `git ls-files` does
  not list the draft.

- **The window: first panel in JUCE (Claude, commit `46c7b8d`).** `EmuHost` moves `g1run`'s loop
  (flash, MIDI, JACK/ALSA, real time and statistics) into a class with its own thread; `g1run`
  becomes a thin console and now reports the load (~55%) and the cores (~2.9). `g1gui`
  (`./g1gui.sh`): display with the CGRAM custom characters, 18 knobs and volume (ADC), the
  identified buttons and LEDs, a raw matrix view and a status bar. JUCE is included once in the
  main CMake. Check: built, `g1run` 10 s over JACK as before, window open with the G1 display
  ("Empty Patch", voices per slot) and the slot A LED.

- **The panel, emulated (Claude, commit `f1e7573`).** HD44780 LCD (`$202006/7`), 32 LEDs in 4 rows
  and a 24-button matrix (`$202004/5`, `$201800`), with an API for a front end. Identified A–D,
  Store and System. Check: `g1patchtest` shows the G1 display (patch name and voices per slot) and
  reacts to buttons (System menu, Store, slots).

- **Control-rate modules, audio inputs and four outputs (Claude, commit `f1e7573`).** Gearmulator's
  JIT did not follow changes of the LA register, and that is how the OS extends the main loop when
  it loads control-rate modules: envelopes, clocks, master/slave, the chorus LFO and the overdrive
  amount stood still. Now it resynchronises (`onLaChanged`). Two missing DMA modes (fixed→fixed and
  block per request without clearing DE) bring the audio inputs. The outputs were swapped in pairs
  (1↔2, 3↔4). `g1run` over JACK with `out_1..out_4` and `in_L`/`in_R`. New test bench
  `g1patchtest`. Check: battery of the 101 module types, A/B of the chorus (L≠R) and the overdrive
  (follows its knob), AudioIn with different sines on L and R, 4Output with four signals, `g1run`
  over JACK for 16 s without dropouts, CTest.

- **The level, explained (Claude, commit `f1e7573`).** The rack OS caps the master volume at
  −36 dB: it takes it from a 128-entry table (`$153CAC`) indexed with ADC ÷ 2, at boot and when the
  knob moves. The −62 dBFS of an OscA → 2Output is what the OS computes; the emulator loses no
  level, and `g1run`'s +36 dB undo that cap. New `G1_FINDTX` and `G1_ADCALL` in `g1boot`. Check:
  CPU trace down to DSP 3's `Y:$5F`, table read from the ROM, and a replay with the 20 ADC channels
  at maximum (same level).

- **Clean sound: real DSP clock and 9-word links (Claude, commit `79e16aa`).** The DSPs run at
  82.944 MHz (864 cycles per sample, from the OS's `PCTL`) with IRQD on a fixed common grid; the
  ESSIs at the rate derived from their CRA (96 cycles per word on the links). The link between DSPs
  works by position (each word goes to the receive ring slot the DMA will write, from the block 8
  blocks ago) and the output is read from DSP 3 block by block. The steps and clicks came from
  there: the link moved 2 of the 9 words per sample and the channels shifted. `g1run` adds +36 dB
  by default. New `G1_BLOCKS` in `g1boot`. Check: built, CTest, replay with FFT (C at 261.6 Hz on
  1/2, harmonics at −82 dB, a single jump in the whole run) and `g1run` at 100% real time. Tested
  with NME by the maintainer: no noise.

- **Real-time audio through the sound card (Claude, commit `0723e40`).** `g1run` plays outputs 1/2
  through ALSA (`app/alsaaudio.h`, 48 kHz; `G1_AUDIO`, `G1_GAIN_DB`). The four DSPs run on their own
  threads, and their audio goes from one to the next on the CPU thread when all have stopped: 100%
  real time (was ~79%), replay 2.2× faster, same audio as serial (`G1_THREADS=0`). Check: built,
  CTest, replay in both modes with FFT and dropout counts, `g1run` for 15 s.

- **It sounds at the output (Claude, commit `59287f1`).** The audio goes through the chain
  DSP0→1→2→3 and DSP 3 plays the note's C at 261 Hz. Three fixes: the DMA with dual counters on
  source and destination (missing in Gearmulator, it copied nothing), immediate block transfers (the
  copy arrived late and overwrote the voice) and the master volume, which the OS reads from the
  panel ADC (code `$30`) and the emulator returned as 0. Check: everything built, CTest, a 60 M
  instruction replay and FFT of the four DSPs' output.

- **First audio from a patch (Claude, commit `5450cd8`).** Codex's replay was silent because of the
  replay itself: in the recorded session NME reconnected to a rebooted G1, so the second upload got
  PID 1 again; in the replay the OS gives PID 2 and drops the following messages (modules and
  note). `g1boot ... replay` now rewrites the PID with the one the OS assigns (ACK `$36`) and redoes
  the checksum. With that, DSP 0 loads the modules, links their code at `P:$197` and sends a
  periodic 261 Hz wave out of ESSI0. `G1_TAP=file` dumps what leaves each DSP's ESSI0. Check:
  everything built, CTest passes, 60 M instruction replay with 19 rewritten messages, FFT.

- **JIT extensions validated (Codex).** Short MOVEM and DO FOREVER in the build copy, with one DO
  iteration per dispatch. Fixed SR initialisation and accumulator reading in `g1dspcheck`,
  registered in CTest. CMake includes only the needed cores and the MIDI bridge, so nothing is
  generated inside the external Gearmulator clone. Check: `g1boot`, `g1run`, `dspdis` and
  `g1dspcheck` built; CTest passes MOVEM, JIT invalidation, DO FOREVER, IRQD and nested DO
  (blocks of 1/32).

- Plan for the next session (getting audio out, performance) and the maintainer's ideas (use the
  emulated G1 to improve NME, recreate modules from their DSP code, a patch as a plugin).
  `AGENTS.md` for Codex/opencode (commit `fd85567`).
- `g1run` no longer always records a WAV: only with `G1_RECORD=seconds`. One night without a limit
  had reached 35 GB.

- **The OS loads the patch code into the DSPs and the oscillator computes (commit `fe4c24d`).**
  Five chained problems fixed: the system clock (the SIM's PIT, not emulated by Gearmulator), an
  excessive wait in HI08 status polls, host-command arbitration (incompatible with the G1's fast
  interrupts), the interrupt queue filling up in a single thread, and IRQD ignoring the IPRC. The
  ESSI slot masks start as on the chip (all enabled).
- `g1boot`: the replay sends messages spaced out like NME; new `diff` mode, and dumps of the ESSIs,
  serviced vectors, processing mode and memory changes of each DSP.

- **NME connects to the emulated G1 and builds patches** (OscA → 2Output; the OS confirms
  everything and even reports the DSP load). No sound yet.
- Audio: 96 kHz ESSI clock, IRQD as the processing clock, meters and WAV recording of DSP 3's
  output in `g1run`, and a log of everything coming in through the PC Port (`pcport-in.bin`) to
  replay sessions.
- Fixed DSP 3's double boot: its sound program was left incomplete. Pending words now go to the
  boot ROM, and when the CPU polls the status with a pending word, the DSP runs until it takes it,
  so the OS does not drop it.
- `g1boot ... replay FILE`: replays an NME session without NME and dumps DMA, buffers and the
  output of each DSP. `G1_WATCH`: watchpoints in the OS code.

## 2026-09-18

- **`g1run` / `g1.sh`: the emulated G1 in real time with virtual MIDI ports (commit `e852a34`).**
  The ALSA client "G1-Emu" has two ports, "PC Port" (editor) and "MIDI". The flash is saved in
  `~/.local/share/Animatek/G1-Emu/flash.bin` on exit and whenever the OS writes to it. Tested with
  `aseqsend`/`aseqdump`: an IAm on the PC Port gets its answer. ~94% of real time.

- **The emulated G1 answers NME's handshake (commit `5e84273`).** Two ports, like the hardware:
  MIDI IN/OUT is the CPU's SCI (connected with `SciMidi`), and the editor's PC PORT is an external
  SCN2681 DUART on a parallel bus built from the GP port and port E. `g1Lib/g1duart.h` emulates it,
  and the byte-received notification (RxRDY → PAI → PAOV interrupt, vector IVBA+`$A`) is emulated in
  the CPU, because Gearmulator's GPT lacks the pulse accumulator. To the IAm
  `F0 33 00 06 00 03 03 F7` it answers `F0 33 00 06 01 03 03 3F 7F 7F 01 F7`.

- **The four DSP56303s boot with the OS program (commit `10f108f`).** There are 8 HI08 ports and
  the four on the main board have a DSP behind them; the HF flags go back and forth, host commands
  work and the jump to `$FF0000` returns to the boot ROM. DSP 3 boots twice (loader + OS), like the
  hardware. ~88% of real time. New tool `dspdis`.

- **OS 3.03 boots on the emulated 68331 and reaches its main loop (commit `0b2829c`).** `g1Lib`
  (CPU with ROM, RAM and flash) and `tools/g1boot` (headless boot, log of accesses to unknown
  hardware, chip-selects and disassembler). Found on the way: the loader picks the mode from the
  keys held at power-on, the OS is copied from the flash at `$300000` to RAM, and the four DSPs are
  at `$200000`/`08`/`10`/`18` over HI08. The flash is emulated as an AMD Am29F080, one of the three
  chips the OS accepts; on first boot it formats the patch area.

- New project, separate from `Elektron-Emu` (commit `6fa939e`). Analysis of the rack OS 3.03: the
  CPU is a 68331 (confirmed by the GPT/SIM/QSM accesses), the DSPs are 56303s and `$50000`–`$5FFFF`
  looks like the DSP code. The template is Gearmulator's Nord Lead 2X.
