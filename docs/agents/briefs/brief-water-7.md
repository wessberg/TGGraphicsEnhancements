# Brief: the sun lights the water's body (the sea's colour, and the green orbs), then the foam (PT-81, PT-85) - one agent

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\water7`
(pristine base: `...\scratchpad\wt\base56`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/water-7.md`; write it early and after every step.

## Where the water stands (2026-10-08; read `docs/agents/water-6.md` whole, and PT-81 and PT-85 in `docs/PT-REQUESTS.md` by grep)

The owner: "water and foam is improved, but far from there still", and of NRD: "we see these weird fireflies/glowing
orbs here and there appearing". Pictures (the MAIN tree, read only):
`C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\20261008-144459\water-day-before-after.jpg` (the
game, before and after the last water job) and `...\20261008-145509\orb0.jpg`, `orb1.jpg` (an orb: the game, no
denoiser, three NRD frames). Kept frames: `...\20261008-145509\water-day\2-traced-none` and `3-traced-nrd`.

- The game's sea: dark GREEN water, wave shapes, thin white foam streaks on crests and at the shore, the bed showing
  in the shallows. Traced now: near-black brown water with large soft clouds of foam metres wide; 1.5 to 1.7 times the
  game's light as a whole.
- THE ORBS: with no denoiser single green dots (a few render pixels a frame); with NRD each becomes a ball about 12
  render pixels wide with a burnt-out white core and a green glow, held for 6 to 8 frames. Their source is in
  `pt_water_path.hlsli` (near line 55) and in `water-6.md` section 12: the sun does not light the water's body. A
  path inside the water gets the sun only when it happens to leave straight at the sun's disc (about one path in a
  hundred thousand) and then carries the whole sun. The same gap leaves the body near black.
- Also in section 12: the water's own smoothness is not used (the surface is a clear mirror facet at the wave
  normal, so the sun's glints are single burnt pixels where the game's are soft).

## What to do, ONE THING AT A TIME, in this order; report after each

1. **The sun lights the water's body.** At every point where a path scatters inside the water (and at the bed under
   it), ask for the sun through the surface: its direction bent at the wavy surface, what the surface lets through,
   what the water takes on the way down. Unbiased: light that a path may also find by chance is counted once (the
   chance hit of the disc goes, or is weighed against the asked-for light). No clamp, no stand-in colour, no "about
   right" tint: the water's scattering and absorption values are the game's (the plugin logs them: `Water: "Ocean"
   numbers: ...` in the game's `BepInEx\LogOutput.log`, run `20261008-144459`). If the caustics pass's refracted
   sun beams are the right carrier (water-6 thought so), use them; say what happens with the Caustics row off.
   Your check (extend `tests/water_sea_gpu.inl`, `TGPT_TEST_WATER_SEA=1`): (a) the deep sea's mean colour against a
   long accumulation of the same scene; (b) no sample in the water over N frames is more than a stated small multiple
   of its pixel's mean (today's dots fail it).
2. **The water's own smoothness** (HDRP's values, near and far): the sun's and the lamps' glints as a lobe that is
   sampled toward the light, soft as the game's, not a chance hit of a mirror facet.
3. **The foam: where it is and how much.** Thin streaks on crests, not clouds metres wide. Find out what differs
   from the game: the foam's tiling and scale against the texture's world size, how the game turns "amount",
   "threshold" and "intensity" into cover (its surface foam is carried over time in a buffer the simulation writes:
   does the tracer read that, or make its own from the waves of this instant?), the shore's foam.
   NOTE: the last lines of `water_foam` (`pt_water_foam.hlsli`) were found to be a port of Unity's `FoamErosion`;
   the owner is deciding what to do with them. Do not port anything more from Unity's or the game's shader code:
   learn what it does, write it your own way, and say in the report where the knowledge came from.

## Rules (the owner's, 2026-10-08: one thing at a time, short loops; the card is not ours)

- START NO GPU TEST until the coordinator tells you the card is free for workers. Reading, CPU arithmetic over the
  fixture's data and over the kept frames, code, shader compiles (`tools\Build-PtShaders.ps1 -BuildDir <yours>
  -Throttle 8`) and builds are fine. If only GPU checks are left: their exact command lines in the report, then stop.
- When the card is yours: your one fixture, minutes. Never the whole GPU test, never two builds' whole runs.
- Never the game, a deploy, a commit, the main tree or the game folder. Build with `-DTGPT_WITH_NRD=OFF`.
- The coordinator runs ONE scene in the game after each step (`water-day` with `quick`): write what it should show
  (the sea's colour against the game's, no green dots with no denoiser, no orbs with NRD) and how to count it.
- Do not tune or touch a denoiser or the tracer's filters to hide a bright sample: fix where it is made.
- Keep tokens down: never read `pt_common.hlsli`, `tracer.cpp`, `tests/pathtracing_gpu_test.cpp` whole; grep, then a range.
- RTXDI and NRD sources never copied or quoted. No ABI change unless needed; say so at the top if there is one.
- Final message: five lines at most: what each step did (with its number), what is built, what is not run (command
  lines), the report's path.
