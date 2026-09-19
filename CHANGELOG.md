# Changelog

## 2026-09-19

- **Primer audio del patch (Claude, cambio local, sin commit).** El replay de Codex no sonaba
  por un fallo del propio replay: en la sesión grabada NME se reconectó a un G1 reiniciado, así
  que la segunda subida de patch recibió otra vez pid 1; en el replay el OS da pid 2 y descarta
  los mensajes siguientes (los módulos y la nota). `g1boot ... replay` reescribe ahora el pid
  con el que asigna el OS (ACK `$36`) y rehace el checksum. Con eso el DSP 0 carga los módulos,
  enlaza su código en `P:$197` y saca por el ESSI0 una onda periódica a **261 Hz** (Do central,
  la nota del replay), saturada y escalonada. El DSP 1 la recibe; al DSP 3 aún no llega.
  `G1_TAP=fichero` vuelca lo que sale por el ESSI0 de cada DSP. Verificación: compilado todo,
  CTest pasa, replay de 60 M instrucciones con 19 mensajes reescritos, FFT de la salida.

- **Codex — cambio local, sin commit:** validadas las extensiones pendientes del JIT
  para MOVEM corto y DO FOREVER en copia de compilación, con límite de una iteración
  DO por despacho. Corregida la inicialización de SR y la lectura del acumulador en
  `g1dspcheck`; registrado en CTest. CMake incluye solo los núcleos y el puente MIDI
  necesarios, evitando generar archivos dentro del clon externo de Gearmulator.
  Verificación: compilados `g1boot`, `g1run`, `dspdis` y `g1dspcheck`; CTest pasa
  MOVEM, invalidación JIT, DO FOREVER, IRQD y DO anidado (bloques 1/32); `git diff --check`.
  Replay real de 60 M de instrucciones y 45 mensajes/705 bytes con nota pulsada:
  cuatro DSP arrancados, salida todavía cero en DSP0–2 y `$155` en DSP3. Documentado
  que falta revisar el enlace del código del patch; no se afirma que ya suene.

- `SIGUIENTES-PASOS.md`: el plan para la siguiente sesión (sacar el audio, rendimiento) y las ideas
  de Javier (usar el G1 emulado para mejorar NME, recrear módulos a partir de su código DSP, un
  patch como plugin). `AGENTS.md` para Codex/opencode.
- `g1run` ya no graba el WAV siempre: solo con `G1_RECORD=segundos`. Una noche sin tope había
  llegado a 35 GB (borrado).

- **El OS ya carga el código de los patches en los DSP y el oscilador calcula.** Arreglados cinco
  fallos encadenados: el reloj del sistema (el PIT del SIM, que Gearmulator no emula), una espera
  excesiva en las consultas de estado HI08, el arbitraje de host commands (incompatible con las
  interrupciones rápidas del G1), la cola de interrupciones que se llenaba en un solo hilo, y
  IRQD, que no respetaba el IPRC. Además, las máscaras de slots del ESSI arrancan como en el chip
  (todas activas). Todavía no sale audio. Detalle en `NOTAS.md`.
- `g1boot`: la reproducción manda los mensajes espaciados, como NME; nuevo modo `diff`, y vuelcos
  de ESSI, vectores atendidos, modo de proceso y cambios de memoria de cada DSP.

- **NME se conecta al G1 emulado y construye patches** (Javier montó OscA → 2Output; el OS lo
  confirma todo y hasta informa de la carga de DSP). Todavía **no suena**.
- Audio: reloj del ESSI a 96 kHz, reloj de proceso por IRQD, medidores y grabación a WAV de la
  salida del DSP 3 en `g1run` (`~/.local/share/Animatek/G1-Emu/salida.wav`), y registro de lo
  que entra por el PC Port (`pcport-in.bin`) para reproducir sesiones.
- Arreglado el arranque doble del DSP 3: su programa de sonido quedaba incompleto. Ahora las
  palabras pendientes pasan a la ROM de arranque. Si la CPU consulta el estado con una palabra
  pendiente, el DSP avanza hasta recogerla, para que el OS no la descarte.
- `g1boot ... replay FICHERO`: reproduce una sesión de NME sin NME y vuelca DMA, búferes y
  salida de cada DSP. `G1_WATCH`: puntos de observación en el código del OS.
- Diagnóstico de por qué no suena: el OS asigna 0 voces (su comprobación de recursos dice
  "no cabe" a todo), así que nunca carga el código del patch en los DSP. En `NOTAS.md`.

## 2026-09-18

- **`g1run` / `g1.sh`: el G1 emulado en tiempo real con puertos MIDI virtuales.** El cliente ALSA
  "G1-Emu" tiene dos puertos, "PC Port" (editor) y "MIDI". La flash se guarda en
  `~/.local/share/Animatek/G1-Emu/flash.bin` al salir y cuando el OS escribe en ella. Probado
  con `aseqsend`/`aseqdump`: el IAm por el PC Port recibe su respuesta. Va al ~94% del tiempo real.

- **El G1 emulado contesta al saludo de NME.** Hay dos puertos, como en el aparato: el MIDI
  IN/OUT es la SCI de la CPU (conectada con `SciMidi`), y el PC PORT del editor es un DUART
  SCN2681 externo en un bus paralelo hecho con el puerto GP y el puerto E. `g1Lib/g1duart.h`
  lo emula, y el aviso de byte recibido (RxRDY → PAI → interrupción PAOV, vector IVBA+`$A`) se
  emula en la CPU porque el GPT de Gearmulator no tiene acumulador de pulsos. Al *IAm*
  `F0 33 00 06 00 03 03 F7` responde `F0 33 00 06 01 03 03 3F 7F 7F 01 F7`.

- **Los 4 DSP56303 arrancan con el programa del OS** (`g1Lib/g1dsp`). Hay 8 puertos HI08 y los 4
  de la placa base llevan DSP detrás; las banderas HF van y vuelven, hay host commands y el salto
  a `$FF0000` vuelve a la ROM de arranque. Para que nada se bloquee en un solo hilo, los ESSI
  reciben silencio continuo, como de un códec. DSP 3 arranca dos veces (cargador + OS), como
  en el aparato. Todo va al ~88% del tiempo real. Nueva herramienta `dspdis`.

- **El OS 3.03 del G1 arranca en el 68331 emulado y llega a su bucle principal.** Hay `g1Lib`
  (CPU con ROM, RAM y flash) y `tools/g1boot` (arranque sin interfaz, registro de accesos a
  hardware desconocido, chip-selects y desensamblador). Descubierto por el camino: el cargador
  elige el modo según las teclas al encender, el OS se copia desde la flash de `$300000` a la
  RAM, y los 4 DSP están en `$200000`/`08`/`10`/`18` por HI08. La flash se emula como un AMD
  Am29F080, uno de los tres chips que acepta el OS; en el primer arranque formatea la zona de
  patches. Todo en `NOTAS.md`.

- Proyecto nuevo, separado de `Elektron-Emu`. Análisis del OS 3.03 del rack en `NOTAS.md`:
  la CPU es un 68331 (lo confirman los accesos a GPT/SIM/QSM), los DSP son 56303 y la zona
  `$50000`–`$5FFFF` parece el código de los DSP. La plantilla es el Nord Lead 2X de Gearmulator.
