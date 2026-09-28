# CLAUDE.md - a stub. The instructions live in `AGENTS.md`.

One instruction file, two harnesses: Codex reads `AGENTS.md` on its own; Claude Code reads `CLAUDE.md`, so this file
exists only to import the other one.

Never write a project rule here. It would reach Claude and silently miss Codex. Put it in `AGENTS.md`, and put
learnings in `docs/PROJECT-CONTEXT.md`, never in one agent's private memory.

@AGENTS.md
