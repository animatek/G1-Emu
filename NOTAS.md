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

## Los DSP (2026-09-18)

**Los 4 DSP56303 arrancan con el programa del OS** (`g1Lib/g1dsp`). Cómo funciona:

- **8 puertos HI08** en `$200000 + 8·n`: DSP 0–3 en la placa base y 4–7 en la expansión. El OS
  tiene la tabla de punteros en `$15BD68` y guarda cuántos DSP hay en `$1AB91C`.
- **El cargador** solo envía un programa de 52 palabras a los DSP 3 y 7. Ese programa configura
  el PLL y los dos ESSI, manda `$155` dos veces por ellos (¿inicializa el códec?), levanta HF2
  ("estoy aquí"), espera HF0 de la CPU y vuelve a su ROM (`jmp $FF0000`). Así detecta el OS la
  expansión: mira si el DSP 7 levantó HF2.
- **El OS** carga cada DSP con un arranque HI08 estándar (longitud `$205`, dirección 0). Hay tres
  programas: el del DSP 0 (`$144644`), el de los intermedios (`$144C18`) y el del último
  (`$1451EC`). Después manda tablas de 128 palabras, cada una seguida de un *host command*
  (`$BF`, `$B2`, `$B3`…). La rutina de envío (`$10C0E0`) espera TXDE y, si no llega en 10
  intentos, **descarta la palabra en silencio**: por eso sin DSP el OS seguía como si nada.
- **Emulación:** todo en un hilo. Cada DSP avanza ~6 ciclos por ciclo de CPU (la CPU va a
  20,97 MHz tras programar el SYNCR) y se pone al día cuando la CPU toca su puerto. Las banderas
  HF0/HF1 del ICR van al HSR del DSP y HF2/HF3 vuelven al ISR. El salto a `$FF0000` rearma la
  ROM de arranque. Los ESSI reciben silencio continuo (como un códec) y su salida se descarta.
- **Resultado:** DSP 0–2 arrancan una vez y DSP 3 dos (cargador + OS). Los cuatro quedan en su
  bucle (`$16C`) generando tramas de audio. Velocidad: **~88% del tiempo real** en un núcleo del
  5700X, sin patch cargado.

## Los puertos MIDI y el PC PORT (2026-09-18)

El G1 tiene **dos puertos serie independientes**: MIDI IN/OUT para tocar y PC PORT IN/OUT,
dedicado al SysEx del editor. En la placa van así:

- **MIDI IN/OUT = la SCI del 68331** (UART interna, `SCCR0=$15` → 31.207 baudios a 20,97 MHz,
  interrupción de recepción a nivel 3, vector `$42`). Un *IAm* del editor por aquí se lee pero
  no se contesta.
- **PC PORT = un DUART SCN2681/68681 externo**, colgado de un bus paralelo hecho con puertos de
  la CPU:
  - datos: puerto GP del temporizador (`$FFF906/7`), DDRGP `$FF` para escribir y `$00` para leer;
  - control: puerto E (`$FFFA11`). Bit 0 = /CS, bit 1 = /RD, bit 2 = /WR, bits 3/6/7 = A0/A1/A2;
  - inicialización del canal A: CR `$0A,$10`, MR1/MR2 `$13/$07` (8N1), CSR `$EE`, ACR `$FF`,
    IMR `$00`, y CR `$20,$30,$50,$C0,$90`; al final CR `$05` (RX y TX activos);
  - aviso de byte recibido: RxRDY → patilla PAI. El OS deja PACNT en `$FF` con PAOVI activo
    (TMSK2 bit 5), y el desbordamiento salta al vector **IVBA+`$A`** (`$5A`, GPT ICR=`$0250`,
    nivel 2). El manejador (`$117364`) lee RHR, reconstruye el SysEx y lo reparte.
  - El GPT de Gearmulator no emula el acumulador de pulsos: se hace en `g1mc.cpp`.
- VBR = `$1AB4E0` (tabla de vectores en RAM).

**Resultado:** al arrancar, el G1 emulado se anuncia por el PC PORT
(`F0 33 50 06 00 07 08 08 F7` y `F0 33 50 06 00 05 01 00 00 00 7F F7`) y, al recibir el *IAm*
de NME (`F0 33 00 06 00 03 03 F7`), contesta `F0 33 00 06 01 03 03 3F 7F 7F 01 F7`: emisor 1,
versión 3.3, número de serie y ID de aparato. Es el saludo completo que espera NME.

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
2. ~~**DSP:** que el OS cargue sus programas en los DSP.~~ Hecho: los 4 arrancan.
3. ~~**MIDI:** que el G1 conteste al editor.~~ Hecho por el PC PORT (DUART). Falta exponerlo
   como puerto MIDI virtual del sistema para que NME lo vea, y el MIDI normal por la SCI.
4. **Patch:** mandar un patch desde NME y que suene.

## Nombre

Pendiente. Ideas sueltas: G1mulator, Nordulator, Modulator G1, NME Engine.
Ojo: "Nord" y "Clavia" son marcas; mejor que el nombre no las lleve.
