# G1-Emu

Emulación del **Nord Modular G1** sobre el núcleo de Gearmulator: el OS original
del G1 corriendo en un 68331 y unos DSP56303 emulados, y tocado desde **Animatek NME**
(`../Nomad2026/`) como si fuera el sinte. Nombre provisional: el producto todavía no
tiene nombre, y es mejor que no lleve "Nord" ni "Clavia", porque son marcas.

Es un proyecto aparte de `../Elektron-Emu/` (MM Voice). No comparten build ni ROMs.

## Estado

**El OS 3.03 arranca en el 68331 emulado y llega a su bucle principal.** Faltan los DSP,
el panel y el MIDI. El mapa de memoria, el cargador y el plan están en `NOTAS.md`. La
plantilla es la emulación del Nord Lead 2X de Gearmulator (`source/nord/n2x`).

## Compilar y probar

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target g1boot -j$(nproc)
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
| `tools/g1boot.cpp` | Arranque sin interfaz y desensamblador. |

## Reglas

- **Las ROMs nunca entran en el repo** ni en un release. `Roms/` está fuera de Git:
  el OS 3.03 del rack, el actualizador oficial y el editor de Mac.
- **Licencia:** si se enlaza con Gearmulator es GPLv3. NME solo habla MIDI y es otro programa.
- Antes de tocar el sinte de verdad desde aquí, mirar la conexión (ver la memoria
  "NME: mirar la conexión antes de tocar slots").

## Changelog

Cada cambio va en `CHANGELOG.md` de este repo **y** en el global
`/mnt/SPEED/CODE/CHANGELOG.md` (regla de `/mnt/SPEED/CODE/AGENTS.md`, sección Global
Changelog). El global es un enlace a Obsidian: se edita su destino, no se reemplaza.
