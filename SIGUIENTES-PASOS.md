# G1-Emu — siguientes pasos

Estado al cerrar la sesión del 2026-09-19. El detalle técnico de todo lo averiguado está en
`NOTAS.md`; esto es el plan.

## Dónde estamos

- El OS 3.03 del rack arranca en el 68331 emulado, con su flash, su reloj del sistema (PIT) y
  los 4 DSP56303 cargados con su programa.
- `./g1.sh` lo corre en tiempo real (~88–95%) con dos puertos MIDI virtuales: **PC Port** (el
  editor) y **MIDI**.
- **Animatek NME se conecta y monta patches.** Al insertar un módulo, el OS para los DSP, carga
  el código del módulo y los reanuda; el oscilador calcula (su fase avanza en el DSP 0).
- **Ya suena** (desde la tarde del 2026-09-19): el oscilador del patch sale por el DSP 3 y por la
  tarjeta de sonido a la altura de la nota, aunque flojo, escalonado y con clics.

## 1. Sacar el primer sonido (prioridad: **que suene**)

**Actualización 2026-09-19 (tarde): suena por la tarjeta de sonido y en tiempo real.** Ver
`NOTAS.md` («Sale por el DSP 3» y «Tiempo real»). Lo que queda, por orden:

1. ~~**Los escalones y los clics**~~ Hecho (2026-09-19, tarde): reloj real de los DSP y enlaces
   entre DSP a 9 palabras por muestra, por posición. El Do sale limpio por 1/2 (armónicos −82 dB,
   sin saltos). Ver `NOTAS.md`, «Sonido limpio».
2. ~~**El nivel**~~ Explicado: el OS limita el volumen maestro a −36 dB (tabla de 128 valores
   indexada con ADC ÷ 2; ver `NOTAS.md`). El emulador no pierde nivel; `g1run` sube +36 dB para
   deshacer ese tope. Queda comprobar con el G1 real, grabando el mismo OscA → 2Output con el
   volumen al máximo, si el aparato suena más (etapa analógica).
3. ~~**Módulos de control muertos**~~ Hecho: el JIT no seguía los cambios de LA (ver `NOTAS.md`).
   Chorus, overdrive, envolventes, relojes y maestro/esclavo funcionan.
3b. **Una prueba propia para cada módulo** que la batería no puede juzgar (LFO lentos, lógica,
   secuenciadores con reloj, S&H, DrumSynth con disparo...), con `g1patchtest`.
3c. **Probar patches más complejos** (mixer, filtros, relojes, secuenciadores) ahora que el
   enlace es fiable: lo que falle ya será de los módulos o del OS, no del transporte.
4. **Carga de la CPU:** el replay va a 1,21× del tiempo real (antes 1,31×): el ESSI a su ritmo
   real cuesta un 8%. Si falta margen, el TX de los enlaces ya no hace falta (se lee de memoria).
5. **Volumen en marcha:** el OS no reacciona si el ADC cambia con el G1 encendido. Mirar los
   eventos `$100|canal` que genera `$1009BE` y quién los consume.

1. ~~**Seguir el código del módulo de salida.**~~ Hecho: el patch se enlaza en `$197`. Desde `$197` del DSP 0 (el código del patch va
   detrás de la rutina de bloque `$175`), ver dónde escribe 2Output su muestra y si llega a
   `Y:$6C0/$6E0`, de donde lee el DMA4 hacia TX0.
2. ~~**Topología de los ESSI entre DSP.**~~ Resuelto: cadena DSP0→1→2→3 (ver `NOTAS.md`). ¿Bus TDM compartido (todos los DSP y el códec en la
   misma línea, cada uno en sus slots) o cadena DSP0→1→2→3? Pistas: el DSP 3 pone
   `TSMA=$FFFFF9` y `RSMA=1`, copia lo que recibe hacia su salida y recibe su propio `$155`; los
   DSP de voz no tocan sus máscaras. Ahora hay una cadena provisional (hipótesis) en
   `g1mc.cpp` (`setNext`). Si alguien ha documentado la placa del G1 (esquemas, fotos, foros de
   reparación), ahorra mucho tiempo.
3. **Nota:** el G1 solo calcula voces con nota. En la sesión grabada la nota va por el PC Port
   (`cc=$17`, `sc=$56`: `00 3C` pulsa, `01 3C` suelta). La prueba con la nota mantenida son
   los **45 primeros mensajes** del `pcport-in.bin` actual (hasta `56 00 3C`).
