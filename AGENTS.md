# G1-Emu (para cualquier agente: Claude Code, Codex, opencode)

Las instrucciones del proyecto están en `CLAUDE.md`, lo averiguado del hardware y del OS en
`NOTAS.md`, y el plan en `SIGUIENTES-PASOS.md`. Léelos antes de tocar nada.

Reglas duras:
- Las ROMs nunca entran en Git ni en un release (`Roms/` está fuera).
- No se modifica `~/src/gearmulator-md-mm` (clon de terceros, GPLv3): se enlaza desde aquí.
- **Regla: todo cambio que entre en el repo lleva su línea en `CHANGELOG.md`, en el mismo
  commit.** Sin excepciones.
- Cada cambio va en `CHANGELOG.md` de este repo y en el global `/mnt/SPEED/CODE/CHANGELOG.md`
  (enlace a una nota de Obsidian: se edita el destino, no se reemplaza), con fecha de Madrid,
  verificación real y "cambio local, sin commit" si no hay commit.
