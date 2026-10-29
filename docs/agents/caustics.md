# PT-14: lamps and torches reflected off water (caustics agent, 2026-10-06)

The owner, after the harness's water runs: "Not seeing any caustics in any of these shots, but really the only one of
them where I guess I'd assume they should appear in terms of actual physics is the final one inside the cave with the
blue minerals. So yeah, build caustics from lamps and torches". And: "I'd also expect to see better reflections in that
pool of water in the mineral cave."

Status: built and checked offline in the copy `wt/caustics` (tgpt_gpu_test passes, source checks pass, every new check
mutation-checked). Not in the game yet.

## In short

- The mineral cave's "pool" is not water to the tracer. It is the cave floor's own glossy wet patches (part of the
  floor's look in HDRP's G-buffer), not a water surface. So the new light (below) does nothing there yet.
- The cave does have HDRP water: a pool 19 x 16 m whose middle is 5.5 m behind the camera of the owner's save, and two
  more further off. The tracer already has those as water.
- Built: a lamp or torch reflected off HDRP's water lights what faces the water (walls and roof over a pool), with the
  water's own ripple pattern, Fresnel, falloff over the whole path and shadows. Caustics row off: none of it.
- The soft streaks in the cave's puddles: the tracer draws a lamp's highlight as a ball the size of HDRP's light radius
  (0.25 m typical, up to 1.5 m for these lamps); the game draws a point. Written down, not changed (it changes every
  lamp highlight in the game: the owner's call).

## 1. What the cave's water and lights are

The save `PT mineral-cave` (harness `glow`, run `20261006-215244`): scene `Dungeon_ChangeHeart` (Lovers' Hollow), camera
at (12.35, 8.16, 58.52) looking yaw 333.8 degrees (towards +z, a little -x), 0.5 degrees down.

What the tracer has as water there:

- The harness step line: `scene: 1125 instances (... water 3)`. These are HDRP `WaterSurface`s with caustics on that
  `WaterCaustics.Capture` put into the traced scene (`TGPT_INSTANCE_WATER`).
- I read the scene's bundle (`scenes_scenes_dungeon_changeheart.bundle`, UnityPy in a scratch folder; the script is
  `scratchpad/caus/scene_water2.py`, not in the repo). It has exactly three `WaterSurface`s, all with caustics on,
  intensity 1.5, tiling 0.5, 256 px pattern, geometry Custom:
  - `WaterSurfaces_Dungeon_SmugglersBay_01`: OceanSeaLake, HDRP's default water material, smoothness 0.906. Its mesh is
    Unity's built-in 10 m Plane (`Library/unity default resources`, path id 10209) at world (11.76, 5.62, 52.97), scaled
    1.92 x 1.56 (19.2 x 15.6 m), turned -43 degrees. From the camera it reaches only 4.4 to 6 m ahead, 2.5 m below the
    eye: the very bottom edge of the picture at most. Its middle is 5.5 m behind the camera. The sound emitter
    `Env_Lake_Dungeon_3D_01` sits beside it at (14.34, 6.40, 52.96): this is the cave's lake, behind the player.
  - `WaterSurfaces_Dungeon_SmugglersBay`: the same plane at (20.01, -2.31, 55.04), 8 m lower, 40 x 41 m.
  - `Water`: a River with a custom material at (16.38, 5.94, -12.10), 70 m away.
- The floor in front of the camera, with the lamps standing on it, is not one of them. It is the floor's own wet look:
  - The tracer's water-reflection pass (it runs on every pixel of HDRP water and of transparent surfaces HDRP gives
    screen-space reflections) cost 0.03 ms in `glow`: nothing to trace. In the sea cave `water-torch` it cost 0.86 to
    0.95 ms, on the beach `water-day` 0.28 to 0.33 ms (`TGPathTracing.Native.log`, "stages").
  - The traced reflections of the floor (`pt_specular`, the opaque surfaces' reflections from HDRP's G-buffer) cost
    4.57 ms there, the most of the run. The streaks in the puddles are the traced lamps' (yellow, as the traced lamps
    are), not HDRP's (whose lights do not light surfaces while tracing). So the puddles are opaque surfaces with a low
    roughness in HDRP's G-buffer: the floor material's wet patches, or HDRP decals that make it glossy (the scene has a
    `Decal Projector_04`). Either way their gloss exists only in the camera's G-buffer, not in the traced scene: a ray
    from a wall that meets the floor meets dry rock.
  - `TG/Water/CustomWater/CustomWater` was first logged in this step ("invisible to rays (transparent or unlit)"): a
    transparent mesh of the game's own water shader somewhere within the tracer's reach. Not on screen (the 0.03 ms).
