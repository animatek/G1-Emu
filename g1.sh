#!/bin/bash
# Runs the emulated G1 in real time with its virtual MIDI ports.
#   ./g1.sh            (ROM in Roms/, flash in ~/.local/share/Animatek/G1-Emu/flash.bin)
# The DSP log is filtered out: only the status is shown.
cd "$(dirname "$0")"
exec ./build/app/g1run Roms/NORD-MODULAR-RACK-VER-3.03.BIN "$@" 2>/dev/null \
	| grep --line-buffered -E '^\[|^flash|^new flash|^MIDI ports|saved|^ +audio:|^audio'
