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

## Comprobación del núcleo pendiente (2026-09-19, Codex)

- Validadas las extensiones locales de MOVEM corto y DO FOREVER, con invalidación del
  JIT, IRQD y bucles DO anidados, en bloques de 1 y 32 instrucciones. `g1dspcheck`
  usa programas sintéticos sin ROM. La prueba debe inicializar SR directamente y
  actualizar el modo del JIT; `writeReg(Reg_SR, ...)` no está implementado en este núcleo.
  B0 se comprueba por la API de registros, porque el acumulador interno está desplazado.
- Las correcciones se aplican a una copia de compilación en `build/g1-dsp`, nunca al
  clon externo. CMake ahora incluye solo los núcleos y el puente SCI/MIDI: el CMake
  global de Gearmulator intentaba generar `synthLib/buildconfig.h` en el clon.
- Reproducción real: `g1boot ROM 60 replay /tmp/g1-replay-hold.bin`, con los primeros
  **45 mensajes** del `pcport-in.bin` actual (705 bytes, hasta `56 00 3C`, sin soltar).
  El fichero completo contiene 63 mensajes y reconexiones posteriores: quitar solo
  el último mensaje **no** mantiene la nota. No se ha cambiado el registro original.
- Los cuatro DSP arrancan; el OS lee los 705 bytes. Tras la nota MIDI adicional,
  439.699 tramas por ESSI: DSP0–2 a cero y DSP3 fijo en `$155`. No hay sonido validado.
  En el volcado final de DSP0 el vector apunta a `$175`, hay código de módulos desde
  `$1A3`, pero `$18B–$190` siguen siendo NOP y `$197` da paso a restaurar registros/RTI.
  **Revisar el enlace al código del patch** antes de dar por confirmada su ejecución
  con estas correcciones. La hipótesis del bus ESSI sigue pendiente.

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

## Primer audio del patch (2026-09-19, Claude)

- **Corrección de lo anterior:** el patch sí se enlaza. El cargador del OS (`$122F72`) manda
  por módulo `$BF` (dirección, vector `$7E`: `r0`), `$B2`/`$B3` (X/Y), una palabra suelta con
  `$B7` (vector `$6E`: `movep` a `p:(r0)` sin incrementar) en `base + desplazamiento` y el
  resto con `$B4` (`p:(r0)+`) desde `base`. El desplazamiento (byte con signo, `-$C`) es el del
  módulo anterior: cada módulo sobrescribe el `NOP` de antes del epílogo del anterior. El primero
  parchea `P:$197` de la rutina de bloque; el último acaba en su propio epílogo y `RTI`.
- **Por qué el replay de Codex no sonaba:** `pcport-in.bin` trae dos sesiones. En la segunda NME
  se reconectó a un G1 recién arrancado, que dio **pid 1** otra vez a la nueva subida de patch.
  En el replay no hay reinicio: el OS da **pid 2** (`F0 33 58 06 02 36 02`), recarga la rutina
  base (con los `NOP`) y descarta todo lo que lleva pid 1 (módulos y nota; responde `7F 02`).
  `g1boot ... replay` reescribe ahora el pid de los mensajes Parameter (cc `$13`),
  PatchModification (cc `$17`, salvo `$41`) y PatchPacket de un patch cargado (cc `$1C–$1F`
  sin el bit de comando) con el último que dio el OS por ese slot, y rehace el checksum
  (suma desde `F0` hasta el payload, `& $7F`).
- **Resultado:** con los 45 primeros mensajes, el DSP 0 carga los módulos (`$1A3–$29C`), enlaza
  `$197` y su ESSI0 saca una onda periódica de ~367 tramas: **261 Hz, el Do central** de la nota
  60. Tiene forma de seno saturado a ±1, escalonada (cada valor dura 4 o 5 tramas) y con algunos
  picos sueltos a 0. El DSP 1 recibe esas muestras en `X:$6C0` por la cadena provisional, pero
  saca ceros; el DSP 3 sigue en `$155`.
- **Herramienta:** `G1_TAP=fichero g1boot ...` guarda el slot 0 del ESSI0 de los 4 DSP, en
  `int32` por DSP y trama.

## Sale por el DSP 3 (2026-09-19, Claude)

