# Water 4 - water on today's tree, its waves as real triangles (PT-71 item 5)

Copy: `wt\water4` (base `wt\base45`, ABI 0x00012100). Worker: Claude. Started 2026-10-07 night.
Nothing was written to the main tree, to `wt\water3` or to the game folder; the game was never started; nothing was deployed
or committed.

## Read this first (the finisher, 2026-10-08 04:14)

**The copy builds and its tests pass, except four lines that are the base's. It can go to the merge job as it is. It
was not seen in the game.** Two things need a decision or a look before anyone calls water done.

1. **I changed what the Caustics row's "Off" does under water, and that reverses the first agent's choice.**
   The first agent's "off": no sun or lamp light reaches anything under water, so the bed goes black. Its own tooltip,
   README and GPU check said the opposite. Mine: with the row off the light goes straight through the water, tinted by
   it, not bent. That is a stand-in, but only under "off", never by default. One line switches back
   (`water_passes_light`, `shaders/pt_water_surface.hlsli`). **Please confirm or reverse.**
2. **With the row ON, which is the default, nobody had checked what clear water's open edge does to the sun on dry
   ground. I found it by accident and could not judge it.** The check that seemed to cover it only ever ran with the row
   off. With the row on, the water's straight shadow is mostly lit: 0.62 of the open floor's light. That light does go
   through the water (an opaque liquid in the same spot leaves a clean shadow). Whether it is the right amount in the
   right place, nothing can say yet. The number is printed by the test and marked "NOT JUDGED".

What I did, in order: built the copy as it was left (it built; no half-done edit; the read-back path was already
finished). The whole GPU test from just after the cut-off had 9 FAIL lines; five were this job's. One I fixed in the
tracer (item 1). The other four are two checks, fixed in the tests: their scenes no longer fitted a world where the
water is real triangles. My tracer fix then broke the wave-shadow check, which led to item 2. That check is rebuilt as
three pictures: two hold whichever way item 1 is decided, the third pins the decision and is one line to flip.

Numbers: the whole GPU test once, a valid run, 1,239 ok and 4 FAIL (section 7). Water costs about 0.85 ms a frame at
1280 x 720 for a lake in view; my change costs nothing I could measure. The test lake holds **240 MB** of memory, which
nobody has decided is acceptable. Start-up from cold: 71 s against the base's 142 s (one run each).

Not done: the game, NRD, Ray Reconstruction, any mutation check. The first agent's tables on the denoisers' guides and
the waterline (section 5) are written and compiled, not run and not seen.

Where: section 0 is the log of what happened and when; 4 the numbers; 5 what is left; 6 the files and the ABI
(next free number 0x00012400); 7 the final build and its runs.

## 0. The finisher's log (water 4b, 2026-10-08 from 02:50; the first agent was cut off by a usage limit at about 01:17)

