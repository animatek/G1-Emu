# Changelog

Todo lo que cambia en G1-Emu, lo más reciente arriba. **Regla: cada cambio que entra en el repo
lleva aquí su línea, en el mismo commit** (ver `CLAUDE.md`). Cada entrada dice quién lo hizo, qué
cambia y cómo se ha comprobado; el commit es el que la trae (`git log -- CHANGELOG.md`). Las
entradas antiguas citan su commit a mano.

## 2026-09-19

- **Panel como el del aparato, mandos en su sitio y repo listo para ser público (Claude).**
  El ADC devolvía el canal elegido y no la conversión anterior: los mandos
  iban corridos uno y el volumen en marcha se leía de otro canal; arreglado. Identificados con
  `g1patchtest` (nuevos `G1_KNOBS`, `G1_ADCSWEEP`, `G1_LEDSTATE`, `G1_PRESS`) los 18 mandos y sus
  LEDs, los LEDs de slots y modos, Edit, Patch/Load y el navegador. La ventana, rehecha a partir de
  las fotos: pantalla con la fuente de puntos del HD44780, mandos rojos y negros con el número
  bajo el LED, Panel Split, Find/Panic, Oct Shift, Assign/Morph, Shift, la rueda y un LED de MIDI;
  sin la tira de matrices. `LICENSE` (GPLv3), `README.md` y la regla del changelog en `CLAUDE.md`
  y `AGENTS.md`. Verificación: barrido del ADC con los 18 mandos asignados, 18 pruebas de LED y
  24+36 de botones, audio igual que antes, capturas de la ventana; historial revisado (sin ROMs).

- **Ampliación del post de Patreon (Codex; cambio local, sin commit).** Añadido
  en ambos idiomas el apartado de edición de varios G1 físicos o emulados desde
  Animatek NME como mejora prevista, con la referencia a los cuatro dispositivos
  del editor original y un par MIDI In/Out del PC Port por dispositivo.
  Verificación: texto revisado contra `SIGUIENTES-PASOS.md`, sección 3a-bis;
  `git diff --check` y exclusión del borrador con `git check-ignore -v` correctos.

- **Post de Patreon bilingüe (Codex; cambio local, sin commit).** Borrador corregido
  en `Patreon post/2026-09-19-g1-emulation.md`, inglés primero y español debajo,
  con aviso de pre-alpha sin beta, espacio para el vídeo y posibilidades futuras
  diferenciadas de funciones disponibles. Carpeta excluida mediante `.gitignore`.
  Verificación: revisión editorial contra las notas del proyecto y fuentes de los
  autores; `git check-ignore -v` confirma la exclusión, `git ls-files` no incluye
  el borrador y `git diff --check` pasa. Sin publicación en Patreon.

- **La ventana: primer panel en JUCE (Claude, commit `46c7b8d`).** `EmuHost` saca el bucle de `g1run` (flash, MIDI,
  JACK/ALSA, tiempo real y estadísticas) a una clase con su hilo; `g1run` queda en una consola
  fina y ahora dice la carga (~55%) y los núcleos (~2,9). `g1gui` (`./g1gui.sh`): pantalla con
  los caracteres propios de la CGRAM, 18 mandos y volumen (ADC), los botones y LEDs identificados,
  vista de matrices en crudo y barra de estado. JUCE se incluye una vez en el CMake principal.
  Verificación: compilado, `g1run` 10 s por JACK igual que antes, ventana abierta con la pantalla
  del G1 («Empty Patch», voces por slot) y el LED del slot A.

- **El panel, emulado (Claude, commit `f1e7573`).** Pantalla LCD HD44780 (`$202006/7`),
  32 LEDs en 4 filas y matriz de 24 botones (`$202004/5`, `$201800`), con API para una interfaz.
  Identificados A-D, Store, System y Assign/Morph. Verificación: `g1patchtest` enseña la pantalla
  del G1 (nombre del patch y voces por slot) y responde a los botones (menú System, Store, slots).