- **Cadena de audio confirmada: DSP0 → DSP1 → DSP2 → DSP3 → códec.** Cada DSP recibe por ESSI en
  `X:$6C0` (DMA2/3). Al empezar cada bloque, el **DMA0 copia `X:$6C0` → `Y:` búfer de salida**
  (`$6C0`/`$6E0` alternos, 18 palabras), las voces suman encima y el DMA4/5 lo saca por TX.
  Un DSP sin voces solo deja pasar lo que recibe.
- **Fallo 1, en Gearmulator:** el DMA0 usa doble contador en origen y destino a la vez
  (`DCR=$1801B4`, DAM `011 011`, los dos con DOR3 = −17). No estaba implementado: en Release
  daba el bloque por hecho sin copiar. Corregido en `cmake/Dsp56300.cmake` (los dos lados
  comparten DCOH/DCOL y cada uno suma su DOR al terminar la línea).
- **Fallo 2, en Gearmulator:** las transferencias de bloque iban retrasadas y la copia llegaba
  después de que la voz sumara su muestra, así que la pisaba. Ahora son inmediatas.
  (La saturación de antes venía de ahí: sin copia, el búfer nunca se reiniciaba y la voz se
  acumulaba encima.)
- **Fallo 3, en el emulador: el volumen maestro.** El DSP 3 hace `salida = X:$5F + entrada ×
  Y:$5F`. `X:$5F = $155` es el silencio (el DSP 3 llena con él sus búferes al pararse);
  `Y:$5F` es el volumen: el DSP 3 lo pone a 0 al arrancar y el OS lo fija a partir del **ADC
  del panel**. El OS elige canal escribiendo en `$202000` (tabla de 20 códigos en RAM
  `$14420A`: `31 37 2d 32 28 2e 33 29 2f 34 2a 1a 35 2b 1b 36 2c 1c 30 18`), lee 8 bits en
  `$202800` y guarda cada canal en `$15EC20`. **El código `$30` es el volumen maestro**
  (probado canal a canal con `G1_ADCMUX`): a `$FF`, `Y:$5F = $01FEAA`; a `$80`, `$0022F1`. El
  emulador devolvía 0, así que no salía nada. Ahora el volumen empieza al máximo; los demás
  mandos siguen a cero. El OS parece leerlo al encender: subirlo con el G1 en marcha no cambió
  la ganancia (pendiente).
- **Resultado:** el Do de la nota 60 sale por los dos ESSI del DSP 3: un seno limpio a 261 Hz.
  Nivel muy bajo (±0,0008 de fondo de escala, unos −62 dBFS, con la voz a ±0,052 en los DSP de
  voz) y todavía escalonado (cada valor dura 4 o 5 tramas), con algún corte suelto a 0.

## Tiempo real: un hilo por DSP y salida de audio (2026-09-19, Claude)

- **Dónde se iba el tiempo** (cronómetros en `catchUpDsps`, 12 s de `g1run`): 10,6 s en los
  DSP, a partes iguales (2,65 s cada uno), y 1,3 s en la CPU y lo demás. Con la máquina algo
  cargada (Bitwig), la versión de un solo hilo iba al ~79%, también la del día anterior.
- **Un hilo por DSP** (`g1mc.cpp`): el DSP 0 corre en el hilo de la CPU y los DSP 1–3 en tres
  hilos que esperan girando: se sincronizan cada ~1.000 ciclos de CPU, unas 20.000 veces por
  segundo, demasiado seguido para dormir y despertar. Solo duermen (mutex y variable de condición)
  si la CPU tarda. Lo que sale por los ESSI se guarda en una cola por DSP (`drainAudio`) y el hilo
  de la CPU lo pasa al DSP siguiente y al callback de audio cuando todos han parado
  (`flushAudio`), así que ninguna cola tiene dos productores. El resultado es el mismo que en serie
  (mismo audio y mismos cortes en el replay). Replay de 60 M instrucciones: 13,0 s con hilos
  frente a 28,3 s en serie. `g1run` va al 100%. `G1_THREADS=0` vuelve a correrlo todo en serie.
- **Salida de audio** (`app/alsaaudio.h`): ESSI0 del DSP 3 (salidas 1/2) por ALSA `default`, de
  96 a 48 kHz promediando cada par, en su propio hilo, con 30 ms de colchón. Cuenta como corte cada
  vez que el colchón se vacía (PipeWire no avisa de los xrun). En 15 s: 4 cortes, todos al
  arrancar, mientras el OS recarga los DSP.