4. ~~**Salida a la tarjeta de sonido**~~ Hecho en `g1run` (salidas 1/2). Pendiente, 3/4: ESSI0 del DSP 3 = salidas 1/2 y ESSI1 =
   3/4 (probable), a 96 kHz, con remuestreo al dispositivo.

Herramientas:
- `g1boot ROM N replay FICHERO`: reproduce una sesión de NME espaciada, como NME.
- `g1boot ROM N diff A.bin B.bin`: código del OS que ejecuta un mensaje concreto.
- `G1_WATCH=dirs`: puntos de observación en el OS. `G1_MARK=1`: marca los búferes del DSP 0.
- `g1run` graba en `~/.local/share/Animatek/G1-Emu/pcport-in.bin` todo lo que manda NME, para
  reproducirlo. `G1_RECORD=10 ./g1.sh` graba 10 s de salida en WAV (sin la variable no graba
  nada: una noche sin tope llegó a 35 GB).

## 2. Rendimiento (para tocarlo en directo)

- **Hecho:** un hilo por DSP; `g1run` va al 100% con margen (el replay corre 2,2× más rápido).
- Si hace falta más: saltarse el bucle de espera de los DSP sin voces y compilar con PGO (como en
  Elektron-Emu).

## 3. Ideas de Javier para después (apuntadas el 2026-09-19)

### 3a. Usar el G1 emulado para mejorar NME

Con el OS real corriendo en el emulador tenemos un G1 "de laboratorio" sin encender el sinte:
- **Banco de pruebas automático para NME:** conectar NME al emulador y probar subidas, ediciones,
  bancos, morphs, etc. sin hardware y sin riesgo para los patches del G1 real.
- **Ver lo que hace el OS por dentro al recibir cada mensaje** (con `diff` y `G1_WATCH`): qué
  mensajes acepta, cuáles ignora, cuándo contesta con ACK y cuándo con NewPatchInSlot, qué
  recargas dispara y cuánto tardan. Con eso se puede ajustar el ritmo y el orden de envío de NME
  a lo que el OS realmente soporta, que es de donde salen muchos cuelgues.
- **Tiempos exactos:** medir cuánto tarda el OS en procesar cada tipo de mensaje y cuándo
  descarta palabras (la rutina de envío a los DSP descarta si el DSP no está listo en 10
  consultas). Sirve para que NME no sature al sinte real.
- **Reproducir cuelgues:** si NME se cuelga con el G1 real, grabar la sesión y reproducirla en
  el emulador para ver qué le pasó al OS.

### 3a-bis. NME con varios G1 a la vez (idea de Javier, 2026-09-19)

- El editor original editaba **hasta 4 Nord Modular a la vez**: su «MIDI Setup» tiene Port 1–4,
  cada uno con su In/Out y su casilla Enabled. NME ahora solo maneja uno.
- Con el G1 emulado tiene más sentido que nunca: editar el emulado y el real a la vez desde el
  mismo NME (por ejemplo, comparar un patch en los dos). Es trabajo de NME, no del emulador.
- Pendiente de pasar a Google Tasks («Issues para la IA»): `gws` tenía el token caducado.

### 3b. Recrear módulos a partir de su código DSP

- El OS lleva el código DSP de cada módulo (tablas de recursos por tipo en `$1C3B0C`,
  `$1C3B24`, `$1C3B28`, en pasos de `$30`; el cargador `$122F72` lo sube con `$B2/$B3/$B4`).
  Leyendo ese código (osciladores, filtros, envolventes, el DrumSynth) se puede entender cada
  algoritmo exacto.
- Con eso se pueden hacer **recreaciones nativas** (C++, sin emulación) de módulos concretos:
  osciladores, filtros o el DrumSynth en un VST o en un módulo de VCV.
- **Ojo con la licencia:** el código DSP es de Clavia. No se puede copiar ni distribuir; hay
  que reimplementar el algoritmo (estudiar cómo funciona y escribirlo de nuevo). Es lo mismo que
  en Elektron-Emu: las ROMs nunca viajan.

### 3c. Un patch del G1 como plugin

- Convertir un `.pch` en un plugin que suene como ese patch, sin tener que montarlo.
- Dos caminos: **(1)** con la emulación: el plugin arranca el G1 emulado con ese patch cargado y
  solo expone sus knobs (necesita la ROM del usuario); **(2)** "compilar" el patch a C++ nativo
  con los módulos recreados de 3b (sin ROM, pero solo con los módulos que estén recreados).

