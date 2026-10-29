# PT-46 and PT-37: what a ray into a mirror still missed (fog, water, the hero's head)

Agent report, 2026-10-07. Copy `wt/mirror2`, base `wt/base27`. Nothing ran in the game, nothing was deployed or
committed. The first agent stopped at a usage limit at 11:45; a second one finished it (from "In short" and the parts
marked "(finish)").

## In short

- **Built, and passes the offline test (`tgpt_gpu_test`, all checks).** Not seen in game. Nothing deployed.
- **Fog.** A ray into a mirror now reads HDRP's own lit volumetric fog where HDRP has it: inside the camera's view, in
  front of what the camera sees, within 128 m. Offline, a mirror in a known fog shows the wall as seen through that fog
  within 2 %. **Gap, not done:** fog outside the camera's view, behind what it sees, or past 128 m still gets the old
  stand-in (the height fog and the distant fog's color). In `mirror-day` the mirror shows what lies behind the camera,
  so most of that mirror's fog is still the stand-in. The real answer (the tracer lights the fog itself) is written down
  in section 1, not built.
- **Water.** A ray into a mirror, and bounced light, now meet HDRP's water: its reflection, what lies under it, and
  its body color, built as the camera's own water pixel is. Offline within 0.5 % (reflection, absorption) and 8 to 17 %
  low on the body color in the mirror (the mirror's own wall shades the water: explained below). **Gaps:** no waves,
  no foam, at most four waters, water behind glass is passed.
- **The hero's head (the "egg").** It was the inside of the back of the hero's head: the game's first-person body keeps
  an open shell there and never draws it from in front. Mirror rays now pass its back faces at any distance; offline the
  mirror shows nothing of it at 0.5, 1 and 3 m. **Gap:** bounced light's further rays and rays through glass
  (`trace_closest`) still meet that inside. Putting the same rule there hung the GPU (next point).
- **A GPU hang, found and worked around (finish).** The clean build hung the GPU on its very first frame, in the
  reflections pass. 27 experiment builds showed: it needs the first-person rule inside `trace_closest` plus enough of
  the other new code in the same pass; no single piece alone. It is no infinite loop of ours (every loop is bounded,
  the stage takes 0.2 to 0.3 ms when it runs) and not DXC's optimizer (-O1 hangs too): most likely the NVIDIA driver's
  shader compiler (RTX 5080, driver 32.0.16.1692). So `trace_closest` is back to the base's code and the gap above is
  named in its comment and in a source check.
- **Cost (offline, tiny scene):** the reflections stage 0.24 to 0.31 ms against 0.14 to 0.26 ms without this work. The
  game's cost is for the harness to measure.

## 1. Volumetric fog in reflections

State: built; passes its offline checks ("Offline checks"). Not seen in game. Real traced light where HDRP's buffer
covers the ray; a stand-in everywhere else (below: "Where the buffer ends"), which is a gap.

### How HDRP keeps and reads its lit fog (HDRP 17, Unity Graphics 6000.0; the game's own HDRP DLL read with ilspycmd)

- `HDRenderPipeline.VolumetricLightingPass` fills a 3D texture, `VBufferLighting` (R16G16B16A16_SFloat, a render-graph
  texture of the frame, `s_CurrentVolumetricBufferSize` large; the camera's own part is `vBufferParams[i].viewportSize`).
  One texel a "froxel": a screen tile (8 pixels at the default) times a depth slice. The slices are spread
  logarithmically over the distance from the camera: slice coordinate `e = x + y log2(distance - z)`
  (`_VBufferDistanceEncodingParams`), from the near plane to the Fog volume's Depth Extent (128 m in this game, 64 in
  the swamp).
- What a froxel holds (`VolumetricLighting.compute`, `FillVolumetricLightingBuffer`): the light the fog scatters towards
  the camera **summed from the camera through the end of that slice**, and the optical depth from the camera **to the
  slice's middle**; written as `LinearizeRGBD(light, depth)`, the light times the frame's exposure.
- It stores **nothing behind what the camera saw**: the loop stops at the depth buffer (`ray.geomDist`, `ray.maxDist`) and
  writes zeros for the slices behind.
- In this HDRP the pass runs late: after opaque lighting and the sky, right before `RenderOpaqueFog` (the game's DLL,
  `HDRenderPipeline.RenderGraph`: `VolumetricLightingPass`, then `RenderOpaqueFog`, then transparency).
- Its fog pass (`AtmosphericScattering.hlsl`, `EvaluateAtmosphericScattering`) reads it with `SampleVBuffer` at the
  pixel's screen position and distance, decodes it (`DelinearizeRGBA`: the light times opacity over optical depth; the
  opacity), and adds the "distant fog" past `_VBufferLastSliceDist` (the height fog and `GetFogColor`).

### What the tracer does with it

- A ray that does not start at the camera cannot use the sum from the camera. But the **difference** between a slice and
  the slice before it is what that one slice adds: over the transmittance to the slice's start it is the light that slice
  scatters, and the difference of the optical depths is its extinction. So HDRP's buffer gives, at any point it covers,
  the lit fog's light per metre and its density per metre: HDRP's own lighting of the fog (its sun and lamps through its
  shadow maps, the fill lights' fog, the ambient probe, local fog volumes' density), nothing invented.
- `fog_along` (pt_common.hlsli) marches the ray's way in 16 steps from a random start (the ray's own number), over the
  stretch that lies within the volumetric fog's reach of its camera. A step whose point the buffer covers takes its light
  and extinction (`fog_volume_at`); a step it does not cover, and the way outside that reach, takes the height fog and the
  distant fog's color, as before (`fog_height_stretch`). `fogged` (reflections, out of solid glass, the camera's water)
  and `fog_over` (a reflection in water seen in a mirror) use it.