- **Los escalones y los ceros ya salen del DSP 0**, el primero de la cadena: bloques enteros de 4
  o 5 tramas (unas 170 rachas por segundo en el replay). Pista: el DMA4 de los DSP de voz manda
  **9 palabras por bloque** (`DCO4=8`) y el del DSP 3 solo **2** (`DCO4=1`); el DSP 3 recibe 9
  (`DCO2=8`) y procesa 4. Parece que el enlace entre DSP lleva varios canales por bloque (salidas y
  buses) y no 4,5 tramas de tiempo. El emulador da una IRQD por trama y el mismo reloj serie a los
  cuatro, y eso probablemente no es lo que hace el aparato (los divisores del ESSI también
  difieren: `PM=1` en los de voz, `PM=2` en el DSP 3).

## Sonido limpio: reloj real y enlaces de 9 palabras (2026-09-19, tarde, Claude)

- **El reloj de los DSP, del propio OS.** Los cuatro programas escriben `PCTL=$3C001A`: MF+1=27,
  PD+1=4. Con un cristal de 12,288 MHz (128 × 96 kHz) da **82,944 MHz = 864 ciclos por muestra**.
  El emulador los tenía a 125,8 MHz (6 × CPU); ahora van a 2025/512 × el reloj de la CPU.
- **Los enlaces entre DSP**, de sus CRA/CRB: los DSP 0–2 transmiten por ESSI0 y ESSI1 con
  `CRA=$181801` (PM=1, PSR, 2 palabras por trama, 24 bits), reloj interno y modo red. Periodo de
  palabra = 2·(PM+1)·24 = **96 ciclos: 9 palabras por muestra por ESSI**, justo lo que manda el
  DMA4/5 en cada bloque. Son 18 canales por enlace. El DSP 1–3 los reciben con el DMA2/3 en dos
  anillos de 9 palabras (`X:$6C0` y `X:$6C9`, 2D con DOR2=−8, continuo), y el DMA0 los copia al
  búfer de salida al empezar cada bloque. El DSP 3 tiene `CRA=$181802` (PM=2, 144 ciclos): es el
  reloj hacia el códec; su receptor, en modo asíncrono, va con el reloj del DSP 2.
- **La IRQD es la muestra:** el vector `$16` hace `jsr $175`, un bloque (una muestra) por IRQD
  (lo de «un bloque cada 4 cuentas» era de antes de cargar el patch). Ahora va en una rejilla fija
  de 864 ciclos, común a los cuatro (los DSP van a la par en ciclos, sin paradas).
- **Por qué sonaba escalonado:** el emulador movía 2 palabras por muestra en cada enlace (una
  trama), y el DMA pedía 9. Con el ESSI a su ritmo real (modo «fine link» del fork), el DSP 3
  seguía recibiendo a 144 ciclos (6 de 9 palabras): el oscilador saltaba de canal en canal cada
  4–5 muestras. Y aun al ritmo bueno, los canales llegaban desplazados: el receptor reactiva su
  DMA al volver de cada recarga y la primera palabra que llega es el canal 0; en el aparato eso
  lo garantiza el reloj común, y aquí las palabras esperan en cola hasta que los hilos se
  sincronizan (cada ~4.000 ciclos).
- **Cómo se hace ahora** (`g1dsp.cpp`): el enlace va **por posición**. En cada IRQD, cada DSP deja
  lo que acaba de mandar (`Y:[X:$5]` y `Y:[X:$6]`, 9 + 9 palabras) con su número de bloque. El
  receptor, cuando su ESSI pide una trama, mira a qué palabra del anillo va a escribir su DMA
  (DDR) y le da ese canal del bloque de hace 8 (retardo fijo de ~83 µs por DSP, para no ir nunca
  por delante de los hilos). Si falta ese bloque (el anterior está parado recargando), silencio.
- **La salida**, igual: en cada IRQD del DSP 3 se leen las 2 + 2 palabras que acaba de mandar al
  códec (salidas 1/2 y 3/4): una muestra por bloque (`setBlockCallback`). `g1run` y
  `g1boot` (`G1_BLOCKS=fichero`) usan eso en vez de las tramas del ESSI.
