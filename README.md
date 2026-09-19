# G1-Emu

![G1-Emu: el panel del Nord Modular G1 emulado](docs/g1gui.png)

**Emulador del Nord Modular G1** (rack, OS 3.03) sobre el núcleo de
[Gearmulator](https://github.com/dsp56300/gearmulator): el sistema operativo original del aparato
corriendo en un 68331 emulado y cuatro DSP56303 emulados, controlado desde
[Animatek NME](https://github.com/animatek/Animatek-NME) como si fuera el sinte de verdad.

> *An emulator of the Nord Modular G1 (rack, OS 3.03) built on the Gearmulator core: the original
> OS runs on an emulated 68331 and four emulated DSP56303s, and Animatek NME edits it like the real
> thing. Pre-alpha. You need your own ROM. Docs are in Spanish.*

**Estado: pre-alfa.** Arranca el OS, NME se conecta por el PC Port y sube patches, y suena limpio
en tiempo real: osciladores, filtros, envolventes, relojes, efectos (chorus, overdrive...), cuatro
salidas y dos entradas. Tiene ventana con el panel (pantalla, mandos, botones y LEDs). Faltan
cosas: algunos botones del panel, la rueda y probar módulo a módulo. El detalle técnico está en
[`NOTAS.md`](NOTAS.md), el plan en [`SIGUIENTES-PASOS.md`](SIGUIENTES-PASOS.md) y lo que cambia en
[`CHANGELOG.md`](CHANGELOG.md).

## Lo que no viene

- **Ninguna ROM ni firmware.** Hace falta la ROM de 512 KB del Nord Modular rack con el OS 3.03
  (`Roms/NORD-MODULAR-RACK-VER-3.03.BIN`), sacada de un aparato propio o de la actualización
  oficial. `Roms/` está fuera de Git y así debe seguir.
- Este proyecto no tiene relación con Clavia DMI. «Nord» y «Nord Modular» son marcas de Clavia;
  aquí solo se usan para decir qué aparato se emula.

## Compilar

Linux (probado en Arch/CachyOS con PipeWire). Hace falta:

- Un clon de [gearmulator-md-mm](https://github.com/joelanders/gearmulator-md-mm) en
  `~/src/gearmulator-md-mm` (o `-DGEARMULATOR_DIR=...`). No se modifica: las correcciones del núcleo
  que necesita el G1 se aplican a una copia al compilar (`cmake/Dsp56300.cmake`).
- ALSA, y JACK (pipewire-jack) para las cuatro salidas y las entradas.
- JUCE para la ventana y el banco de pruebas: por defecto el de Animatek NME al lado
  (`../Nomad2026/JUCE`), o `-DG1_JUCE_DIR=...`. Sin JUCE se compila solo la consola.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Usarlo

```bash
./g1gui.sh   # con el panel en una ventana
./g1.sh      # en la consola (Ctrl+C guarda y sale)
```

- **MIDI** (ALSA): cliente **G1-Emu** con dos puertos, como el aparato: **PC Port** (el editor:
  en NME se elige como entrada y salida) y **MIDI** (notas y controladores, por ejemplo desde un DAW).
- **Audio** (JACK): `G1-Emu:out_1..out_4` e `in_L`/`in_R`; `out_1`/`out_2` se conectan solos a la
  tarjeta. Sin JACK, salidas 1/2 por ALSA.
- La flash (OS instalado y patches guardados) se guarda en `~/.local/share/Animatek/G1-Emu/flash.bin`.
- El nivel sale bajo porque el propio OS limita el volumen maestro a −36 dB; se compensa con
  +36 dB (`G1_GAIN_DB`). Más variables en [`CLAUDE.md`](CLAUDE.md).

## Licencia y créditos

GPLv3 (ver [`LICENSE`](LICENSE)), porque se enlaza con Gearmulator. Gracias a The Usual Suspects
por [Gearmulator](https://github.com/dsp56300/gearmulator) y a joelanders por el fork con la
Monomachine y la Machinedrum, que es la base de este trabajo. Hecho por Animatek
([animatek.net](https://animatek.net)).