- **Los módulos de control, las entradas y las cuatro salidas (Claude, commit `f1e7573`).** El JIT de Gearmulator no seguía los cambios del registro LA, y el OS alarga así el
  bucle principal al cargar módulos de control: envolventes, relojes, maestro/esclavo y el LFO del
  chorus y la cantidad del overdrive se quedaban quietos. Ahora se resincroniza (`onLaChanged`).
  Dos modos de DMA que faltaban (fija→fija y bloque por petición sin borrar DE) dan las entradas
  de audio. Las salidas estaban cruzadas por parejas (1↔2, 3↔4). `g1run` por JACK con
  `out_1..out_4` e `in_L`/`in_R`. Nuevo banco de pruebas `g1patchtest`. Verificación: batería de
  los 101 tipos de módulo, A/B del chorus (L≠R) y del overdrive (sigue al mando), AudioIn con senos
  distintos en L y R, 4Output con cuatro señales, `g1run` por JACK 16 s sin cortes, CTest.

- **El nivel, explicado (Claude, commit `f1e7573`).** El OS del rack limita el volumen
  maestro a −36 dB: lo saca de una tabla de 128 valores (`$153CAC`) indexada con ADC ÷ 2, al
  encender y al mover el mando. Los −62 dBFS de un OscA → 2Output son lo que calcula el OS; el
  emulador no pierde nivel, y los +36 dB de `g1run` deshacen ese tope. Nuevos `G1_FINDTX` y
  `G1_ADCALL` en `g1boot`. Verificación: traza de la CPU hasta `Y:$5F` del DSP 3, tabla leída de
  la ROM y replay con los 20 canales del ADC al máximo (mismo nivel).

- **Suena limpio: reloj real de los DSP y enlaces de 9 palabras (Claude, commit
  `79e16aa`).** Los DSP van a 82,944 MHz (864 ciclos por muestra, del `PCTL` del OS) con la IRQD en
  una rejilla fija común; los ESSI, al ritmo que sale de su CRA (96 ciclos por palabra en los
  enlaces). El enlace entre DSP va por posición (cada palabra al sitio del anillo de recepción
  donde va a escribir el DMA, del bloque de hace 8) y la salida se lee del DSP 3 bloque a bloque.
  Los escalones y clics venían de ahí: el enlace movía 2 de las 9 palabras por muestra y los
  canales se desplazaban. `g1run` sube +36 dB por defecto (el nivel sigue a −62 dBFS). Nuevo
  `G1_BLOCKS` en `g1boot`. Verificación: compilado, CTest, replay con FFT (Do a 261,6 Hz por 1/2,
  armónicos −82 dB, un salto en toda la ejecución) y `g1run` al 100% en tiempo real. Javier lo ha probado con NME: sin ruidos.

- **Suena en tiempo real por la tarjeta de sonido (Claude).** `g1run` saca las salidas 1/2 por
  ALSA (`app/alsaaudio.h`, 48 kHz, +24 dB provisional; `G1_AUDIO`, `G1_GAIN_DB`). Los 4 DSP van
  en hilos propios, y su audio pasa de uno al siguiente en el hilo de la CPU cuando todos han
  parado: 100% del tiempo real (antes ~79%), replay 2,2× más rápido, mismo audio que en serie
  (`G1_THREADS=0`). Verificación: compilado, CTest, replay en los dos modos con FFT y conteo de
  cortes, `g1run` 15 s (96.000 tramas/s, sin cortes tras el arranque). Pendiente: escalones y
  clics del propio DSP 0.

- **Ya suena por la salida (Claude, commit en este repo).** El audio recorre la cadena
  DSP0→1→2→3 y el DSP 3 saca el Do de la nota a 261 Hz. Tres arreglos: el DMA de doble contador
  en origen y destino (Gearmulator no lo tenía y no copiaba nada), las transferencias de bloque
  inmediatas (la copia llegaba tarde y pisaba la voz) y el volumen maestro, que el OS lee del
  ADC del panel (código `$30`) y el emulador daba a 0. Verificación: compilado todo, CTest,
  replay de 60 M instrucciones y FFT de la salida de los 4 DSP. Pendiente: nivel muy bajo
  (−62 dBFS), escalonado y cortes sueltos; salida a la tarjeta de sonido.

- **Primer audio del patch (Claude, commit `5450cd8`).** El replay de Codex no sonaba
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