- **Resultado** (replay de 45 mensajes, nota 60 mantenida): salidas 1 y 2 iguales, Do a 261,6 Hz,
  armónicos a −82 dB y ruido a −81 dB de la fundamental, 0,4% de muestras repetidas (los picos
  del seno) y un solo salto en toda la ejecución (el ataque de la nota); 3/4 en silencio, como
  manda el 2Output. Antes: −9 dB de armónicos, +6 dB de ruido y ~8.300 saltos por segundo.
- **Nivel**, sin cambios: −62 dBFS. `Y:$5F` del DSP 3 = `$01FEAA` con el ADC del volumen a `$FF`.

## El nivel: el OS limita el volumen maestro a −36 dB (2026-09-19, Claude)

- **Cómo llega el volumen al DSP 3:** la CPU escribe `Y:$5F` del DSP 3 con el ayudante genérico
  `$10C268` (dirección `$BF` + dato `$B6`). Lo llaman `$1102F8` (bajada al apagar),
  `$1134AE` y `$113824`: una rampa que acerca el valor enviado (`$162CE2`) al objetivo
  (`$162CDE`) 1/32 de la diferencia cada vez. Por eso la traza enseña `$01FD9C`, `$01FDA5`…
- **El objetivo** lo pone `$11030C`: `tabla[$153CAC + 4·índice]`, 128 valores en largos (de −113
  a **−36,1 dB** en `$7F` = `$01FEAA`). Lo que hay detrás (`$153EAC`) es otra cosa, no la mitad
  alta de la tabla.
- **El índice es ADC ÷ 2**, siempre: al encender, `$10427E(canal $12, …)` devuelve
  `$15EC20[2·canal] >> 1`; al mover el mando, el evento `$100|canal` que genera `$104226` también
  lleva `valor >> 1`, y `$102E46` lo pasa a la misma función. Con el mando al máximo, −36 dB.
- **Descartado:** el resto de mandos del panel. Con los 20 canales del ADC a `$FF`
  (`G1_ADCALL=ff`) el nivel no cambia.
- **Conclusión:** los −62 dBFS de un OscA → 2Output son lo que calcula el OS (voz a ±0,052 en el
  enlace, −26 dBFS, por −36 dB de volumen maestro). El emulador no pierde nivel. Si el G1 de
  verdad suena más, la diferencia está después del DSP (etapa analógica) o en algo del patch que
  aquí no se ve. Para saberlo: grabar el mismo patch del G1 real con el volumen al máximo.
- `g1run` sube +36 dB por defecto: deshace justo el tope del volumen maestro, de modo que la
  salida es el nivel del enlace (OscA → 2Output a unos −26 dBFS).
- Herramientas nuevas en `g1boot`: `G1_FINDTX=palabra` (qué código de la CPU manda esa palabra a
  un DSP) y `G1_ADCALL=valor` (todos los canales del ADC a ese valor).

## Los módulos de ritmo de control: el fin de bucle movido (2026-09-19, Claude)

- **Síntoma:** todo lo de ritmo de control (azul) se quedaba quieto: MasterOsc → OscSlv sin
  oscilar, Keyboard → ADSR en silencio, ClkGen muerto, y el chorus y el overdrive «no hacían nada»
  porque su LFO interno y su cantidad son de control (el chorus sacaba L = R; el overdrive,
  la misma distorsión con el mando a 0 que a 127).
- **Cómo lo monta el OS:** el bucle principal del DSP es `do forever` hasta `$174` (un `NOP`). Si
  el patch lleva módulos de control, el OS escribe su código desde `$174`, mueve la rutina de
  bloque detrás (y el vector `$16/$17` a la nueva dirección, guardada en DOR0) y **alarga el bucle
  cambiando el registro LA** con un host command (vector `$7C`: `movep x:$FFFFC6,la`), sin tocar
  la instrucción DO. El código de control corre cada 4 bloques (24 kHz) cuando `X:$1` pasa de 3.
  Sus punteros (`r3`, `r4`) salen de DOR1 y DCO1, que el OS usa como almacén.
- **El fallo, en Gearmulator:** el JIT se apunta el fin de bucle al compilar el DO (desde la
  instrucción) y corta ahí los bloques; si luego cambia LA, no se entera. Solo se ejecutaba la
  primera instrucción del código de control.