- How the buffer gets there: D3D11 cannot share a 3D texture with D3D12. The plugin hooks `VolumetricLightingPass`
  (Harmony postfix) and records a pass right behind it that reads HDRP's buffer and hands its pointer over
  (`TGPT_SetFogVolume`, then render event 7). The event copies the buffer whole into a 3D texture of the game's device,
  and HDRP's exposure into a texel. The next traced frame lays the copy's slices side by side into a 2D atlas shared with
  D3D12 (`TGPT_FOG_VOLUME` in d3d11.hlsl: decoded as HDRP decodes it, over the exposure), one atlas per frame parity, in
  the same D3D11 work as the frame's other inputs. So the fog a ray reads is one frame old, with that frame's camera.
- With HDRP's volumetric fog off, or its hook missing, nothing changes: the height fog alone, as before.

### Where the buffer ends, and what stands in (precisely)

Each of these is a gap: there the ray gets the height fog's transmittance and the distant fog's color, the stand-in
that was there before this work, not the lit fog.

1. **Outside the camera's view.** A mirror mostly shows what the camera does not look at. In `mirror-day` the mirror
   stands 0.8 m ahead and faces the camera: its rays run back past the camera, and all of the fog it shows lies behind
   the camera. HDRP has no froxel there. The height fog and the distant fog's color stand in (`20261007-075250`: the far
   haze is in the mirror). **So this step alone does not put the lit, thick fog into the `mirror-day` picture.** It does
   where the mirror shows what the camera can also see: a mirror on a side wall, a puddle, a polished floor ahead.
2. **Behind what the camera saw**, inside its view: nothing stored. Height fog.
3. **Past the Depth Extent** (128 m): HDRP itself uses the distant fog there. Same here.
4. **The light's direction.** HDRP's light is what the fog scatters towards the camera. Along another direction the
   share of its anisotropy (0.31 in this game) differs: towards the sun a reflection's fog is as bright as the camera's
   view of that fog, not as bright as a view along the reflected ray would be.
5. **A frame late**: the fog's own camera is the frame before's. Standing still it is the same; turning fast, the edge of
   the view lags by a frame.

### The lasting answer for 1: the tracer's own lit fog (not built; what is missing)

What a ray needs outside the view is the fog's density and its light at any point, not only in HDRP's froxels. The
tracer can work the light out itself along the ray, one scattered sample a ray (pick a point on the way in proportion to
the fog's density and transmittance; light it; the denoiser and the frames average it). The pieces, and which exist:

| Piece | Have | Missing |
|---|---|---|
| The height fog's density at a point | `height_fog_depth` (the Fog volume's mean free path, base height, scale height) | local fog volumes (`LocalVolumetricFog`: boxes with their own density, color, 3D masks, blend modes): not sent by the plugin |
| The fog's albedo | in `fogColor` only as albedo times tint | the albedo alone (the Fog volume's `albedo`) |
| The sun's light at the point | `sun_fog`, a shadow ray (`transmittance`) | its Volumetric Dimmer and Shadow Dimmer; HDRP's phase function (Cornette-Shanks with the Fog volume's `anisotropy`) |
| The traced lamps' light at the point | the light grid and `light_incident` | each light's Volumetric Dimmer; a light pick at a point in the air (the grid's cells hold surfaces' lights) |
| The fill lights' fog (the owner's ruling: their fog stays) | nothing: fill lights are not in the tracer's list | a second list of the lights whose light is left out but whose fog is kept |
| The sky's part | `fog_sky` (HDRP's sky at its fog mips) | HDRP's volumetric ambient probe (the sky's light folded with the phase function) and `globalLightProbeDimmer` |
| The reach | - | where HDRP's volumetric fog ends for a view from the ray's start (its Depth Extent from there) |

