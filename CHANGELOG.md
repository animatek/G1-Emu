# Changelog

## 2026-09-18

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
