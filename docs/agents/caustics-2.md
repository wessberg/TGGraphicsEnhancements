# PT-14: real traced caustics (caustics-2 agent, 2026-10-07)

Copy `wt/caustics2`, base `wt/base28`. Brief: `<scratch>/brief-caustics.md`. Nothing run in game, nothing deployed or
committed.

## First lines: what is built, what is not

- Built: water caustics traced for real. HDRP's own waves reach the tracer every frame; the sun's and every lamp's light
  is traced onto them, reflected and refracted, traced on to what it meets, and its light per area where it lands is the
  caustic. Every surface the camera sees and the bed seen through the water show it. The three stand-ins are gone
  (`water_caustic`, `water_reflected_sun`, `water_reflected_lamps`, and `water_caustic_at`); HDRP's caustic pattern is
  no longer read.
- Not built (stays out, not faked): light off glossy surfaces that are not water (the cave's wet floor patches: brief
  item 3; section 7 says what it takes), caustics seen in mirrors and in bounced light, HDRP's current maps, water masks
  and deformers (the waves are HDRP's without them; the F8 report names a slot that has them), emissive surfaces
  (crystals) as caustic sources.
- Offline: the whole `tgpt_gpu_test` passes on the final build ("all path tracing checks passed",
  `%TEMP%\tgb\caus2-test-full.txt`); every new check fails when its piece is broken on purpose (6 mutants, section 4);
  source checks 87 OK; the plugin builds (`dotnet build`, to a scratch folder). Not seen in game.
- ABI changed: `TGPT_WaterCaustics` is a new struct of 192 bytes (was 88); FrameConstants 7040 bytes (was 6160). Set the
  ABI number at merge (base28 had 0x00011C00; main is at 0x00011D00 after PT-50).

## 1. The waves (HDRP 17, read from Unity's Graphics repo, nothing pasted)

HDRP's water shader moves its surface per band b (up to 3): the undisplaced point's position in the surface's space
(x, z), times the band's scale less its offset (`_BandN_ScaleOffset_AmplitudeMultiplier`: 1 / patch size, the current
times time, amplitude), samples the displacement array (bilinear; x the height, yz the horizontal move), times the
amplitude and the distance fade (`_BandN_Fade`: saturate(a d + b), squared, smoothstepped); the horizontal move times
-2.25 (choppiness). The normal: the additional data array's xy (each band's surface gradient, amplitude already in),
read bicubic, faded, summed; normal = normalize(up - (gx, 0, gz)). A custom mesh's surface space is the world.

- Plugin (`WaterCaustics.cs`): a Harmony finalizer on `WaterSystem.UpdatePerSurfaceConstantBuffer` (the old hook) reads
  HDRP's own per-surface constant buffer (`m_ShaderVariablesPerSurfaceArray[surfaceIndex]`) right after HDRP fills it:
  the three bands' data, fades, `_MaxWaveHeight` / `_MaxWaveDisplacement`. The buffers by reflection
  (`simulation.gpuBuffers.displacementBuffer`, `additionalDataBuffer`), their native pointers read when they change.
  Every water surface near the camera now counts (not only those with HDRP caustics on). Each slot also sends its
  extent (x, z), absorption (`WaterSurface.extinction` as HDRP works it out) and band count.
- Native: `copy_waves` copies each array's top mip on D3D11 into a texture array shared with D3D12, every frame; a slot
  whose buffers can't be copied is traced as still water (the log says so once). The per-band texture coordinate maps are
  folded in double relative to the scene origin (as the old pattern mapping was).

## 2. The light (`pt_caustics.hlsl`, stage "caustics", before the composite)

