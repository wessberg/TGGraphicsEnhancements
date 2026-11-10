# Contributing

Read [AGENTS.md](AGENTS.md), [BACKLOG.md](BACKLOG.md) and [PROJECT-CONTEXT.md](docs/PROJECT-CONTEXT.md) before changing rendering behaviour. [README.md](README.md) covers building; [TESTING.md](docs/TESTING.md) records validation.

Keep changes focused and describe the player-visible problem, implementation, checks and remaining limits.
Separate build success, synthetic validation, in-game evidence and visual acceptance. Query runtime capabilities rather than assuming support from GPU names.

Path Tracing remains tabled. Its [status](docs/PT-STATE.md), [requirements](docs/PT-DESIGN.md) and [issue register](docs/PT-REQUESTS.md) preserve the restart point. Do not treat older design proposals as implemented.

Record technical decisions and measurements with their conditions. Keep conversations, prompts, personal local paths, SDK downloads and generated captures out of source. Preserve third-party notices and never commit proprietary game assemblies or restricted SDK source.

For CPU source checks: `python tests/source_checks.py`. GPU/game runs need an appropriate available test machine and explicit scheduling; do not start them merely to validate documentation.