It is the same work as PT-37's step 2 (the tracer lights the fog), done along rays instead of in HDRP's froxels, and it
needs the game to compare against: the yardstick is `mirror-day` (the game's view from the mirror's side is the
reference). Not started here: it cannot be checked offline against HDRP, and half of its inputs are new plugin work.

## 2. Water in reflections

State: built; passes its offline checks ("Offline checks"). Not seen in game.

### What was wrong

- HDRP's water is in the traced scene as flat quads or meshes of their own kind (`TGPT_INSTANCE_WATER`, instance mask
  0x08), and only two things asked for them: the caustics' query and the camera's water pixels. Every other ray's mask
  (`MASK_ALL`, 0xF7) leaves water out. A mirror facing a lake showed the lake's bed, dry; light bounced off a lake as
  off dry ground. And only water **with caustics on** was in the scene at all.

### What a ray does now

- `water_crossing` (pt_common.hlsli): where a ray first crosses, from above, a water whose look the plugin sent. A cheap
  test first (the ray's heights against the waters' highest and lowest points), then a query for water alone. From below
  it is passed, as before: HDRP's absorption over twice the way already stands for the light's way down.
- The look, put together as the camera's water pixel is (`docs/agents/water.md`, d3d11.hlsl `TGPT_WATER_SHADE`):
  - its **reflection** by the Fresnel term (HDRP's 0.0204 for an index of 1.333), drawn from the water's GGX lobe;
  - **what lies under it**, along the ray bent into the water (Snell), dimmed by `exp(-2 x absorption x way)`, the bed's
    direct light with HDRP's caustic pattern;
  - its **body color**: HDRP's scattering color for still water (`EvaluateScatteringColor`: the surface's scattering color
    times 0.25 x its ambient scattering, times what the water absorbs over the way), lit as a white diffuse surface at the
    water's place is (its direct light, and one ray that gathers the rest);
  - the sun's and the lamps' highlights on it.
- **A reflection** (pt_specular's loop over the water's rays, `water_leg` and `water_arrive`): both rays, the reflection and the one into the water, with no chance
  in it. What each meets is lit as the camera's water does it (a path with the Bounces setting's bounced light).
- **A path's ray** (`water_pick` in a loop of two traces: bounced light's first ray in pt_gi, a path's further rays and the radiance cache's
  through `trace_bounce`, a smooth metal's own reflection in `shade_seen`): one of the two by chance (the Fresnel term's
  chance, kept between a tenth and nine tenths), each over its chance, and the path goes on from there. No fixed depth:
  every further ray of the path checks for water again; the only end is the path's own (Russian roulette, the radiance
  cache).
