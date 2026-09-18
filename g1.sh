#!/bin/bash
# Arranca el G1 emulado en tiempo real con sus puertos MIDI virtuales.
#   ./g1.sh            (ROM en Roms/, flash en ~/.local/share/Animatek/G1-Emu/flash.bin)
# El log de los DSP se filtra: solo se ve el estado.
cd "$(dirname "$0")"
exec ./build/app/g1run Roms/NORD-MODULAR-RACK-VER-3.03.BIN "$@" 2>/dev/null \
	| grep --line-buffered -E '^\[|^flash|^puertos|guardada'