Brief: `docs/agents/briefs/brief-water-4b.md` (main tree). Builds: `%TEMP%\tgb\water4b-*`; scripts: the session's
`scratchpad\water4b-work\`. Same rules: nothing in the main tree, no game, no deploy, no commit.

- **02:53 The copy builds as it was left.** A fresh configure and build into `%TEMP%\tgb\water4b-build` (Ninja, NRD off):
  exit 0 in 54 s, no error. The Mono plugin: 0 errors, 0 warnings (`-p:DeployToGame=false`). Source checks: 93 of 93.
  No half-done edit was found: the last-touched files compile, and the first agent's own last build (`b13`, 01:14) holds them.
- **The read-back path is finished, not half done.** It is `GpuScene::water_triangles` (`scene.cpp`), with its member
  `waterReadbacks_` (`scene.hpp`), its use in `plan_water` (`scene_water.cpp`, "if(!water_triangles(...)) continue;") and its
  release when the water is cleared. It went in at 00:59 (build `b8`), before the cut-off. It is needed for a correct
  picture: a mesh shared between a water surface and anything else has lost its CPU copy by the time the water wants its
  triangles, and without the read-back that water would have no surface in the tracer at all. Kept.
- **The first agent's chain of GPU runs ran to its end after the cut-off** (plain processes, 01:17 to 01:46; logs
  `%TEMP%\tgb\water4-test-f2-*.log`, `water4-cold*-test-f2.log`): the water fixture PASS, the cost run, the passes from
  cold, the glass checks ("optical checks passed"), and the whole GPU test once: 1,233 ok, 9 FAIL, 25,047 frames, none
  failed, last error 0 (a valid run). Its FAIL lines are in section 7 with what each is.

- **03:05 What that whole run's own FAIL lines were, and what was done** (built: `water4b-build-b2.log`, exit 0; source
  checks 93 of 93; **not yet run on the GPU when this was written**: the next entry has the runs). Three causes, six lines:
  1. *"with TGPT_OPTION_NO_CAUSTICS the water stops no ray and the light under it is the straight light"* (the floor under
     the water was BLACK with the Caustics row off: the surface, now in the structure, stopped the sun's ray, and with the
     row off no caustic brings the light). **Changed in the tracer. This reverses a choice the first agent made on
     purpose, so it is the coordinator's to confirm.** The first agent wrote it into the API header ("stopped at its
     surface, row on or off") and into this report's game checks ("the bed ... is darker without the caustics"). But the
     copy also said the opposite in three places it left untouched: the row's tooltip ("Off: glass tints the light that
     passes through it without focusing it, and water casts no pattern"), the README, and this GPU check. I took the
     side of what players are told and of what was already settled, because the other side makes every lake bed lose
     the sun when a player turns the row off. So with the row off a light's straight ray passes the water's surface
     (`light_mask`, in the shadow rays' queries alone: `occluded_any`, `occluded_from_surface_rays`; a camera's or a
     bounce's ray always meets the water) and the water takes its share over each part of the ray under it
     (`water_straight`, `water_through`). With the row ON nothing changed: the ray stops at the surface.
     **The straight light is a stand-in** (no bending, no Fresnel loss at the surface). It exists only under the row's
     "off", as glass's does, and is never the default. To go back to the first agent's version: `water_passes_light`
     returns false (one line, `pt_water_surface.hlsli`), and then the tooltip, the README, `docs/TESTING.md` step 15,
     the API header and this check's text must say "off: no sun or lamp light under water".
  2. *"no water below the floor's level: nothing changes on it"*: **the check's own scene was inconsistent**, fixed in the
     test. It said "the water tops out at -0.1 m" in the slot and left the water's mesh 0.4 m up. The surface a ray meets
     is the mesh's triangles now, so the mesh stopped the sun over a floor no caustic reached. The check moves the mesh
     with the slot (`water_scene(true, -0.1f)`); what it asserts is unchanged.
  3. *"ground above the highest water and under the water now is not made wet"* (three lines, one check at three ages):
     **the check could no longer see what it judges**, fixed in the test. It counted the sand under the water from a
     camera above it; that camera's own ray meets the water's surface first now (0 pixels of ground: "under it 0 of 0").
     The ground under the water is now counted from a camera in the pool, under the surface (`underCam`, picture
     `44c-wet-ground-from-under-water.png`). What it asserts is unchanged.
  And one line that is the base's, not this job's: *"every mesh with alpha-tested instances has opacity micromaps"* fails
  the same way in four other workers' whole runs of last night (`glasshair-whole\gpu.log`, `light-gpu.log`,
  `memory-build\test-full.log`, `mats2-build\full-6.log`: "micromaps: 2 meshes, 0 waiting", then FAIL).

- **03:17 Those three fixes on the GPU: all six lines pass** (`water4b-test-p1-caustics.log`: the whole test's first
  part, up to and with the caustics and the wet ground, 592 s, nothing else on the GPU when it started). One FAIL in it,
  the base's: "glass bends light by its material's own index of refraction".
  - Row off: the floor under still water is off the open floor's light by 0.00000 at most (it was 1.00000: black).
  - Row on, unchanged from the first agent's run: 0.9784 of the open floor's sun (the water's transmittance 0.9779),
    32,346 of 32,346 pixels within 3 %; the sine wave's brightest line 10.10 x the mean (the CPU's rays: 10.30 x).
  - Water below the floor: 0.00000 off (it was 1.00000).
  - Wet ground under the water now, from the camera in the pool: 18,012 of 18,012 pixels unchanged, at 30 s, 450 s and
    1,000 s. Above the highest water 6,006 of 6,006 and 6,983 of 6,983 unchanged, as before.
  - The passes made from cold on this build (its shaders new to the driver): 70.8 s (base45 142.5 s, the first agent's
    build 64.9 s; one run each).

- **03:20 The same build BROKE a check that passed before. Not fixed yet when this was written.**
  `water4b-test-p1-water.log`: the water fixture, which passed on the first agent's build, fails one check on mine:
  "a shadow ray meets the same waves: the sun's shadow of the water's edge on dry ground has the wave's outline",
  wrong 11,495 of 26,376 pixels (it was 0). Every pixel that should be dark is lit.
  **Cause: my change to the row's "off", not noise.** The whole fixture runs with the Caustics row off
  (`water_surface_gpu.inl` line 44), and with the row off my change lets the sun's straight ray through the water, so the
  water's edge casts no shadow. I had not thought of that when I chose to let the straight light through.
  The fixture's other eleven measured lines are the same to the last digit as on the first agent's build (bed ratio,
  absorption, underwater bed, total internal reflection, mirror/camera, grazing, displaced normal, wave bed, wavy
  mirror/camera, shore, surface near), so nothing else it measures moved.
  So the entry above ("all six lines pass") is true and is not the whole truth: one edit fixed six lines and broke one.
- **The glass checks have NO result on this build yet.** `water4b-test-p1-glass.log` did not run: at start-up the
  bridge's device "could not run (a command queue)", the texture self-test passed 0 of 22 formats, 16 checks of texture
  sharing failed (none about glass), and the log stops after "scene shaders compile" with no verdict line: the process
  ended before any glass check ("optical checks passed" never printed, nor "FAILED"). The same build
  and stage passed those texture checks eleven minutes earlier in the caustics run, so I take it for a start-up failure
  of that one process, not the build; it is to be run again, and if it repeats it is a defect to find. The first
  agent's "optical checks passed" was on its own build: it does not cover my change, which is in the path every
  unblocked shadow ray takes (`water_through`), glass's too.

- **03:20 Build b3 (the wave-shadow check run with the row on, and counted with the row off too).**
  `water4b-test-t1-water.log`, `water4b-test-t1-glass.log`. The owner started Starfield during these runs (the gate let
  the offline tests through, as the rules allow): timings from here on mean less.
  - **Glass: "optical checks passed"**, 61 ok, 0 FAIL, the self-test 22 of 22. So the run before was that process's
    start-up failure. The glass checks pass on this build.
  - Row off: 11,495 of the 11,495 pixels in the wave's geometric shadow are lit. The new check for it passes.
  - **Row on: STILL FAILS. wrong 7,104 of 26,376.** Running the check with the row on did not bring it back. The
    picture (`water4b-images-t1-water\water-wave-shadow-0.png`) shows a thin dark band along the wave's outline and
    LIGHT above it, where the first agent's row-off picture (`water4-images-f2-water\water-wave-shadow-0.png`) is solid
    black. So with the row on, most of the water's straight shadow is filled with light.
  - What I do not know yet: whether that light is the caustics' (the sun bent through the wave onto the floor beyond,
    which the first agent never ran this check with) or something my change does with the row on. The first agent
    only ever ran this check with the row off.
- **The experiment that told those two apart (done; its one line is RESTORED, checked by a search of the sources).**
  For one build `water_passes_light()` returned false, which is the first agent's behaviour exactly
  (`water4b-test-x1-water.log`, pictures `water4b-images-x1-water\`):
  - Row off: 0 of the 11,495 shadow pixels lit. The solid black shadow, the first agent's own result again.
  - **Row on: wrong 7,104 of 26,376. The same number and the same picture as on my build.**
  So the light in the water's straight shadow with the row on is NOT my change's. It is there on the first agent's
  behaviour too. The first agent's check passed because it only ever ran with the row off, when the water was opaque to
  light and nothing brought light through it. **With the row on, which is what a player sees by default, nobody had
  looked at what the water's open edge does to the sun on dry ground. It is not the shadow that check describes.**
  Whether that light is right (the sun bent through a clear wave) or wrong is not known from this: the next entry.

- **03:31 The wave-shadow check, rebuilt as three pictures (build b4, `water4b-test-w4-water.log`: PASS).**
  The same floor, the same wave, the same pixels (26,376 compared; 11,495 of them in the wave's straight shadow):
  | Picture | Result | Judged? |
  |---|---|---|
  | An OPAQUE liquid (it takes all light within millimetres), row ON | **wrong 0 of 26,376**: the shadow has the wave's outline | yes: this is the check of the surface a shadow ray meets, now in the mode a player sees |
  | Clear water, row OFF | 11,495 of 11,495 shadow pixels lit: no shadow | yes: it pins what "off" does |
  | Clear water, row ON | 7,000 of 11,495 shadow pixels lit; the straight shadow holds **0.619 of the open floor's light** on average | **NO. Printed, not judged.** |
  What the first picture proves beyond the check itself: the light that fills the shadow under clear water goes
  THROUGH the water, because it is gone when the liquid is opaque. So it is the sun bent through the wave by the
  caustics pass, not a leak past the surface and not the straight ray counted twice.
  **What nobody knows: whether 0.619 is the right amount in the right place.** Nothing in the tests says where the
  sun should land beyond an open edge of water; the caustics have such a reference only for a sine wave over a level
  bed. And this scene is the report's own "open edges" gap: the water here is a sheet with no side, 10 cm over the floor,
  under a 12 cm wave. I would not trust that number in either direction. It is the first thing a next worker on water
  should build a reference for.
  These three hold whichever way the row's "off" is decided: only the second flips, and it is one assertion.
  My earlier line "cause: my change" (03:20) was half right: my change removed the only setting in which the OLD
  form of the check could pass; the light in the shadow with the row on was there before it.
- **03:46 The final build, and its runs (section 7 has them in full).** Build `b5-final` at 03:34 from the sources as
  they stand: exit 0, `TGPathTracing.Native.dll` sha256 `869999CC6F24F758B1312DFE2820884DAA09AD2D5EC9D340F33E3976DBD8A5CD`
  (staged in `%TEMP%\tgb\water4b-stage`). Source checks 93 of 93. No C# file changed since the plugin build of 02:53.
  The water fixture PASS; the glass checks "optical checks passed"; **the whole GPU test once: a valid run, 4 FAIL
  lines, none of them this job's** (it was 9 on the first agent's build).
- **After the whole run: one condition tightened in one check; built 04:06, run to 04:13.**
  I looked at the picture my new camera takes (`water4b-images-f1-whole\44c-wet-ground-from-under-water.png`): the
  sand ramp's foot is a red band across it, as meant. But the count took every non-black pixel that my geometry called
  "the ramp under the water", and "unchanged" is true of any surface that is never wet, so it could not tell the ramp
  from the floor. It now counts only the ramp's own red (`tests/pathtracing_gpu_test.cpp`, the loop over `underAt`).
  **The whole run did NOT have this condition.** It was built after it (`water4b-build-b6-test-only.log`: the test
  alone was compiled and linked; the tracer's DLL is the whole run's, the same sha256) and run in the whole test's
  first part, up to the caustics and the wet ground (`water4b-test-p6-caustics.log`, 376 s): **18,012 of 18,012 again
  at all three ages**, so every pixel counted before was the ramp. One FAIL in that part, the base's glass line.
- **The coordinator's two notes (03:28), both read.** (1) `tools\Build-PtShaders.ps1` for faster shader builds:
  **not used, it does not apply here.** It reads `CMakeFiles\TGPathTracingShaders.dir\build.make`, which an NMake tree
  has; this job's build folder is a Ninja tree (`CMAKE_GENERATOR=Ninja`, no such file), and Ninja already compiles the
  passes side by side: a full build here takes 54 to 83 s. (2) The next free ABI number is 0x00012400: written into
  section 6.

## 1. Status (kept current)

As the finisher left it on 2026-10-08 at 04:14. "Passes" means offline, on this PC's RTX 5080, without a denoiser.
Nothing here was seen in the game.

| Step | State |
|---|---|
| water3 merged onto base45 (`merge_copy.py water3 --base=base37`) | done by the first agent: 63 conflicts resolved by hand, builds |
| Waves as triangles in the acceleration structure | built; the water fixture passes (the surface near and far, 15 measured lines unchanged) |
| The read-back of a shared water mesh's triangles | finished by the first agent before the cut-off; kept (section 0) |
| One dielectric boundary for glass and water | done (`pt_interface.hlsli`); the glass checks pass |
| Wet ground's store reads the new surface | done (`water_over`); its checks pass, the ground under the water now seen from a camera under it |
| The Caustics row's "off" under water | **changed by the finisher, to be confirmed by the coordinator**: the straight light (section 0, 03:05) |
| The sun at an open edge of clear water, row on | **found, not judged**: section 5, item 1 |
| Guides for the denoisers, waterline | the first agent's tables in section 5: written and compiled, not run, not seen |
| Cost and start-up | measured: section 4. Memory (240 MB for the test lake): measured, not decided |
| Source checks | 93 of 93 |
| Managed build (Mono, net472) | 0 errors, 0 warnings |
| The whole GPU test, once | a valid run, 1,239 ok, 4 FAIL: three the base's by the brief's list, one the base's by other workers' logs (section 7) |

## 2. The design of the wave geometry (written before the code)

### What the game gives us (read from the harness logs of 2026-10-07, not guessed)

- Every water surface of the game simulates its waves at **64 x 64 texels a band** (log lines "has its waves in the tracer
  (N bands, 64x64, ...)": Ocean 3 bands, rivers 2, lakes 1). So the game's wave data is coarse: a band's texel is its
  patch size / 64, and HDRP itself reads it bilinear.
- 11 water surfaces in one scene; most are **custom meshes** (rivers and lakes drawn from the level's own meshes, in world
  space), some are quads, the sea is a quad under the camera. So the base of the water is an arbitrary triangle mesh, not
  a level rectangle: a river slopes. A grid in the world's x and z cannot stand for that; the grid has to live on the base
  triangles themselves (water3 chose the same, for the same reason).

### The surface (one definition, for every ray)

The water's surface is a function of two things only: the game's wave data of this frame and the camera's **position**.
Nothing of the screen, of the view direction or of the ray enters it. Camera, mirror, bounce and shadow rays all meet the
same triangles, because the triangles are in the acceleration structure like any other mesh.

1. Each base triangle of a water mesh carries a lattice of points that halves again and again (level j: the triangle's
   edges cut in 2^j pieces). A lattice point's undisplaced place is exact (whole-number lattice coordinates), so two
   pieces of the mesh that share a point compute the same bits.
2. A lattice edge is wanted when it is longer than the target cell at its distance: `h(d) = max(h0, d * K)`, with `h0`
   = one texel of the surface's finest wave band and `K` = 1/64 (a cell grows by 1.56 cm for each metre of distance).
   `d` is the distance from the camera to the undisplaced edge. (Because the lattice halves, real cells are between half
   that and that.)
3. A point that first appears at level j sits at the middle of an edge of level j-1 (its "parent edge"). Its position:
   `lerp(middle of its two parents' positions, the game's displaced position at the point, w)`, with
   `w = clamp(length of the parent edge / h(d) - 1, 0, 1)`. So a point slides continuously from "on the straight line
   between its parents" (not needed: the surface is the coarser one there) to "where the game's waves put it" (fully
   needed) while the camera approaches. No step in space, none in time.
   The length used is the base edge's own length for points on an edge of the base triangle (both neighbours then agree
   bit for bit), and the base triangle's longest edge for points inside it.
4. Level 0 points (the mesh's own vertices) are always at the game's displaced position.

Because of 3, tessellating finer than needed changes nothing (the extra points have w = 0 and lie on the coarser
triangles), and tessellating as fine as needed gives the surface exactly. That is what makes it crack-free without any
stitching: a fine piece next to a coarse piece has its extra edge points on the coarse piece's edge by construction.
(The extra points are computed as the middle of two floats, so they are on that edge to half a unit in the last place;
that is the one tolerance in the scheme.)

### How it becomes triangles (the machinery, as the plants' groups)

- CPU, once a water mesh instance (`scene_water.cpp`): its base triangles sorted along a space-filling curve and cut into a
  tree of ranges. The mesh keeps its positions and indices on the CPU for this (`Mesh::water`).
- CPU, every frame, per water mesh in view (`plan_water`): walk that tree from the top. A range whose triangles all need
  at most 2^m pieces an edge and holds at most 2,048 triangles after cutting becomes one **unit** (range, m); a single
  base triangle that needs more than 32 pieces is cut in four and each quarter is asked again (so a 300 m lake drawn with
  two triangles and a river drawn with 5 m quads both come out right). The test is conservative (nearest point of the
  range's box, 1 % on the safe side), which only ever cuts finer than needed: allowed, see above.
- A unit has a fixed topology for its life: its vertices in a pooled buffer (position, undisplaced point, position a frame
  ago), an index pattern shared by all units of its level, its own BLAS in a pooled buffer. Built when first wanted,
  **refitted** every frame after that, dropped 120 frames after its last use. No limit on the number of units.
- GPU, every frame (`pt_water_mesh.hlsl`, new): one compute dispatch per lattice level writes the units' vertices from the
  game's wave textures (this frame's and the last frame's, for motion), coarse level first so a point can read its
  parents. Then the refits, then an instance each with a mask bit of its own (`MASK_WAVES`, part of `MASK_ALL`).
- A ray that hits a unit gets an ordinary triangle hit (`fetch_hit`). `water_hit` adds the water's part: the undisplaced
  point and the place a frame ago by the triangle's barycentrics, and the game's own wave normal there (HDRP's bicubic
  gradient read, `pt_waves.hlsli`) for the Fresnel reflection and refraction. The triangle's own normal says which side
  the ray is on and which medium a direction leads into. So the look of the waves does not depend on the cell size; only
  the surface's position does, by the error in section 4.
- The per-ray walk of the height field (`water_patch`, `water_subdivision`, `water_box`, `water_triangle`, `trace_water`'s
  loop over every base triangle of every water mesh) is deleted. `trace_water` is one query of the acceleration structure
  for the water alone now (the camera's first walk, the medium a point lies in).

### What is kept exactly

water3's transport: `water_path` (Fresnel by chance, the index squared for camera radiance, total internal reflection,
free-flight absorption and scattering, foam coverage). Its boundary maths moved into `pt_interface.hlsli`, which glass uses
too now (`dielectric_interface`, `interface_radiance`, `segment_transmittance`, the one `fresnel_dielectric`):
`pt_water_optics.hlsli` is gone.

### What changed against water3, and why

- **The normal at a water hit is the game's wave normal, not the triangle's.** water3 shaded each texel-sized triangle
  flat: a mirror made of facets. HDRP shades its water with the gradient texture; so does the tracer now. Where the waves
  are finer than the triangles (far away) a mirrored direction can point into the triangle; it is turned back out
  (`above_surface`), the rule every normal-mapped mirror of the tracer already follows. That is an approximation and is
  named as one (section 5).
- **Light under water.** In water3 a surface under water got the sky's light only: a straight shadow ray stops at the
  water, and nothing brought the sun's or a lamp's light through it. The caustics pass traces exactly that light through
  the same waves; `water_path` now adds it at every path vertex that lies in the water (`caustic_light`). It reads the
  landed light of the frame before (the caustics pass runs later in the frame), which is why the old buffers now live one
  frame longer when the room for them grows (`make_caustic_triangles`). (All of this with the Caustics row on. With it
  off the finisher let the straight light through instead: section 0.)
- **The water pixel's path is shade_path's one water path.** The reflections pass called `shade_seen` twice and
  `water_path` once more; it calls `shade_seen` once now for glass, water and reflections alike. Its DXIL is 19.0 MB of
  header text against base45's 31.5 MB (and 38.7 MB right after the merge).
- **Water without a slot stays HDRP's.** water3 stopped HDRP from drawing any water; with 11 surfaces and 4 slots, 7 would
  have vanished. HDRP's draw and lighting stop only for the surfaces the tracer draws (`WaterCaustics.Traced`).
- **The open sea's quad stands on a 1,024 m grid** (it was re-centred on the camera at every scan, which would shift the
  lattice under every wave).

## 3. Workarounds met

Removed in this job (with water3's list, which the merge carried over: the old passes `pt_water.hlsl` and
`pt_water_under.hlsl`, their histories, render events 4 and 6, `TGPT_SetWater`, `TGPT_SetWaterShade`, `WaterShade.cs`, the
Water Reflections row, the D3D11 water shading):

- The per-ray height-field walk (above).
- `pt_water_under.hlsl` (the merge had left it: main changed it after water3's base) and with it `g_firstKnown`,
  `g_firstMaterial`, `g_firstCaustic` in `pt_common.hlsli`.
- `crosses_water` and the second water query of the shadow rays: the surface is in the structure.
- `water_height` (the wet store's four-step search for the water's height): the store asks the surface itself now.
- `WaterCaustics.Hold` and the checks that pinned it (HDRP's own caustics need no holding back for water HDRP does not draw).
- water3's worker scripts (`tools/Build-Water3.ps1` and three more) were not carried over.

Met and left (not this job's, or named in section 5):

- The four water slots (`TGPT_MAX_WATER`): a limit that changes the picture. Mitigated (water past them is HDRP's), not
  removed.
- `water_base` and the level water instances (`MASK_WATER`): the caustics pass still launches its light from the level
  mesh plus `water_displace`, the exact field, not from the units' triangles. The two agree to the error of section 4.
- `occluded_from_surface_rays`' near-surface passes, the composite's HDRP depth matching, `OPTION_GBUFFER_SURFACES`: the
  camera-ray workers'.
- `water_medium`: "in the water" is "under the nearest water surface straight above". A cave under a lake, or two
  bodies one over the other, need real membership.

## 4. Numbers

All offline (`tgpt_gpu_test` with `TGPT_TEST_WATER_SURFACE=1`, 128 x 72, no denoiser, RTX 5080), while up to three other
workers' tests shared the GPU: read the times as "about", the ratios as real. Logs and pictures: `%TEMP%\tgb\water4-test-*.log`,
`%TEMP%\tgb\water4-images-*`.

### The stage's cost, before and after (the same fixture, the same scenes)

| Scene (128 x 72) | water3: the walk for every ray | water4: triangles in the structure |
|---|---|---|
| Wavy pool, camera above | prepare 20.49 ms, reflections 5.41 ms, bounced light 0.57 ms, scene 0.03 ms | prepare 0.06 ms, reflections 0.17 ms, bounced light 0.03 ms, scene 0.30 ms |
| Wavy pool in a mirror | prepare 29.02 ms, bounced light 28.28 ms, reflections 8.84 ms, scene 0.03 ms | prepare 0.08 ms, bounced light 0.22 ms, reflections 0.11 ms, scene 0.19 ms |
| Still pool | prepare 0.04 ms, reflections 0.05 ms | prepare 0.06 ms, reflections 0.18 ms, scene 0.08 ms |

The walk's 20 to 66 ms a frame are gone; what the water costs now sits in the scene's stage (posing the vertices and
refitting): 0.2 to 0.3 ms for the 8 m pool, which is cut to its finest everywhere (the camera is 2 m over it).

### A lake in view at 1280 x 720 (`TGPT_TEST_WATER_COST=1`; filled in by the finisher from the first agent's last runs)

A floor 512 m wide seen from eye height towards the horizon, dry, then under 40 cm of water with the fixture's waves.
No denoiser, no radiance cache, the Caustics row off. Measured by the finisher from 04:04 to 04:06 with the first
agent's build and the final build back to back, twice (`water4b-test-c2-*.log`), while the owner's game was NOT running
and one or two other workers' tests were. Each cell: the two runs of the first agent's build / the two of the final build.

| | Dry floor | The lake |
|---|---|---|
| scene (posing the vertices, the refits) | 0.03, 0.03 / 0.03, 0.03 ms | 0.57, 0.57 / 0.57, 0.57 ms |
| prepare (the camera's own rays) | 0.24, 0.27 / 0.25, 0.28 ms | 0.33, 0.34 / 0.34, 0.33 ms |
| reflections (the water pixel's path) | 0.24, 0.22 / 0.24, 0.22 ms | 0.45, 0.45 / 0.45, 0.45 ms |
| bounced light | 4.04, 3.84 / 4.09, 3.83 ms | 0.98, 0.94 / 0.96, 0.97 ms |
| GPU, a frame | 4.42, 4.48 / 4.51, 4.56 ms | 1.92, 1.93 / 1.92, 1.93 ms |
| The water's surface in the structure | none | 1,376 pieces, 1,250,816 triangles, 687,888 vertices |
| Its memory | none | **240 MB** of pooled pages (vertices and acceleration structures) |

How to read it:
- **The water's own cost here is about 0.85 ms a frame at 1280 x 720**: the scene stage (0.54 ms more than dry) plus what
  its pixels add in prepare (about 0.08 ms) and reflections (0.22 ms).
- **What the finisher's change to the row's "off" costs** (this run has the row off, which is the path that changed):
  nothing in the lake's frame (1.92 and 1.93 ms on both builds, every stage within 0.04 ms). For the dry floor every
  stage agrees to 0.05 ms, but the whole frame's GPU time reads 0.09 ms higher on the final build (4.51 and 4.56
  against 4.42 and 4.48 ms). That is about the spread between two runs of one build: with two runs each I cannot call
  it a cost or call it nothing.
- The lake's frame is cheaper than the dry floor's only because the dry floor's bounced light is the big number in this
  scene. I read the lake's lower bounced light as "a water pixel's light is traced in the reflections stage, not there"
  (section 2 says so of the design); I did not measure it apart. It does not mean water is free.
- **Times taken while the owner's game ran are not usable, and the first agent's were of that kind.** Its own cost run
  (01:17, three other tests on the GPU) read bounced light 15.95 ms dry and prepare 2.86 ms for the lake. Six more runs
  of mine while Starfield held the GPU at 90 to 95 % (`water4b-test-c1-*.log`) read 12 to 22 ms for the dry floor's
  bounced light, and reflections and composite for the lake anywhere from 0.6 to 2.3 ms, the same build differing from
  itself by three times. One reading there I cannot explain and did not chase: under that load the lake's bounced light
  read 0.16 ms on both builds, lower than the 0.95 ms of a quiet GPU.
**The 240 MB is the number to look at.** Half a kilometre of lake cut to its finest near the camera holds 1.25 million
triangles. It is not a limit and nothing is cut, but nobody has yet decided whether that much memory is acceptable, and
it was not measured in the game (the game's lakes and their wave texels differ from the fixture's).

### Start-up: the passes made from cold (the driver's shader cache emptied by a nudged constant)

| Tree | Passes made in |
|---|---|
| base45 (main at 23:44) | 142.5 s |
| water4, the first agent's build | 64.9 s (64.0 s on that build's own first run) |
| water4 with the finisher's shader change | 70.8 s (build `b2`, 03:06: the first run of shaders the driver had not seen, nothing else on the GPU when it started). The final build's shaders differ from `b2`'s by comments only, and the driver took them from its cache (1.5 to 2 s), so it has no cold run of its own |

One run each, on a shared GPU, so 64.9 against 70.8 s is not a measured difference: the finisher's change adds one
ray query to the shadow rays' path, which may or may not be those 6 s. (A fourth figure, 87.0 s for the experiment
build at 03:23, was taken while the owner's game held the GPU at 92 %: not usable.) The slowest of water4's 25 pipelines by its own time: the reflections 64.9 s, the two
shader-execution-reordering variants 57.9 s and 45.4 s, bounced light 34.1 s (they are made side by side on their own
threads, so the total is the slowest one's, not the sum of 245 s). base45's log has no per-pipeline times to set beside
them.

### What the triangles leave of the game's wave data (the fixture's waves: one band, 32 x 32 texels over 4 m, 12 cm high,
their texels deliberately off the lattice's points)

| Where | Off the wave data | For scale |
|---|---|---|
| Near (1.3 to 3.8 m along the camera's ray, six directions) | at most 0.072 mm | level water would be up to 119 mm off |
| 30 m | 2.4 mm | an edge fully on the waves is up to 0.95 m there |
| 63 m | 1.6 mm | up to 1.97 m |
| 126 m | 57 mm | up to 3.95 m: as long as the wave itself (4 m), which is then no longer followed |

How to read it for the game: near the camera (within 64 x half a texel of the surface's finest band: a few metres for
the game's 64-texel bands) every lattice edge of a texel's length is fully on the waves, and the surface is the bilinear
wave data to a tenth of a millimetre here; in general to at most a quarter of the data's second difference between
neighbouring texels. Farther out an edge that is fully on the waves grows to at most 1/32 of the distance, and a wave of
length L whose crests stand A over its middle is followed to at most A x (pi x edge / L)^2 / 2: a 20 m swell 30 cm from
trough to crest, 100 m away (edges up to 3 m), to under 2 cm. Waves shorter than two such edges are not followed there
at all; they keep their look, because the normal at every hit is the game's own.
**Not measured in the game.** The plugin's log has each surface's bands and texels ("has its waves in the tracer (N
bands, 64x64, ...)"), not each band's patch size and height; with those two numbers per band the formula above gives
the game's millimetres. The native log's "The water's surface: N pieces, T triangles, ..." says how much is traced.

## 5. What is left

### First, what the finisher found (2026-10-08), most important first

1. **Nobody has checked what clear water's open edge does to the sun on dry ground with the Caustics row ON, and that
   is the default.** The offline check that seemed to cover it only ever ran with the row off. Run with the row on, the
   water's straight shadow is mostly lit: it holds 0.619 of the open floor's light on average (7,000 of 11,495 pixels
   over half the brightest). That light goes through the water (an opaque liquid in the same place leaves a clean
   shadow with the wave's outline, 0 wrong of 26,376). Whether it is the right amount in the right place is unknown:
   there is no reference for light beyond an open edge, and the scene is itself gap 7 below (a sheet with no side).
   **Needed:** a reference traced on the CPU for the sun through a wave at an open edge onto a floor beyond it (as the
   caustics' sine wave has), then judge the printed line. In the game: look at a shore where a water mesh ends in the
   air over dry ground.
2. **What the row's "off" does under water is for the coordinator to confirm** (section 0, 03:05). As left: off = the
   straight light, tinted by the water's absorption, no bending, no Fresnel loss; a stand-in that exists only under
   "off". The first agent had chosen: off = no sun or lamp light under water at all. One line switches between them.
3. **`water_straight` (row off only) misses the water a ray passes under an open edge**: a ray that starts in the air,
   slips under a water mesh's open edge and leaves through the surface from below is counted as never in the water.
   Too much light there. Found by reading, not measured; noted in the code. The row on has no such case.
4. **Memory was never decided.** The 512 m test lake holds 240 MB of pooled pages (1.25 million triangles). Not
   measured in the game.
5. **Not done by the finisher, and not claimed:** the tables below on the denoisers' guides and the waterline are the
   first agent's, read from the code. I changed nothing there and checked none of it on the GPU. NRD is not built in
   this copy (`-DTGPT_WITH_NRD=OFF`), Ray Reconstruction never runs offline, and the game was not started. So "done"
   in those tables means "written and compiled", never "seen".

### The denoisers' guides at a water pixel (read in the code; NRD is not built in this copy, the game not run)

| Guide | What a water pixel has now | State |
|---|---|---|
| Position / depth | the water's own surface where the camera ray meets it (`pt_primary`, the surface record) | the tracer's own filters and NRD's inputs read it. **Ray Reconstruction still gets HDRP's depth**, which at a water pixel is the bed's (HDRP no longer draws that water): the cross-mod frame hand-off is TG Better Upscaling's file, another session's. Left. |
| Motion | the wave's own: the same lattice point a frame ago, from the wave data of the frame before (`waterPrevious`) | in the tracer's own motion buffer. Ray Reconstruction gets HDRP's (the bed's). Left, same hand-off. |
| Normal | the game's wave normal at the hit (shading) and the triangle's (geometric) | done |
| Roughness | 0 on clear water; the foam's own where foam covers | done |
| Diffuse albedo | the foam's colour times its cover (0 on clear water) | done |
| Specular albedo (RR) | 1: the whole of the pixel's light is in the reflections' signal | done (`specular_guide_albedo`) |
| Hit distance | from the surface to the first thing the pixel's path met past it, whichever it was (the mirrored world or the bed) | a mix of two images by chance: where the bed shows through and the sky mirrors, the distance jumps between them from frame to frame. A steady rule (the distance of whichever of the two carries more light) needs both distances a frame. Left; expect ghosting of the weaker image under a moving camera. |
| NRD's diffuse input | **excluded** (this job): water and what is seen from inside it are passed to RELAX as glass and sky are, so the shore beside the water keeps its own bounced light unmixed | done, compile-checked against NRD's headers by hand (`pt_nrd_prep.hlsl`); not run |

So with **None** a water pixel is its path, one a frame. With **NRD** the same path goes through the tracer's own filter for
reflections (NRD here denoises bounced diffuse light only): the water will be as steady as mirrors are today, no better.
That is the first thing to look at in game.

### The waterline

- Where waves meet rock and sand the tracer now takes the nearest of the two real surfaces, ray by ray: the waterline
  follows the waves and the rock's own shape, with no flat step and no screen-space mask (water3's shore check still
  passes on the triangles). Water gets clearer as it gets shallower by its own absorption, so the line is soft where the
  bed rises gently, as in the game.
- **Foam at the shore is not there.** HDRP makes foam in two ways: from the simulation (whitecaps, traced: `water_foam`
  reads the Jacobian and the game's foam texture) and from the level's foam generators and water decals, which it draws
  into a foam buffer per surface. That buffer is not shared with the tracer yet. It needs: the buffer's texture handed
  over per slot (`waterFoamMaps` has two unused numbers a slot), its region (centre and size, one more row a slot in the
  frame constants) and a read in `water_foam`. Not done.
- The wet band above the waterline (`pt_wet`) reads the new surface (a ray straight down at the triangles).

### Other gaps, in the order I would take them

1. **Slots.** Four water surfaces are traced; a scene had 11. The rest are HDRP's (drawn and lit by HDRP, as before the
   migration, without traced reflections). The fix is to take the slots out of the frame constants' fixed arrays.
2. **Current maps, water masks, deformers** (water3's list, unchanged): a surface that uses them gets the game's waves
   without them. The F8 report names them per slot.
3. **Glass inside a water path** is shaded as a plain surface (the water path has its own surface shading and does not
   follow glass). A bottle under water looks painted. `dielectric_follow` and `water_path` share the boundary maths now
   but are still two loops.
4. **Far water at a low angle**: where the lattice is coarser than the waves (beyond roughly 64 texels' distance) the
   wave normal can point a mirrored ray into the flat triangle; it is turned back out. That is the tracer's rule for
   every normal-mapped mirror, and an approximation: the true answer is the neighbouring wave's reflection.
5. **The caustics launch from the exact wave field, the rays meet the triangles.** Near the camera they agree to a
   millimetre; far away the caustic's pattern is finer than the surface the camera sees refracting. The caustics pass
   could launch from the units' vertices instead.
6. **Cost when the camera moves fast**: a unit is built the frame it is first wanted (a few hundred to two thousand
   triangles each). Teleporting to an open sea makes a few hundred in one frame.
7. **Open edges**: a water mesh that ends in the air (not under the shore) ends there in the tracer too, as a sheet with
   an edge.

### What the coordinator's two game runs must show

`lantern-lake@dots` (no denoiser; the step "2-water-pass-off" is gone from the recipe with the pass):
- Log, once: `Water: "..." has its waves in the tracer (N bands, 64x64, ...)`; native status line `water: U pieces of its
  surface (T triangles, V vertices, M made this frame, K kept)` with U above 0, M falling to 0 within a second of standing
  still, and no "could not be made". No "water reflections" or "water shade" stage in "GPU time by stage".
- Picture `1-all-on` (mean of the frames): the lake reflects the lantern and the cave, the bed shows through near the
  shore and fades with depth; no isolated bright dots on the water (`flicker.py`'s six spots should not sit on open water
  more than on rock; a spot that does is the water path's noise, not a pass's). `3-caustics-off` against `1-all-on`
  (corrected by the finisher, section 0): with the row off the bed under the lantern is lit by the lamp's STRAIGHT light,
  so it is not black and has no bright lines or moving pattern; with the row on the same light arrives bent and focused
  into the pattern, and a little dimmer on average: the surface's Fresnel loss, which the straight light skips (offline,
  under still water in the test's sun: 0.978 of the open floor's light with the row on, 1.000 with it off; not measured
  in the game). A bed that goes black with the row off means `light_mask` did not reach the build.
- `6-all-on-world-running`: the waves move, and the reflection moves with them; no cracks (lines of bed colour through
  the surface) where the triangles change size, near the camera or at a few tens of metres.

`water-day@quick` (no denoiser, then NRD):
- `2-traced-none`: the bed under shallow water, the far shore in the reflection at a low angle, the waterline following
  the rock. Exposure and gain lines: the corrected transport (1/n squared) and the caustics-only sun under water change how
  bright the bed is; read them before judging. The sea at the horizon is still water if the log says "is traced as STILL
  water" for it.
- `3-traced-nrd`: `flicker.py --boil` on the water region against the game's own figure. Expect the water to be the
  noisiest thing in the picture (one path a pixel, a mix of two images); the number says by how much.
- If a far river or lake looks like the game's own water (screen-space reflections, no traced light): it is one of the
  surfaces past the four slots (gap 1).

## 6. Shared files changed, structs, ABI

**The next free ABI number is 0x00012400** (the coordinator, 2026-10-08 03:28: main took 0x00012300 tonight for
the skin program and a submesh's closed mark; the brief's "0x00012300" is out of date). Main's `GeometryRecord` now has
`layers` and `flags` as its third and fourth words (bits 1 = closed, 2 = own layers). This copy still has base45's
record. For the merge job, not checked by me: the water's units write geometry records of their own (`pose_water`,
"the units' mesh and geometry records after the groups'"), so they will need those two words filled.

**ABI number**: the copy keeps main's 0x00012100; the coordinator assigns the merged one. It must change: against base45
the exports `TGPT_SetWater` and `TGPT_SetWaterShade` and render events 4 and 6 are gone, and `TGPT_WaterCaustics` (managed
`NativeWaterCaustics`) grew from 192 to 288 bytes (scattering, foam: water3's). `TGPT_Status` keeps its 216 bytes (its two
water counters stay, at zero). Option bits: none. The Water Reflections row and its config key are gone (the recipe
`dots` in `tests/ingame/scenarios.json` lost its step that switched it off).

Not ABI, but pinned in several files: `FrameConstants` 6,512 to 7,888 bytes (water3's rows: scattering, the waves of the
frame before, foam, the water meshes' records; C++ and HLSL agree, `test_path_tracing_abi` has the sum). New GPU records:
`WaterRecordGpu` 64 bytes (its last word is now the slot's cell, a float), `WaterTriGpu` 32 bytes. Instance mask
`kMaskStill` 0xE1 to 0x61 with `kMaskWaves` 0x80 new (shader `MASK_WAVES`, `MASK_SOLID` 0x77; `MASK_ALL` and `MASK_STILL`
unchanged, so every existing query meets the water without being touched). Instance flag 0x400 (`INSTANCE_WAVES`), set by
the tracer alone. One material of no kind is uploaded after the scene's own (index = the scene's material count).

Shared files, and what changed in each:

| File | Change |
|---|---|
| `native/pathtracing/src/tracer.cpp` | water3's removals (passes, events, histories, D3D11 water shaders) and frame constants; the `waterMesh` pass; `set_water` before the scene is recorded; the status line's water part; the caustics' old buffers kept one frame longer |
| `native/pathtracing/src/scene.cpp`, `scene.hpp` | the water records' cell; water meshes keep (or read back) their triangles; the material of no kind and its index; `plan_water` and `pose_water` called in `record`; the units' mesh and geometry records after the groups'; `kMaskStill`; the structs above |
| `native/pathtracing/src/scene_water.cpp` | **new**: everything of the water's surface on the CPU |
| `shaders/pt_common.hlsli` | the masks and `INSTANCE_WAVES`; `fetch_hit` calls `water_hit`; `trace_closest`, `occluded_any`, `trace_from_surface`, `occluded_from_surface_rays` through `water_mask`; `passes_through` never passes water; `transmittance`; `shade_path`'s one water path (`WATER_HERE`, `g_waterGuide`); `shade_seen`'s one `shade_path`; `g_first*` gone; `fresnel_dielectric` moved to `pt_interface.hlsli`; the duplicate `reflected_lamps` the merge left is gone |
| `shaders/pt_primary.hlsli` | the camera's first walk leaves the water out (`MASK_SOLID`); the water pixel's record (water3's) with the wave normal |
| `shaders/pt_specular.hlsl` | one `shade_seen` for glass, water and reflections |
| `shaders/pt_dielectric.hlsli` | uses `pt_interface.hlsli`; its medium queries leave the water out |
| `shaders/pt_interface.hlsli`, `pt_water_surface.hlsli`, `pt_water_path.hlsli`, `pt_water_foam.hlsli`, `pt_water_mesh.hlsl` | **new** (the second to fourth water3's, rewritten or adapted) |
| `shaders/pt_waves.hlsli`, `pt_wet.hlsl`, `pt_caustics.hlsl`, `pt_nrd_prep.hlsl`, `pt_compose.hlsl`, `pt_rtxdi.hlsli`, `d3d11.hlsl`, `pt_shaders.hpp.in` | `water_height` gone; the store asks `water_over`; the shared boundary; water left out of NRD's diffuse input; water3's small changes |
| `CMakeLists.txt` | the path tracing shader lists and `scene_water.cpp` only |
| `src/TGPathTracing/*.cs` | water3's removals (`WaterShade.cs`, the row, the old pass's hooks); `WaterSurfaceRendering.cs` per surface; `WaterCaustics.cs` (`Traced`, `AnyUntraced`, the sea's quad on a grid); one string in `SceneCapture.cs` |
| `tests/pathtracing_gpu_test.cpp`, `tests/water_surface_gpu.inl`, `tests/source_checks.py`, `tests/ingame/scenarios.json` | the old water pass's test gone; the fixture and its new checks; `test_path_tracing_water_surface` and the pins that moved |
| `README.md`, `docs/TESTING.md`, `native/pathtracing/include/tgpt_api.h` (comments) | say what is true now |

**What the finisher (water 4b) changed on top of that: eight files, all in the copy, listed by their write times.**
No struct, no export, no option bit, no render event, no config key, no C# file. The ABI is untouched by the finisher.

| File | Change |
|---|---|
| `shaders/pt_water_surface.hlsli` | new: `water_passes_light`, `light_mask`, `water_straight` (the row's "off": the straight light) |
| `shaders/pt_common.hlsli` | `occluded_any` and both queries of `occluded_from_surface_rays` ask through `light_mask` (they asked through `water_mask`); new `water_through`, called by `transmittance` and `transmittance_from_surface`; comments. `trace_closest` and `trace_from_surface` are unchanged: a camera's or a bounce's ray always meets the water |
| `include/tgpt_api.h` | one comment (what "off" does at the water). No declaration changed |
| `tests/pathtracing_gpu_test.cpp` | `water_scene(on, level)` and the "water below the floor" picture's scene; `at(..., from)`, `underCam`, `underAt`, the count of ground under the water from under it, picture `44c-wet-ground-from-under-water.png` |
| `tests/water_surface_gpu.inl` | the wave-shadow check as three pictures (`liquid`, `shot`, `rowOn`); one check renamed, one added, one line printed and not judged |
| `tests/source_checks.py` | the shadow rays' mask pins and the "off" pins; the list of allowed ray masks; the count of queries for the water alone, 3 to 4; the fixture's check names |
| `docs/TESTING.md` | step 15 (row off: the straight light) and the fixture's paragraph (the three pictures) |
| `docs/agents/water-4.md` | this report |

For the merge onto today's main: the brief says main has moved on with glass and a speed job. I did not look at what
they changed; `pt_common.hlsli`'s shadow-ray functions and `transmittance` are the likely place to meet them. The
finisher's edits there are five lines and one small function. What must survive is pinned in `tests/source_checks.py`:
the shadow rays' masks and the "off" pins in `test_path_tracing_no_placed_lights` (the block that starts "The light
under the water came through its surface"), the count of queries and the fixture's check names in
`test_path_tracing_water_surface`.

Not touched: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md` (the merge had
changed them with water3's text: put back to base45's), the neural rendering session's files, the bridge, frame generation,
upscaling.

For the coordinator's files, what this job learned (not written there):
- A mesh shared between a water surface and anything else loses its CPU copy before the water needs it; the tracer reads
  it back from its own buffers (8 frames).
- The driver's shader cache hides a pass's real making time: a measurement needs a build whose shaders it has not seen
  (section 4).
- `passes_through` lets a ray from a camera surface through any back face within 15 cm: it took a wave's shadow off the
  shore until water was excepted. And such a ray starts 2 cm over its surface (`ray_offset`), which shortens a 10 cm
  wave's shadow by 1.2 cm. Both are the G-buffer era's and are still used for surfaces the tracer's own camera rays made.
- The Bash tool's heredocs lose backslashes even when quoted; write scripts with the Write tool.

## 7. The final build's runs (the finisher, 2026-10-08)

Three claims, kept apart: **built** (yes), **passes offline** (below), **seen in the game** (no: the game was never
started, nothing was deployed). NRD is not in this build (`-DTGPT_WITH_NRD=OFF`); Ray Reconstruction never runs offline.

**The build.** `%TEMP%\tgb\water4b-build` (Ninja, Release), log `water4b-build-b5-final.log`, exit 0.
`TGPathTracing.Native.dll` sha256 `869999CC6F24F758B1312DFE2820884DAA09AD2D5EC9D340F33E3976DBD8A5CD`, staged in
`%TEMP%\tgb\water4b-stage`. The Mono plugin: 0 errors, 0 warnings (built 02:53; no C# file changed after it). Source
checks: 93 of 93 on the sources as they stand.

**The whole GPU test, once** (`water4b-test-f1-whole.log`, 03:35 to 03:46, 666 s, two other workers' tests on the GPU
when it started). **A valid run:** "frames traced 25099, failed 0, last error 0x00000000". 1,239 checks ok, 4 FAIL:

| FAIL line | Whose |
|---|---|
| glass bends light by its material's own index of refraction (another index lays the light elsewhere) | the base's (the brief's list) |
| clear glass casts no shadow | the base's (the brief's list) |
| a character's eye drawn forward is shaded by the tracer, not left to HDRP | the base's (the brief's list) |
| every mesh with alpha-tested instances has opacity micromaps | **not in the brief's list.** I take it for the base's on this evidence only: it fails with the same line before it ("micromaps: 2 meshes, 0 waiting") in four other workers' whole runs of last night (`glasshair-whole\gpu.log`, `light-gpu.log`, `memory-build\test-full.log`, `mats2-build\full-6.log`). I did not run base45's whole test myself |

The first agent's build had 9 FAIL lines in the same test. The five that are gone are the five of section 0 (the row's
"off", the water below the floor, the wet ground at three ages). No new line appeared. "The passes were made" and the
lake's timing check both passed here (the driver's cache was warm).

**The water fixture** (`water4b-test-f1-water.log`): PASS, 2,484 frames, none failed. Its fifteen older measured lines
(optics, the surface near and far) are the same to the last digit as on the first agent's build, compared line by line
on this run. The wave-shadow check is the three pictures of section 0.

**The glass checks at their own size** (`water4b-test-f1-glass.log`): "optical checks passed", 61 ok, 0 FAIL.

**One thing the whole run does not cover.** After it, one condition was tightened in the wet-ground check (section 0).
The test in the copy is that later one: `tgpt_gpu_test.exe` of build `b6`, with the same tracer DLL. Its changed check
ran in the whole test's first part (`water4b-test-p6-caustics.log`): ok at all three ages, 18,012 of 18,012. The rest of
the whole test was not run again with it; nothing else in the test changed.

**Runs that were not results** (so nobody takes them for one): `water4b-test-p1-glass.log` (the process could not make
a command queue at start-up and ended before any glass check; the same build passed on the next try);
`water4b-test-x1-water.log` (the experiment build, on purpose); `water4b-test-c1-*.log` (cost runs while the owner's
game held the GPU). `water4b-test-p1-water.log` and `water4b-test-t1-water.log` are real FAILs of builds that are not
the final one: they are how the wave-shadow finding was made.

**What was not done.** No mutation checks, as the brief says (one deliberate experiment to find a cause, section 0,
restored and checked). NRD and Ray Reconstruction: not built, not run. The game: not started. Nothing deployed,
nothing committed. base45's whole test: not run by me.

