# Brief: sky-3 - finish sky-2's build in its copy: compile, fix, run its own sky check (PT-112, the sky) - one agent, one job

Copy: `C:\Users\Frede\AppData\Local\Temp\tg-agents\wt\sky2` (pristine base: `...\wt\base20sky`). Follow `AGENTS.md` there;
read "The owner's rulings" first (no shortcuts or stand-ins; no limits that change the picture; the game's own textures,
never copies). You are the ONE subagent the owner allows at a time (2026-10-11).

## Why

sky-2 (a Claude worker) wrote the tracer's own sky and stopped at a usage limit before compiling it. Its report, with
"State when stopped" and the exact next steps: `docs/agents/sky-2.md` IN THE COPY. Read that report first and whole; the
design behind it is `docs/agents/sky-1.md`. Keep writing into `docs/agents/sky-2.md` (a section "sky-3" at its end),
early and as you go; "State when stopped" at once if a limit shows.

## What to do

1. Let the copy's shader build finish (log `%TEMP%\tgb\sky2-shaders.log`; if it is not running and headers are out of
   date, run the copy's `tools\Build-PtShaders.ps1 -BuildDir %TEMP%\tgb\sky2-build`, never `-First`).
2. Build the native C++ (`%TEMP%\tgb\sky2-build.bat` per the report) and fix every compile error; build the plugin
   (`dotnet build src\TGPathTracing\TGPathTracing.csproj -c Release -f net472 -p:TargetFrameworks=net472 -p:DeployToGame=false`).
3. Run the copy's `python tests/source_checks.py` (the base had 2 failures; add none) and sky-2's own check
   (`TGPT_TEST_SKY_ONLY=1`, steps in the report), only while the game is closed and the MAIN tree's
   `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0. Look at its PNGs; write what they show and the numbers.
4. Stop there. Do not merge into the main tree (the coordinator merges with `tools/agents/merge_copy.py`).

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration`.
- NEVER the whole `tgpt_gpu_test` or ctest as a whole (the owner: "full test suite never in subagents").
- Build only into `%TEMP%\tgb\sky2-*`. Grep and read ranges of big files, never whole. Copy no HDRP code text; never copy
  or quote RTXDI/NRD sources. Final message: five lines at most, pointing at the report.
