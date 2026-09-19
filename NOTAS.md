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

## El audio (2026-09-19, en curso)

- **Salida:** solo el DSP 3 transmite hacia fuera. Los dos ESSI van en modo de 2 slots por
  trama (estéreo) por TX0. Muy probablemente ESSI0 = salidas 1/2 y ESSI1 = 3/4.
- **Frecuencia:** 96 kHz. El DSP emulado va a ~125,8 MHz; el reloj del ESSI cuenta palabras,
  así que es 1 palabra cada 655 ciclos (2 por trama).
- **Reloj de proceso:** la patilla **IRQD** de cada DSP (vector `$16`) recibe el reloj de
  muestra. Su rutina (`$200`) solo incrementa `X:$1`, y el bucle principal procesa un bloque
  cuando pasa de 3 (bloques de 4 muestras). Se emula con una IRQD por trama, solo si el DSP
  la tiene habilitada.
- **DMA:** DMA2/3 llevan ESSI0/1 RX a `$6C0`/`$6C9` (8 palabras), DMA4/5 sacan `$6C0`/`$6C2`
  hacia ESSI0/1 TX, y DMA0 copia de memoria a memoria. Los temporizadores del DSP están en
  `X:$FFFF8x` (no son DMA).
- **Arranque doble del DSP 3:** el OS empieza a mandar el segundo arranque en cuanto el
  programa del cargador ve HF0. Lo que queda en el puerto al volver a `$FF0000` se entrega a
  la ROM de arranque; sin eso, el programa del DSP 3 quedaba incompleto.
- **Cadena de audio (hipótesis, sin confirmar):** ESSI de DSP n → ESSI de DSP n+1. Conectada,
  no cambia nada todavía.
- **Por qué no suena:** el bloque que ejecuta cada DSP es una rutina que guarda registros,
  rearma los DMA y vuelve, con **6 `NOP` (`$18B–$190`) donde iría la llamada al código del
  patch**. Siguen vacíos. Crear OscA → 2Output solo manda 12 palabras (todas al DSP 0),
  ningún DSP ha devuelto nunca una palabra a la CPU y NME ve 0 voces. Parece que el OS no
  llega a compilar ni cargar el patch; quizá espera una respuesta de los DSP que no llega.
- **Por qué no se carga (2026-09-19): el OS asigna 0 voces.** Con `G1_WATCH` se ve la cadena:
  `$1226F0` → `$123E94` (asignación de voces) → `$1239E8` ("¿cabe otra voz?", suma los
  recursos de cada slot: ciclos y memoria X/Y/P) devuelve **no** las 4 veces → la lista de voces
  de cada DSP (`$1A84A8 + DSP×$602`, cuenta en `+1`) queda vacía → `$12287E` (reparto de
  memoria) y `$122DE2` (carga por voz) no tienen nada que hacer → el cargador de módulos
  (`$122F72`, que escribe X/Y/P con `$B2/$B3/$B4` y parchea el salto con `$B7`) no se llama
  nunca. Falta saber qué recurso le parece agotado: la capacidad que el OS atribuye a cada DSP
  o las tablas de recursos por módulo (`$1C3B0C`, `$1C3B24`, `$1C3B28`, en pasos de `$30`).
- **Resuelto (2026-09-19, tarde): por qué no se cargaba el patch.** Eran cinco fallos encadenados:
  1. **No había reloj del sistema.** El OS usa el PIT del SIM (`PICR=$0140`: nivel 1, vector
     `$40`; `PITR=$0002`: cada 244 µs; rutina `$1008A4`). El SIM de Gearmulator no lo emula, así
     que los temporizadores por software del OS nunca vencían. Emulado en `g1mc.cpp`.
  2. **Espera excesiva en `readIsr`.** Al recargar, el OS levanta HF0; el DSP para, levanta HF2
     y espera en `$C2` sin leer el puerto. Cada consulta de estado dejaba correr al DSP 200.000
     ciclos. Ahora son 2.000.
  3. **Arbitraje de host commands.** Los del G1 son interrupciones rápidas (un `movep` en el
     vector, sin `RTI`) y el arbitraje de Gearmulator nunca los daba por terminados. Desactivado.
  4. **Cola de interrupciones llena.** Sin arbitraje, la CPU manda cientos de host commands
     seguidos y la cola de 32 se llenaba. Ahora el DSP despacha lo pendiente antes de cada uno.
  5. **IRQD durante la parada.** El DSP deshabilita IRQD en el IPRC (`$FF0800`, IDL=0), pero el
     emulador solo mira el SR. La IRQD inyectada dejaba al DSP "dentro" de una interrupción larga
     para siempre y bloqueaba los host commands. Ahora IRQD respeta el IDL del IPRC.
  Con eso, al insertar un módulo el OS para los DSP, **carga el código del módulo**
  (`$122F72`; unas 400 palabras al DSP 0), los reanuda, y el oscilador calcula (cambian su fase
  y sus variables). Las respuestas DSP → CPU (lecturas de memoria del "monitor") funcionan.
- **Máscaras de slots del ESSI:** los DSP de voz no escriben TSMA/TSMB/RSMA/RSMB y se fían del
  reset (todos los slots activos); el emulador las dejaba a 0. Ahora se inicializan a `$FFFFFF`.
- **Cómo se procesa el audio en cada DSP (2026-09-19):** la rutina de bloque (`$175`) es el
  **manejador de IRQD**. El vector IRQD (`P:$16` = `jsr`, destino en `P:$17`) se reescribe en
  marcha: `$200` en reposo (solo cuenta) y `$175` procesando. El OS manda el destino en `X:$FFF3`
  con un host command. En la parada de recarga (`$9E`), el DSP apunta el vector a `$CA`, y esa
  rutina, en la siguiente IRQD, rearma los DMA y copia `X:$FFF3` → `P:$17`. El código de los
  módulos se añade detrás de la rutina de bloque (desde `$197`). En la emulación, el bloque se
  ejecuta cientos de miles de veces y la fase del oscilador (`Y:$60/$61` del DSP 0) avanza.
- **Pendiente: sacar el audio.** El DSP 0 calcula, pero su ESSI transmite ceros y el DSP 3
  sigue con `$155`. Falta entender qué espacio lee cada DMA (DSS) y la topología del bus serie
  (¿TDM compartido? El DSP 3 recibe su propio `$155` y copia RX → TX). Comprobado: el DMA4 del
  DSP 0 completa sus 9 transferencias por bloque desde `Y:$6C0/$6E0` hacia TX0, pero los búferes
  llegan a cero y TX0 queda a 0 (hay subdesbordamientos, TUE). Con la nota mantenida (el `sc=$56`
  de NME sin soltar) tampoco cambia. Lo siguiente: localizar dónde escribe el módulo 2Output
  (seguir el código de `$197` en adelante) y el cableado real de los ESSI entre los DSP.
- **`g1boot ... replay`** manda ahora los mensajes espaciados (uno cada 500.000 instrucciones),
  como NME, que espera cada ACK: si llegan todos seguidos, el OS pierde ediciones mientras
  recarga. `g1boot ROM N diff A.bin B.bin` compara el código ejecutado en reposo y tras un
  mensaje.
- **Herramienta:** `g1boot ROM N replay pcport-in.bin` reproduce una sesión de NME grabada
  por `g1run`, mantiene una nota por MIDI IN y vuelca estado, búferes y DMA de cada DSP.
  `G1_WATCH=123e94,1239e8 g1boot ...` cuenta los pasos del PC por esas direcciones y enseña
  los registros.

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
