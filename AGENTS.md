# G1-Emu (for any agent: Claude Code, Codex, opencode)

The project instructions are in `CLAUDE.md`, the hardware and OS findings in `NOTES.md`, and the
plan in `ROADMAP.md`. Read them before touching anything.

**Picking the work up?** `docs/next-steps.md` is the short list, in order, with what is known
about each one and the traps around it.

Hard rules:
- **Everything in this repo is in English**: code, comments, messages, docs, changelog, commits.
- ROMs never go into Git nor into a release (`Roms/` is ignored).
- Do not modify `~/src/gearmulator-md-mm` (third-party clone, GPLv3): it is linked from here.
- **Every change that goes into the repo gets its line in `CHANGELOG.md`, in the same commit.**
  No exceptions. Newest first, with who did it, what changes and the real verification; mark
  "local change, not committed" if there is no commit yet.
- In the maintainer's workspace it also goes into the global `/mnt/SPEED/CODE/CHANGELOG.md` (a link
  to an Obsidian note: edit its target, never replace it), dated in Madrid time.
