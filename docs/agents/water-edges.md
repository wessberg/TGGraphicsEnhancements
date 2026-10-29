# Water meets rock and shore; the traced water's defects (water-edges agent, 2026-10-07)

Copy `wt/wedges`, base `wt/base30`. Brief: `<scratch>/brief-water-edges.md`. Nothing run in game, nothing deployed or
committed. **Stopped by the coordinator (15:50)**: the owner ruled that the tracer fires its own camera rays for every pixel
(PT-59, PT-60), so water becomes an ordinary surface of those rays and `pt_water.hlsl`, `pt_water_under.hlsl` and the water
part of `d3d11.hlsl` go. The mutation runs were not started (section 3.1). Section 0 is what to keep.

## 0. For the switch to the tracer's own camera rays

### 0.1 What was found (details: sections 1 and 5)

- **The blocky cave lake** is mostly pale blue-grey flat slabs that flip from frame to frame. Their cause, read from the code
  and the pictures (not measured in game): the tracer's own copy of a rock (Medusa's meshes, at another level of detail) stands a
  few centimetres above HDRP's water where the drawn rock lies under it; the refracted ray started inside the copy, met its back
  face and took "the bed at the surface" with the copy's own material, and the cliffs' copy is flat pale grey (the Materials view;
  `TG/Environment/Cliffs/Cliff` has two layers). Each slab is the copy's flat face cut by the moving water.
