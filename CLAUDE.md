# G1-Emu

Emulación del **Nord Modular G1** sobre el núcleo de Gearmulator: el OS original
del G1 corriendo en un 68331 y unos DSP56303 emulados, y tocado desde **Animatek NME**
(`../Nomad2026/`) como si fuera el sinte. Nombre provisional: el producto todavía no
tiene nombre, y es mejor que no lleve "Nord" ni "Clavia", porque son marcas.

Es un proyecto aparte de `../Elektron-Emu/` (MM Voice). No comparten build ni ROMs.

## Estado

Todavía no hay código. El análisis de la ROM y el plan están en `NOTAS.md`: la CPU es
un 68331, el mismo que emula Gearmulator para el Nord Lead 2X (`source/nord/n2x`), y
esa emulación es la plantilla.

## Reglas

- **Las ROMs nunca entran en el repo** ni en un release. `Roms/` está fuera de Git:
  el OS 3.03 del rack, el actualizador oficial y el editor de Mac.
- **Licencia:** si se enlaza con Gearmulator es GPLv3. NME solo habla MIDI y es otro programa.
- Antes de tocar el sinte de verdad desde aquí, mirar la conexión (ver la memoria
  "NME: mirar la conexión antes de tocar slots").

## Changelog

Cada cambio va en `CHANGELOG.md` de este repo **y** en el global
`/mnt/SPEED/CODE/CHANGELOG.md` (regla de `/mnt/SPEED/CODE/AGENTS.md`, sección Global
Changelog). El global es un enlace a Obsidian: se edita su destino, no se reemplaza.
