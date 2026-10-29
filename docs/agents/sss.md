# The subsurface agent: light inside translucent materials, traced (PT-40, PT-43)

State: DONE OFFLINE, in testing. Built; the clean build passes every check of `tgpt_gpu_test` (run 5, below); source
checks pass (87); the plugin compiles. Not seen in game. Nothing deployed, nothing committed.
Copy: `<scratch>/wt/sss`, base `<scratch>/wt/base26`. Builds: `%TEMP%\tgb\sss-*`. Finished by a second agent after a usage
limit stopped the first at about 11:45 (it had all the code in; what was left was the clean run, the docs and the tail
of this report).

## In short
- Wax and skin: light that meets them goes in, scatters inside the traced mesh and comes out elsewhere (a random walk by
  the material's own diffusion profile and color, HDRP's own path tracer's conversion). Done for the pixels the camera
  sees (new pass `pt_walk.hlsl`) and wherever a path meets wax or skin: bounced light, the radiance cache's samples and
  reflections (`subsurface_vertex`). HDRP's screen-space skin blur is deleted (`pt_sss.hlsl`), no stand-in left for it.
- Candle wax: the black folds are lit by light that went in elsewhere (offline: a fold in its walls' shadow at 1.2 % of
  the lit face, black before; a cylinder's unlit side 0.995 to 1.003 of a reference walk on the CPU, black before).
  The tavern candle's overblown top: the walk spreads the light that lands on the top down into the wax; whether that
  is enough is for the harness and the owner, not claimed.
- Leaves and grass: where a path meets one it now passes the material's own share of the light from behind (HDRP's
  transmittance of its profile at its thickness, times its mask), tinted; the camera's own pixels already did.
- `TRANSMISSION` (0.5) and `CLOTH_TRANSMISSION` (0.3) are NOT fully replaced. Left, and named as gaps: 0.5 where a
  material names no profile; 0.5 as how often a light's shadow ray passes a blade (a look for the grass-shadow ruling,
  not the material's); cloth's 0.3 (the game's cloth gives no transmission data at all). See "Gaps".
- Cost offline: with wax over the whole 1280x720 view, the direct light's stage 3.9 ms against 0.8 ms without the walk
  (the quietest of five runs; other agents' GPU tests ran beside the others).

## The request
- The owner, of candles (2026-10-07): "what are these black spots in the wax bases of the candlelights here? Looks fully
  black inside the wax in spots". PT-43: skin looks flat.
- Rulings: "dont do shortcuts, we want realism and correctness, and then we will optimize from there"; no HDRP shading
  while tracing.
- Two more from the owner while this ran (through the coordinator):
  1. Of the screen-space scattering blur: "No, I said we don't want shortcuts or hacks - we want proper path tracing,
     and everything should be path traced". So the walk replaces `pt_sss.hlsl` everywhere: on screen, at path vertices,
     in reflections. `pt_sss.hlsl` is deleted.
  2. "That would also affect vegetation like leaves and grass I hope." So thin things pass light by their own material
     too. See "Thin things".

## What was there before
- Wax (`MI_Candle_01`, HDRP Lit, subsurface scattering with transmission through a thin diffusion profile) was lit as a
  surface or as a thin leaf (light from behind through 1 cm: `near_back_towards`, `THIN_THROUGH`). A thick lump's folds
  got nothing: black.
- Skin (thick profiles): HDRP's own screen-space blur, copied (`pt_sss.hlsl`, Burley's profile over 24 screen points),
  and HDRP's thin transmission from the thickness map (`skin_transmission`). Both are HDRP's tricks, not traced light.
- Where a path or a reflection met wax or skin: a plain painted surface.

## The design
### Thick things: a random walk inside the traced mesh
1. Light that meets wax or skin goes in, is scattered and absorbed inside, and comes out somewhere else. Traced
   backwards: from the point the camera (or a path) sees, one random walk into the traced mesh; where the walk leaves
   the mesh, the light is gathered. This is volumetric path tracing of a homogeneous medium inside the mesh, the
   "random walk subsurface scattering" of film renderers.
2. The medium is the material's own: its diffusion profile's scattering distance per colour channel, and the surface's
   colour. The conversion is HDRP's own (below).
3. The walk:
   - in at the surface, in a cosine-distributed direction (a diffuse entry);
   - free flights with exponentially distributed lengths, drawn from one colour channel; the channel is picked by what
     the walk still carries, and the three channels' densities are weighed together, so every channel stays unbiased;
   - at each scattering the walk keeps the share the colour allows and turns into a new, evenly distributed direction;
   - a flight that crosses the thing's own surface from inside ends the walk there. A surface of the same substance
     entered from outside is another piece grown into this one (melted wax blobs): the walk goes on and counts how deep
     it is. Anything else inside (a wick, a holder's spike) reflects the walk diffusely by its own colour.
4. No walk is cut short. It ends by leaving the mesh or by Russian roulette on what it carries (roulette keeps the
   mean). A GPU loop needs a bound all the same: 4096 rays. From 1024 scatterings on, every 128th halves the walks still
   going and doubles the rest, so one in 16 million of those reaches the bound. HDRP's own path tracer stops at 16 steps
   and drops the walk (it loses light); this one does not.
5. At the exit the light is gathered as a diffuse surface gathers it: the sun and the lamps with shadow rays, the sky
   and bounced light with a cosine-distributed ray.

### Where it runs
- Pixels the camera sees (`pt_walk.hlsl`, a new pass after the direct light's): HDRP's subsurface scattering in the
  GBuffer (its features 1 and 3; transmission alone through a thick profile too), a profile whose scattering distances
  the plugin sent, and a closed traced mesh under HDRP's surface. One walk a pixel a frame. The pixel's two diffuse
  signals (direct, bounced; both divided by the colour) are replaced by the walk's: what it carries over the pixel's
  colour, times the light at the exit. The composite multiplies the colour back in, so a texture's detail stays sharp.
  The highlight stays where the camera sees it.
- Path vertices (`subsurface_vertex` in `pt_common.hlsli`): bounced light (`shade_path`), the radiance cache's samples
  (`cache_sample`) and what a reflection shows (`shade_seen`). The vertex moves to the walk's exit and sends on what the
  walk carries. Needs the plugin to mark the material (`TGPT_MATERIAL_SUBSURFACE`, the profile's shape in
  `TGPT_Material.subsurface`).
- Not walked: sheets drawn from both sides, leaves, cloth, hair, glass (a thin thing has no inside). Where no closed
  traced mesh lies under an HDRP subsurface pixel, the pixel keeps its light as a plain surface's.

### What it replaces
- HDRP's screen-space scattering: `pt_sss.hlsl` is deleted, its pass and its dispatch too. No blur of light on screen
  is left for subsurface materials.
- HDRP's thin-object transmission for these pixels (`skin_transmission`, the thickness map): the walk comes out on the
  far side of an ear by itself. The code is still there for pixels the walk does not serve (transmission-only skin with
  no scattering data, an older plugin).
- `near_back_towards` / `THIN_THROUGH` still serve the pixels that are not walked.

### ReSTIR and the denoisers
- ReSTIR's samples are the light at the pixel, not at the walk's exit, so they are not used for walked pixels. The
  exit's direct light is the path vertices' sampler (8 of the cell's lights compete, one shadow ray), its bounced light
  one path ending on the radiance cache. Unbiased, but noisier than ReSTIR's direct light: one walk and one light sample
  a pixel a frame.
- The denoisers get the walk's light in the same two signals as before, so nothing changes for them: Ray Reconstruction
  and Ray Regeneration take it undenoised, the built-in filter and NRD filter it as diffuse light. For NRD the bounced
  signal's direction is the bisector of the pixel's two normals, so NRD's normal-map step leaves the walk's light alone.
- ReSTIR still runs on walked pixels and its result is thrown away: wasted work, kept simple.
- Next step, not done: reuse of exits between pixels and frames (Werner, Schuessler, Dachsbacher 2024, "ReSTIR
  Subsurface Scattering for Real-Time Path Tracing").

### Thin things (the owner's second word)
- A leaf or a blade the camera sees already passed light by its own material: HDRP's transmittance of its diffusion
  profile at the pixel's thickness, tinted, times the transmission mask (`leaf_transmittance`, from the GBuffer; the
  vegetation agent's work).
- New: the same where a path meets it. The plugin reads the material's profile, its `_Thickness` and
  `_TransmissionMask`, works out HDRP's transmittance (tint / 4 x (e^-St + 3 e^-St/3) x mask) and sends the share per
  channel in `TGPT_Material.subsurface` (with `TGPT_MATERIAL_TRANSLUCENT`). A path vertex on a leaf lit from behind
  takes that share, tinted, in place of a grey half.
- `TRANSMISSION` (0.5) is now only: the grey share ReSTIR weighs such a pixel's samples by (the final shading divides it
  out), the share where the material says nothing (no profile read), and how often a light's ray passes a blade. That
  last one is the owner's ruling on grass shadows (2026-10-06: "soft and partial"), a look and not a material's number:
  left as it is. Fed HDRP's transmittance, the game's foliage (up to 6 times the light, its tint is above 1) would cast
  no shadow at all, which the ruling forbids.
- Thin cloth has nothing of its own to go on. The game draws its cloth (`TG/Clothes/Cloth_Wind` and plain Lit materials
  `ClothRules` finds by name) as HDRP's standard material: no diffusion profile, no thickness, no transmission; HDRP
  lights it opaque. Its rule stays the stated one: `CLOTH_TRANSMISSION` (0.3) of the light through the weave, tinted by
  the cloth's own colour. Checked by the existing "thin cloth lets a share of the light through, tinted by its color".
- The reasons are written at the two constants in `pt_common.hlsli`.

## Sources
- HDRP's own path tracer, the same materials: `Runtime/RenderPipeline/Raytracing/Shaders/SubSurface.hlsl`,
  `RemapSubSurfaceScatteringParameters` (read on Unity's Graphics repository, 2026-10-07):
  `a = 1 - exp(A (-5.09406 + A (2.61188 - 4.31805 A)))`, `s = 1.9 - A + 3.5 (A - 0.8)^2`, `sigmaT = 1 / (d s)`,
  `sigmaS = a sigmaT`. And `Runtime/Material/Lit/LitPathTracing.hlsl`: `d = 0.001 / (shapeParam x worldScale)` metres,
  A the diffuse colour, the first direction cosine-distributed into the surface, then uniform on the sphere. This is
  the conversion used here, so the traced look is what HDRP's own path tracer gives the game's materials. Blender
  Cycles' random walk uses the same fit.
- Christensen and Burley 2015, "Approximate Reflectance Profiles for Efficient Subsurface Scattering" (Pixar TM 15-04):
  the profile HDRP's diffusion profiles are (`R(r) = s (e^-sr + e^-sr/3) / (8 pi r)`), and the fit `s(A)` between its
  `d` and the medium's mean free path for light that enters and leaves diffusely.
- Chiang, Kutz and Burley 2016, "Practical and Controllable Subsurface Scattering for Production Path Tracing";
  Wrenninge, Villemin and Hery 2017, "Path Traced Subsurface Scattering using Anisotropic Phase Functions and
  Non-Exponential Free Flights": the random walk in the mesh as film renderers do it.
- Wilkie, Nawaz, Droske, Weidlich, Hanika 2014, "Hero Wavelength Spectral Sampling": weighing the channels' densities
  together.
- Werner, Schuessler and Dachsbacher 2024, "ReSTIR Subsurface Scattering for Real-Time Path Tracing": the next step.
- No RTXDI or NRD source was quoted or copied. No AMD DLL was touched.

## What changed, file by file
Native:
- `native/pathtracing/shaders/pt_walk.hlsl` (new): the camera pixels' pass.
- `native/pathtracing/shaders/pt_sss.hlsl`: deleted.
- `native/pathtracing/shaders/pt_common.hlsli`: `subsurface_medium`, `subsurface_walk`, `subsurface_material`,
  `subsurface_vertex`, `sphere_sample`, `WalkExit`; `shade_path`, `cache_sample`, `shade_seen` take the walk at
  subsurface vertices; `thin_through` and `SurfaceMaterial.tint` (a leaf's own share at path vertices);
  `direct_light` / `direct_diffuse` take a tint (the old signatures stay); `g_ownLight` (no 25 cm floor for the camera
  pixel's own light at the exit); `MaterialRecord.subsurface` (was `textureReserved`); the notes at `TRANSMISSION` and
  `CLOTH_TRANSMISSION`.
- `native/pathtracing/src/tracer.cpp`: the `walk` pass (both shader models), its dispatch after the direct light's; the
  `sss` pass and its two dispatches removed.
- `native/pathtracing/src/extension.cpp`: `TGPT_SetDiffusionProfiles` counts a profile by its shape, not its radius.
- `native/pathtracing/include/tgpt_api.h`: see "ABI and bits".
- `CMakeLists.txt`, `native/pathtracing/shaders/pt_shaders.hpp.in`: `pt_walk` in, `pt_sss` out.
Plugin (compiles; not run in the game):
- `src/TGPathTracing/HdrpPasses.cs`: every profile's scattering goes to the tracer (was: the thick ones'); `ProfileOf`
  (the diffusion profile a material names, by `_DiffusionProfileHash` or any float of a Shader Graph that holds a loaded
  profile's hash) and `ReadSubsurface` (fills `TGPT_Material.subsurface`); two new log lines ("Subsurface material ...",
  "Thin material ...") that say what each material gave.
- `src/TGPathTracing/SceneCapture.cs`: calls `ReadSubsurface` for each traced material.
- `src/TGPathTracing/NativeTracer.cs`: the constants and the struct's three floats.
Tests:
- `tests/pathtracing_gpu_test.cpp`: a reference random walk on the CPU (`walk_reference`, `walk_view`) and block 2c
  (checks A to F below); the skin-floor check keeps its numbers and now tests the walk.
- `tests/source_checks.py`: the `pt_sss` block is now the walk's; four lines that named changed code.

## ABI and bits (names; the number is the merge's to set)
`TGPT_ABI` was not touched in the copy (still the base's 0x00011C00; main is at 0x00011D00 and moves again). The
coordinator sets the number. What changed:
- `TGPT_Material.subsurface[3]` (floats): was `textureReserved[3]` (uint32, zeros). Same size and place, 160 bytes still.
  With `TGPT_MATERIAL_SUBSURFACE`: Burley's shape per channel per metre. With `TGPT_MATERIAL_TRANSLUCENT`: the share of
  the light from behind per channel; zeros: not known. The shader side: `MaterialRecord.subsurface` (float3, was
  `uint3 textureReserved`).
- `TGPT_MATERIAL_SUBSURFACE` = 2097152 (0x200000, material bit). Check main has not given this bit to someone else.
- `TGPT_OPTION_NO_SUBSURFACE` = 268435456 (0x10000000, option bit, for testing: no walk, subsurface materials lit as
  plain surfaces). Check main has not given this bit away either. No row sets it; `NativeTracer.OptionNoSubsurface` is
  there for a hidden one.
- `TGPT_SetDiffusionProfiles`: same call, new meaning: every profile's shape (thin ones too); the fourth float (HDRP's
  filter radius) is no longer used. `FrameConstants.sssProfiles` keeps its layout.
- No new export, no struct size change. The `sss` pass is gone from `tracer.cpp` and the shader list, `walk` is new.

## Offline checks and their numbers
All in `tgpt_gpu_test` block 2c (A to F, described at its head), the Direct view undenoised, no reuse, no clamps,
against a random walk on the CPU in double precision through the same shapes (`walk_reference`, written from the
method, not from the shader).

Runs (`%TEMP%\tgb\sss-test-<tag>.txt`):
| Run | What | Result |
|---|---|---|
| 2 (11:01) | clean, before a fix in `pt_walk.hlsl` (`exit_normal`: the exit's normal from HDRP only where HDRP drew that very surface) | 2 FAIL (A: 0.84 of the reference) |
| 3 (11:12) | clean, the code as it is now | exit 0, every check passes |
| X1 (11:18) | mutant: pieces not counted + no walk at path vertices + a leaf's share not read | 5 FAIL (A2, E1, E2, F1, F2) |
| X3 (11:29) | mutant: light gathered at the entry + the roulette's survivors not divided up | 8 FAIL (A, A2, A3, B1, C1, D1, D2, and the older "light scattered under skin keeps the light's total") |
| X4 (11:38) | mutant: the channels' densities not weighed together | 7 FAIL (A, A2, B1, C1, C2, D1, E2) |
| 5 (14:13 to 14:18) | clean, the shaders rebuilt from the clean sources (see "Trap" below) | exit 0: "all path tracing checks passed" (1157 ok lines); every 2c number the same as run 3's |

Run 3's and run 5's numbers (the code as it is; the two runs agree to the last digit):
- A. Wax cylinder, lamp behind: far side 0.995, 0.997, 1.003 of the reference (red, green, blue); with a second piece of
  the same wax inside it 0.996, 1.000, 1.007; lit as a plain surface 0.
- B. Slabs, lamp behind: 3 mm 0.995 of the reference in each channel (0.81 of the red that went in crosses, 0.69 of the
  blue); 10 mm 0.995 (0.54 and 0.39). HDRP's thin-object formula would give 0.81 and 0.88 of the walk's light.
- C. Thick flat piece under a lamp: 0.985, 0.997, 1.000 of the same piece painted; within 1.7 % of the reference;
  22.7 flights a walk on average.
- D. A deep fold, in its walls' shadow: 0.0117 of the lit wall's face, red over blue 3.2; 0 lit as a plain surface.
- E. Floor behind a 3 mm wax slab, its bounced light through paths: 0.920 with the walk at path vertices, 0.0023
  without; green and blue against red 0.930, 0.851 (the reference's 0.930, 0.852).
- F. Floor behind a leaf: share 0.6, 0.3, 0.1 gives 1.992, 0.999, 0.341 of what 0.3 grey gives (2, 1, 0.333 wanted);
  no share given: 1.667 (the grey half: 1.667 wanted).
- The older skin check (a floor drawn as skin, 3 cm scattering): shadow edges 0.51 of the sharp ones, light 1.012 of the
  sharp floor's: now tested on the walk, same numbers as before.

Weak checks (say so before trusting them):
- D1 (the fold) passed under X1 at 0.0042 against a floor of 0.004; the floor was raised to 0.008 after (clean 0.0117),
  not re-run against X1.
- "a thicker slab passes less, and less of the blue than of the red" (B2) failed under no mutant: qualitative only.
- The roulette mutant ran only together with the entry mutant (X3): not shown to fail on its own.
- X2 (no absorption) was written but never run.
- Nothing checks the walk where a reflection (`shade_seen`) or the radiance cache's samples (`cache_sample`) meet wax:
  the same `subsurface_vertex` call as bounced light's vertices (E checks that one), but untested at those two sites.
- Nothing checks the plugin's reading of the materials (`ProfileOf`, `ReadSubsurface`): compiles, logs, not run.

Trap found (for whoever mutates next): `sss-mutants.ps1` restores the shaders with `Copy-Item`, which keeps the old
file time, so ninja did not recompile them: build 4 (11:32) held mutant X4's shaders. Run 5 was built after touching
`pt_common.hlsli` and `pt_walk.hlsl` (42 shaders recompiled). Touch the files after a restore.

## GPU cost
- Offline, wax over the whole 1280x720 view, the direct light's stage: 3.90 ms with the walk against 0.83 ms without
  (run 2, the quietest); 4.61 against 0.92 (run 5); 10.85 against 2.64 (run 3) and 6.60 against 1.50 (X1) with other
  agents' GPU tests beside it.
  So about 4 to 5 times that stage for the pixels that are wax, about 3 ms a full 720p screen of wax on this GPU.
- In game, wax and skin cover a small part of the screen; add one walk (about 23 rays) at every path vertex, cache sample
  and reflection that meets wax or skin. Not measured in game. ReSTIR still runs on walked pixels and is thrown away
  (wasted work, kept simple).

## What the next harness runs should show (the coordinator runs them)
- `candle`: the folds and pockets in the melted wax at the candles' feet no longer black; the wax glows softly where the
  flame's light comes through; the top less of a hot spot (its light spread down into the wax). The tavern candle
  ("bloom too much/are overblown"): compare its top third with the game's cream; a walk spreads light, it does not
  remove it, so if the top still blows out the cause is elsewhere (the light's strength, PT-47's notes). Read the
  exposure and gain log line (the wax's light moved).
- `npc-candle`: the same for the wax, and the NPC's skin: softer light, a red edge at shadow lines, an ear or nose with
  the candle behind it glowing red. More noise on skin and wax before denoising (one walk a pixel a frame).
- `hair-eyebrows`: Ron's face: the skin no longer flat-lit with a screen-space blur; no halo of blurred light around the
  face's outline (the old blur's), the blush and freckles still sharp (the color is multiplied back in after).
- `forest`: leaves and grass met by bounced light pass their own tinted share (bounced light under the canopy greener or
  yellower than before, by the foliage profile's tint); grass shadows unchanged (the 50 % rule is kept).
- `BepInEx/LogOutput.log`: "Subsurface material \"MI_Candle_01\" ..." for the wax (if it is missing, the wax is drawn
  from both sides or names no profile, and it is not walked); "Subsurface material" for skin materials; "Thin material"
  lines for foliage with their shares.

## Gaps and risks (named, not done)
- Thin cloth: `CLOTH_TRANSMISSION` 0.3 stays a fixed number. The game's cloth is HDRP's standard material with no
  profile, no thickness, no transmission; HDRP itself lights it opaque. A real value needs a source the game does not
  give (the owner's call: keep a stated number, or treat cloth as opaque as HDRP does).
- Leaves where the material names no profile: the 0.5 grey half stays (a stand-in).
- Grass shadows: a light's shadow ray passes a blade half the time (`alpha_opaque`, `TRANSMISSION`): a look kept for the
  owner's "soft and partial" ruling, not the material's. The physical way: the blade stops the straight ray and its light
  from behind reaches the ground as its back side's diffuse light (which path vertices now do with the material's
  share). Not started: it changes the grass shadows the owner has seen, so it needs the owner first.
- A leaf with a thickness map: where a path meets it, its share uses the middle of `_ThicknessRemap` (a path cannot
  read the map); the camera's pixels use the map.
- A subsurface material drawn from both sides gets no walk (no inside): lit as a plain surface, as before.
- Where a walked pixel's traced mesh is missing or a different level of detail, `walk_entry` may find no entry: the
  pixel keeps its plain-surface light.
- Skin's old thickness-map transmission (`skin_transmission`) still serves pixels the walk does not (no profile data, an
  older plugin).
- ReSTIR's samples are not used for walked pixels: unbiased but noisier. Next step for speed and noise: reuse exits
  between pixels and frames (Werner, Schuessler, Dachsbacher 2024).

## How to merge
- Take from the copy: `native/pathtracing/shaders/pt_walk.hlsl` (new), delete `pt_sss.hlsl`; `pt_common.hlsli`,
  `tracer.cpp`, `extension.cpp`, `tgpt_api.h`, `CMakeLists.txt`, `pt_shaders.hpp.in`; `src/TGPathTracing/HdrpPasses.cs`,
  `SceneCapture.cs`, `NativeTracer.cs`; `tests/pathtracing_gpu_test.cpp` (block 2c, `walk_reference` near the top, the
  skin hunk near 4343), `tests/source_checks.py`; `CHANGELOG.md` (one entry at the top of the TGPT list);
  `docs/PT-REQUESTS.md` (notes under PT-40, PT-43, PT-50); this report.
- Not this job's: `src/TGPathTracing/obj/` in the copy (build output, gitignored).
- Merge against `base26` with a three-way merge; `pt_common.hlsli`'s conflicts are likely at `direct_light` (a new
  `tint` argument, the old signature kept), `hit_material` (`SurfaceMaterial.tint`), `shade_path`, `cache_sample`,
  `shade_seen`, and the `MaterialRecord` field. Then set `TGPT_ABI`, check the two bits are free on main, build, and run
  `tgpt_gpu_test` (block 2c) and the four harness scenes above.