- Under the HDRP pools the lamps' light already takes the ripple pattern (`water_caustic`: where there is no sun above
  the horizon it multiplies the direct light, lamps included, by the pattern straight up). Nothing new was needed for
  "the lamps' light refracted onto the bed" in HDRP water; the refraction's sideways shift is not modelled.

Lights: `94 traced lights` (and no sun), `59 placed lights left out (3 left out: specular-only or on other light
layers)` (the status line). New light names in this scene, all left out as fill light: `Point Light_17B` x31,
`Spot Light` x2, `Point Light_01Vol` x2. The lamps' cookies `Cookie_Candelabra_02B` and `Cookie_TorchBig_Low` were
traced in this step, so the floor lamps are the game's candle and torch lights. Over the session (LogOutput line 240):
HDRP radius 0.025 to 2 m, median 0.25 m; the lamp lights' names carry radii `PointLight_Main` 0.22 to 1 m,
`PointLight_Fire` 1.5 m, `PointLight_Shadows` 0.6 to 1.06 m, `PointLight_Cookies` 0.25 to 4 m.

How the tracer could know the cave's wet floor (not built: each is a bigger step; for the owner and the coordinator):

1. Screen space, cheap: for a wall point, mirror the lamp in the floor's level as seen in HDRP's depth, project the
   reflection point onto the screen, and take the floor's gloss there from HDRP's normal buffer (its roughness) and its
   height from HDRP's depth. Works only for puddles on screen; light from a puddle that leaves the screen goes too.
2. Puddles into the traced scene: read the decals (or the floor material's wet mask) into the traced material, so a
   ray that meets the floor knows where it is wet. Then the floor's wet patches act as mirrors to every ray. Bigger:
   HDRP decals are projected boxes with their own textures.
3. The game's transparent water meshes (`TG/Water/CustomWater/CustomWater`) as water instances with no pattern: a
   plugin change in each of SceneCapture's capture paths (GameObjects, Drake, Medusa), and a slot "water without a
   pattern" (today a slot needs HDRP's pattern texture). Helps where the game uses that shader for pools; not this floor.

## 2. Why the lamps' reflections in the cave's puddles are long soft streaks

Seen (`glow/1-game` against `glow/2-traced-rr`, the floor cropped): the game shows each lamp as a short, sharp glint in
the puddle; traced, each is a long soft column, three to four times as long, and brighter.

The cause, most likely first:

1. **The tracer reflects a lamp as a ball the size of HDRP's light radius.** The direct light's sample is a point on a
   disc of the light's radius (`pt_rtxdi.hlsli` `RAB_SamplePolymorphicLight`: `disk_sample(uv) * l.radius`), and the
   highlight is the specular lobe evaluated there (`rtxdi_shade`). Over frames that is the reflection of a glowing ball
   of that radius. These lamps carry radii of 0.25 to 1.5 m (above). A ball half a metre to three metres across, seen in
   a glossy floor from a grazing angle, is a long soft column. HDRP uses the radius for the shadow's softness only; its
   highlight of a point or spot light is a point's (the game's short glints). The radius is right for soft shadows and
   wrong for the highlight.
2. The floor's roughness, read as HDRP has it (the normal buffer's alpha, `pt_specular`): the GGX lobe stretches a
   highlight along the view at grazing angles, in the game as traced. That alone gives the game's short glints.
3. Ray Reconstruction denoises the reflection from one ray a pixel; it softens a little more.
4. The 64x64 texture copies make the reflected walls in the puddles blurry. They don't touch the lamps' streaks
   (a highlight is light, not texture).

Not the cause: the puddles are not water to the tracer (section 1), so the water-reflection pass, its 12-frame
history and its own roughness have nothing to do with them.

The fix I would propose (not built: it changes every lamp's highlight on every glossy surface, metal and stone alike,
so the owner should see it first): evaluate the highlight towards the light's centre, or a disc capped at a few
centimetres, and keep the disc for the shadow ray. How to confirm before building it: an F8 capture in the cave with
the crosshair on a puddle (the report gives HDRP's roughness there and the lights near it), or the harness `glow`
with lamp radii capped for the highlight only, the floor cropped against `1-game`.

## 3. What I built, and why

`pt_compose.hlsl` `water_reflected_lamps(Surface s)`, added beside `water_reflected_sun` into the direct light after its
filter, in the final image and the Direct debug view. It works on the water the tracer already has (HDRP water,
`TGPT_INSTANCE_WATER`, the slots of `TGPT_SetWaterCaustics`), with or without a pattern texture in the slot's math (a
slot today always has one).

