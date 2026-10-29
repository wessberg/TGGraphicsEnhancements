# PT-48: the mineral cave's lake, traced (lake agent, 2026-10-07)

The owner, watching the harness turn the mineral cave's save round to its lake (`glow-lake`, run `20261006-225619`):
"That looked weird". Traced: a blue haze over the far half, the water a dark opaque slab, grass with light edges.

Status: built and checked offline in the copy `wt/lake` (tgpt_gpu_test, source checks, the plugin compiles). Not seen
in game. Not deployed, not committed.

## In short

- All three come from one thing: the game's own fog, made 5.8 times as bright by Fog Adaptation, over a cave the
  tracer barely lights.
- The cave's lights are fill lights the tracer leaves out (`Point Light_17B` x31 and the crystals' `Point Light` x39,
  PT-47). They still light the game's fog (Fill Light Fog). So the fog is 5 stops brighter than the traced light under
  it. Dark Adaptation then brightened the view by x5.8, measured off that fog, and Fog Adaptation put the x5.8 back
  on the fog itself.
- The haze: that fog at x5.8. The slab: HDRP's water, which HDRP draws after Fog Adaptation's pass, so the fog in front
  of it stays at x1 while everything around it got x5.8. The light grass: blades with almost no traced light, so each
  blade is the x5.8 fog alone, against the darker water.
- Fixed: Fog Adaptation now lifts the fog only as far as the traced light it hangs in front of (never above the
  brightened surfaces, never below the game's fog). In the cave that is x1: the game's fog. Where the traced light
  makes up the view (a lit room, any day) nothing changes.
- Not fixed here: the cave is still lit only by what the tracer traces. Its rocks and moss get their light in the game
  from the left-out lights (PT-47).

## 1. What the haze is

The game's own fog, at 5.8 times its brightness. Not HDRP's underwater look, not fog the tracer lights.

- The fog (state audit of the same save, `20261006-162322/glow/1-game/audit-a.txt`, `Lights/Global Volume`): volumetric,
  mean free path 74.1 m, base height 59.4 m (the camera at 8.2 m is deep inside it), albedo (0.33, 0.43, 0.49): blue.
  The game's exposure there is fixed (EV 14, compensation 1). No local fog volumes.
- What lights it: the cave's lights. The run's light list (`LogOutput.log`, "Lights by name"): the tracer leaves out
  `Point Light_17B` x31 (radius up to 3.7 m, range 10 m, under "Lights"), the crystal molds' `Point Light` x39 and two
  `Spot Light`s; it traces candles (61), torch ambience (8), cookies (7) and a bonfire (3), none near the lake. With
  Fill Light Fog on, the left-out lights keep lighting the fog (`SceneLights.Darken`), as the owner ruled.
- The numbers (`2-traced-rr/step.json`): traced light median log2 -13.2 before the gain; the view as seen, fog
  included, -8.1 at the game's exposure. The fog is 5.1 stops over the traced light. Dark Adaptation takes the brighter
  of the two as the view's level (`HdrpPasses.AutomaticGain`) and brightened by (-3.06 + 8.1) x 0.5 = 2.5 EV: x5.78.
- Fog Adaptation (`d3d11.hlsl` TGPT_FOG, render event 5) gave the fog's own light that whole gain: color + (gain - 1) x
  fog. The seen median went from -8.1 to -5.6. A gain the fog set for itself, put back onto the fog.
- On screen (8-bit levels, `full.jpg`): the far rocks' region 32 in the game, 85 traced; the right wall 25 and 58.
  Shadow Detail (46 % active there) adds about 0.4 EV on top at the haze's level.
- Not underwater: the lake's `WaterSurface` has underwater rendering on, but the camera is 2.5 m above it, the game's
  own picture (which tracing does not change) shows no underwater look, and the haze covers rocks above the water too.
- Not the tracer lighting fog: it lights none (PT-37 is that feature, not built).
- The same thing elsewhere: the harness's dark scenes where the fog outshines the traced light come out brighter than
  the game's (median levels game -> traced: `stairwell` 16 -> 47, fog 8.7 EV over the traced light; `dark-room` 13 ->
  34, 5.7 EV; `npc-candle` 11 -> 32, 2.0 EV). PT-16 (the tavern looks washed out at x3.7) may be the same.

## 2. What the slab is

HDRP's own water surface, shaded by HDRP, drawn after Fog Adaptation's pass.

