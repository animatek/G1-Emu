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

## Arranque emulado (2026-09-18)

`tools/g1boot` ejecuta la ROM en el 68331 de Gearmulator y **el OS 3.03 llega a su bucle
principal**. Lo que se ha aprendido por el camino:

**El cargador** (`$000000`–`$0007FF`) mira el panel al encender: selecciona filas en `$202005`
y lee `$201800`. Según las teclas pulsadas elige:

| Teclas (filas leídas) | Qué hace |
| --- | --- |
| ninguna | Copia a RAM el OS de la flash de `$300000` (longitud en `+8`, datos desde `+$20`) y salta a `$100000`. Si la flash está vacía (`$FFFFFFFF`), va al modo actualización. |
| D1=`$7F`, D2=`$BF` | Modo actualización por MIDI: copia a RAM el programa de `$0800` (48 KB, *Update utility*). |
| D1=`$7F`, D3=`$F7` | Arranca el **OS de fábrica que va en la propia ROM** (`$C800`, `$1CE01` palabras largas). |
| D3=`$EF` y D1=`$F7` / `$FB` | Tests de fábrica y de RAM. |

**El mapa de memoria** (chip-selects que programa el OS):

| Dirección | CS | Qué es |
| --- | --- | --- |
| `$000000` | BOOT | ROM de 512 KB (cargador, utilidad de actualización, OS de fábrica) |
| `$100000`–`$1FFFFF` | 8/9/10 | RAM de 1 MB (el OS corre aquí) |
| `$200000`, `$200008`, `$200010`, `$200018` | 0 | **Los 4 DSP56303 por HI08**, 8 registros cada uno: host command en `+1` (CVR, valor `$CD`), estado en `+2` (ISR), palabra de 24 bits en `+4/+6` |
| `$200020`–`$20003F` | 0 | Probablemente la tarjeta de expansión (otros 4 DSP); el cargador escribe en `$200038` |
| `$201000` | 1 | Escritura de 8 bits, muy frecuente: ¿display? |
| `$201800` | 4 | Lectura de la matriz de botones |
| `$202000`–`$202007` | 2 | Filas de botones y LEDs |
| `$202800` | 3 | Lectura: ¿potenciómetros (ADC)? |
| `$300000` | 5/7 | **Flash de 1 MB** (8 bits): el OS instalado y los patches |

**La flash:** el OS acepta un Intel 28F008 (`$89/$A6`), un Fujitsu MBM29F080 (`$04/$D5`) o
un AMD Am29F080 (`$01/$D5`), y si no se para en un bucle infinito. Se emula el AMD
(`g1Lib/g1flash.h`). En el primer arranque el OS formatea la zona de patches: borra 10
sectores y escribe 64 KB.

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

1. ~~**Arranque:** que el 68331 llegue al OS.~~ Hecho: llega al bucle principal.
2. **DSP:** localizar cómo sube el código a los DSP (HI08) y cuántos inicializa.
3. **MIDI:** conectar el puerto serie del QSM a un MIDI virtual y que NME detecte el sinte.
4. **Patch:** mandar un patch desde NME y que suene.

## Nombre

Pendiente. Ideas sueltas: G1mulator, Nordulator, Modulator G1, NME Engine.
Ojo: "Nord" y "Clavia" son marcas; mejor que el nombre no las lleve.
