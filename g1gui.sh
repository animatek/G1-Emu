#!/bin/sh
# El G1 emulado con su panel en una ventana (JUCE). Mismos puertos y audio que ./g1.sh.
#   ./g1gui.sh            (ROM en Roms/, flash en ~/.local/share/Animatek/G1-Emu/flash.bin)
cd "$(dirname "$0")"
exec ./build/app/gui/g1gui_artefacts/Release/G1-Emu Roms/NORD-MODULAR-RACK-VER-3.03.BIN "$@" 2>/dev/null