- Which water: `WaterSurfaces_Dungeon_SmugglersBay_01` (`docs/agents/caustics.md`): an HDRP `WaterSurface`
  (OceanSeaLake, HDRP's default water material, smoothness 0.906), Unity's 10 m plane scaled to 19.2 x 15.6 m at height
  5.62. Not the game's own water shader: the tracer's water pass ran on it (water reflections 2.86 to 4.46 ms in that
  run's native log, against 0.03 ms looking the other way).
- The order (HDRP 17's `HDRenderPipeline.RenderTransparency`, decompiled from the game's
  `Unity.RenderPipelines.HighDefinition.Runtime.dll`): transparent SSR, then the BeforePreRefraction custom passes
  (Fog Adaptation's), then pre-refraction transparents, then `WaterSystem.RenderWaterLighting`. So Fog Adaptation
  brightened the fog over the lake bed and the grass, and then HDRP laid the water and its own fog (x1) over it.
- Its own light is dark while tracing: the left-out lights' light dimmer is 0 (`SceneLights.Darken`) and HDRP's
  indirect diffuse multiplier is 0 (no ambient for the water's scattering), and its reflection is the tracer's of an
  unlit cave. Light does pass into it (HDRP refracts the color buffer), but the bed under it is unlit too.
- On screen: 22 levels in the game, 10 traced, inside a haze of 40 to 85. A dark cut-out in a bright fog.

## 3. What the light grass edges are

Grass blades standing out of the water, each one the x5.8 fog alone.

- Leshy's grass is traced (211 grass and plants within 20 m, `LogOutput.log`). Near the lake no traced light reaches
  it, so a blade's own light is close to 0 and what HDRP shows there is the fog over it, x5.8.
- Zoomed (`crop.py --box 0.20,0.66,0.30,0.74 --zoom 2`): the blades are filled, flat, light blue shapes, not outlines
  with dark middles. 40 to 50 levels against the water's 10. Same in both traced steps (Caustics on and off).
- Not the composite's coverage or the denoiser: those would leave edges, not evenly filled fog-colored blades.

## 4. What changed

Fog Adaptation's gain on the fog goes only as far as the traced light makes up the view.

- `native/pathtracing/shaders/d3d11.hlsl` (TGPT_FOG): the pass still adds (fog gain - 1) x the fog's light. It now
  also fills a histogram: per surface pixel (every other one, centre-weighted, the sky left out), log2 of the fog's
  light over the traced light it veils (as seen through the fog, the gain the composite gave it taken out).
- `native/pathtracing/src/tracer.cpp`: `read_fog_bins` reads the histogram back without waiting (three read-backs),
  takes its median (the fog's stops over the traced light) and eases it over 0.5 s; snaps after a camera cut or when
  Fog Adaptation starts. `fog_automatic`: the fog's automatic gain = the traced light's automatic gain shrunk by those
  stops, clamped to 1 .. the traced light's. The player's Exposure stays a factor on all of it, as before. The fog pass
  gets it in `camera.y` (and the traced gain in `camera.w`); the D3D12 frame constants in `textures.z` (a float's bits;
  it was unused). The 10-second native log line says "fog adaptation: the fog's light xA (the traced light's xB; the
  fog C EV over the traced light it veils)".
- `native/pathtracing/shaders/pt_common.hlsli` `traced_gain_at`: with Fog Adaptation the traced light's gain fades
  behind fog to the fog's own (it used to stay whole); without it, to 1 as before. So a surface deep in the fog never
  stands out lighter than the fog in front of it (the old "castle through the mist", PT-33's kind).
- No ABI change (0x00011800 stays), no new option or material bit. One slot taken: FrameConstants `textures.z` (was
  "unused"); `FogConstants.camera.w` and `frameSize.z` are the D3D11 pass's own.
- No managed change. The Dark Adaptation meter is untouched: where the fog now stays at x1, it measures the fog at x1
  and brightens the traced light further (in the cave up to about 5 EV, where the traced light meets the fog's level;
  the cap there is 6 EV), as it did before Fog Adaptation existed.
- `tests/pathtracing_gpu_test.cpp`: two new checks (below). `tests/source_checks.py`: the old Fog Adaptation lines
  updated, new lines pinned.
- Docs: `CHANGELOG.md` (top of the path tracing part), `docs/PROJECT-CONTEXT.md` 9.4's Fog line (one line longer),
  `docs/PT-REQUESTS.md` PT-48's state, `native/pathtracing/include/tgpt_api.h` (the option's comment only).

Files: `native/pathtracing/shaders/d3d11.hlsl`, `native/pathtracing/shaders/pt_common.hlsli`,
`native/pathtracing/src/tracer.cpp`, `native/pathtracing/include/tgpt_api.h`, `tests/pathtracing_gpu_test.cpp`,
`tests/source_checks.py`, `CHANGELOG.md`, `docs/PROJECT-CONTEXT.md`, `docs/PT-REQUESTS.md`, this file. For the merge:
another agent's use of FrameConstants `textures.z` or `textures.w` would clash with this one (`textures.z` here).

## 5. Checks

All in the copy, built with Ninja in `%TEMP%\tgb\lake-build`, installed to `%TEMP%\tgb\lake-inst` (never `artifacts/`).

- `tgpt_gpu_test`: 778 ok, 0 failed (`%TEMP%\tgb\lake-test-final2.txt`, the final source). The new ones:
  - "Fog Adaptation keeps the game's fog where its light outshines the traced light it veils (PT-48), after a few
    frames, not at once": a fog 50 times the dim one over the lit test scene: x1.000 on all 921,600 pixels after 4
    frames (x2 before the readings arrive), still x1.000 after more.
  - "a fog the traced light outshines takes the traced light's whole gain again": a quarter of the dim fog after it:
    x2.000 after 13 frames (eased), still x2.000 after more.
  - The old Fog Adaptation checks pass unchanged (x2 over 894,362 surface pixels; with HDRP's maximum fog distance, all
    921,600), as do the fog's dimming of the sun and lamps.
  - A first version of the second check used the dim fog itself and failed once in three runs: a run with the readings
    logged showed that fog sits 0.04 EV under the test scene's traced light, exactly at the rule's edge, so the gain was
    still easing back when the check gave up. The readings themselves arrived every frame. The check now uses a fog
    2 EV under it.
- Mutations (each its own build and prefix, the source restored and compared byte for byte after), against the final
  test:
  - the measurement ignored (`fog_automatic` returns the traced light's gain): the bright fog stays x1.999 after 90
    frames; the first new check fails (777 ok, 1 failed).
  - the reading never easing back (`fogOver = max(fogOver, median)`): the faint fog stays x1.000; the second new check
    fails (777 ok, 1 failed).
- `python tests/source_checks.py`: 82 tests OK. The two old lines that pinned "the gain stays whole with Fog
  Adaptation" now pin the fade to the fog's gain; new lines pin the histogram, the median rule, the constants and the
  two GPU checks' names. Each new line mutation-checked (the histogram line, the clamp, `fogAutomatic`'s start): its
  test fails.
- `dotnet build src\TGPathTracing -c Release -o %TEMP%\tgb\lake-managed`: succeeded, 0 warnings (no managed change).
- Not run: the in-game harness, Ray Reconstruction (`tgup_gpu_smoke_with_pt`: this change is on the D3D11 fog pass
  and the gain, not the RR path).

## 6. What the next harness run should show

`.\tools\harness\Run-PtHarness.ps1 -Scenario glow-lake -Recipe views`:

- The native log (`TGPathTracing.Native.log`, "Path tracing 10s"): "fog adaptation: the fog's light x1.00 (the traced
  light's xN; the fog about 5 EV over the traced light it veils)". Before it would have been x5.78 on both.
- `step.json` exposure line: "traced light x" well above 5.8 (expected 20 to 35: the meter now sees the fog at x1), and
  "seen with the fog" about -8 rather than -5.6.
- `caustics.jpg` / `game-vs-traced.jpg`: no bright blue haze. The far region near the game's 32 levels, not 85. The
  water no longer a dark cut-out in a bright fog: the fog in front of it and around it are both x1. The grass blades
  lit by the traced light (brighter with the larger gain) rather than flat fog-blue shapes.
- The views: Direct and Emissive show how little traced light reaches the lake (that is PT-47, not this).
- Also worth a run: `stairwell`, `dark-room`, `npc-candle`, `tavern-room` (the fog over the traced light there 1 to 9 EV;
  their traced pictures came out brighter than the game's), and a day scene (`vista`: nothing should change, the fog
  there is under the traced light).

## 7. Risks and what is left

- This changes Fog Adaptation in every dark place where the fog outshines the traced light, not only the cave. The
  tavern (PT-16) will look different: its fog lifted less, its traced light lifted more. The owner's call; the Fog
  Adaptation row still turns it all off.
- The larger gain in the cave (about x30) brightens what little the tracer lights there, grain included. Dark
  Adaptation and Shadow Detail stay the owner's rows.
- The cave still lacks its own light: rocks and moss get theirs in the game from lights the tracer leaves out (PT-47).
  Until those are traced, the traced cave stays darker than the game's under the same fog.
- The water's own shading stays HDRP's, dark while tracing (no ambient, the left-out lights dimmed). That is the
  ruling's open item ("water's own shading", raise with the owner), not changed here.
- Where the traced light makes up the view and the gain is above 1, HDRP's water still gets its fog at x1 while the
  rest gets the gain (the pass runs before HDRP's water). Not seen to matter yet.
- Readings come a few frames late and ease over half a second; after a camera cut the first reading is taken as it is.
- A view right at the rule's edge (the fog about as bright as the traced light, as the offline test scene) takes a
  second or two to settle between the fog's gain of 1 and the traced light's. Nothing else drifts.
- Not covered offline: the fade of the traced light's gain behind height fog to the fog's own (`traced_gain_at`); the
  offline test has no height fog. A source check pins its formula.