- **The waterline** under that: HDRP's water mesh cut by flat rock polygons, so it runs straight along each face, and a face
  lying nearly level goes under or comes out all at once as the water moves a centimetre (the game's own frame shows the same).
  Real rock has bumps of centimetres there; the drawn rocks carry them only in their normal maps.
- **The beach**: one hard line, no wet band; the bed under the water at the picture's bottom edge pale grey with the caustics'
  net (`strip.jpg`): the bent ray's bed lies below the picture, so the tracer's own pale copy of the ground shows; plants under
  water blurred where their bent point is hidden (64 x 64 copies).
- **Found besides**: light leaving the water lacked the radiance law (L/n^2 is kept along a ray: the bed through the water was
  1.78 times too bright, brighter than the same rock just above the line), and the bed lacked its own light that the water's
  surface reflects back down (A / (1 - r_i A), r_i = 0.4749).
- **The outside review's findings**: 1 confirmed (the bed's material switches where its point leaves the screen or is hidden);
  2 confirmed (light and way averaged apart, absorption after, misses counting 1,000 m); 3 confirmed in its first part (the 10 cm
  back-face rule: the slabs' cause), its second (the binary waterline) is HDRP's mesh and needs the rock's relief; 4a confirmed
  (a white diffuse surface stands in for the water body's scattering); 4b confirmed (with caustics on the light's way down was
  counted twice); 5 confirmed (the rough reflection's sample weight; `shade_path` for what it meets). None found wrong. Fixes for
  1 (partly), 2, 3, 4b and 5 were built and pass offline (sections 2.1, 3), in passes that are now to be removed.

### 0.2 What survives the switch

- **The memory of where the water has been (PT-52) and the wet-surface optics apply to any surface**, and are independent of the
  water passes:
  - `native/pathtracing/shaders/pt_wet.hlsl` (the store's pass: per world cell, when the water last stood at each height, from
    HDRP's own waves), the store's plumbing in `tracer.cpp` (32 MB buffer, the levels' constants in double, the game's clock, the
    running ceiling, the pass before `prepare`), FrameConstants `wet`, `wetGrid`, `wetLevels[8]`;
  - `pt_waves.hlsli`: HDRP's waves at any point (`water_base`, `water_displace`, `water_point` moved out of `pt_caustics.hlsl`,
    and `water_height`, HDRP's own fixed-point search for the surface's height over a point);
  - `pt_common.hlsli` `wet_state` (the read), `wet_dry` (Jeffreys' draining film, sand's soaking, evaporation; sand told by
    slope), `wet_albedo` (Lekner and Dorf), `wet_material` (the film's mirror chosen by its share), applied in `hit_material`, so
    every ray's hit, and so the tracer's own camera rays, gets it with no more work; `pt_prepare.hlsl`'s call on HDRP's surfaces
    goes with the G-buffer path;
  - hidden row "Wet ground", `TGPT_OPTION_DRY_GROUND` (536870912).
  How far: built; offline every check passes (run w5: rock and sand at 30, 450 and 1,000 s on the expected darkening at every
  band pixel, rays the same, under the water and above the highest water untouched, the film's mirror; run w6: section 3.1);
  mutation runs not done; not seen in game. Cost: about 0.12 ms over a 120 m sea at 1280 x 720.
- **Under-water transport in `shade_path`** (`water_window` and the `g_firstUnder` flags: a path vertex under the water takes light
  from above only within Snell's window, n^2 times the air's radiance times the Fresnel transmittance, dimmed over its own way;
  beyond, total internal reflection): right for any vertex under water until the water is a traced surface that paths cross.
- The F8 water line's foam settings (`WaterCaustics.cs`).

### 0.3 What water as a surface of the tracer's camera rays needs from this

- **HDRP's displaced waves as the surface rays meet**, not the flat level quad the traced scene has: `pt_waves.hlsli` gives the
  height and normal at any point, the same reading the caustics and the wet store use (one reading of the waves for all).
- **The radiance law at every crossing**: a ray refracted between water and air carries radiance scaled by (n_to / n_from)^2
  (PBRT's non-symmetric scattering due to refraction); missing it made the bed 1.78 times too bright. And total internal
  reflection from below, which the paths then get for free (the bed's trapped light, r_i = 0.4749 for diffuse light).
- **Absorption per path segment in the water** (Beer-Lambert over each segment's own length), never on averaged lengths, and none
  counted twice (the light's way down belongs to the light's own path).
- **Traced geometry that agrees with what HDRP draws at the waterline**: the slabs came from the tracer's coarser copy of rocks
  standing above the water where the drawn ones lie under it. When the tracer's rays are the picture, that copy is what the
  player sees: near water (and everywhere near the camera) the finest level of detail, and HDRP's water level against the same
  rock.
- **Right traced materials**: the cliffs' copy is pale grey and plants are 64 x 64 copies; with HDRP's G-buffer gone from the
  picture they are the picture (PT-45, PT-46).
- **The waterline through the rock's relief**: a height map per normal map (integrated once, Frankot and Chellappa; section 4)
  or displacement, else the line stays straight along each face.
- **The water's body as a medium** along each refracted segment (single scattering of the light that came through the surface)
  in place of HDRP's scattering color lit as a white surface (finding 4a); HDRP's foam amount at the hit (its simulation and decal
  foam buffers, read as the wave buffers are), since HDRP's water GBuffer will not be there for the tracer's rays.
- The wet trail needs nothing more: `hit_material` already applies it.

## In short

- Seen (section 1): the cave lake's blockiness is mostly pale flat slabs flipping frame to frame, where the tracer's own copy of
  a rock stands above HDRP's water and the refracted ray started inside it; under that, the waterline runs straight along the
  rocks' flat faces.
- Built (section 2): the bed seen through the water is HDRP's where the traced copy disagrees (the slabs); light out of the water
  obeys the radiance law (it was 1.78 times too bright) and the bed takes back the light the surface reflects down; absorption per
  path before averaging and the view's way once (review findings 2 and 4b); the rough reflection's own weight and `shade_seen`
  (finding 5); wet ground with a memory (PT-52): a store of where HDRP's waves have been, darker and glossy by the optics of water
  on a rough surface, drying by slope and time, read by the camera and by every ray alike.
- Not built: the waterline through the rocks' own relief (needs a height map per normal map; designed in section 4), foam from
  breaking waves (HDRP makes none at a shore), off-screen beds' wrong traced materials, the water body's volume.
- Offline: every check passes (section 3, run w6: "all path tracing checks passed"); the mutation runs were not done (stopped).
  Not seen in game. Cost: 0.12 to 0.17 ms for the store over a 120 m sea at 1280 x 720.

## 1. What reads as blocky (looked at before any code)

Pictures: `artifacts/harness/20261007-105204/lantern-lake` (the cave lake), `20261007-103925/water-day` (the beach),
`<scratch>/strip.jpg`. Crops made with `<scratch>/we/*.py` (`lake9_left.jpg`: six frames of `9-shade-anim`, brightened
x3; `lake_left.jpg`: the game's frame over the traced one, same view; `wday4.jpg`: the beach's game, traced, Albedo and
Materials views).

- The cave lake's worst part is not the waterline: it is **pale blue-grey flat polygons** on the water, with straight
  edges, that appear and vanish from one frame to the next (`lake9_left.jpg`: in six frames 170 ms apart a slab a metre
  across comes, splits and goes). Their color is flat (no texture), the same in the Direct view. The game shows lighter
  sheets in the same places (`lake_left.jpg`, top): those are shelves of rock a few centimetres under the surface, seen
  through thin water. Reading of the traced ones (from the code, not measured in game): where the tracer's own copy of
  the rock (Medusa's meshes, one LOD per scene, `PROJECT-CONTEXT` 9.2) stands a few centimetres above HDRP's water while
  the drawn rock lies below it, `pt_water_under`'s refracted ray starts inside the traced rock, meets its back face at
  once, and takes "the bed at the surface" (`hit.position = position`); `hdrp_material_at` then misses (HDRP's depth
  there is the drawn rock, deeper than 3 cm + 1 %), so the bed's color is the tracer's own copy of the rock's material:
  the Materials view shows these cliff rocks (`TG/Environment/Cliffs/Cliff`, two layers) flat pale grey where the
  Albedo view has moss and stone. Each slab is the traced rock's flat face cut by the moving water: as the waves lift
  and drop the lake by a centimetre or two, whole near-level faces flip in and out.
- Under that, the waterline itself: the drawn rocks are flat polygons too, with no relief in their geometry, so the line
  where water meets them is straight along each face, and a face lying nearly level goes under or comes out all at
  once (the game shows the same, `lake_left.jpg` top). Real rock has bumps of a centimetre or more there.
- The beach (`wday4.jpg`): the traced shore is one hard line, with a dark fringe along it in places, and the sand just
  above it looks as dry as the sand far up the beach. No wet band, no gloss.
- `strip.jpg` (the beach's bottom edge): the pale grey slab with a net pattern is the bed seen through the water where
  the refracted ray's point lies below the picture's bottom edge (refraction bends the ray steeper, so the bed it meets
  is nearer the camera than the straight line's: about 0.9 m nearer through 1 m of water at 60 degrees). There HDRP's
  buffers have nothing, `hdrp_material_at` fails, and the tracer's own copy of the ground (flat pale grey) shows,
  with the caustics' net on it. The plants under water are blurred the same way: where the bent ray's point is hidden
  on screen (behind another blade), the tracer's 64x64 copy of the plant shows.
- HDRP's water mesh itself: no facets seen; open water moves smoothly in the animation.

## 2. What is built

### 2.1 The bed seen through the water (`pt_water_under.hlsl`, `d3d11.hlsl` TGPT_WATER_SHADE)

- **Where the traced copy of the bed disagrees with HDRP's, the bed is HDRP's.** The refracted ray's first hit a back face (it
  began inside the traced copy: a rock the tracer has at another level of detail stands above HDRP's water where the drawn one
  lies under it), or no hit where HDRP drew a bed behind the water: the bed is the plane through HDRP's own surface behind the
  pixel (its depth; the plane from its neighbours' depths), where the refracted ray meets it, with HDRP's color and normal
  there. The old rule ("a back face within 10 cm: the bed at the surface", with the traced copy's own material) is gone.
- **HDRP's material within near_back's window** (`hdrp_material_near`: 15 cm + 1 % of the distance, the project's allowance for
  a traced mesh's level of detail) instead of the composite's 3 cm + 1 %. Off screen or hidden the traced material stays
  (section 4).
- **Light out of the water obeys the radiance law**: radiance crossing from water into air is the water's over n^2 (1.78; L/n^2
  is kept along a ray), times the Fresnel transmittance the shade applies. Missing before: the bed seen through the water was
  1.78 times too bright, brighter than the same rock just above the line once that is wet.
- **The bed is lit again by its own light the surface reflects back down**: of diffuse light going up, r_i = 0.4749 comes back
  (total internal reflection beyond 48.6 degrees, Fresnel within; Stokes: r_i = 1 - (1 - r_e)/n^2), so the bed is lit as if
  its albedo were A / (1 - r_i A T^2), T^2 the water's transmittance up and back over its depth there (Lekner and Dorf 1988).
  Its bounced rays take light from above only within Snell's window (`water_window`, `shade_path`'s first bounce: the air's
  radiance times n^2 times the Fresnel transmittance, dimmed over the ray's way under water; beyond the window nothing, the
  trapped light being the A / (1 - r_i A) above).
- **Absorption per path, before averaging** (the outside review's finding 2): where the water is a caustics slot's and the
  Caustics row is on, each path's light is dimmed over the view's own way under water as it is traced (the slot's absorption,
  HDRP's extinction) and stored as dimmed; the shade averages light, not ways. The view's way once: the light's own way down is
  in its own light (the caustics' light carries its absorption, the bounced rays theirs). Elsewhere (more than four waters, or
  the Caustics row off) HDRP's model stays (twice the way), now per texel before the 3 x 3 average.
- **The rough reflection** (`pt_water.hlsl`, finding 5): each GGX sample weighted by F(v.h) G1(l) over the Fresnel term at the
  normal the shade multiplies by (1 for mirror-smooth water); what it meets lit by `shade_seen` (diffuse and specular light, as
  `pt_specular`'s reflections) instead of a path vertex's diffuse `shade_path`.

### 2.2 Wet ground (PT-52: `pt_wet.hlsl`, `pt_common.hlsli` wet_*, `pt_prepare.hlsl`, `hit_material`)

- **The store.** Levels of 256 x 256 cells round the camera, the finest 2 cm, each next twice as coarse, as many as reach the
  farthest water (8 at most: 2.56 m cells, 655 m); 64 bytes a cell, 32 MB. Each level's square sits on the world's own cells of
  its size (its corner's world index worked out in double on the CPU), a cell's record found by its world index modulo 256 and
  keyed by it: a cell stays the same piece of ground as the camera moves. Emptied before its first frame and with another scene.
- **What a cell remembers**: when the water last stood at or above each height, as up to six points (height, time) on a
  falling line, the oldest highest. Each frame, before the camera's surfaces are made, every cell reads where HDRP's water stands
  over its middle (`water_height`: the caustics' reading of HDRP's waves, moved to `pt_waves.hlsli`, with HDRP's own fixed-point
  search for the undisplaced point whose displaced one lands there). A rise drops the points under it and keeps where the
  remembered fall crossed the new height; a level held keeps its arrival time; a steady fall merges (within 0.25 s + 10 % of the
  age: drying times are seconds to hours); a full record drops the inner point its neighbours' line misses by least (a crest is
  never lost to its fall's points). Points older than 4 h (twice sand's drying) are forgotten. The clock is the game's shader
  time (Time.time, already sent each frame with the cloth's wind): the store stands still when the world does.
- **The drying** (`wet_dry`; every number with its source in `pt_common.hlsli`):
  - a film runs off a slope a as a draining viscous film (Jeffreys 1930): h(t) = sqrt(nu x / (g sin(a) t)) at a run x below where
    the water stood, x = (how far above the point it stood) / sin(a); one mirror while thicker than the surface's roughness, 0.1
    mm (fine sand's grains are 0.125 to 0.25 mm, Wentworth), so for nu x / (g sin^2(a) (0.1 mm)^2); its share after that falls
    as h / 0.1 mm. nu = 1.0e-6 m^2/s. On a 60-degree rock face 10 cm under the reach: 1.4 s.
  - sand soaks the film up: 0.5 mm into sand of conductivity 1e-4 m/s (medium sand, Darcy) in 5 s;
  - then the water held evaporates at 0.2 mm an hour (about 5 mm a day, a temperate potential evaporation): a rock's roughness
    holds 0.05 mm (about 15 minutes), sand its top millimetre's pores at porosity 0.4 (0.4 mm: about 2 hours); the wet share falls
    evenly with the water left.
  - sand or rock: the tracer can't tell them by material, so by slope: sand lies no steeper than its angle of repose (34
    degrees dry); nothing steeper than 45 degrees (damp sand's steepest) is taken for sand; between, a blend.
- **The optics** (`wet_albedo`, `wet_material`): light scattered by the surface under water meets the water's surface from
  below, r_i of diffuse light goes back down, the surface absorbs 1 - A of it again: A' = (1 - r_e)(1 - r_i) A / (1 - r_i A), r_e
  = 0.0665 (the water's Fresnel term averaged over a Lambertian hemisphere, computed), r_i = 0.4749 (Lekner and Dorf; water in
  sand's pores, their second case, works the same at the grains' scale). Wet albedo 0.3 is 0.57 of dry, 0.7 is 0.73; the moist
  share darkens by it. The film's share is one mirror of water: f0 0.0204 (HDRP's water) and HDRP's still water's roughness 0.05,
  chosen per pixel and frame by its share, so a pixel's two kinds of surface average over frames to their mixture (the normal
  stays the normal-mapped one: a film a tenth of a millimetre thick follows bumps of centimetres).
- **Who reads it**: `pt_prepare` changes HDRP's G-buffer values before anything lights them (so the traced reflections and
  highlights follow), on deferred surfaces that are ground (not skin, hair, eyes, glass, what HDRP shades itself), not where the
  water stands over the point now (HDRP draws its water there; the water passes see the bed through it). `hit_material` applies
  the same to every ray's hit (mirrors, the water's own reflection, bounced light, the Materials view). Nothing above the
  highest water since the store was emptied asks.
- **Switch**: hidden row "Wet ground" (on); `TGPT_OPTION_DRY_GROUND` (536870912) keeps the ground dry.

### 2.3 Foam (brief item 4): looked at, nothing built

HDRP 17's water makes foam from two sources only: the wind (whitecaps from its simulation: `simulationFoamAmount`, its wind
curve and optional mask) and the water decals and foam generators a level places (its foam buffers, `foamPersistenceMultiplier`).
No foam comes from depth at a shore or a rock. The tracer already lights HDRP's foam amount (its water GBuffer) with traced
light (`docs/agents/water.md`). So where the game's levels placed no foam generator at a rock, its water has none, and the cave
lake's calm surface has none. A real source would be breaking: waves break where about 0.78 of the water's depth high (McCowan
1894), and the swash's turbulence. The store knows the water's depth over the ground and the waves' height, so a foam source
from breaking could be built on it (a simulation of its own: made where waves break, carried by the water, decaying). Not
built. The F8 report's water line now names each surface's foam settings, so the next run tells which waters have foam on.

## 3. Checks (offline, `tests/pathtracing_gpu_test.cpp`)

Wet ground: in the caustics block (before `TGPT_TEST_STOP_AFTER_CAUSTICS`), two red ramps (0.75, 0.08, 0.06) run into the pool,
one at 60 degrees (rock), one at 20 (sand); the water stands at 0.45 m until the clock reads 9.99 s, at 0.30 m from 10 s; each
image against the same frames with `TGPT_OPTION_DRY_GROUND`, per pixel, the expected darkening from the drying model at each
pixel's own height (red within 1.5 %, green within 4 %: 8-bit sRGB in the GBuffer). Water shade: at the end of the water
reflections block (`docs/agents/water.md`'s lake). Numbers from run w5 (`%TEMP%\tgb\wedges-test-w5.txt`), the clean copy's full
run; w6 and the mutants: section 3.1.

| Check | Result | Mutant meant to break it (`<scratch>/we/mutate.py`; not run: stopped) |
|---|---|---|
| wet rock 30 s after the water left: every band pixel on the darkening (moist 0.97), above the highest water and under the water now unchanged | 11,575 of 11,575 (mean ratio 0.9967); 6,006 of 6,006 above, 1,392 of 1,392 under | mW1 (a tint, 0.6 A) |
| the same at 450 s (rock half dry) and 1,000 s (dry) | 11,575 of 11,575 each (0.9967, 1.0033, 0.9913) | mW3 (nothing evaporates) |
| wet sand at 30, 450, 1,000 s (moist 0.996, 0.938, 0.862) | 23,891 of 23,891 each (1.0057, 0.9993, 0.9985) | mW1 |
| rays see the same wet rock (the Materials view: `hit_material`) | 11,575 of 11,575 at each age | mW2 (rays don't read the store) |
| ground under the water now is left as it is | all of it (above) | mW4 (it is made wet too) |
| a film just left on the rock is a mirror of its own, gone once run off: the band's specular light | 0.00323 with it against 0.00395 dry (-18 %); 3,000 s on 0.00389 against 0.00396 (-1.7 %, the frames' noise) | mW7 (the film never chosen) |
| HDRP's waves (the caustics' 3 cm sine, eight phases): the rock wet as high as the crests reached, dry above | w6: 806 of 806 out of the water now wet; 7,623 of 7,623 above dry | mW6 (the store reads the level surface, not the waves) |
| a cell's full record keeps the highest the water reached (still water stepping down in pauses: six points filled) | w6: 462 of 462 | mW5 (a full record drops its oldest point, as first written) |
| the lamp on the bed seen through the water: (1 - F) / n^2 x A / (1 - r_i A) x E / pi within 10 % | 23,273 of 23,286 (mean 0.992) | mB2 (no radiance law), mB3 (no trapped light) |
| what lies under the water dimmed over the refracted way (per texel now), the Fresnel term, the scattering color | 547,516 of 547,519; 547,760 of 547,760; 27,969 of 27,989 (unchanged checks) | |
| a traced rock standing out of the water over HDRP's lower bed: the bed seen through it is HDRP's (green), never the rock's own (red) | w6: 496 of 496 green, 0 red (w5's 30 cm box gave 104, under the count's floor) | mB1 (the old 10 cm rule) |

What the first run taught (each fixed): the early-out above the water read the water's current top, so ground the water had
left above it read as dry (a running ceiling now, since the store was emptied); my sand ramp ran into the room's box; and in the
wave check a third of the band lies under the wave crest in the frame looked at, which is under water now and left as it is
(the check now skips it).

Cost (offline, 1280 x 720, other agents' tests sharing the GPU now and then): the store's pass, in the prepare stage, over a 120 m
sea with waves (seven levels, every cell over water): 0.46 ms with it, 0.34 ms without, so about 0.12 ms; over the pool, no
difference to read (0.14 and 0.14 ms). The water's passes over the lake (pt_water and pt_water_under): 0.62 ms (the water agent
measured 0.81 ms on a busier GPU). The camera's surfaces and every ray's hit below the highest water read four cells (64 bytes
each) for wetness. At the game's 1924 x 1084 expect the store's 0.1 to 0.2 ms by the sea, less in a cave; no change to the
water stage's 12 to 20 ms the coordinator reports from game (what takes that long there was not looked into here).

### 3.1 The clean copy's second run and the mutants

Run w6 (`%TEMP%\tgb\wedges-test-w6.txt`, the copy as it stands): "all path tracing checks passed". HDRP's waves: 806 of 806
rock pixels between 0.405 and 0.42 m that are out of the water in the frame looked at are wet, 7,623 of 7,623 over 0.445 m dry. A
full record: 462 of 462 rock pixels just under the first level still wet 90 s on. The traced rock over HDRP's lower bed (60 cm
box): 496 of 496 water pixels over it see the green bed, none its own red. The wet ground's other numbers as w5's. The sea's
prepare stage 0.45 ms with the store, 0.28 ms without (0.12 to 0.17 ms for the store).
The mutation runs (`<scratch>/we/mutate.py`, ten groups, `run-all.ps1`) were not run: the coordinator stopped the job at 15:50.
So no check above is mutation-checked yet; for the wet ground, run `build-mutants.cmd` and `gputest.ps1 -KillAt` with the
groups mW1 to mW7 once the stop lines at the top of both scripts are taken out.

## 4. What is missing

- **The waterline through the rock's own relief (brief item 2): not built.** The drawn rocks are flat polygons; their relief is
  only in their normal maps, which give slopes, not heights. The way to it: a height map per shared normal map, integrated once
  (Frankot and Chellappa's least-squares integration of its slopes, a periodic Fourier solve at 256 x 256 when the texture is
  registered; heights in texture units times the world length of a texture unit at the hit, |dP/du| over the tiling), its
  index in `MaterialRecord.textureReserved.x`; then at the camera's surfaces within a relief's height of the water (pt_prepare:
  a camera ray to the drawn surface for its texture coordinates, as it does for forward surfaces) the rock's height (the
  polygon plus the relief along its normal) against the water's from the store: a hollow under the water's level is a pool (the
  film's mirror at the water's level), a bump over it at a water pixel bare wet rock (pt_water_under), each pixel's share by the
  relief's slope across its footprint. About a day; the store and the wet optics it would use are in place.
- **Off-screen and hidden beds** (the review's finding 1, `strip.jpg`): where the bent ray's point is below the picture or
  hidden, the bed is the traced scene's own copy, and that copy is wrong for the cliffs (`TG/Environment/Cliffs/Cliff`, two
  layers: the Materials view shows them flat pale grey) and blurred for plants (64 x 64 tiles where the game's own textures are
  not shared). Fixing the copy (the cliff shader's layers; PT-45, PT-46) fixes mirrors too; nothing in the water pass can.
- **Light scattered in the water's body** (finding 4a): still HDRP's scattering color lit as a white diffuse surface at the
  water's place, a stand-in for single scattering along the refracted way. Not changed.
- Water outside the four caustics slots (or the Caustics row off): HDRP's absorption model (twice the way, per texel), and its
  bed's bounced rays not dimmed on their way up (no absorption known there).
- The radiance cache's light at a bed vertex under water ignores the water (no window, no absorption).
- Wet ground: no rain (PT-12), no splashes; sand told from rock by slope; one evaporation rate (no sun, wind or a cave's damp
  air); characters wading are not made wet; puddles on flat rock dry like sand (the slope rule takes flat rock for sand).
- Foam from breaking waves (2.3).

## 5. The outside review's water findings (GPT-6-astra, 14:40), tested against the code

| Finding | Verdict | What was done |
|---|---|---|
| 1. The bed's material switches at the screen's edge (`hdrp_material_at`) | Confirmed: off screen or hidden the traced copy shows, and the cliffs' copy is pale grey (Materials view) | Partly: HDRP's material within near_back's window; where the traced copy disagrees the bed is HDRP's. Off-screen and hidden beds still need right traced materials (section 4) |
| 2. Light and way averaged apart, absorption after the average; misses count 1,000 m | Confirmed (the shade's 3 x 3 average and the 12-frame history of ways) | Fixed for a caustics slot's water (absorbed per path, stored as dimmed); elsewhere per texel before the spatial average; the temporal average of the way stays there |
| 3. Back faces within 10 cm moved onto the surface; the waterline a binary stencil | First part confirmed and fixed (the drawn bed instead, the slabs' cause). The stencil part: HDRP's water mesh is the waterline; a pixel's share of water needs the rock's relief (section 4, not built) | |
| 4a. A white diffuse surface stands in for scattering in the water's body | Confirmed (HDRP's scattering color, lit at the surface) | Not changed: a volume to trace along the refracted way; named in section 4 |
| 4b. The light's way under water taken as the view's (exp(-2 sigma way)) | Confirmed: with caustics on, the light's way down is already in the caustics' light, so it was counted twice | Fixed for a caustics slot's water (the view's way once; the bed's bounced rays dimmed over their own way, `water_window`); elsewhere HDRP's model stays |
| 5. Rough reflection without its sample's Fresnel and masking; `shade_path` not `shade_seen` | Confirmed | Fixed (`pt_water.hlsl`) |

Also found here, not in the review: the light out of the water lacked the radiance law (n^2, 1.78 times too bright) and the
bed's trapped light (A / (1 - r_i A)); both fixed (2.1).

## 6. What the next in-game runs should show (`water-day`, `water-torch`, `glow-lake`, `lantern-lake`; `views` and an `anim` step)

- `lantern-lake`: no pale blue-grey slabs flipping on the lake; where the shelves lie a few centimetres under the water the
  drawn rock (moss and stone) shows through it, darker than before (the radiance law and the wet optics), as in the game's own
  frame but lit by the traced light. The line where water meets rock still runs straight along each face (the relief is not
  built); the rock just above it, as far as the lake's ripples reached since the save loaded, darker and, for a second or two
  after each ripple, glossy.
- `water-day`: behind each wave a glossy film for about 5 s, then dark wet sand up to the highest reach since the save loaded
  (the store starts empty on load and fills as the waves come; with the world frozen its clock stands still, so the band shows
  what the waves reached during the run, all of it fresh: the `anim` step shows it grow and dry). The sand under shallow water
  darker than before.
- `water-torch`: the cave's rock above the sea wet as high as the swell reaches.
- With "Wet ground" off (hidden row) the ground as before: the comparison for the owner.
- The native log's stages: "prepare" about 0.1 to 0.2 ms more by the sea; "water reflections" about as before.
- Exposure: the bed through the water and wet ground are darker: in water-heavy views the image is a little darker, which the
  automatic exposure may lift; read the exposure and gain line (AGENTS.md).
- The F8 report's water line: each surface's foam ("foam: whitecaps 0.3, decals and generators (Resolution512)" or "no foam").

## 7. Files, ABI, bits, merge

- New: `native/pathtracing/shaders/pt_wet.hlsl` (the store's pass), `native/pathtracing/shaders/pt_waves.hlsli` (HDRP's waves:
  `bicubic`, `water_base`, `water_displace`, `water_point`, moved out of `pt_caustics.hlsl` unchanged in what they do, and
  `water_height`), `docs/agents/water-edges.md`.
- Changed: `pt_common.hlsli` (FrameConstants `wet`, `wetGrid`, `wetLevels[8]`; the wet functions before `hit_material`;
  `hit_material` reads them; `hdrp_material_near`; `water_window` and the `g_firstUnder` flags, used in `shade_path`'s first
  bounce), `pt_prepare.hlsl` (wet ground on the camera's surfaces), `pt_water_under.hlsl` (2.1), `pt_water.hlsl` (finding 5),
  `d3d11.hlsl` (the shade's per-texel absorption), `pt_caustics.hlsl` (includes `pt_waves.hlsli`), `pt_shaders.hpp.in`;
  `tracer.cpp` (FrameConstants 7200 B; the store's buffer, constants, clock, ceiling, its pass before `prepare`, the pipelines);
  `tgpt_api.h` (`TGPT_OPTION_DRY_GROUND`); `CMakeLists.txt` (the `wet:pt_wet` pass, `pt_waves.hlsli` among the shader
  dependencies); `src/TGPathTracing/NativeTracer.cs`, `Settings.cs` (hidden "Wet ground"), `WaterCaustics.cs` (foam in the F8
  water line); `tests/pathtracing_gpu_test.cpp`, `tests/source_checks.py` (91 tests: two new, five pins moved); `CHANGELOG.md`,
  `docs/PT-REQUESTS.md` (PT-42, PT-48, PT-52).
- ABI: one new option bit (536870912, the one offered); no export, no exported struct changed. FrameConstants (internal) 7040
  to 7200 B. No material or instance bit.
- Merge: the mirror agent (`wt/mirror2`) also changes `FrameConstants`, `hit_material`, `shade_seen`, `trace_bounce` and
  `pt_specular.hlsl`; mine appends three FrameConstants rows at the end, adds a block at `hit_material`'s end and three lines in
  `shade_path`'s first bounce. The water caustic functions now live in `pt_waves.hlsli`: a merge that also edits them in
  `pt_caustics.hlsl` must move the edit. `d3d11.hlsl`'s shade loop and `pt_water_under.hlsl`'s under-surface block are rewritten
  whole (sections 2.1).
