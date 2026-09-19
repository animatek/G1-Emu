# G1-Emu

Emulación del **Nord Modular G1** sobre el núcleo de Gearmulator: el OS original
del G1 corriendo en un 68331 y unos DSP56303 emulados, y tocado desde **Animatek NME**
(`../Nomad2026/`) como si fuera el sinte. Nombre provisional: el producto todavía no
tiene nombre, y es mejor que no lleve "Nord" ni "Clavia", porque son marcas.

Es un proyecto aparte de `../Elektron-Emu/` (MM Voice). No comparten build ni ROMs.

## Estado

**El OS 3.03 arranca en el 68331 emulado y carga sus programas en los 4 DSP56303**, que
quedan corriendo; todo va al ~88% del tiempo real. **Contesta al saludo de NME por el PC
PORT**, y `g1run` lo corre en tiempo real (~94%) con puertos MIDI virtuales de ALSA. Con un
patch y una nota **ya suena**: el audio recorre los 4 DSP y sale por el DSP 3 a la altura
correcta, y `g1run` lo saca por la tarjeta de sonido. Los 4 DSP van en hilos propios: 100% del
tiempo real con margen. **Suena limpio** (desde el 2026-09-19 por la tarde: los DSP a su reloj real
de 82,944 MHz y los enlaces entre ellos a 9 palabras por muestra; ver `NOTAS.md`). Sale flojo porque
el propio OS limita el volumen maestro a −36 dB; `g1run` lo compensa. Los módulos de control
(envolventes, relojes, maestros, el LFO del chorus) funcionan desde que se arregló el fin de bucle
del JIT. Cuatro salidas y dos entradas por JACK. Falta el panel (ver `SIGUIENTES-PASOS.md`).

## Usarlo

```bash
./g1.sh      # arranca el G1 emulado en la consola; Ctrl+C guarda la flash y sale
./g1gui.sh   # lo mismo con su panel en una ventana (JUCE); al cerrarla guarda la flash
```

La ventana (`app/gui`) enseña la pantalla, los 18 mandos y el volumen, los botones y LEDs ya
identificados y una barra con la velocidad, la carga del emulador y los núcleos que usa. «Matriz»
abre los 24 botones y los 32 LEDs en crudo, para identificar los que faltan. JUCE sale de
`../Nomad2026/JUCE` (o `G1_JUCE_DIR`); sin él solo se compilan la consola y las herramientas.

Crea el cliente ALSA **G1-Emu** con dos puertos, como el aparato: **PC Port** (el del editor:
en NME se elige como entrada y salida) y **MIDI** (el MIDI IN/OUT normal). La flash (OS +
patches guardados) vive en `~/.local/share/Animatek/G1-Emu/flash.bin`; si no existe, se crea
con el OS de fábrica de la ROM.

El audio va por JACK (pipewire-jack): cliente **G1-Emu** con `out_1..out_4` e `in_L`/`in_R`, como el
panel trasero; `out_1`/`out_2` se conectan solos a la tarjeta (`G1_JACK_CONNECT=0` no). Sin JACK, o
con `G1_AUDIO=alsa`/`G1_AUDIO=dispositivo`, salidas 1/2 por ALSA; `G1_AUDIO=no` sin audio. Más: `G1_GAIN_DB` (por defecto +36 dB, que deshace el tope de
−36 dB que el OS pone al volumen maestro); `G1_THREADS=0` para correr los DSP en serie; `G1_RECORD=segundos` graba un WAV de 4 canales. El mapa de memoria, el cargador y el plan están en `NOTAS.md`. La
plantilla es la emulación del Nord Lead 2X de Gearmulator (`source/nord/n2x`).

## Compilar y probar

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target g1boot -j$(nproc)
cmake --build build --target g1dspcheck -j$(nproc)
ctest --test-dir build -R '^g1dspcheck$' --output-on-failure
./build/tools/g1boot Roms/NORD-MODULAR-RACK-VER-3.03.BIN 60     # 60 M de instrucciones
./build/tools/g1boot Roms/NORD-MODULAR-RACK-VER-3.03.BIN dis C800 C900   # desensamblar
```

`g1boot` dice por dónde va el PC, en qué bucle se queda, los chip-selects programados y
cada acceso a hardware que aún no se emula. Ojo: el OS corre en RAM (`$100000`) copiado
desde la ROM en `$C800`, así que la dirección de RAM X está en la ROM en `X - $100000 + $C800`.

| Ruta | Qué es |
| --- | --- |
| `g1Lib/g1mc.*` | La CPU (68331) con su mapa de memoria: ROM, RAM y flash. |
| `g1Lib/g1flash.h` | La flash AMD Am29F080 de `$300000`. |
| `g1Lib/g1duart.h` | El PC PORT: DUART SCN2681 en bus paralelo (puerto GP + puerto E). |
| `g1Lib/g1dsp.*` | Un DSP56303 con su ROM de arranque HI08, conectado al puerto host de la CPU. |
| `tools/dspdis.cpp` | Desensamblador de DSP56300 (palabras en hex por stdin). |
| `app/g1run.cpp`, `app/alsamidi.h`, `app/alsaaudio.h`, `g1.sh` | El G1 en tiempo real: MIDI virtual y audio por ALSA, flash persistente. |
| `cmake/Dsp56300.cmake`, `g1Lib/dsp56300.cpp` | Correcciones del núcleo DSP (JIT y DMA), aplicadas a una copia de compilación. |
| `tools/g1boot.cpp` | Arranque sin interfaz y desensamblador. |
| `tools/patchtest/` | `g1patchtest`: sube un `.pch` como NME, toca una nota y mide (necesita `../Nomad2026`). |
| `app/jackaudio.h` | Audio por JACK: 4 salidas y 2 entradas. |
| `app/emuhost.*` | El G1 funcionando (flash, MIDI, audio, tiempo real) en su hilo; lo usan `g1run` y `g1gui`. |
| `app/gui/` | `g1gui`: la ventana con el panel. |
| `g1Lib/g1lcd.h` | La pantalla (HD44780). |

**El plan y las ideas pendientes están en `SIGUIENTES-PASOS.md`.**

## Reglas

- **Las ROMs nunca entran en el repo** ni en un release. `Roms/` está fuera de Git:
  el OS 3.03 del rack, el actualizador oficial y el editor de Mac.
- **Licencia:** si se enlaza con Gearmulator es GPLv3. NME solo habla MIDI y es otro programa.
- Antes de tocar el sinte de verdad desde aquí, mirar la conexión (ver la memoria
  "NME: mirar la conexión antes de tocar slots").

## Changelog — regla

**Todo cambio que entre en el repo lleva su línea en `CHANGELOG.md`, en el mismo commit.** Sin
excepciones: código, documentación, herramientas, arreglos pequeños. Lo más reciente arriba,
bajo la fecha, con quién lo hizo, qué cambia y cómo se ha verificado. No hace falta el hash: la
entrada va en el mismo commit que el cambio. Lo que quede sin commitear se marca «cambio local, sin
commit». El repo es público: el changelog es lo que lee la gente.

## Changelog global

Cada cambio va en `CHANGELOG.md` de este repo **y** en el global
`/mnt/SPEED/CODE/CHANGELOG.md` (regla de `/mnt/SPEED/CODE/AGENTS.md`, sección Global
Changelog). El global es un enlace a Obsidian: se edita su destino, no se reemplaza.