- The grid: levels of 512 x 512 cells round the camera, 2 cm the finest, each next twice as coarse holding the finer
  one's square on its own cell lines, as many as reach the farthest water (8 at most: 2.56 m cells, 1.3 km); moved by a
  random fraction of the coarsest cell every frame (the light converges over frames to a finer grid's). A group traces a
  tile of 15 x 15 cells; tiles off the water or inside the finer level's square stop at once.
- A corner: a ray down onto the slot's level water instance, then HDRP's waves there (section 1).
- The tile's lights: its two strongest (the sun's irradiance times the tile's area across it; a lamp's intensity times
  the tile's area over the nearest distance squared, out of range none), and one of all the others in proportion to
  that, divided by its chance. All lights are walked by the group's 256 threads (each its share, then one thread joins
  the shares' best two and their weighted picks). The coordinator's number: the stand-ins' per-pixel lamp loop cost
  42 ms in the mineral cave after PT-50 (677 lamps a cell); there is no lamp loop per pixel now.
- A lamp's light leaves one point of its emitter's ball a frame (the same for every tile); the sun's one direction within
  its disc. Each corner: a shadow ray to it (glass tints it), and the reflected and refracted rays (index 1.3333, the
  exact unpolarized Fresnel split) traced on with the bounce rays' rules; HDRP's fog on the reflected leg, the water's
  absorption on the refracted one.
- Each cell, two triangles. The light through the water's triangle: a lamp's intensity (as `light_incident` gives it,
  cone, cookie and fog included) times the solid angle the triangle fills seen from the lamp (Van Oosterom and
  Strackee); the sun's irradiance times the triangle's area across its light; times what the corners carry on average.
  It lands on the triangle the three rays hit; irradiance there = that light / that triangle's area, held per corner as
  each corner carries it (a point reads it by its barycentric weights; the mean is the triangle's). Each ray leaves the
  wave 5 mm along its own way (off the normal it landed that much aside). Left out: a triangle whose corners landed on
  different surfaces (the meshes' smooth normals more than 60 degrees apart, or it lies across them), and one wholly
  outside the camera's view (with half a view's margin). Wound to face where its light came from.
- Where it lands: a world-space hash (2^21 buckets, 8 MB; triangles 64 B, room for a million to start, grown from the
  counters' read-back when a frame wanted more, 16 million at most). A triangle sits in one bucket: the level whose cell
  just holds its box widened by its tolerance, the cell of the box's low corner. `caustic_light(p, n)` walks the 2 x 2 x 2
  cells round p at every level that holds any (each bucket once) and adds the light of each triangle p lies on (within
  0.5 cm + 10 % of its longest edge of its plane, inside its edges) whose side faces n.
- Straight light under the water: `transmittance` and `transmittance_from_surface` stop a light's shadow ray that crosses
  a slot's level water (`crosses_water`, a water-only query, only below the highest water or going down; a waterfall's
  steep sheet is passed, since the grid lies on level water only) while the Caustics row
  is on. Rays from the water's own surface pass (`g_passWater`: pt_water's highlights, pt_water_under's light on the
  surface, the caustics' own rays), since HDRP's waves dip below the level surface the tracer has.
- Who sees it: pt_compose adds `caustics_at(s)` to the direct light after its filter (in the Direct view too);
  pt_water_under adds it on the bed it sees through the water (`g_firstCaustic` in `shade_path`'s first vertex).

## 3. Why these choices

- The grid lies on the water, not in the light's view: every bit of water gets rays whatever the light's angle, lamps
  as well as the sun, and the flux through a cell is exact for the cell's flat triangle.
- The hash, not a splat into the screen: any point can ask (the bed seen through the water today; mirrors and bounced
  light can ask the same way once the view culling goes, section 7), and a point gets exactly the triangles it lies on:
  no blur, no noise from the method.
- After the filters, as the stand-ins were: the waves move it every frame; in the direct light's history it would trail.
  Ray Reconstruction gets it in its input and treats it as the rest.

## 4. Checks (offline, `tests/pathtracing_gpu_test.cpp`, block "Caustics (TGPT_SetWaterCaustics, pt_caustics.hlsl)")

The room's pool (2.4 x 1.6 m, 0.4 m over the floor), the sun at 33 degrees from the zenith, the room's lamp off. A sine
wave as HDRP's buffers: 3 cm high, 0.4 m period along x, one period over a 128-texel texture. Images undenoised, the last
24 (or 48) of 40 (64) frames averaged.

| Check | Result | Mutant that breaks it |
|---|---|---|
| under still water the floor takes the sun through the surface: the water's Fresnel transmittance of it (the straight light stopped) | 0.9782 of the open floor on average, the transmittance 0.9779; 32,342 of 32,346 pixels within 3 % | mB (straight light not stopped): 1.978; mD (index 1.5): 0.958 |
| with the Caustics row off the water stops nothing | 0.00000 at most | |
| waves only move the light: the floor's mean under the sine wave is the still water's | 0.9982 (the CPU tracer's 0.9982) | mA (no area ratio): 0.963 |
| the sine wave's crests focus the sun into lines where a CPU tracer (a million rays a period) puts them, as bright | brightest 1 cm bin 10.1 x the mean at 0.395 m; the CPU's 10.3 x at 0.395 m | mA: 1.24 x; mC (the waves' slope ignored): 1.05 x; mB: 5.5 x |
| the lines move with the waves (a quarter period on) | 0.395 m to 0.095 m | |
| no change on dry land (the sunlit floor away from the pool, its mean) | -0.00012 | |
| water said to top out below the floor: nothing changes | 0.00000 | |
| a wall facing still water takes the sun's mirror image (exact Fresnel at the sun's angle, the wall's cosine) | 200,680 of 200,681 pixels within 3 % | mD (index 1.5): 0 of them |
| the waves split the reflected sun into bright lines and dark gaps | 23,224 bright (over 1.5 x), 110,330 dark (under 0.5 x) | |
| no reflected light where the mirrored path misses the water; none from water the sun doesn't reach (5 cm from the shadow's edge on); none with the row off | 0; 0.000047 at most (1.5 % of the mirror's light); 0 | mF (no shadow ray to the light): 0.003095 (all of it) |
| a wall facing still water takes a spot's mirror image (I / d^2 over the whole path, its cone, exact Fresnel, the wall's cosine), 15 cm off the wall's edges | 253,300 of 253,352 pixels inside the cone's edge within 3 %; the cone's edge in sum 1.0035 | |
| the waves split the lamp's reflected light into lines and gaps; none where the path misses the water; none with the row off; none behind a card before the wall, under a card below the lamp, from a pool behind the wall | all hold (111,675 of 111,675 open pixels beside the card on value) | |
| three spots add up exactly; four on average (a tile's two weakest sampled for each other) | 1.00008 (289,750 of 290,888 within 3 %); 1.00263 (289,011 within 5 %), the two strongest alone 0.785 | mE (the rest not divided by its chance) |

Each mutant run was killed at the FAIL line of the check it is about (`<scratch>/caus2/run-all.ps1`, outputs
`%TEMP%\tgb\caus2-test-m*.txt`); every one failed its check. The clean copy's run: `%TEMP%\tgb\caus2-test-3.txt`.

What the checks taught (each fixed, each in the code's comments):
- A ray that leaves the wave off its normal lands that much aside: 5 mm up a wall, a spot's light 7 % short at its cone's
  edge. The rays now leave along their own way.
- One light value per landed triangle missed the light's change across a cell (a spot's cone, a shadow's edge, the Fresnel
  split): the triangle now holds each corner's light and a point reads it by its barycentric weights; its total is the same.
- A cell whose corners' rays land across an edge (some on a wall, some past it) is left out: within one landed cell of such
  an edge some light is missing (up to 8 cm on the test's wall, where the lamp's mirror image magnifies the water's 2 cm
  cells up to 4 times). The corners are judged by the meshes' smooth normals, so a rock's facets are no edge.
- A shadow's edge is soft over about one cell (2 cm near the camera) besides the light's own penumbra.

## 5. Cost

Offline (1280 x 720; other agents' work shared the GPU in some runs, so these are rough):
- A 120 m sea with the sine wave under the sun (all 8 grid levels hold water): the caustics stage 0.76 to 0.82 ms.
- The test's pool (2.4 x 1.6 m): 0.02 to 0.2 ms.
- 32 lamps over the pool, the lit wall filling the view: caustics 0.02 to 0.12 ms; caustics and composite together 0.34 ms
  in the final run against 0.99 ms with the row off (noise: other runs swung 0.3 to 2 ms on the shared GPU).
- In game, at 1924 x 1084, expect: the caustics stage about 1 ms over open water by day, well under that in a cave with a
  lake; the composite's reading up to a millisecond or two where caustic light covers the view. The coordinator's number
  for comparison: the stand-ins' per-pixel lamp loop cost 42 ms in the mineral cave after PT-50. This has no per-pixel
  lamp loop: a tile of the water picks 3 lamps, its 256 threads walking all the lights once.
- Memory: 8 MB of buckets, 64 MB for a million triangles to start (grown when a frame wants more; logged).
- To read in game: the native log's "caustics" stage (between "denoising" and "composite") and the 10 s line's
  "caustics: N triangles a frame, at most M (room for K)".

## 6. Files, ABI, bits

- New: `native/pathtracing/shaders/pt_caustics.hlsl`, `docs/agents/caustics-2.md`.
- Changed: `pt_common.hlsli` (FrameConstants: `water[60]`, `waterNormals`, `caustics`, `causticGrid`, `causticLevels[8]`;
  `water_row`, `caustics_on`, `crosses_water`, `g_passWater`; the caustic query; `g_firstCaustic` replaces
  `g_firstDirect`; `water_caustic_at` removed), `pt_compose.hlsl` (stand-ins removed, `caustics_at`), `pt_water.hlsl`,
  `pt_water_under.hlsl`, `pt_shaders.hpp.in`; `tracer.cpp` (FrameConstants, the wave copies, the grid's constants, the
  caustics stage and its buffers, read-back and log), `extension.cpp`, `runtime.hpp`, `tgpt_api.h`; `CMakeLists.txt`;
  `src/TGPathTracing/WaterCaustics.cs`, `NativeTracer.cs`; `tests/pathtracing_gpu_test.cpp` (the water caustics block
  rewritten; `TGPT_TEST_STOP_AFTER_CAUSTICS` ends a run after it), `tests/source_checks.py`; `CHANGELOG.md`,
  `docs/PT-REQUESTS.md`, `docs/SOURCES.md`.
- ABI: `TGPT_WaterCaustics` replaced (192 B: the wave buffers, HDRP's band numbers, extent, absorption, largest move);
  managed `NativeWaterCaustics` and its size check follow. TGPT_Frame, TGPT_Status unchanged. No option bit, material
  bit, instance bit or FrameConstants `textures.z` use. The stage list gains "caustics" (TGPT_GetStageTimes is a string).
- Merge notes: base28 is before PT-50's merge; pt_compose.hlsl, pt_water.hlsl and source_checks.py conflict (the
  coordinator's, per the brief). In pt_compose the whole block from `water_caustic` to `water_reflected_lamps` goes, and
  the two sums read `sun[p].rgb + caustics_at(s)`.

## 7. What is missing, and what it would take

- Glossy floors that are not water (brief item 3; the cave's wet floor patches): not built. The same machinery takes it: a
  second grid, the camera's own pixels of glossy G-buffer surfaces (their position, normal-mapped normal and specular
  reflectance), 16 x 16-pixel tiles picking lamps as the water's tiles do, each reflected ray traced on, the same triangles
  into the same hash. Owed for it: a rough surface's lobe (a tile's microfacet tilt drawn per frame, so the light converges
  to the lobe's spread, not a mirror's lines), corners across depth edges left out, and saying that a puddle off screen
  reflects nothing. The bounce agent's flat-floor values (`<scratch>/wt/bounce`, its `docs/agents/bounce.md`) are its limit
  checks. About a day.
- Mirrors and bounced light don't see caustics: landed triangles are kept only where the camera can see them (half a
  view's margin), to keep the buffer small. Drop that cull (more memory; the read-back grows the room) and call
  `caustic_light` at a reflection's hit (pt_specular) and at every path vertex (`shade_path`: today only the first vertex,
  the bed seen through the water). Meanwhile, under water, a path vertex's direct light stops at the water and gets no
  caustic, so bounced light under water is darker than it should be.
- HDRP's current maps, water mask and deformers: the waves are HDRP's simulation without them (the F8 water line says "not
  followed: ..." for a slot that has them). Their sampling ports the same way (the current's quadrants and sector data, the
  mask texture, the deformation buffer).
- Emissive surfaces (glowing crystals) as caustic sources: a point of an emissive cluster a tile and frame, as a lamp's.
- The camera under water: HDRP draws it, as before.
- A lamp's caustic leaves one point of its emitter a frame (its size blurs the lines over frames): with NRD or the built-in
  denoiser the light is added after the filters, and that shows as a faint shimmer; Ray Reconstruction takes it in.
- Four water slots, the nearest surfaces (as before).
- Merge with PT-50: `light_weight` (pt_caustics.hlsl) takes `l.range` as a lamp's reach, as base28's `light_incident` does.
  Match it to whatever reach PT-50 gave `light_incident`.