- Shadow rays still pass water (their light takes HDRP's caustic pattern, as before).
- The plugin (`WaterCaustics.cs`): every water surface near the camera takes one of the four slots now, with or without
  caustics, and its look goes over each frame it changes (`TGPT_SetWaterLook`): absorption (as `WaterShade` works it out),
  roughness (1 - `startSmoothness`), scattering color times the ambient term, highest and lowest point.

### What a ray does not get (written down, not built)

- **Waves.** The traced water is HDRP's flat quad or its mesh, not its displaced, animated surface. A mirror shows a flat
  sea where the camera sees waves. (The camera's own water pixels use HDRP's depth and normals, so they have the waves.)
- **Foam** (per pixel, from HDRP's simulation), HDRP's smoothness fade with the distance, its wave-tip scattering.
- **Water seen through water or in water**: the two rays a reflection sends on from the water (pt_specular's loop) do not
  themselves meet water again (their own further bounces do). A waterfall reflected in a lake reflected in a mirror shows
  the rock behind the fall. The camera's own water reflections (pt_water) do not show other water either.
- **Seen through glass**: what lies behind see-through glass is still found without water.
- **More than four waters** near the camera: the nearest four are met, the rest are passed (two bits of slot).

## 3. The hero's head in the day mirror

State: built; passes its offline checks. Not seen in game. Mirror rays and bounced light's first ray pass the shell's
inside; bounced light's further rays and rays through glass still meet it (a gap: "The rule now").

### What the picture shows

- Run `20261007-100106`, `mirror-day/3-mirror-traced-rr/frame.jpg` and `mirror-tavern/...`: the hero has a head-shaped thing
  in **both** mirrors, not only by day. By day it is bright orange and large (the mirror stands 0.8 m ahead); in the tavern
  it is the same shape, half as large on screen and dark (the mirror stands 1.6 m ahead). It is not gone in the tavern.
- Its shape changed when the 0.3 m rule came in. Before (`20261007-023808`): a whole egg. Since (`051746`, `075250`,
  `084918`, `100106`): a bowling pin, a bulb on top, a narrow waist exactly at the camera's height, a neck below. That is
  what a ball of 0.3 m around the camera leaves of a thing that sits behind the camera: the rule took the middle and left
  the rim.
- Measured in the two frames (the bulb 77 px wide and its top 72 px over the picture's centre by day; 42 and 37 px in the
  tavern; 960 px to a metre at 1 m): a bulb 13 to 16 cm wide whose top is 12 to 14 cm above the camera, 10 to 30 cm behind it.

### What the egg is (from the game's own files, nothing run)

- `VHeroController` (TG.Main.dll, ilspycmd): in first person the camera follows `BodyData.cinemachineHeadTarget`, and the
  body hangs under `fppParent`, which has a z offset of its own (`_defaultFPPHeadZPosition`).
- Kandra keeps its meshes in `StreamingAssets/Kandra/kandra.arch` (a UnityFS file, stored uncompressed): `<name>.mdkandra`
  (20 B a vertex: position and packed normal and tangent; 8 B: packed uv; 12 B: four bone indices and weights; then the
  bind poses) and `<name>.ixkandra` (16-bit indices). No vertex colours: the game cannot hide a part of a mesh by them.
- The one cut-out human body in it: `Mesh_BodyCutout_Mesh_BaseHuman_Body` (5479 vertices, 9406 triangles, 51 bones). It
  is the two hands and forearms, and one piece from the collarbones up (1770 triangles): a ring over the shoulders, the
  neck, and a head. That this is `Fpp_Body`'s mesh is read from its shape, not from the prefab: it fits the picture.
- The head is not an egg. It is **the back of a head**: at eye height (z 1.63 to 1.69 m) its vertices span 15 cm across and
  only 6 cm front to back (y 0.064 to 0.129; a whole head is 20 cm deep). The face and the front of the neck are cut out,
  so that the camera, which sits where the eyes would be, looks out through the hole. Its open edge (120 edges that belong
  to one triangle only) runs from the forehead down both temples to the collar.
- All 730 triangles of the head face outwards (their winding's normal against the direction from the head's axis: 0.97 on
  average, every one positive): the shell's front is the back of the head; from in front one looks at its inside.
- The game's skin (`TG/Character/RealSkin`) is drawn one-sided. So from in front (the camera's side, a mirror's side) HDRP
  draws nothing of the shell: the game's view from the mirror's side (`2-mirror-reference`) shows the inside of the
  armour's back through the collar and nothing above it. From behind HDRP would draw a bald back of a head.
- The tracer met the shell's inside and shaded it as a surface (a ray takes a back face as a surface: meshes are mostly
  closed): the "faceless egg" was the inside of the back of the skull.
- Why 0.3 m failed, for any mirror: the distance to the mirror plays no part. The shell lies 20 to 35 cm behind the
  camera; a ball of 30 cm around the camera holds its middle only. Offline the old check used a box 30 cm across around
  the camera, wholly inside the ball: it could not see this.

### The rule now

- A ray traced by `trace_from_surface` (reflections, a reflection in a reflection, what is seen through water, bounced
  light's first ray) passes the **back faces** of a `TGPT_MATERIAL_FIRST_PERSON` material, at any distance:
  `first_person_inside`. That is what HDRP does with a one-sided material, so the mirror shows what the game's own view
  from there shows. No distance, no camera position in the rule.
- **Gap (finish):** a ray traced by `trace_closest` (bounced light's further rays, the radiance cache's rays, what is seen
  through glass, the camera's forward pixels) still meets that inside as a surface. The first agent had the rule there
  too, as a second query from behind what was met; that build hung the GPU (see "The GPU hang"), so `trace_closest` is
  the base's again. What it costs the picture: a little bounced light off a shell the game never draws, near the camera;
  a mirror seen through glass would show it. The lasting fix, not built: make the first-person body's instances
  non-opaque (scene.cpp sets `FORCE_NON_OPAQUE` for them) and leave its back faces uncommitted in the query's candidate
  loop, with no second query. Check first that the queries that look at non-opaque geometry alone (`RAY_FLAG_CULL_OPAQUE`:
  the thin-cloth walk and the glass caustics) ignore it.
- Its front faces are met as before: a hand, a forearm, the back of the head seen from behind.
- A first-person material the game draws two-sided (`_DoubleSidedEnable`) is met from both sides, as HDRP draws it.
- Shadow rays meet both sides, as before: the hero's shadow keeps its head.
- `TGPT_FIRST_PERSON_HEAD`, `g_reflectionRay` and `first_person_head` are gone. The plugin's log line now says whether the
  body's skin is one-sided ("rays pass its back faces") or two-sided.

### Found on the way, not changed (outside this job)

- `CharacterCapture.Find` keys a character's triangle list by the mesh and `RenderingMesh.IndexStart(0)`. Kandra's
  `KandraTrisCullee.UpdateCulledMesh` frees the culled index range and takes a new one each time a piece of clothing is put
  on or taken off (`MeshBroker.ReleaseCullableMesh`, then `CreateCullableMesh`); the new range can start where the old one
  did, with other triangles in it. Then the key is the same and the tracer keeps the old triangles: skin the game has since
  hidden under clothing stays traced (or the other way round). Not seen in a picture; a fix would add the index count, or
  the culled mesh's submesh table, to the key.
- The general case of this rule: a mirror ray meets the back face of **any** one-sided material as a surface, where HDRP
  from there would draw nothing. For closed meshes that never shows. For open ones (a cloak's inside, a roof seen from
  under, terrain from below) the mirror can show a face the game would not. Not changed: it needs its own look in game.

## The GPU hang (finish)

What the first agent left: its last clean build hung the GPU on the test's first frame. The native log: "the path
tracer's D3D12 device hung (its own work took too long) 0x887A0006 ... finished stage hair and eyes of frame 0, and
stopped in stage reflections" (Windows logged nvlddmkm event 153 each time). Its runs `w4` and `w5` were the clean build;
`xA` was an experiment (the reflection loop cut to one stage), which hung too. The "FAIL: frame submitted" lines are
what every check prints once tracing is off for the rest of the run. Its last good run (`egg`, 10:51) had the
first-person rule only.

Experiments (each built into its own prefix, quick-tested on the GPU; `scratchpad/m2tmp/mutate.py`, `swap.py`,
`swap2.py`, `noloop.py`): the five shared functions this work changed are `fogged`, `hit_material`, `trace_closest`,
`trace_bounce` and `shade_seen`.

| Build | Hangs |
|---|---|
| any one of the five changes alone (the other four the base's), each | no (full runs) |
| all five the base's | no (but its test program ended without a word or an exit code 37 s in, at the 600 lights check; no GPU reset logged; not seen in any other run) |
| all new but the fog (`fogged` off), or but `shade_seen`, or but `hit_material`, or but the water in `trace_bounce`, or in `shade_seen` | yes |
| all new, the reflections pass the base's (`pt_specular.hlsl`) | yes |
| all new, the shaders the base's (the new native code) | no |
| new `trace_closest` with new `hit_material` (the rest the base's) | yes |
| new `hit_material` with new `trace_bounce`, or `shade_seen`, or `fogged` | no |
| all new, the first-person rule switched off inside `first_person_inside` | yes |
| all new, `trace_closest`'s loop: run once, unrolled, its query declared once before it, its `[loop]` hints removed | yes, each |
| all new, the reflections pass compiled with DXC -O1 | yes |
| all new, no cache sample from a reflection's hit; no reflection in a reflection | yes, each |
| **all new, `trace_closest` the base's** | **no** (and the full test passes) |

Read together: every build that hung had the new `trace_closest` with enough of the rest; the base `trace_closest`
with everything else runs. Every loop in the code is bounded, and the stage takes 0.2 to 0.3 ms in the builds that run,
so this is no slow pass and no endless loop of ours; -O1 hangs too, so not DXC's optimizer. The likeliest cause is the
NVIDIA driver's compiler for this pass (RTX 5080, driver 32.0.16.1692): DXIL sizes say nothing (a hanging build at
81,200 lines, a running one at 82,100). Not proven: no driver debug layer, no DRED (the tracer does not turn on
Device Removed Extended Data; worth adding, it would say whether it is a page fault).

## Files

| File | What |
|---|---|
| `native/pathtracing/include/tgpt_api.h` | `TGPT_WaterLook`, `TGPT_SetWaterLook`; `TGPT_FogVolume`, `TGPT_SetFogVolume`, `TGPT_EVENT_FOG_VOLUME` 7, `TGPT_FOG_VOLUME_FRAMES`; the first-person flag's text; `TGPT_FIRST_PERSON_HEAD` gone |
| `native/pathtracing/shaders/pt_common.hlsli` | `first_person_inside` (in `trace_from_surface`; `trace_closest` is the base's, with a comment on the gap); `fog_volume_slice`, `fog_volume_distance`, `fog_volume_at`, `fog_height_stretch`, `fog_along`, `fogged`, `fog_over`; `WaterCrossing`, `water_crossing`, `water_pick`, `water_leg`, `water_arrive`, `g_waterBody` in `hit_material`; `trace_bounce` (its ray turns on water; a new signature); `shade_seen` (the water's body, its own reflection ray); `FrameConstants` |
| `native/pathtracing/shaders/pt_specular.hlsl` | the reflection ray and the water's rays in one loop; the sun's disc off water; `fog_over` |
| `native/pathtracing/shaders/pt_gi.hlsl` | bounced light's first ray turns on water |
| `native/pathtracing/shaders/d3d11.hlsl` | `TGPT_FOG_VOLUME` |
| `native/pathtracing/src/tracer.cpp` | `FrameConstants` (6464 B); `fog_volume_event`, `copy_fog_volume`, the atlas per parity and its barriers (trace and water pass); the water looks' rows |
| `native/pathtracing/src/extension.cpp`, `runtime.hpp` | the two exports and their state |
| `src/TGPathTracing/HdrpPasses.cs` | `FogVolumePass`, `AfterVolumetricLighting`, `FillFogVolume`, `FogVolume`, `FogVolumeText` |
| `src/TGPathTracing/WaterCaustics.cs` | every water takes a slot; `SendLooks` |
| `src/TGPathTracing/NativeTracer.cs`, `DebugCapture.cs`, `SceneCapture.cs` | the structs and bindings; the F8 report's fog line; the first-person log line |
| `tests/pathtracing_gpu_test.cpp` | the first-person sheet at 0.5, 1 and 3 m; the mirror in a known volumetric fog (three fogs, and none); the mirror facing water (its steep-angle group fixed in the finish: it was empty); bounced light off water |
| `tests/source_checks.py` | `test_path_tracing_mirror_shows_fog_and_water`; the first-person rule (not in `trace_closest`, and why); pins moved with the code |
| `CHANGELOG.md`, `docs/PT-REQUESTS.md` (PT-46, PT-37), `docs/agents/mirror-2.md` | the entry; the notes; this file |

## ABI and bits

The ABI number is left at 0x00011C00 here. Main is at 0x00011D00 since the limits job's merge (12:50): the coordinator
sets the next number and resolves the conflicts. No material bit, no option bit, no instance bit is taken.

- **Added**: `TGPT_SetWaterLook` and `TGPT_WaterLook` (48 B; managed `NativeWaterLook`); `TGPT_SetFogVolume` and
  `TGPT_FogVolume` (136 B; managed `NativeFogVolume`); render event `TGPT_EVENT_FOG_VOLUME` 7; `TGPT_FOG_VOLUME_FRAMES` 4.
  The plugin binds both exports by name: a plugin of this copy needs a DLL of this copy (the ABI number guards that
  once the merge raises it).
- **Removed**: the constant `TGPT_FIRST_PERSON_HEAD` (a `#define`, never part of a struct). `TGPT_MATERIAL_FIRST_PERSON`
  (1048576) stays, with a new meaning: its back faces are passed, at any distance.
- **`FrameConstants`** (shader constants, not the plugin's ABI): 6160 to 6464 B. New at its end: `waterLook[12]`,
  `fogVolumeViewProj`, `fogVolumeCamera`, `fogVolumeEncode`, `fogVolumeAtlas`.
- `TGPT_Frame`, `TGPT_Status`, `TGPT_WaterCaustics`, `TGPT_WaterShade`: unchanged.
- Merge notes: `trace_bounce` is now `(origin, inout dir, tmax, inout rng, out hit, out weight)` (it may turn the ray:
  callers use `dir` after it); pt_specular's reflection is a loop with one `trace_from_surface` and one `shade_seen`
  (other agents' edits to that block go inside the loop: `seen` is the ray's direction there, `light` its light, `first`
  the reflection ray's own hit); pt_gi's first ray is a loop of at most two traces; `fogged` is built on `fog_along`;
  `hit_material` answers a hit with instance `INVALID` from `g_waterBody`; `trace_closest` is byte for byte the base's
  (if another merge changed it, keep theirs, but add no second query to it before the hang is understood).
- How to merge: take this copy's files over a base snapshot of `wt/base27` (a three-way merge per file against main).
  Conflicts are likely in `pt_common.hlsli` (`FrameConstants` grows at its end: keep both sides' rows and fix the size
  pins in `tracer.cpp` and `source_checks.py`), `tracer.cpp` (the `FrameConstants` struct, the trace's read list),
  `tgpt_api.h` (the ABI number), `CHANGELOG.md` (the entry goes on top). Then build, `tgpt_gpu_test` (watch the
  reflections stage: if it hangs on the first frame again, the driver fault came back with the merged code; see "The
  GPU hang"), `python tests/source_checks.py`, and the plugin.

## Offline checks and mutations

The final build (`%TEMP%\tgb\mirror2-inst`, run `mirror2-test-clean2.txt`): `tgpt_gpu_test` **1153 checks, all pass**,
exit 0. `python tests/source_checks.py`: 88 tests, all pass. The plugin compiles (`dotnet build src/TGPathTracing -c
Release`, into a scratch folder).

What the new checks measured (no denoiser; the reference worked out in the test, not taken from the tracer):

| Check | Measured | Passes when |
|---|---|---|
| A perfect mirror shows a red wall in HDRP's volumetric fog, a fog everywhere in its buffer | 0.985, 0.983, 0.986 of the wall seen directly through that fog (r, g, b); the fog's blue light 1.7 times the wall's own | within 5 %, and the fog shows |
| ... a fog only within 2.56 m of its camera (HDRP stores nothing behind what it saw) | 0.984, 0.980, 0.981 | within 5 % |
| ... a fog in the lower half of its camera's view | 0.989, 0.980, 0.977 | within 5 % |
| No volumetric fog named any more | 1.0000 of the mirror without it | within 2 % |
| The inside of the first-person body behind the camera, mirror 0.5, 1, 3 m ahead | off by 0.004, 0.005, 0.011 from no body | under 0.03 |
| ... another material there; the body from in front; the body drawn two-sided | off by 0.65 to 0.69, each | over 0.2 |
| A mirror facing opaque water shows the sky by the water's Fresnel term | 0.999, 0.999, 1.000; 0.999 where it reflects over a quarter (81,944 px), 0.999 under 15 % (16,336 px) | within 4 % and 5 % |
| The bed through clear water | its green 8.7 times its red, and more than the sky's reflection there | green over 3 times red, over 0.2 of the reflection |
| The bed through absorbing water, against twice the bent way's absorption | 0.997, 0.997, 1.000 (against the straight way: 4.06, 1.83, 1.32) | within 5 % |
| No look sent: the water is passed, as before | with the water known the mirror's red is 4.5 times and its blue 2.7 times the dry bed's | dry under 0.7 of it |
| The water's body color in the mirror, over the color times the open sky's light | 0.829, 0.922, 0.916 (the mirror's wall hides part of the low sky from the water before it, more of the redder horizon) | mean 0.5 to 1.05, each channel within 8 % of it |
| Bounced light off opaque water under a white sheet | 1.002, 0.999, 0.995 of the sky's Fresnel-weighted reflection | within 8 % |
| ... with a body color | 1.012, 0.960, 0.955 | 0.88 to 1.05 |

Fixed in the finish: the water test's group of steep angles ("under 8 %") was empty in its view (the water's Fresnel term
there runs from about 0.11 to 0.6), so its check compared 0 with 0 and failed; it is "under 15 %" now, and each group
must hold 2000 pixels.

Mutations (each set breaks one thing per check family, built and run whole; `scratchpad/m2tmp/mutate.py`):

| Set | What it breaks | Wanted failure | Result |
|---|---|---|---|
| mA | the first-person body's inside met again; a constant Fresnel term for water; the fog's light not divided by HDRP's exposure | the body check; the water's reflection check; the three fog checks | **all caught** (8 checks failed: those, plus the water checks that build on the reflection) |
| mB | the body passed from in front too; no bending into the water; the buffer's sum from the camera taken for one slice | the "from in front shows" check; the absorption over the bent way; the fog checks | **all caught** (exactly those 5 checks failed) |
| mC | rays pass the water again; slices that hold nothing read as fog | the water checks; the "only as far as its buffer holds it" check | **not run** (stopped at the coordinator's word) |
| mD | the absorption counted once; bounced light's water ray not over its chance; a fog no longer named kept | the absorption check; the bounced-light check; "without HDRP's volumetric fog a mirror is as before" | **not run** |
| mE | the water's body unlit; the buffer not decoded as HDRP decodes it | the two body-color checks; the fog checks | **not run** |

So not mutation-checked: the water's body color checks (both), the bounced-light check's weighting by the pick's chance
(the check did fail under mA's constant Fresnel term, so the Fresnel term in bounced light is checked), the "only as
far as its buffer holds it" fog check against reading empty slices, the "no fog named" check, and the "with no look
sent, water is passed" checks (they failed under mA, but only as a side effect of the reflection's break). Weak by design: the body color check allows the
color's overall level anywhere from 0.5 to 1.05 of the open sky's light (only its hue is held to 8 %); the first-person
check looks at the middle of the mirror image only (16 to 82 pixels square).

## GPU cost

Offline only (`tgpt_gpu_test`'s own small room at 1280x720, other programs may share the GPU): the reflections stage
0.24 ms in the final run, 0.31 ms in the run before; 0.14 ms in the first agent's `egg` run (the first-person rule
only) and 0.26 ms with the base's shaders. So roughly 0.1 ms more in a room with no water and no fog volume, where the
new code only checks and passes. The fog's own work per frame: a copy of HDRP's buffer (128x64x64, 4 MB at half
floats) on the game's device, one small pass that lays its slices into a 2D atlas, and 16 steps of the buffer read per
reflected ray inside the fog's reach. Water: one extra query per reflected or bounced ray whose heights cross a water's
heights, and on water up to three more traced rays for a mirror's ray, one for a path's. The game's numbers are the
harness's to measure.

## What the next harness runs should show

Recipe `mirror`, scenarios `mirror-tavern` and `mirror-day` (the coordinator runs them):

- **No GPU hang.** The first frames trace (the reflections stage was where it hung).
- **The head.** No head-shaped thing on the hero in either mirror: by day the bright orange bulb and its neck (77 px wide
  at the picture's centre, 72 px above it) and in the tavern the dark one (42 px) are gone; the armour's collar and the
  back of the armour stay, as in the game's view from the mirror's side (`2-mirror-reference`).
- **Fog, day.** Little change by day: that mirror shows what lies behind the camera, outside HDRP's buffer, so its fog
  is still the stand-in. A change there is not expected; none would not be a failure. The F8 report gets a line for
  HDRP's volumetric fog (its buffer's size and reach) and the native log "Volumetric fog: HDRP's lighting buffer ...":
  if that line is missing, the hook did not run (`HdrpPasses.cs` `AfterVolumetricLighting`).
- **Fog, tavern.** Indoors with volumetric fog on, the parts of the mirror that show what the camera also sees may get
  a little haze; it should look like the game's own view of that place.
- **Water.** Neither mirror scenario has water in view. A scenario with a mirror over water, or a puddle, lake or river
  seen in a glossy reflection, would show it. Bounced light near water changes: a bank by a lake no longer takes the
  lake's bed for dry ground (a little bluer, darker light where the bed was green or brown).
- **Exposure.** Nothing here removes light on purpose; read the exposure and gain line anyway.

## Risks and what is left

- **The hang is worked around, not understood.** The reflections pass sits close to a driver fault: another change to
  `trace_closest`, or more code in that pass, may bring it back on this card and driver, and other cards were never
  tried. Every merge into this pass needs a `tgpt_gpu_test` run before anything else. DRED in the tracer would say more
  next time.
- **Gaps (stand-ins, not done):** HDRP's fog outside the camera's view, behind what it saw, past 128 m, and along a
  direction other than the camera's (section 1); the first-person body's inside for `trace_closest`'s rays (section 3);
  water's waves, foam, more than four waters, water seen through glass or through water (section 2).
- **Fog a frame late.** The fog a ray reads is the frame before's, with that frame's camera.
- **Not run in the game at all.** The Harmony postfix on `VolumetricLightingPass` and the render event 7 copy were
  checked offline against a buffer laid out as HDRP's; HDRP's real one (its size, format, exposure) is only described
  by the decompiled code.
- **Found, not changed:** `CharacterCapture.Find`'s key (section 3), and any one-sided open mesh seen from behind in a
  mirror (section 3).

Ideas, not started: the tracer lights the fog itself along its rays (section 1's table: PT-37's step 2); the
candidate-loop rule for the first-person body (section 3); DRED in the tracer; HDRP's water displacement for traced
water (its waves in a mirror).
