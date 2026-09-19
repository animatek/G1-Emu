#!/bin/sh
# The emulated G1 with its panel in a window (JUCE). Same ports and audio as ./g1.sh.
#   ./g1gui.sh            (ROM in Roms/, flash in ~/.local/share/Animatek/G1-Emu/flash.bin)
cd "$(dirname "$0")"
exec ./build/app/gui/g1gui_artefacts/Release/G1-Emu Roms/NORD-MODULAR-RACK-VER-3.03.BIN "$@" 2>/dev/null
