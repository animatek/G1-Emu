#!/bin/sh
# The emulated G1 with its panel in a window (JUCE). Same ports and audio as ./g1.sh.
#   ./g1gui.sh            (ROM in Roms/, flash in ~/.local/share/Animatek/G1-Emu/flash.bin)
cd "$(dirname "$0")"
# On macOS the JUCE app is a bundle, not a bare binary next to it.
bin=./build/app/gui/g1gui_artefacts/Release/G1-Emu
[ -x "$bin.app/Contents/MacOS/G1-Emu" ] && bin="$bin.app/Contents/MacOS/G1-Emu"
exec "$bin" Roms/NORD-MODULAR-RACK-VER-3.03.BIN "$@" 2>/dev/null