### 3d. Una interfaz para el G1 emulado (en marcha)

- **Hecho (2026-09-19):** el modelo del panel en el emulador: pantalla HD44780, 32 LEDs, matriz
  de 24 botones y mandos por el ADC, con API para leerlos y moverlos desde otro hilo. Ver `NOTAS.md`.
- **Hecho:** `EmuHost` (el bucle de `g1run` en una clase) y la primera ventana en JUCE (`g1gui`,
  `./g1gui.sh`): pantalla, mandos, botones, LEDs y barra con velocidad, carga y núcleos.
- **Falta:** casar los botones que quedan (Edit, Patch/Load, Shift, Navigator, Panel Split, Find,
  Oct Shift), los LEDs de los mandos y de los modos, y comprobar el orden de los mandos contra el
  aparato; la rueda (encoder), que aún no se sabe por dónde entra; y un aspecto más fiel.

### 3d-antes. Una interfaz para el G1 emulado (idea original)

- Ahora es una aplicación de consola. Más adelante: una ventana con el panel del rack (LEDs,
  display, botones y knobs), que es lo que el OS ya pinta y lee en `$201000`, `$201800`,
  `$202000–7` y `$202800` (sin emular todavía), además del estado de los DSP y el audio.

### 3e. Otros modelos del G1: teclado y Micro Modular

- **Nord Modular (teclado):** en principio es el rack con teclado. Debería servir casi todo; cambia
  la ROM/OS y el panel.
- **Micro Modular:** Javier cree que lleva un solo DSP. Si es así, es un "motor" más sencillo y
  podría ser un buen primer G1 que suene: sin cadena ni bus entre DSP. **Por confirmar:** su
  hardware (CPU, DSP, memoria) y conseguir su OS (ROM propia, distinta de la del rack).

### 3f. Módulos nuevos dentro del G1 (OS modificado)

- Idea: módulos propios (secuenciador euclídeo, oscilador aditivo, **salida MIDI desde el patch**)
  cargados en el G1 real con una actualización de OS por MIDI.
- Qué haría falta: el código DSP del módulo (ensamblador DSP56300), darlo de alta en las tablas de
  módulos del OS (recursos, parámetros, conexiones), y que NME lo conozca (NME es nuestro). Una
  salida MIDI además necesita que el DSP pase datos a la CPU y la CPU los saque por la UART:
  cambios en el código 68k del OS.
- Red de seguridad: el cargador de la ROM trae el OS de fábrica y un modo de actualización por
  MIDI (combinación de teclas al encender), así que un OS malo se puede recuperar. **El emulador
  es el sitio para probarlo todo antes de flashear el aparato.**
- Proyecto grande y a largo plazo; primero hay que entender el formato de las tablas de módulos.
- **¿Cabe?** Hay que distinguir dos cosas:
  - *Cuántos tipos de módulo puede conocer el OS:* depende del espacio en la flash (1 MB, con el
    OS de ~470 KB y la zona de patches que el OS formatea) y del tamaño de las tablas. El código
    DSP de un módulo son unos cientos de palabras, así que un puñado de módulos nuevos no debería
    ser problema. Hay que medir el hueco real.
  - *Cuántos módulos caben en un patch:* lo limita la memoria y el tiempo de cada DSP (4K palabras
    de programa internas; el OS calcula el reparto y las voces). Un módulo nuevo muy pesado solo
    significa menos voces, no que no se pueda añadir.
- **Módulos del G2 en el G1:** los sencillos probablemente sí; los que dependen de la potencia de
  los DSP del G2 (56362, más rápidos y con más memoria) quizá no quepan o den pocas voces. Se
  prueba en el emulador.

### 3g. Referencia: el plugin de voz de Monomachine sin ROM

- El autor del plugin del vídeo (en blanco y negro; "Monomodule") dice que será open source y que
  no necesitará ROM. Encaja con reescribir los motores en C++ a partir de estudiar el firmware
  (como el repo `glassg333/mmmm`), que es el camino de 3b. Cuando lo publique, revisar cómo lo ha
  hecho y con qué licencia.

## Para el agente de Codex

Empieza por `CLAUDE.md` (compilar y usar), `NOTAS.md` (todo lo averiguado) y este fichero.
La tarea abierta más útil es el punto 1: seguir en el DSP el código del patch (desde `$197`) y
el cableado de los ESSI hasta que salga una muestra distinta de cero por la salida del DSP 3.
No toques `~/src/gearmulator-md-mm` (clon de terceros) ni subas ROMs.
