# Brief: finish the water's speed job (PT-81) - one agent, a finisher

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\water8`
(pristine base: `...\scratchpad\wt\base58`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/water-8.md` in the copy (it exists: keep it, bring its "Read this first" up to date first).

## Where the job stands

The agent before you was stopped by a usage limit at 21:10 on 2026-10-08, in the middle of its second build ("the
loop's own costs cut, the same sums"; it was also running the base build's body part alone to see whether the
closed-form line depends on which frames it gets). Its report holds the design (a grid of wave corners a water slot,
made once a frame by a new pass, that `water_sun` takes its patches from) and says nothing of what was measured.

Read first: `docs/agents/briefs/brief-water-8.md` (the job and its rules: they hold for you), `docs/agents/water-8.md`
(the design), `docs/agents/water-7.md` (the sun in the water, its fixture and numbers). Then find out what the copy
holds: diff its changed files against `wt\base58` (the main tree's `python tools/agents/merge_copy.py water8
--base=base58 --root=<the scratchpad folder> --dry` lists them), look into `%TEMP%\tgb\water8-*` for builds and logs
and into the session's `scratchpad\water8-work\` for its scripts. Write into the report what is whole, what is half
made and what has run, BEFORE changing anything.

## What to do

Finish it: the sun in the water at under a fifth of today's cost with the SAME result. The proof is water-7's sea
fixture (`TGPT_TEST_WATER_SEA=1`): the closed form (x0.99 to x1.01), waves against still water (x1.000), 16 against
256 frames, the largest single sample (26.6 times the mean), the glint's light, the foam's cover: each stated before
and after, each within its tolerance; and the fixture's time a frame for the deep-water part before and after. If a
part moves a number, that part does not ship. If the grid cannot be finished cleanly, ship the smaller certain gains
(the loop's own costs) whole, and leave the grid as a plan with what stopped it.

In the game the beach costs 223 ms a frame today (78 to 93 before the sun lit the water). Say what frame time to
expect in `water-day` with `quick`, and that the open sea's light must not move (x3.6 / 2.7 / 3.3 of the game's with
no denoiser, run `20261008-194341`).

## Rules

As `brief-water-8.md`: your one fixture through the `-GpuCheck` gate, NEVER the whole GPU test or ctest as a whole
("full test suite never in subagents"); never the game, a deploy, a commit, the main tree or the game folder; no
clamp and no stand-in; `-Throttle 8` for shader compiles; the big files by grep and ranges; the report kept current.
Final message: five lines at most: what is whole, the time before and after, each fixture number before and after,
what is not run, the report's path.
