# Brief: fog-3 - the traced fog has no moon haze at night, and the fog check says the ground under fog is too bright (PT-37, PT-138)

Copy: `%TEMP%\tg-agents\wt\fog3` (pristine base: the one the coordinator names when it starts you). Follow `AGENTS.md`
in your copy; read first its rulings "Keep the game's fog", "The tracer should light that fog too", "No fake lights",
"No limits that change the picture", "Follow NVIDIA's and AMD's guides to the letter", "Judge the picture without a
denoiser first", "The game is the first test". Report: `docs/agents/fog-3.md` IN YOUR COPY; the plan within 30
minutes, kept current; "State when stopped" at once if a limit shows.

## Why

The camera's fog is traced in cells since this morning (`docs/PT-REQUESTS.md`, grep "### PT-118" and "### PT-138";
`docs/agents/fogrule2.md`): `pt_froxel.hlsli`, `pt_fog_froxel.hlsl` (the cells' light), `pt_fog_integrate.hlsl`
(each pixel's walk), `pt_fog_tail.hlsl` (a lamp's light right by it), `pt_medium.hlsli` (the medium, and the fog a
reflected ray crosses). Two things came up today that say its LIGHT is not right yet. Both are numbers, not looks.

### A. The fog's own offline check fails on the ground (first run ever, 2026-10-12 09:37)

`tests/pathtracing_fog_froxel_checks.inl` (`TGPT_TEST_FOG_FROXEL_ONLY=1`; six minutes; `TGPT_TEST_FOG_CASE=<n>` for one
case) had never reached a frame before today. On main's build (18784FDDA14D) it reads 108 ok and three cases FAIL
"the fog is what is known exactly (each region's mean within 3 %)"; the coordinator's output is
`%TEMP%\tgb\coord-fogcheck.txt` (UTF-16). In every one the SKY and the AIR OVER THE HORIZON are right and THE GROUND
IS TOO BRIGHT:
- "open: a uniform fog, sun and sky, the light scattered once": sky x1.000, above the horizon x0.999, far ground
  x1.054 (0.06928 for 0.06574), near ground x1.241 (0.02298 for 0.01852);
- "open: no sun, no sky, a lamp 6 m ahead": round the lamp x0.997, above the horizon x1.000, far ground x1.001,
  NEAR GROUND x1.926 (0.11258 for 0.05845);
- "open: no sun, no sky, a lamp 60 m ahead": round the lamp x1.162, above the horizon x0.996, FAR GROUND x3.434
  (0.33133 for 0.09648), near ground x2.633 (0.01638 for 0.00622).
Either a surface seen through fog, or lit through fog, gets more light than it should (the light's way to the
surface not dimmed by the medium, the surface's own bounced light, the fog in front of it counted twice), or the
check's "known exactly" leaves out a part that is really there (the ground's bounced light into itself, the sky's
part on the ground). Which, per region, with the terms written out and each one switched off in the fixture: that
is the job. Whatever is wrong is mended: the tracer where it is the tracer, the check where it is the check (then
say exactly which term its value lacked, and add it by hand, not by pinning the tracer's number).
The room cases' pins are still empty (`tests/pathtracing_fog_froxel_pins.inl`: PT-138 (6)); filling them is yours
once the outdoor cases hold.

### B. At night the game's fog is a blue haze over everything; the traced fog under the moon is nearly black

The game's own Fog volume outdoors says "directional lights only": HDRP lights the mist there with the sun or the
moon and the sky's probe, with no lamp (`docs/PT-REQUESTS.md`, grep "### PT-37", its newest bullets: the table of
the game's fog values by scene). The tracer lights it with every lamp. A hidden switch since 09:30 takes the lamps
out of the camera's fog where the game says so (`Traced fog: lamps light the mist outdoors` off; frame flag bit 24,
`TGPT_FRAME_FOG_NO_LAMPS`; `fog_froxel.inl`, `pt_fog_froxel.hlsl`). In the game's night scene (run
`artifacts\harness\20261012-093855`, `night\overview.jpg`; frozen, 16 frames; luma whole (region), darkest and
brightest 5 %): the game 38.7 (41.0), 17 and 54; lamps in the mist 53.5 (67.2), 9 and 151; THE SUN AND MOON ALONE
20.5 (18.7), 3 and 52. So without the lamps the bright end is the game's, and the picture is half as bright: the
haze the moon makes in the game's fog is not in ours.
The night's numbers (the harness's audit of that save, `artifacts\harness\20261008-130029\night\1-game\audit-b.txt`):
Fog: mean free path 9 m, albedo (0.243, 0.239, 0.208), anisotropy 0.48, base height 67.56, maximum height 184.63,
Max Fog Distance 5000, tint (0.906, 1, 0.862) x 1.226 in Sky colour mode, depth extent 300, global light probe dimmer
1, "multiple scattering intensity" 1. The camera stands at y 69.7. The light that lights surfaces and fog:
`DayNightSystem_HOS_UltraPlus/Lights/ShadowCaster`, directional, 60,511 lux, colour (1, 0.988, 0.843), VOLUMETRIC
DIMMER 1.23, soft shadows (a second light of the same strength, ".../Sun", lights neither surfaces nor fog: it is
the sky's). The tracer's log has its sun there at that strength ("a white surface in the sun 1.608e+04").
What HDRP does with it (read its source, do not guess: the owner allowed downloading Unity's HDRP source to compare,
2026-10-08; the package is `com.unity.render-pipelines.high-definition` 17 for Unity 6000.0.64f1:
`Runtime/Lighting/VolumetricLighting/VolumetricLighting.compute`, `LightEvaluation.hlsl`'s height-fog attenuation
of a directional light, `AtmosphericScattering.hlsl`, the ambient probe's share): work out BY HAND what radiance
HDRP's fog sends to the camera for that medium, that light and that sky along a level ray, term by term (the
directional light dimmed by the fog above the point, times its volumetric dimmer, times the phase function it
uses; the probe's share; the tint and where it applies), and the same for the tracer's cells (`froxel_direct`,
`sun_fog`, `directional_fog`, `sky_radiance`, `fog_phase`). Then build the case in the fog fixture (the night's
medium, a directional light at a moon's height, a dark sky) and read both. THE FACTOR BETWEEN THEM, AND WHICH TERM
HOLDS IT, is what is asked. Known not to be applied in the tracer: a light's volumetric dimmer (x1.23 here: far too
little to explain half a picture). Not known: everything else.
Mend what is the tracer's fault. What is a difference of kind (HDRP's own approximations: its phase function, a
blur it calls multiple scattering, its tint) goes into the report as a choice for the owner with its number; do
not copy an approximation of HDRP's into the tracer on your own.

## Order and hand-overs

A first (hours, offline alone). Then B. A hand-over after each: the list of files, and for B a DLL (path, SHA-256)
with the plugin's change if any; the coordinator runs `night@fog-lamps` and `fog@traced-fog` and sends you the
numbers. Behind a hidden switch only where the change is a choice; a plain fault is mended plainly.

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree
  (`C:\Users\Frede\Developer\TGFOA\TGFrameGeneration`: read only). Write only in the copy and in
  `%TEMP%\tgb\fog3-*`. One build tree; delete your own test pictures once read.
- GPU tests: the fog's own check and its single cases only, only while the game is closed and the MAIN tree's
  `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0, asked right before EVERY test start; wait with one background
  script, never poll. NEVER the whole `tgpt_gpu_test` or ctest. If a game of the owner's opens while a test of
  yours runs, the coordinator's watchdog ends the test: start it again later.
- `pt_fog_froxel.hlsl` carries a note KEEP THIS SHAPE (PT-133: a driver fault that turns a skipped branch's zero
  into garbage; a source check pins two of its lines). After any change there, the check's "no sun" cases must
  still be "finite and nonnegative". A factor at a function's end is safe; a new branch round the lamps' or the
  sun's code is not.
- Build: `tools\Build-PtShaders.ps1 -BuildDir <your build>` (never `-First`), then the native build into
  `%TEMP%\tgb\fog3-build`; configure as `docs/AGENT-WORKERS.md` and `docs/agents/briefs/brief-campspeed.md`'s
  build line say. Copy the main tree's compiled headers (`build\native\generated\pathtracing`) into your build
  before your first shader edit. The fog's own passes compile in seconds; `pt_medium.hlsli` and `pt_common.hlsli`
  are in every pass (25 minutes to an hour): change them last and once.
- Two other workers are in the tree: `waterblas` (the water's acceleration structures) and `caustgrid`
  (`pt_caustics.hlsl`). Frame flag bit 19 is caustgrid's, 24 the fog lamps trial's; 25 to 29 are free.
- RTXDI and NRD sources are never copied or quoted; HDRP's source is Unity's: read it, quote a line where the report
  needs it, copy no code into the tree. Grep and read ranges of the big files, never whole. The Bash tool eats
  backslashes: scripts and text with backslashes go through the Write tool. Say every switch or log line you add.
  Run `python tests/source_checks.py` before you hand over.
- Final message: five lines at most, pointing at the report.