The physics. Over level water at height h, the light a point p gets from a lamp at L by the mirror is the light the lamp
sends to p's mirror image p' = (p.x, 2h - p.y, p.z) along a straight line. That line crosses the water at the reflection
point w. So:

- Which lamps: the light grid's cell at p' lists exactly the lamps whose range reaches p' (the whole path's length).
  Point lights and spots above the water; emissive clusters are left to the paths.
- How much: `light_incident(l, p')` (the lamp's range window, I / d^2 over the whole path, its spot cone and cookie
  along the direction L to w), HDRP's fog along p to w to L instead of the straight line, times the water's Fresnel
  reflectance at the lamp's angle on the water (Schlick, 0.02 face on: index 1.33), times HDRP's pattern at w (blurred
  by the lamp's disc seen over the distance, as the sun's), times the point's cosine towards w, over pi. Diffuse only.
- Which two: of all slots and all lamps in the cell, the two with the most unshadowed light at p. Only those are traced.
  This keeps the cost bounded: at most 2 water-only rays and 4 shadow rays a pixel, and the shadow rays only where the
  water is really there.
- Is the water there: a water-only ray (MASK_WATER) from p towards w must meet that slot's water within 5 cm (+1 % of
  the distance) of the water's level at w. No water there (a pool that ends, water behind a wall): nothing.
