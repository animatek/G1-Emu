# Nord Modular G1 emulado — notas

Objetivo: correr el sistema original del G1 dentro de chips emulados (Gearmulator) y
tocarlo desde **Animatek NME**, igual que el sinte de verdad. Se vendería el editor
y se daría el VST sin la ROM.

## La ROM (`Roms/NORD-MODULAR-RACK-VER-3.03.BIN`)

Es el OS 3.03 del rack: 512 KB, sha256 `d9b199f2…c997e`. Viene de Javier, en
`Roms/NORD-MODULAR-RACK-VER-3.03.BIN.zip`. Nunca entra en Git. Al lado están el
actualizador oficial (`Nord Modular OS v3.03b Update.exe`/`.hqx`, de 1999) y el
editor 3.03 de Mac (`.hqx`). Todavía no se han abierto: el actualizador debería
llevar el mismo OS en otro formato, y sirve para cotejarlo.

Lo que se ve dentro (2026-09-18):

| Rango | Qué hay |
| --- | --- |
| `$00000`–`$08xxx` | Vectores del 68k (pila `$1FFF00`, reset `$000008`) y cargador. `MIDI BOOT`, *Update utility* y menú de tests de fábrica: *DSP Memory test*, *Expansion Board*, *Exp Boot Failure*, DAC/AD, knobs, teclado. |
| `$0A000`–`$0C3xx` | Basura: cabeceras COM de Windows (`objidl.h`) que se colaron en la imagen. No es código. |
| `~$0C400`–`$4FFFF` | El sistema principal (código 68k, casi sin textos). |
| `$50000`–`$5FFFF` | Zona con 40–60% de ceros: probablemente el **código y los datos de los DSP** (palabras de 24 bits guardadas en 32). En `$50311` pone `NORD MODULAR`. |
| `$60000`–`$68xxx` | Más código/datos. |
| `$6C000`–`$7FFFF` | Vacío (`0xFF`). |

## El hardware

- **CPU: Motorola 68331.** Lo dice el propio código: escribe en SIM (`$FFFAxx`), en
  QSM (`$FFFCxx`) y sobre todo en el **GPT** (`$FFF9xx`, 131 accesos). El GPT es lo
  que distingue al 68331 del 68332. Gearmulator ya emula exactamente este chip
  (`source/mc68k`: `sim`, `qsm`, `gpt`, `hdi08`) para el Nord Lead 2X.
- **DSP: Motorola DSP56303.** Según MATRIXSYNTH, el G1 expandido de 32 voces lleva 8;
  el normal, en principio 4. Falta confirmarlo en la placa o en el código.
- **Plantilla:** `source/nord/n2x` de Gearmulator (Nord Lead 2X). Tiene la misma CPU,
  una flash del mismo tamaño y DSP 563xx por HI08, todo en ~2.100 líneas. El G1 sería
  un `g1Lib` hermano de `n2xLib`.

## El coste que se espera

La MM (2 DSP + CPU interpretada) se come ~95% de un núcleo del 5700X en un solo
hilo. El G1 lleva 4 DSP, así que en un hilo no cabe: hay que repartir los DSP entre
hilos, que es lo que ya hace Gearmulator con el Virus TI.

## Pasos

1. **Arranque:** montar `g1Lib` copiando la estructura de `n2xLib`, cargar la flash
   y ver hasta dónde llega el 68331 (log de accesos a registros desconocidos).
2. **DSP:** localizar cómo sube el código a los DSP (HI08) y cuántos inicializa.
3. **MIDI:** conectar el puerto serie del QSM a un MIDI virtual y que NME detecte el sinte.
4. **Patch:** mandar un patch desde NME y que suene.

## Nombre

Pendiente. Ideas sueltas: G1mulator, Nordulator, Modulator G1, NME Engine.
Ojo: "Nord" y "Clavia" son marcas; mejor que el nombre no las lleve.
