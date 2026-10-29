# Brief: fog-2 - the design of PT-112 step B: the game's fog traced by TG Path Tracing (research and design, no code)

You work in the main tree `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration`, READ-ONLY except one file: your report
`docs/agents/fog-2.md`. Follow `AGENTS.md` (it is loaded for you). Never start the game, never deploy, never build, never
run a GPU test, never edit code or any other file, never commit. Write your report early (its outline first) and keep it
current, so that a stop loses nothing; if a usage limit shows, write "State when stopped" into it at once. Never read the
big files whole (`pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp`, `docs/PT-REQUESTS.md`,
`docs/PROJECT-CONTEXT.md`): grep, then read a range.

## The goal (the owner, 2026-10-10 23:50 and 23:58, PT-112 in `docs/PT-REQUESTS.md`)

"the game's fog is rendered and path traced by *us*, right? It definitely should", and "OK, move fog and transparents
over, then, so we don't render anything with HDRP anymore. Post-processing effects, interface and upscaling I don't
consider part of the rendering per-se". Today HDRP renders and lights the fog (height fog, its volumetric fog grid lit by
HDRP's lights through shadow maps, local fog volumes); the tracer only scales its light (Fog Adaptation, render event 5,
`d3d11.hlsl`) and fogs its own reflected rays with the height fog (`docs/agents/fog.md` step 1). Step A (round 20) made
HDRP draw no opaque object while tracing; the tracer writes the frame's depth, motion and depth pyramid, which HDRP's
fog reads.

## The rulings that bind the design (AGENTS.md has them all)

- Keep the game's fog: "fog shouldn't go away just because people activate PT. It's core to the game's art style". Its
  density, colour, local volumes, weather and time of day stay the game's.
- The tracer lights it: "shadows and light shafts in the mist?" "yes" (PT-37).
- Fill lights keep lighting the fog and only the fog (Fill Light Fog row: "allow the fog-part of them while preventing
  the light-part").
- No fakes, no screen-space: an effect off screen still counts ("off-screen effects must be included, no screen space
  stuff"); a mirror shows the fog as the camera sees it.
- No limits that change the picture (a fixed reach past which the fog is not lit, a capped light count): a budget may
  cost noise or speed, never what the picture converges to. Speed matters now (the town traces at 81 ms a frame):
  every choice with its cost; what would change the picture is a choice for the owner, with numbers.
- Shader Execution Reordering stays on; plain compute where no ray is traced is fine.
- Never `Add<T>(true)` on an HDRP Volume (resets every parameter). Never scale a light's `volumetricDimmer` (the game's
  LightWithOverride caches it: PROJECT-CONTEXT DEAD ENDS). Never the frame setting OpaqueObjects (wakes the game's ECS).

## What to find out and write

1. What HDRP 17 (Unity 6000.0.64f1) computes for fog, end to end, in this game: the height fog (`Fog` volume), the
   volumetric pipeline (density voxelization of the global fog and of LocalVolumetricFog volumes with their masks; the
   lighting of each froxel: which lights, their volumetric dimmers, shadows, anisotropy, the sky's ambient; temporal
   reprojection; filtering; integration), how the opaque fog pass and the transparent shaders apply it, and the sky.
   Sources: Unity-Technologies/Graphics on GitHub (HDRP 17.0 branch, `com.unity.render-pipelines.high-definition`), the
   game's managed assemblies (decompile with `ilspycmd -t <type>`; the game folder is in AGENTS.md), and what earlier
   reports learned (`docs/agents/fog.md`, `docs/agents/lake.md`, `docs/agents/design-particles-fog.md`,
   `docs/agents/mirror-2.md`; PROJECT-CONTEXT 9.4; `src/TGPathTracing/HdrpPasses.cs` FillFog and the Fog Adaptation
   code; `native/pathtracing/shaders/d3d11.hlsl` event 5; the fog functions in `pt_common.hlsli`). Cite file and line.
2. What the game sets: the Fog volume stack's values (the harness audits, e.g. `20261006-234757/vista/1-game/audit-b.txt`
   if still there), its local volumes (the swamp, the castle's Wyrdness volumes), what moves fog over time.
3. The design for the tracer to own the fog, under the rulings: where the density comes from, which light lights it
   (the traced lights with traced shadows, the sky, the fill lights' fog share), how it reaches every pixel and every
   reflected or refracted ray, how the transparent effects that stay HDRP's until step C are fogged consistently (their
   shaders read HDRP's volumetric buffer: should the tracer fill it?), and how HDRP's own fog work is switched off while
   tracing. Compare at least: a froxel grid of our own, per-pixel marching with light sampling (volumetric ReSTIR, RTXDI's
   way), and a hybrid; each with its cost at a 1924x1084 render resolution on an RTX 5080, its noise, its memory, and
   what it would change in the picture against HDRP's. Recommend one, with reasons.
4. The smallest first trial the game can show behind a hidden switch, in ONE scene, within half a day of building:
   what it is, which save (the harness scenarios in `tests/ingame/scenarios.json`; `docs/PT-HARNESS.md`), what the
   picture should show and what number decides it. Then the build order after it, the ABI pieces (`tgpt_api.h`), the
   files, and the risks.

Keep it short and plain where you can (the owner reads plain words: AGENTS.md "How to talk to the owner"); put the long
evidence in sections after a one-screen summary. Stop when the report is done.