- **Arreglo** (`g1dsp.cpp`, `onLaChanged`): después de cada bloque del JIT se mira LA; si ha
  cambiado, se mueve el fin del bucle en el JIT (`removeLoop`/`addLoop`) y se tiran los bloques
  compilados en el fin viejo y el nuevo. `G1_NO_LA_FIX=1` lo desactiva (para comparar).

## DMA: dos modos que Gearmulator no hacía (2026-09-19, Claude)

- **Fija → fija** (DAM `100 100`, un registro a una celda): no tenía rama y en Release daba el
  bloque por hecho sin copiar. El DSP 0 lo usa para las entradas de audio.
- **Bloque por petición sin borrar DE** (DTM=100): se ignoraba. El DMA3 del DSP 0 va así (con
  interrupción al acabar cada palabra, vector `$1E`, que copia el otro canal).
- Los dos, en `cmake/Dsp56300.cmake`. La batería de 101 módulos da lo mismo con y sin ellos.

## Entradas y salidas (2026-09-19, Claude)

- **Salidas:** el DSP 3 manda por cada ESSI dos palabras al revés: primero la par y luego la
  impar. Palabra 0 = salida 2, 1 = salida 1, 2 = salida 4, 3 = salida 3 (comprobado con un 4Output
  y una señal distinta en cada salida). Antes 1/2 salían bien de casualidad (el 2Output mandaba lo
  mismo a las dos). Los auriculares del aparato son una copia de 1/2.
- **Entradas:** entran por el codec al DSP 0, el único sin DSP delante: **R** por el ESSI0
  (DMA2 → `X:$6C4`) y **L** por el ESSI1 (DMA3 → `X:$6C5`). De ahí siguen a los demás DSP por el
  enlace, en los canales 4 y 5, y el módulo AudioIn las lee. `Dsp::setInputProvider`.
- **`g1run` por JACK** (`app/jackaudio.h`, con pipewire-jack): cliente `G1-Emu` con `out_1..out_4`
  e `in_L`/`in_R`; `out_1`/`out_2` se conectan solos a la tarjeta. De 96 kHz a lo que pida JACK
  con interpolación lineal. Sin JACK, o con `G1_AUDIO=alsa`, sigue por ALSA (solo 1/2). Se resta
  el `$155` del DSP 3 (continua).

## Banco de pruebas: `g1patchtest` (2026-09-19, Claude)

- `g1patchtest ROM patch.pch [--note N] [--seconds S] [--wav f.wav] [--input-sine Hz]`: arranca
  el OS sin ventana, saluda como NME (IAm y los 16 mensajes de conexión), sube el `.pch` con el
  código de NME (`PchFileIO` → `PatchSerializer` → `UploadPacketizer`), paquete a paquete esperando
  cada ACK, toca la nota por el PC Port y mide las cuatro salidas y los 18 canales de cada enlace
  entre DSP (qué DSP lleva la voz). Solo se compila si está `../Nomad2026`.
- Variables: `G1_VERBOSE` (tráfico y registros), `G1_DUMP=carpeta` (memoria P/X/Y de los DSP),
  `G1_PCWATCH=174,194` (veces que pasa cada DSP por esas direcciones), `G1_INTERP=máscara` (DSP
  en el intérprete; no sirve con el bucle principal: el intérprete ejecuta un `do forever` entero
  sin volver) y `G1_NO_LA_FIX`.
- **Ojo con los conectores:** en el `.pch` van por su `index` de `modules.xml`, que no siempre es
  el orden de la lista (en el Overdrive, `in` es la entrada 0 y `overdrive mod` la 1).
- **Batería** de los 101 tipos de módulo con valores por defecto (OscA a la entrada si la tienen,
  el gate del teclado si lo piden): 56 suenan, 24 dan una señal lenta o fija y 21 callan; casi
  todos los de esos dos grupos son de control, lógica, LFO lentos o secuenciadores sin reloj, que
  con esa prueba no pueden sonar. Falta una prueba propia para cada uno.

## El panel: pantalla, LEDs y botones (2026-09-19, Claude)