- Is the path clear: a shadow ray from p to the water (`transmittance_from_surface`), one from 2 cm over w to the lamp,
  ending 5 cm short of it (`transmittance`; the lamp's own mesh does not block it). Glass on the way tints it.
- Gates: none with the Caustics row off (`OPTION_NO_CAUSTICS`), no water slot, no lights; none for a surface facing
  straight up (n.y > 0.97: no water below it lies in front of it) or below every water's lowest point.

Why at the reflection point w and not where the shadow ray crosses the water: the shadow ray starts a few centimetres
off the surface; at that crossing the Fresnel term was up to 5 % off on the test's wall (26 % of pixels outside 3 %).
Why no randomness: the light is added after the denoiser, like the sun's reflection; a random lamp choice would be
noise that Ray Reconstruction and NRD never see.

Under water (the lamp's light refracted onto the bed): already there for HDRP water. `water_caustic` multiplies a
point's direct light (sun and lamps) by HDRP's pattern where its light crosses the water; with no sun above the
horizon it looks straight up (a cave's pool). The sideways shift of refraction is not modelled. Nothing new built.

What it looks like, physically: weak. Still water reflects 2 % of a lamp's light face on, about 5 % at 60 degrees and
more only at grazing angles. Next to the lamp's own light on a wall it adds a few percent. It shows where the lamp's
own light doesn't reach (an overhang, a roof over a pool whose lamps are shaded upwards by their cookies) and where
HDRP's ripple pattern bunches it into moving bands (the pattern's peaks are several times its mean of 1).

## 4. Numbers (offline test)

`tests/pathtracing_gpu_test.cpp`, after "Sunlight reflected off the water": the room's wall at x = -1.6 facing a water
quad at y = 0.4 (x -1.5 to 0.9, z -1.2 to 0.4), the sun off, one spot 0.6 m over the water at (-0.3, 1.0, -0.6),
intensity 60, range 10, radius 3 cm, pointing down, cone 60 degrees (inner 50): its own light never reaches the wall,
so the wall's direct view holds the reflected light alone. Each image: 40 frames undenoised, the last 24 averaged, the
same seeds each time; compared with the wall without water. Image `43b-water-reflected-lamp.png`.

| Check | Result | Mutation that breaks it |
|---|---|---|
| wall pixels whose mirrored path meets the pool inside the cone: on the hand-worked value (I x window x cone^2 / d^2 x Fresnel x cos / pi x exposure) within 3 % | 303294 of 303294 (brightest 0.03254) | Fresnel 0.04 face on: 0 of 303294 (batch C) |
| HDRP's stripes: bright (> 1.5x) and dark (< 0.5x) bands | 137598 and 105056 | pattern ignored: 0 and 0 (A) |
| mirrored path misses the pool | 1623 pixels, 0 | no water ray: 0.002375 (A) |
| Caustics row off | 0 | row ignored: 0.032532 (A) |
| a card standing in the pool before half the wall | 77845 shaded pixels at 0 %; 136976 of 136976 open ones on value | no shadow rays: 99.98 % (B) |
| a 0.5 m card 10 cm under the lamp | 0 | no shadow rays: fails (B) |
| the pool moved behind the wall | 0 | no water ray: fails (A) |

Batch A also failed the two older "no ... with TGPT_OPTION_NO_CAUSTICS" checks (the room's own lamp's reflection
appeared there), as it should. On the first build the card under the lamp let 2.6 % through at one pixel: its shadow
ray started 2 cm over the water but ran parallel to the true path and grazed the card's edge; it now aims at the lamp
from there, and the card is 0.5 m (every path inside the cone crosses it within 17 cm of its middle).

Cost: too small for the test to resolve. The composite took 0.070 ms with the lamp's reflection and 0.070 ms with the
row off (one lamp); with 32 lamps over the pool (1280x720, least of three 200-frame runs each) 0.11 ms with it and
0.19 ms without, i.e. noise. Worst case by construction: 2 water-only rays + 4 shadow rays a pixel, plus a loop over
the mirrored cell's lamps for each slot (the mineral cave: 10 lamps a cell on average, 44 at most, 3 slots). Read the
native log's "composite" stage in game (0.78 ms in `glow` before this).

`python tests/source_checks.py`: 80 tests, OK (two pinned compose lines updated, six new asserts for the lamp term).
`tgpt_gpu_test`: every check passes (`exit 0`).

## 5. Files changed, ABI, options, material bits

- `native/pathtracing/shaders/pt_compose.hlsl`: `water_reflected_lamps`, called in the Direct view and the final image.
- `tests/pathtracing_gpu_test.cpp`: the lamp-reflection block (after the reflected-sun block).
- `tests/source_checks.py`: the two compose lines it pins now include `water_reflected_lamps(s)`; new asserts.
- `CHANGELOG.md`: the entry at the top of "TG Path Tracing: what the in-game harness found first".
- `docs/agents/caustics.md`: this file.

No new option bit, no material bit, no ABI change, no FrameConstants change, no managed (C#) change. The Caustics row
(`TGPT_OPTION_NO_CAUSTICS`) switches it with the rest. Merge: in `pt_compose.hlsl` the new function after
`water_reflected_sun`, and the two sums in `main` (the Direct view's and the final diffuse) that now read
`to_storage(water_reflected_sun(s) + water_reflected_lamps(s))`.

Not done, for PT-REQUESTS (new defect): "Lamp highlights on glossy floors are long soft streaks (the tracer reflects a
lamp as a ball of HDRP's light radius; HDRP as a point)" with the evidence of section 2. And for PT-14: "the mineral
cave's puddles are not water to the tracer" with the three ways in section 1.

## 6. What the next run should show

`-Scenario glow -Recipe caustics`: no visible change. The cave's puddles in view are not water the tracer has, and its
HDRP lake is behind the camera; `caustics.jpg`'s difference stays noise. What to read there: the native log's
"composite" stage time against 0.78 ms (3 water slots, about 10 lamps a cell: the loop runs on most wall pixels), and
no new failures.

Where it should show: a pool of HDRP water in a dungeon or cave with a torch or brazier at its edge and a wall or roof
over it. Candidates: the same save turned around (the lake `WaterSurfaces_Dungeon_SmugglersBay_01` is 5.5 m behind the
camera; whether lamps stand by it is unknown), or a new owner's save "PT pool-torch". `water-torch` is a daytime sea
cave with no torch in view: its rock gets the sun's reflection (round 3), not a lamp's. With the Caustics row on, walls
and roof over the pool get a faint warm light from below, in bands that move with HDRP's ripples; off, none. The
Direct debug view (Shift+Insert) shows it best.

## 7. Risks

- Faint by physics: 2 to 5 % of the lamp's light over most angles. The owner may expect a stronger effect; making it
  stronger than the Fresnel term would be a fake light.
- A seam where the two strongest lamps change: a third lamp's reflection is dropped. Small, since it is the weakest.
- Water that is not level (a river's custom mesh with a slope): the water ray must find the water within 5 cm of the
  slot's highest point; sloping water mostly gets nothing (no wrong light).
- An infinite sea inside a dungeon (Sagremor Castle; PT-DETAILS "Caustics under water", risks): the tracer's quad for
  it covers the whole plane under the camera. In a dry room above the sea's height, the paths from a wall down to that
  plane and back up to a lamp go through the room's floor, which the shadow rays meet: no light. Only a hole in the
  floor over the sea's plane would let some through.
- Cost in a cave with many lamps and several water slots: the per-slot loop over the cell's lamps runs for most pixels
  above the water. Not measurable offline; read the composite stage in game.
- `LIGHT_NO_SPECULAR` lights (HDRP's "affects specular" off) are reflected too: the water's mirror is the water's
  reflection, not a highlight on the lit surface.
