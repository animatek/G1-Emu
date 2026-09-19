# Changelog

## 2026-09-19

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