- **Pantalla:** LCD de caracteres con HD44780 en bus de 8 bits. Datos en `$202006`; control en
  `$202007`: bit 0 = RS (0 orden, 1 carácter), bit 1 = E; el byte se recoge al bajar E. El OS lo
  inicia con `$30` ×3 y `$38` y no lee el flag de ocupado. Las minúsculas con rabo (p, y...) son
  caracteres propios en CGRAM (códigos `$08-$0F`): «Empty patch» sale como `Em\x0Bt\x0D \x0Batch`.
  Emulado en `g1Lib/g1lcd.h`; `Microcontroller::getLcd()`. Con el patch cargado enseña el nombre y
  las voces por slot: `( 1) --  --  --`.
- **LEDs:** 32, en 4 filas de 8, multiplexados (`$104000`). El OS pone el byte de la fila en
  `$202004` y la elige con el nibble bajo de `$202005` (bit 3 = fila 0 ... bit 0 = fila 3).
  Activos a nivel bajo (`$FF` = todos apagados). Algunos parpadean. Cuadra con el panel: 18 de los
  mandos, A-D, Store/System/Edit/Patch-Load, Panel Split y los 5 de Oct Shift. `ledRow(fila)`.
- **Botones:** 3 filas de 8. Bits 4-6 de `$202005` eligen la fila (a nivel bajo) y `$201800`
  devuelve sus 8 botones (pulsado = 0). `setButton(fila, bit, pulsado)`. Identificados pulsando
  uno a uno (`G1_PROBE=1 g1patchtest ...`): fila 0 bits 3/4/5 = slots B/C/D (y 2, casi seguro, A),
  bit 6 = Store, bit 7 = System; fila 1 bit 2 = Assign/Morph. El resto (Edit, Patch/Load, Shift,
  Navigator, Panel Split, Oct Shift, Find) falta por casar; hay que mirar los LEDs varias veces
  por el parpadeo.
- **Mandos:** canales del ADC. Mando *n* = entrada *n* de la tabla `$14420A`: `$31` = 1, `$37` = 2,
  `$2D`, `$32`, `$28`, `$2E`, `$33`, `$29`, `$2F`, `$34`, `$2A`, `$1A`, `$35`, `$2B`, `$1B`, `$36`,
  `$2C`, `$1C` = 18; `$30` = volumen maestro; `$18`, sin identificar (¿pedal?). Comprobado asignando
  un mando a cada módulo y moviendo cada canal: el OS avisa al editor del mando que cambia.
- **El ADC devuelve la conversión anterior.** Cada lectura de `$202800` da el resultado de la
  conversión anterior y arranca otra con el canal elegido. En marcha el OS elige el canal
  siguiente, lee y guarda lo leído en el anterior (`$1041BE`); al encender elige y lee dos veces.
  El emulador devolvía el canal elegido en el momento: todos los mandos iban corridos uno, y el
  volumen en marcha se leía de `$18` (por eso «el volumen no reaccionaba con el G1 encendido»).
- **Botones identificados** (fila.bit): A-D = 0.2-0.5, Store 0.6, System 0.7, Edit 1.2,
  Patch/Load 1.3, Navigator arriba 1.4, izquierda 1.5, abajo 1.6, derecha 1.7, Panel Split 2.2
  (probable: enciende el LED 3.2). Sin identificar: Shift, Find, Oct Shift −/+, Assign/Morph y la
  rueda (quedan 0.0, 0.1, 1.0, 1.1, 2.0, 2.1, 2.3-2.7). `G1_PRESS=0.7,1.6` en `g1patchtest`.
- **LEDs identificados:** slots A-D = bit 7 de las filas 0-3 (el del slot activo parpadea);
  Store/System/Edit/Patch-Load = fila 3 bits 3/4/5/6; mando *k* (1-18) = fila (k−1) mod 3,
  bit 1 + (k−1)/3; Panel Split = 3.2 (probable). Quedan 0.0, 1.0, 2.0, 3.0 y 3.1: los cinco de
  Oct Shift, en orden sin comprobar. `G1_LEDSTATE=1` los mira 20 veces en 1 s.
- **`$201000` + PORTF:** otra matriz de 8 filas que el OS barre ~19.000 veces por segundo
  (`$1177CA`), leyendo el puerto F de la CPU. Probablemente el teclado del Nord Modular con teclas;
  en el rack no hay nada.
