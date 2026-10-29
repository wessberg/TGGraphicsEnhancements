# Grass and plants under the tracer: why they look wrong, and what to change

Written 2026-10-06 by a read-only investigation (a subagent of round 6). I built, ran and deployed nothing.
The edits below are proposals. None of them has been compiled by me.

The owner's words: "Seems like the grass shading can still look a bit weird. Some dark patches, other white overblown
ones. Might need an overhaul." and "Definitely make grass/vegetation more 'PBR'/play-nicer-with-PT."

This version (16:45) replaces the first draft of 15:52. Read "What changed since the first draft" if you acted on
that one: one of its claims is withdrawn.

What was used:
- The harness runs `artifacts/harness/20261006-140217` (npc-day) and `20261006-145526` (the baseline, 20 scenarios).
- The owner's F8 captures of 2026-09-27 in the game folder: `20260927-224018` (grass, clear weather) and
  `20260927-225604` (the same kind of grass, rain).
- The game's own files, read offline: its vegetation settings, its diffusion profiles, its 128 grass and leaf
  materials and its cloud settings (decoded from the asset bundles), its grass and wetness shaders, and its code
  (TG.Main, decompiled into a scratch folder; nothing of it is in the repo).
- HDRP's public source (branch 6000.0/staging) and the HDRP assembly the game ships.

Crops are in `artifacts/agents/vegetation/`. The scripts that decoded the game's files, with their output, are in
`artifacts/agents/vegetation/tools/` (untracked, like all of `artifacts/`).

Line numbers are those of the working tree at 16:40. The tree was being edited while this was written, so every
citation also names the function or quotes the line.

Numbers called "on screen" are the picture's brightness with the sRGB curve undone, 0 to 1.

## The short answer

1. Grass and leaves shine 18 to 35 times too much. The game sets how much a leaf mirrors in its "diffusion profile"
   (HDRP's settings for light inside a material). For most grass and leaves that is 0.2 %. The tracer assumes 4 %,
   and Reflection Boost made it 8 %. In rain the game also makes every surface under the sky glossy. Each wet blade
   then mirrors the sky and the sun. These are the white streaks. Confirmed in the game's data and in the code.
2. Traced grass casts hard sun shadows. In the game, grass and plants cast no shadows at all: its vegetation
   settings turn them off at every quality level. Traced, every tuft throws a shadow on the ground and on the grass
   behind it. These are most of the dark patches. Confirmed in the game's data and in the code.
3. The traced sun was 2 to 5 times too strong. HDRP dims the sun by the fog above each surface. The tracer did not.
   So sunlit parts were too bright and shadows too deep, on everything. Confirmed in HDRP's code and by a capture.
   The fix is in the working tree since 16:35. In rain the game's sun looks weaker still. I did not find why.
4. Light through leaves is grey and weak, and the sky's light does not pass through them at all. The game lets
   1.4 to 6 times the light through a tree's leaf, tinted yellow-green. The tracer lets half through, grey, and only
   for the sun and lamps. Trees come out dull and grey. Confirmed in the game's data and in the code.
5. Smaller things: the material's own occlusion is thrown away, the tracer's "which way does this face" is wrong on
   thin blades, and what a bounce sees of grass is too bright.

The pictures that show it: `npc-day-grass-right.jpg` and `npc-day-grass-mid.jpg` (the game's own, traced, traced with
Reflection Boost 0), and `f8-20260927-225604-rain-grass.jpg` (the game's own, traced, the tracer's Direct view, its
Specular view).

## What changed since the first draft

- Withdrawn: the clouds' shadow on the sun (the draft's Change 1b, and BACKLOG's PT-36). The game's files say the
  clouds cast no shadow (below). Do not build it. One log line would confirm it in game ("What to capture", 1).
- In the working tree since 16:35, not installed, not seen in game: the fog on the sun (the draft's Change 1a:
  `directional_fog` and `sun_fog`, `pt_common.hlsli:540-547`) and Reflection Boost leaving grass and leaves alone
  (the draft's Change 2: `pt_prepare.hlsl:62-66`, `d3d11.hlsl:762-763`). I read both: they match HDRP and the draft.
- New, from the game's files: the leaf's reflectance (cause 1), the grass that casts no shadow (cause 2), and the
  leaf's light-through by its profile (cause 4). They are Changes 1, 2 and 3 below and come before the draft's rest.
- The draft's Change 3 (occlusion) is Change 4 now, its Change 4 (normal) is Change 5, its Change 5 (the sky from
  behind) is part of Change 3.

## The questions asked, answered

- Which normal, roughness, reflectance and colour does the tracer use on a blade? HDRP's own from the G-buffer,
  except the reflectance: 0.04 where HDRP uses the profile's 0.0023. The table is below.
- Do Reflection Boost and "All Surfaces" put mirror-like sky reflections on grass and wet rock? Yes (cause 1).
- Are grass cards double-sided with flipped normals, and is that the dark patches? Of the 128 materials, 114 turn
  the normal to the camera on back faces and 14 do not. That is a small part (cause 5). The dark patches are
  shadows (cause 2).
- Do shadow rays shadow the grass by itself? Not within 20 cm (the sway rule). Beyond 20 cm every blade shadows
  every other, hard, which the game never does (cause 2).
- How do alpha tiles treat thin blades? Well: 256x256 coverage, stopped by chance.
- Is transmission applied twice or not at all? Once, grey, at half strength, for the sun and lamps. Not at all for
  the sky's and bounced light. And a blade that is lit through still blocks the light whole for what is behind it.

## What the game's own files say

All of this was read from the files, not from tables of mine.

Which plants cast shadows (`CampaignMapVegetationSettings`, the same in all four campaign maps; `vegsettings.py`).
The game's vegetation renderer, Leshy, gives every plant of a kind this setting (`LeshyPrefabs.PrefabRuntime`:
`filterSettings.shadowCastingMode = qualityManager.Shadows(prefabType)`):

| quality | grass | plants | objects | large objects | trees | ivy |
|---|---|---|---|---|---|---|
| low | off | off | off | off | on | off |
| medium | off | off | off | on | on | off |
| high | off | off | on | on | on | off |
| ultra | off | off | on | on | on | off |

The diffusion profiles that vegetation uses (`vegdata.py`; the formulas are HDRP's `DiffusionProfileSettings.UpdateCache`
and `ComputeTransmittanceDisney`). "Mirror reflectance" is what HDRP calls Fresnel0. "Light let through" is the
transmittance HDRP multiplies the light from behind with: red, green, blue.

| profile | used by | index of refraction | mirror reflectance | light let through, thin | light let through, thick |
|---|---|---|---|---|---|
| DiffusionProfile_Foliage | 88 of the 128 materials | 1.1 | 0.0023 | (5.3, 6.0, 1.2) | (1.4, 1.7, 0.2) at 24 mm |
| DiffusionProfile_Foliage_Grass | 23 | 1.836 | 0.087 | (0.66, 0.42, 0.14) | (0.20, 0.14, 0.01) at 4 mm |
| DiffusionProfile_Foliage_LastTree | 3 | 1.1 | 0.0023 | (5.9, 6.0, 5.2) | (1.6, 1.7, 1.0) at 24 mm |
| DiffusionProfile_Snow | 6 (snowy plants) | 1.3 | 0.017 | (1.8, 2.0, 2.1) | (1.1, 1.2, 1.6) at 32 mm |

The 128 grass and leaf materials (those with `_HealthyColor`; `vegdata.txt` lists each one):
- Normals on back faces: Mirror on 89, Flip on 25, None on 14. With Mirror and Flip the normal faces the camera. With
  None it keeps pointing away (ferns, the field grass, the dwarf pine, the Last Tree's leaves, and others).
- A mask map is set on 125. Three have none.
- `_Thickness` is 1 on 64 and 0 on 29.
- The grass under the owner's crosshair on 2026-09-27, "Mat_Vegetation_Grass_Meadow_01_SG 1": profile
  DiffusionProfile_Foliage, normals Mirror, thickness 1, `_HealthyColor` 0.43 grey, `_DryColor` 0.89 grey,
  `_GroundBaseColor` (0.32, 0.28, 0.28).

The clouds (`cloudlayer3.py`). HDRP can lay the clouds' shadow over the sun as a cookie. In the game's day and night
profiles (Horns of the South, Cuanacht, Forlorn, the prologue beach) the second cloud layer has Cast Shadows ticked,
but its override switch is off, so HDRP does not use it. The first layer has it off. No code in TG.Main touches it.
The harness's audit agrees with the files on the switches it can see. So by the files, clouds cast no shadow.

What rain changes (`PrecipitationController`, `DayNightSystem.HandleCloudLayer`, `ScreenSpaceWetness`): the clouds'
look, a fog of its own around the hero, the rain particles, and the wetness pass (see cause 1). The sun light's
intensity never changes with the weather.

Which baseline scenes had rain (the rain volume's weight in `1-game/audit-a.txt`): npc-day 0.52, forest 0.92, metal
0.68, campfire 0.67. Dry: water-day, vista, doorway, night.

## What a grass blade is, in HDRP and in the tracer

At the pixel the camera sees (HDRP's side is from the game's compiled `TG/Vegetation/Vegetation_Grass` GBuffer pass and
HDRP's `Lit.hlsl`):

| | HDRP | the tracer |
|---|---|---|
| colour | base map x a mix of `_DryColor` and `_HealthyColor` by a noise over the world, blended to `_GroundBaseColor` near the root, lightened in wind gusts | the same pixel (`d3d11.hlsl:649`, `:657`) |
| normal | the mesh's with its normal map; on back faces by the material's mode (above) | the same (`pt_prepare.hlsl:49`) |
| "which way it faces" for rays | not needed | from the depth of neighbouring pixels (`pt_prepare.hlsl:69-90`): wrong on thin blades |
| roughness | 1 minus the mask map's alpha: 0.97 dry (the F8 crosshair). In rain the wetness pass lowers it | the same |
| mirror reflectance | the profile's: 0.0023 for most (`Lit.hlsl`, `FillMaterialTransmission`) | 0.04 (`pt_prepare.hlsl:54`); with Reflection Boost 0.079 until 16:35 |
| specular occlusion | GBuffer2 red, from the mask map's green | thrown away |
| ambient occlusion | the mask map's green; while tracing HDRP writes it into GBuffer3 green behind a tag | thrown away (`d3d11.hlsl:654-656` reads the tag as "no emission") |
| light through the blade | the profile's, coloured, by the thickness in GBuffer2 green; for the sun, lamps and the sky's light | half, grey (`pt_common.hlsli:312-317`); the sun and lamps only |
| casts a shadow | no | yes, from 20 cm on, within 20 m (`pt_common.hlsli:1046-1061`, `LeshyCapture.cs:19`) |

## The causes, with the evidence

### 1. Grass and leaves shine far too much (confirmed)

- The reflectance. HDRP takes a transmission material's mirror reflectance from its diffusion profile
  (`bsdfData.fresnel0 = _TransmissionTintsAndFresnel0[profile].a`). For DiffusionProfile_Foliage that is 0.0023: the
  artists set its index of refraction to 1.1, which all but turns the shine off. The tracer uses 0.04 for every
  material that is not HDRP's standard one (`pt_prepare.hlsl:54`). That is 18 times too much.
- Reflection Boost made 0.04 into 0.079 at 0.1, 35 times the game's, and roughness 0.97 into 0.88. That part is
  fixed in the tree (`pt_prepare.hlsl:66`).
- The 23 materials on DiffusionProfile_Foliage_Grass are the other way round: the game gives them 0.087, more than
  the tracer's 0.04.
- Nothing hides the reflection. Both of HDRP's occlusion values are dropped: the albedo copy overwrites GBuffer0's alpha
  with a stencil bit (`d3d11.hlsl:44-47`), `pt_prepare.hlsl` reads GBuffer2 only for the feature and skin, and no
  tracer pass reads GBuffer3 (`g.inputs0.w` is set at `tracer.cpp:1676` and used by no shader). A ray from a blade
  skips everything the wind moves for its first 20 cm (`pt_common.hlsli:975`, `:1010-1043`), so no blade hides
  another's reflection.
- Reflections are on All Surfaces by default: every grass pixel sends a reflection ray (`pt_specular.hlsl:71-91`).
  And the sun makes its own highlight on every blade (`pt_di_spatial.hlsl:86`).
- Rain. The game's `ScreenSpaceWetness` pass runs after the G-buffer and rewrites the roughness of every pixel that is
  open to the sky: smoothness goes toward 0.9 near the camera, by a moisture value that fills up while it rains
  (`PrecipitationController.UpdateScreenSpaceWetness`). Its shader reads only depth and the normal buffer, so it cannot
  tell grass from rock. The tracer sees that roughness, because it captures the buffers after the pass. In HDRP a
  wet blade still has a reflectance of 0.0023, so it stays matt. Traced, with 0.04, it is a small mirror.
- Wet rock is different: there the game's reflectance is 0.04 too. Its white came from Reflection Boost (twice the
  reflectance, on purpose), the sun at full strength (cause 3), and the reflections the game dims and hides more
  ("Seen on the way").

Measured:
- Rain, harness npc-day (`npc-day-grass-right.jpg`): 2.0 % of the grass is near white traced, 0.7 % with Reflection
  Boost 0, 0 % in the game's own. The grass is 5 times the game's on screen (0.23 against 0.045), still 4 times with
  Reflection Boost 0.
- Clear weather, Reflection Boost 0.2 that day (`f8-20260927-224018-grass.jpg`): the tracer's Specular view alone is
  brighter on the grass than the game's whole grass (0.033 against 0.024 on screen). The grass's colour is washed
  out: saturation 0.26 traced against 0.58 in the game's.
- Rain, the owner's F8 (`f8-20260927-225604-rain-grass.jpg`): the Specular view shows the glints blade by blade.

### 2. Traced grass casts sun shadows that the game does not have (confirmed)

- The game: grass, plants and ivy cast no shadow at any quality level (the table above).
- The tracer: `LeshyCapture.cs:78-79` hands every plant to the traced scene. The game's own answer is right there
  (`r.filterSettings.shadowCastingMode`) and is not looked at. `SceneCapture.cs` reads a renderer's shadow mode only
  to find shadow-only meshes (`:1095`, `:1193`, `:1417`). The capture's own comment says grass is traced so that it
  shadows (`LeshyCapture.cs:7-8`).
- So a shadow ray from the ground or from a blade is stopped by any blade more than 20 cm away
  (`pt_common.hlsli:1046-1061`; within 20 cm the sway rule lets it pass). A blade stops the light whole
  (`pt_common.hlsli:705`), although the same blade is lit through from behind at half strength (`:312-317`).
- The traced grass stands still while the drawn grass sways, so these shadows do not move with the blades. And they
  end 20 m from the camera, where traced grass ends (`LeshyCapture.cs:19`).

Seen: `npc-day-grass-mid.jpg`. Traced, the tuft throws a hard shadow on the gravel and the grass is mottled. The
game's has neither.

### 3. The sun: HDRP dims it by the fog, the tracer did not (confirmed, fixed in the tree; rain is open)

What HDRP does. For every directional light, HDRP multiplies the light by how much of it gets through the height fog
above the surface (`LightEvaluation.hlsl`, `EvaluateLight_Directional`, "Height fog attenuation";
`VolumeRendering.hlsl`, `TransmittanceHeightFog`). The game's main light does not take part in the sky's own dimming
(`interactsWithSky` is false on "ShadowCaster"), and it has no cookie.

What the game does with it (`DayNightSystem.HandleFog`): the fog's base height is the hero's height, and its density
follows the hour. So the fog always sits on the hero and dims the sun around him. At night the same light follows
the moon at a sun's intensity (60,511 lux in the night scene), and a very dense fog does the dimming.

How much the fog leaves of the sun, from the fog values the harness recorded and HDRP's formula, at the hero's height.
By its code's default the game keeps the main light at least 30 degrees above the horizon (`HandleShadowCasterRotation`):

| scene (baseline run) | fog mean free path | light overhead | at 45 degrees | at 30 degrees |
|---|---|---|---|---|
| npc-day, forest, metal, doorway, campfire (Horns of the South, day) | 144 m | 44 % | 31 % | 19 % |
| water-day (beach, morning) | 204 m | 56 % | 43 % | 31 % |
| vista (Cuanacht) | 682 m | 86 % | 81 % | 75 % |
| night (Horns of the South, 23:21) | 9 m | 15 % | 7 % | 2 % |

The traced light's gain was x1.0 in the outdoor day scenes of the run (npc-day, forest, metal, water-day, vista), and
HDRP's exposure is the game's own while tracing. So the extra brightness was light, not exposure.

Checked against the owner's captures. I took the tracer's Direct view as a mask of sun and shadow, and compared the
light (picture over albedo, tone curve undone with a fit) inside and beside the shadows (`tools/shadowtest.py`):
- Clear weather (`20260927-224018`): the game's sun is about 24 % of the traced sun. That fits the fog.
- Rain (`20260927-225604`, `f8-20260927-225604-rain-light.jpg`): the game's picture has almost no shadow where the
  tracer has the laundry's. That is about 8 % of the traced sun if the laundry casts a shadow in the game, and at
  most about 17 % if it does not. The fog alone would leave 19 to 44 %.

So in rain the game's sun may be dimmed by something more. I did not find it. It is not the clouds' shadow by the
game's files, and no game code changes the light with the weather. The rain's own fog is a local volume: it veils the
picture, but HDRP does not dim a surface's light by it. See "What to capture", 1.

### 4. Light through leaves, and the sky's light from behind (confirmed in data and code)

- The strength and colour. HDRP multiplies the light from behind by the profile's transmittance at the pixel's
  thickness (the table above). For a tree's leaf that is 1.4 to 6 times the light, yellow-green. The tracer uses one
  grey number, 0.5 (`TRANSMISSION`, `pt_common.hlsli:312`). The C# side reads every profile's numbers but sends only
  the thick ones (`HdrpPasses.cs:766`).
- The sky. HDRP also adds the far side's ambient light times that transmittance (`Lit.hlsl`,
  `ModifyBakedDiffuseLighting`). The tracer gathers bounced and sky light on the side of the normal only:
  `pt_gi.hlsl:37-38`, `pt_rtxdi.hlsli:201-205`, `pt_gi_spatial.hlsl:49`. Under an overcast sky, where all light is
  sky light, a leaf seen from below gets almost nothing from above.
- Seen: `forest-pine.jpg` and `forest-pine-needles.jpg` (rain 0.92). The game's needles are green and light. Traced
  they are grey-green and darker with white glints (the darker 40 % of them: 68/73/67 traced against 72/81/69 in
  the game's, saturation 0.10 against 0.16), although the rest of the picture got brighter.
- For the 23 materials on DiffusionProfile_Foliage_Grass it is the other way: the game lets 0.14 to 0.42 through and
  almost no blue, the tracer 0.5 grey. Lit from behind, that grass is too bright and too pale.

### 5. Smaller things (confirmed in code)

- Occlusion. HDRP's own ambient occlusion is off while tracing (`HdrpPasses.cs:190`). The traced rays replace it
  everywhere except in grass (the 20 cm rule). The material's own value is dropped (cause 1). So blades are lit
  evenly from root to tip. In the grass of `npc-day-grass-right.jpg`, 17 % of the game's pixels are dark (roots,
  depth). Traced it is 0.4 %.
- The geometric normal. `pt_prepare.hlsl:69-90` builds it from a neighbour pixel on each axis. On a blade one or two
  pixels wide the neighbour is another blade or the ground behind, and the result is often edge-on to the camera.
  Then `pt_gi.hlsl:38` counts every ray below that plane as "no light" and `pt_specular.hlsl:56` mirrors the
  reflection in it. On the 14 materials whose back faces keep their normal, all sky rays fail that test.
- What a bounce sees of grass. At a point a ray hits, grass is its base map from a 64x64 tile times white
  (`pt_common.hlsli:584-588`, `:840-842`): `SceneCapture.cs:811` does not know `_HealthyColor`, `_DryColor` or
  `_GroundBaseColor`. In the F8 Materials view the traced grass is 1.7 to 2.1 times as bright as HDRP's colour.
- Alpha is fine: a 256x256 coverage tile, and rays other than the camera's are stopped by chance as much as a texel is
  covered (`pt_common.hlsli:593-603`, `:700-705`).
- A tree whose leaves share a mesh with the trunk counts as standing still (`scene.cpp:621-627`), so the 20 cm rule
  does not protect its swaying leaves. Not seen in a capture yet.

## The changes

### Already in the working tree (16:35), not installed

- The fog on the sun and on lamps: `directional_fog`, `sun_fog` (`pt_common.hlsli:540-547`), used at
  `pt_di_spatial.hlsl:78-87`, `pt_common.hlsli:1697`, `:1546`, `pt_compose.hlsl:113`, `pt_hair.hlsl:165`,
  `pt_hdrp_shadow.hlsl:62`, `pt_specular.hlsl:88`. It is HDRP's formula, and each light term takes the fog once.
  To watch when it is tried: it removes light, so read the exposure line; at night Dark Adaptation will raise the
  gain. The radiance clamps are still tied to the full sun (`tracer.cpp:1689-1699`), so they are 2 to 5 times looser
  than a sunlit white surface now: watch for more sparkles in bounced light. The offline GPU test has no fog and
  sees no change.
- Reflection Boost leaves grass and leaves alone: `pt_prepare.hlsl:62-66`, `d3d11.hlsl:762-763`.

### Still to do, in priority order

Bits. Change 1 needs 16 floats from C# to the shaders (the ABI number changes). Change 2 in its "as the game" form
needs one instance flag (128 is the next free `TGPT_INSTANCE_*` value). Change 4 needs pt_prepare's flag 128 in
`mat.y`, the last free one of that byte. Nothing else needs a bit. `docs/PROJECT-CONTEXT.md` 9.12 is behind the tree:
material bit 65536 is taken (`TGPT_MATERIAL_HAIR_LAYER`), the still mask is 0xE1, 0x10 is the strands mask, and
`TGPT_Frame.forward[2]` is taken (the fog's maximum distance). Free now: material bit 131072, option bit 33554432,
`forward[3]`.

Source checks pin some lines (`tests/source_checks.py`). The edits keep the pinned lines where they can.

### Change 1. Vegetation's reflectance from its diffusion profile

On screen: the white glints on blades and needles go, wet or dry, as in the game. This is the main fix for the
white streaks. The 23 materials on the grass profile get a little more sheen than now.

The number is already read: `TransmissionOf` (`HdrpPasses.cs:734-752`) reads HDRP's `transmissionTintAndFresnel0`,
whose fourth value is the reflectance, and drops it. The pieces:
1. C#: send 16 floats, one a profile (`t.tint.w`), with the transmission table (`HdrpPasses.cs:764-772`).
2. Native: `TGPT_SetSkinTransmission` (`extension.cpp:516-527`) takes 8 floats a profile. It needs a ninth, or a new
   export, and the frame constants need `float4 profileF0[4]` beside `transmission[32]` (`pt_common.hlsli:176`).
3. `pt_prepare.hlsl`, add after line 94 (`bool translucent = ...`):

```hlsl
    // A leaf's reflectance is its diffusion profile's, as HDRP shades it (Lit's FillMaterialTransmission): the game's
    // foliage profile has an index of refraction of 1.1, a reflectance of 0.0023, where a dielectric's 0.04 made every
    // wet blade a mirror. 0: not given. (Coloured transmission keeps no profile in blue: bit 3 of alpha.)
    if (deferred && translucent && (uint(s.a * 255.0 + 0.5) & 8u) == 0u) {
        const uint profile = uint(s.b / (16.0 / 255.0) + 1.0 / 255.0) & 15u;
        const float profileF0 = g.profileF0[profile >> 2u][profile & 3u];
        if (profileF0 > 0.0) f0 = profileF0;
    }
```

What could go wrong: Ray Reconstruction's specular guide (`d3d11.hlsl:768-771`) would still say 0.04 on these pixels
unless the table reaches that shader too. The light is then smaller than the guide says, which is harmless, but the
two should agree in the end. Skin keeps its 0.04 (the owner tuned it; HDRP's is 0.027).

### Change 2. Grass and plants and the sun's shadow (the owner's choice)

Two ways. Both keep grass fully traced for bounced light, the sky's light and reflections.

Way A, the light a blade lets through (one line, no bit). A shadow ray is stopped by a blade only half the time, as
the blade itself is lit through at half strength. On screen: tuft shadows become soft and half as deep, and fade
with every further blade in the way. `pt_common.hlsli:705`, old:

```hlsl
    if ((m.flags & MATERIAL_ALPHA_TILE) != 0 && !g_cameraRays) return ray_chance(instanceId, geometry, primitive, 0x616C7068u) < a;
```

new:

```hlsl
    // A leaf or a blade lets a light's shadow ray through as often as it is lit through from behind (TRANSMISSION): its
    // shadow is half one. Stopped whole, every tuft threw a hard shadow the game never draws (its grass casts none).
    if ((m.flags & MATERIAL_ALPHA_TILE) != 0 && !g_cameraRays)
        return ray_chance(instanceId, geometry, primitive, 0x616C7068u) < a * ((m.flags & MATERIAL_TRANSLUCENT) != 0 && g_glassTint ? 1.0 - TRANSMISSION : 1.0);
```

`g_glassTint` is set while the final shading's shadow rays are traced (`transmittance`, `transmittance_from_surface`,
`pt_common.hlsli:1127-1142`). Way A also makes the shade under trees lighter, because their leaves are the same kind
of material. The game's tree shadows are full ones. If that is not wanted, way A needs way B's flag to tell grass
from trees.

Way B, as the game (needs one instance flag). Plants the game draws without a shadow stop no shadow ray.
- C#: `SceneCapture.Sink` (`SceneCapture.cs:83`) gets a flags argument; `LeshyCapture.cs:79` passes a new
  `Tgpt.InstanceNoShadow` where `r.filterSettings.shadowCastingMode == ShadowCastingMode.Off`; `AddMesh` already
  takes flags (`SceneCapture.cs:1105`, `:1111`).
- Native: the flag passes through as it is (`scene.cpp:644`). The instance must not be marked opaque, so that the
  test below runs for it.
- Shader, in `alpha_opaque` after the instance is loaded (`pt_common.hlsli:665`):
  `if ((inst.flags & INSTANCE_NO_SHADOW) != 0 && g_glassTint) return false;`

On screen with B: no grass or plant shadows at all, as in the game. Grass then looks less grounded, which the
occlusion of Change 4 gives back softly.

What could go wrong, both ways: ReSTIR's own visibility test (`occluded_from_surface` without `g_glassTint`) still
counts the blade, so a light sample behind grass is dropped a little more often than the final shading would. If
that shows as darker lamp light in grass, set the same marker for that ray.

Which way is the owner's call: A is how a thin leaf behaves, B is what the game's artists chose.

### Change 3. Light through a leaf as HDRP lets it through, and the sky's light from behind

On screen: leaves and needles lit from behind glow yellow-green as in the game, instead of grey. Under an overcast
sky the undersides of trees take the sky's light through them. The grass-profile grass loses its pale back light.

Step 1, C#, `HdrpPasses.cs:766`: send every profile's table, not only the thick ones. Old:

```csharp
                    var t = transmits && profiles != null && i < profiles.Length && profiles[i] != null && ((mask >> i) & 1u) != 0 ? TransmissionOf(profiles[i]) : default;
```

new:

```csharp
                    var t = transmits && profiles != null && i < profiles.Length && profiles[i] != null ? TransmissionOf(profiles[i]) : default;
```

The shader uses that table only where `solid_transmission` says skin (`pt_prepare.hlsl:266`), so this alone changes
nothing on screen.

Step 2, `pt_common.hlsli`, add after `skin_transmittance` (after line 945):

```hlsl
// A leaf's or a blade's light from behind against the grey share the tracer lights it with (s.through): HDRP's
// transmittance of the pixel's own thickness through its diffusion profile (skin_transmittance, with the thin profiles'
// constants). 1 where HDRP drew no such pixel, on thin cloth and hair, on coloured transmission (bit 3 of GBuffer2's
// alpha: no profile in blue), and without the profile's constants.
float3 leaf_tint(uint2 p, Surface s) {
    if (!s.translucent || !s.deferred || !(s.through > CLOTH_TRANSMISSION + 0.01)) return 1.0;
    Texture2D<float4> gb2 = ResourceDescriptorHeap[g.inputs0.z];
    const float4 v = gb2.Load(int3(p, 0));
    if ((uint(v.a * 255.0 + 0.5) & 8u) != 0u) return 1.0;
    const float3 t = skin_transmittance(v);
    return any(t > 0.0) ? t / s.through : 1.0;
}
// Whether a surface gathers bounced and sky light from behind as well (pt_gi), as HDRP adds the far side's baked light
// times a leaf's transmittance: a leaf or a blade of grass, not thin cloth or hair, whose looks were set without it.
bool gathers_behind(Surface s) { return s.translucent && s.through > CLOTH_TRANSMISSION + 0.01; }
```

Step 3, the sun and lamps, `pt_di_spatial.hlsl`. After line 31 (`float3 localDiffuse = 0.0, spec = 0.0;`) add
`const float3 backTint = leaf_tint(p, s);`. After line 60 (`rtxdi_shade(ls, surface, d, sp);`) add
`if (dot(s.normal, ls.dir) < 0.0) d *= backTint;`. After line 81 (the pinned `sunDiffuse = ...` line) add
`if (dot(s.normal, l) < 0.0) sunDiffuse *= backTint;`.

ReSTIR keeps weighing samples with the grey 0.5. Only the final shading gets the colour, so nothing is counted twice.

Step 4, the sky's and bounced light from behind. `pt_gi.hlsl`: delete line 26
(`float3 origin = s.position + s.geoNormal * offset;`) and change the loop. Old (lines 36-39 and 58-62):

```hlsl
    for (uint k = 0; k < rays; ++k) {
        float3 dir = cosine_sample(s.normal, rng.next2());
        const bool above = dot(dir, s.geoNormal) > 0.0;
        const bool feed = feeds && k == 0;
```
```hlsl
        mean += radiance;
        float cosTheta = saturate(dot(s.normal, dir));
        float target = luminance(radiance) * cosTheta;
        // Source density cos/pi: the candidate's weight is target / density.
        float w = cosTheta > 0.0 ? target * PI / cosTheta : 0.0;
```

new:

```hlsl
    // A share of a leaf's rays, as large as the far side's share of its light, leaves from the far side.
    const float behind = gathers_behind(s) ? s.through : 0.0;
    const float backShare = behind / (1.0 + behind);
    const float3 backTint = leaf_tint(p, s);
    for (uint k = 0; k < rays; ++k) {
        bool back = false;
        if (behind > 0.0) back = rng.next() < backShare;
        const float3 sn = back ? -s.normal : s.normal, gn = back ? -s.geoNormal : s.geoNormal;
        const float3 origin = s.position + gn * offset;
        float3 dir = cosine_sample(sn, rng.next2());
        const bool above = dot(dir, gn) > 0.0;
        const bool feed = feeds && k == 0 && !back;
```
```hlsl
        mean += radiance * (1.0 + behind) * (back ? backTint : float3(1.0, 1.0, 1.0));
        float cosTheta = saturate(dot(sn, dir));
        float target = luminance(radiance) * cosTheta * (back ? behind : 1.0);
        // Source density: its side's share times cos/pi.
        float w = cosTheta > 0.0 ? target * PI / (cosTheta * (back ? backShare : 1.0 - backShare)) : 0.0;
```

`pt_rtxdi.hlsli:204`, the last line of `RAB_GetGISampleTargetPdfForSurface`, old:

```hlsl
    return len > 0.0 ? luminance(sampleRadiance) * saturate(dot(surface.s.normal, d / len)) : 0.0;
```

new:

```hlsl
    if (!(len > 0.0)) return 0.0;
    const bool back = gathers_behind(surface.s) && dot(d, surface.s.geoNormal) < 0.0;
    const float c = dot(surface.s.normal, d / len);
    return luminance(sampleRadiance) * (back ? surface.s.through * saturate(-c) : saturate(c));
```

`pt_rtxdi.hlsli:210`, in `rtxdi_gi_visible`, add after `Surface s = surface.s;` (the pinned test below it stays):

```hlsl
    if (gathers_behind(s) && dot(samplePosition - s.position, s.geoNormal) < 0.0) s.geoNormal = -s.geoNormal; // from its far side
```

`pt_gi_spatial.hlsl:41-49`, old:

```hlsl
        float offset = ray_offset(s.distance);
        float3 origin = s.position + s.geoNormal * offset;
```
```hlsl
        if (!blocked) result = r.radiance * saturate(dot(s.normal, dir)) / PI * W;
```

new:

```hlsl
        float offset = ray_offset(s.distance);
        const bool back = gathers_behind(s) && dot(r.position - s.position, s.geoNormal) < 0.0; // a leaf's sample from behind it
        float3 origin = s.position + s.geoNormal * (back ? -offset : offset);
```
```hlsl
        if (!blocked) {
            const float c = dot(s.normal, dir);
            result = r.radiance * (back ? leaf_tint(p, s) * (s.through * saturate(-c)) : float3(1.0, 1.0, 1.0) * saturate(c)) / PI * W;
        }
```

What could go wrong: back-lit leaves get up to ten times brighter than now (the game's 5.3 against the tracer's
0.5), so they can sparkle until the denoiser settles: the sun's part is not clamped. One ray now serves two sides,
which adds noise on foliage. Thin cloth and hair are left out on purpose (the owner set their looks; the hair's
bounced-light add is a dead end). The edit to `pt_gi.hlsl` keeps the pinned call text
`trace_from_surface(origin, dir, 0.0, g.params.y, near_back(s.distance), hit)`. For a tree's leaf the thickness in
GBuffer2 green comes from the leaves shader, which I did not decode ("What to capture", 3). Steps 1 to 3 can go in
without step 4.

### Change 4. Use the material's own occlusion on grass and leaves

On screen: blades get darker toward the root and inside tufts, by what the game's artists painted into the mask map.
It works at any distance, also beyond the 20 m of traced grass. How strong: not known until the values are captured.

`pt_prepare.hlsl`, add after line 94 (after Change 1's block):

```hlsl
    // Grass and leaves keep what the game's material says of how much sky a blade sees: HDRP's specular occlusion
    // (GBuffer2's red on its transmission features) and the material's ambient occlusion, which HDRP writes into GBuffer3's
    // green behind a tag where a pixel has no baked light and no emission (always, while tracing holds its indirect light
    // at 0). The tracer's own rays pass everything the wind moves within SWAY_REACH: nothing else stands for the blades
    // around a blade.
    float ambientOcclusion = 1.0, specularOcclusion = 1.0;
    const bool occlusion = deferred && translucent && g.inputs0.w != INVALID;
    if (occlusion) {
        Texture2D<float4> gb3 = ResourceDescriptorHeap[g.inputs0.w];
        const float3 baked = gb3.Load(int3(p, 0)).rgb;
        if (abs(baked.r - 2048.0) < 1.0 && abs(baked.b - 1024.0) < 1.0) ambientOcclusion = saturate(baked.g);
        specularOcclusion = saturate(s.r);
    }
```

`pt_prepare.hlsl:270-273`, old:

```hlsl
    mat[p] = uint2(pack_rgba8(float4(color, forward ? 1.0 : 0.0)),
                   pack_rgba8(float4(hairLobes ? float3(hairLobe, 0.0) : linear_to_srgb(saturate(thin ? transmittance : f0)),
                                     float((complex ? 1u : 0u) | (glass ? 2u : 0u) | (cloth || hairLike ? 4u : 0u) | (eye ? 8u : 0u) | (thin ? 16u : 0u) |
                                           (hdrpShaded ? 32u : 0u) | (hairLobes ? 64u : 0u)) / 255.0)));
```

new:

```hlsl
    // Bit 128: the two occlusions in green and blue; the reflectance, a leaf's single number, in red alone.
    const float3 specularBytes = hairLobes ? float3(hairLobe, 0.0)
        : occlusion ? float3(linear_to_srgb(saturate(f0)).r, ambientOcclusion, specularOcclusion)
        : linear_to_srgb(saturate(thin ? transmittance : f0));
    mat[p] = uint2(pack_rgba8(float4(color, forward ? 1.0 : 0.0)),
                   pack_rgba8(float4(specularBytes,
                                     float((complex ? 1u : 0u) | (glass ? 2u : 0u) | (cloth || hairLike ? 4u : 0u) | (eye ? 8u : 0u) | (thin ? 16u : 0u) |
                                           (hdrpShaded ? 32u : 0u) | (hairLobes ? 64u : 0u) | (occlusion ? 128u : 0u)) / 255.0)));
```

`pt_common.hlsli`: the `Surface` struct gets `float ambientOcclusion; float specularOcclusion;` after `through`
(line 298). In `load_surface`, after line 367 (`if ((bits & 16u) != 0) s.f0 = 0.04;`):

```hlsl
        s.ambientOcclusion = 1.0; s.specularOcclusion = 1.0;
        if ((bits & 128u) != 0) { s.ambientOcclusion = spec.g; s.specularOcclusion = spec.b; s.f0 = srgb_to_linear(spec.rrr); }
```

In its `else` branch (line 382) add `s.ambientOcclusion = 1.0; s.specularOcclusion = 1.0;`. The two surfaces built
as `(Surface)0` (`pt_hair.hlsl:158`, `pt_rtxdi.hlsli:30`) get 0 there; nothing reads the two fields on them.

`pt_compose.hlsl:252`, add a line after it (the pinned line stays):

```hlsl
    if (s.ambientOcclusion < 1.0) diffuse -= diffuseFinal[p].rgb * (fromNrd * (1.0 - s.ambientOcclusion)); // bounced and sky light only
```

`pt_specular.hlsl`, add after line 92 (before the see-through glass block):

```hlsl
    added *= s.specularOcclusion; // a blade's reflection of what is around it, as much as the game's material leaves of it
```

What could go wrong: three of the 128 materials have no mask map (Mat_Vegetation_Grass_Forlorn_01_SG among them).
If the shader then reads its black default, their occlusion is 0 and they get no bounced light. Check them first.
Plants with emission keep no occlusion (HDRP only writes the tag where there is no emission).

### Change 5. A thin blade's own normal for its rays

On screen: less random darkening from blade to blade in bounced and sky light; reflections no longer mirrored
sideways. The 14 materials whose back faces keep their normal get bounced light at all.

`pt_prepare.hlsl`, add after line 94 (before the other new blocks):

```hlsl
    // A blade of grass or a leaf's card a pixel or two wide has no neighbour on its own plane: the plane through the
    // pixels beside it (the next blade, the ground behind) is edge-on to the camera, and it faces away from HDRP's normal
    // where the game leaves a leaf's back face unflipped. Where it is more than 60 degrees off HDRP's normal, that normal
    // serves: the card's own, smooth over the leaf.
    if (translucent && dot(geo, normal) < 0.5) geo = normal;
```

What could go wrong: on such a back face rays now start on the far side of the leaf, as HDRP lights it. With Change 3
the near side gets its share.

### Change 6. What a bounce sees of grass

On screen: light bounced off grass is about half as strong and greener.

`SceneCapture.cs`, add after line 884 (the line that reads `ColorNames`):

```csharp
            // The game's grass and leaves tint their texture between two colours by a noise over the world: their mean.
            if (m.HasProperty("_HealthyColor") && m.HasProperty("_DryColor")) baseColor = 0.5f * (LinearColor(m.GetColor("_HealthyColor")) + LinearColor(m.GetColor("_DryColor")));
```

What could go wrong: a few materials keep both colours white, which changes nothing there.

### Later

- Rough blades without a reflection ray: let `pt_specular.hlsl:67` take the radiance cache for `s.translucent` pixels
  whatever the Reflections row says. After Change 1 a blade's reflection is so weak that the ray is wasted. It
  touches the Reflections A/B the owner has not judged yet: ask first.
- A light's shadow through leaves, coloured (a tree's shade is green): Change 2 A with the profile's transmittance
  instead of half.
- Trees with bark and leaves in one mesh: apply the 20 cm rule by material instead of by instance.

## What to capture in game (cannot be learned offline)

1. The game's sun in rain. Two things.
   - One log line from the mod, on a rainy save: from the camera's volume stack `CloudLayer.layerA.castShadows` and
     `layerB.castShadows` (value and override state), and whether HDRP's cloud renderer gives the main light a cookie
     (`SkyManager.TryGetCloudSettings`, then `CloudRenderer.GetSunLightCookieParameters`). It also settles PT-36.
   - An F8 capture in rain, and one in dry weather, where a wall's shadow edge crosses open ground. Then
     `artifacts/agents/vegetation/tools/shadowtest.py` (edit the capture's name in it) gives the game's sun against
     the tracer's.
2. Whether the laundry by the pole casts a shadow in the game: an F8 with the probe on it. The probe already prints
   the renderer's shadow mode (`Probe.cs:39`).
3. The G-buffer on vegetation, as an F8 crosshair print: GBuffer3 green when red is 2048 and blue 1024, GBuffer2 red
   and GBuffer2 green. On a grass blade's tip and root, and on a tree's leaf.
4. Wet grass: an F8 in rain with the crosshair on a blade. The report prints HDRP's roughness there.
5. The sun's direction, as a line in the F8 report and in the harness's step. The fog's share depends on it.
6. How far the wind moves a blade: the Coverage view on a windy day (dark streaks are drawn blades where the traced
   copy is not). The 20 cm rule rests on it.

## Harness recipes

They go into `tests/ingame/scenarios.json` under `recipes`. Use them on `npc-day`, `forest`, `metal` and `night`.

Which light makes the streaks and patches. `3-view-direct` shows the sun and its shadows, `4-view-specular` the
glints. After Change 1 the Specular view should be nearly black on grass. With the fog on the sun the Direct view
should be about a third as bright by day as in the F8 of 2026-09-27.

```json
"veg-why": [
  { "name": "1-game", "set": [ { "key": "Mode", "value": "Off" } ], "traced": false, "settle": 3, "frames": 8, "audit": true },
  { "name": "2-traced", "set": [ { "key": "Mode", "value": "On" }, { "key": "Denoising", "value": "Ray Reconstruction" }, { "key": "Reflection boost", "value": "0.1" }, { "key": "Reflections", "value": "All Surfaces" }, { "key": "Debug view", "value": "None" } ], "traced": true, "settle": 6, "settleFrames": 240, "frames": 8 },
  { "name": "3-view-direct", "set": [ { "key": "Debug view", "value": "Direct" } ], "traced": true, "settle": 4, "settleFrames": 120, "frames": 4 },
  { "name": "4-view-specular", "set": [ { "key": "Debug view", "value": "Specular" } ], "traced": true, "settle": 4, "settleFrames": 120, "frames": 4 },
  { "name": "5-view-indirect", "set": [ { "key": "Debug view", "value": "Indirect" } ], "traced": true, "settle": 4, "settleFrames": 120, "frames": 4 },
  { "name": "6-view-normals", "set": [ { "key": "Debug view", "value": "Normals" } ], "traced": true, "settle": 3, "settleFrames": 60, "frames": 2 },
  { "name": "7-view-materials", "set": [ { "key": "Debug view", "value": "Materials" } ], "traced": true, "settle": 3, "settleFrames": 60, "frames": 2 },
  { "name": "8-off", "set": [ { "key": "Debug view", "value": "None" }, { "key": "Mode", "value": "Off" } ], "traced": false, "settle": 1, "frames": 0 }
]
```

How much of the white is reflections. Compare the near-white share of the grass between steps.

```json
"veg-reflections": [
  { "name": "1-game", "set": [ { "key": "Mode", "value": "Off" } ], "traced": false, "settle": 3, "frames": 8 },
  { "name": "2-traced", "set": [ { "key": "Mode", "value": "On" }, { "key": "Denoising", "value": "Ray Reconstruction" }, { "key": "Reflection boost", "value": "0.1" }, { "key": "Reflections", "value": "All Surfaces" } ], "traced": true, "settle": 6, "settleFrames": 240, "frames": 8 },
  { "name": "3-boost-0", "set": [ { "key": "Reflection boost", "value": "0" } ], "traced": true, "settle": 6, "settleFrames": 240, "frames": 8 },
  { "name": "4-glossy-only", "set": [ { "key": "Reflections", "value": "Glossy Surfaces" } ], "traced": true, "settle": 6, "settleFrames": 240, "frames": 8 },
  { "name": "5-reflections-off", "set": [ { "key": "Reflections", "value": "Off" } ], "traced": true, "settle": 6, "settleFrames": 240, "frames": 8 },
  { "name": "6-off", "set": [ { "key": "Mode", "value": "Off" }, { "key": "Reflections", "value": "All Surfaces" }, { "key": "Reflection boost", "value": "0.1" } ], "traced": false, "settle": 1, "frames": 0 }
]
```

Which dark patches are traced plants' shadows. In step 3 the traced scene has no grass or trees: patches that
vanish were their shadows (cause 2). In step 4 the sky gives a hundredth of its light (0 would mean 1): what is left
on the grass is the sun and lamps.

```json
"veg-shadows": [
  { "name": "1-game", "set": [ { "key": "Mode", "value": "Off" } ], "traced": false, "settle": 3, "frames": 8 },
  { "name": "2-traced", "set": [ { "key": "Mode", "value": "On" }, { "key": "Denoising", "value": "Ray Reconstruction" }, { "key": "Trace vegetation", "value": "true" }, { "key": "Sky intensity", "value": "1" } ], "traced": true, "settle": 6, "settleFrames": 240, "frames": 8 },
  { "name": "3-no-traced-plants", "set": [ { "key": "Trace vegetation", "value": "false" } ], "traced": true, "settle": 10, "settleFrames": 300, "frames": 8 },
  { "name": "4-sky-nearly-off", "set": [ { "key": "Trace vegetation", "value": "true" }, { "key": "Sky intensity", "value": "0.01" } ], "traced": true, "settle": 10, "settleFrames": 300, "frames": 8 },
  { "name": "5-off", "set": [ { "key": "Mode", "value": "Off" }, { "key": "Sky intensity", "value": "1" } ], "traced": false, "settle": 1, "frames": 0 }
]
```

After each change, run `ab` on `npc-day`, `forest`, `metal`, `vista` and `night` again and compare with the baseline
`20261006-145526`: the grass's mean on screen against the game's, its near-white share, and the exposure line.

## Seen on the way (not vegetation, not looked into further)

- Night is the same cause 3: `night-game-vs-traced.jpg`. The moon lit the man like a day's sun (his cloak 4.5 times
  brighter on screen). The fog leaves 2 to 15 % of it in the game.
- More than grass is drawn without shadows in the game: Leshy's objects at low and medium quality, its ivy, and any
  renderer with Cast Shadows off. `SceneCapture.cs` traces them all with shadows.
- Reflection Boost adds the same amount to every surface (`pt_prepare.hlsl:68`): it doubles a non-metal (0.04 to
  0.079) and leaves a metal as it was.
- The game dims all reflections by its own multiplier (0.5 to 0.96 in these scenes,
  `IndirectLightingController.reflectionLightingMultiplier`; rain raises it). The traced reflections do not take it.
  And on standard surfaces HDRP's specular occlusion (GBuffer0's alpha) is lost in the albedo copy (`d3d11.hlsl:47`).
- The rain's own fog thickens with the rain (`PrecipitationController.LocalVolumetricFogControl`: its mean free path
  goes from 400 m toward a few metres). The tracer's fog model knows only the global fog (`HdrpPasses.cs:385-398`).
- `vista-game-vs-traced.jpg` (dry, Cuanacht): the game's cart is not sunlit although the fog leaves 75 to 86 % of the
  sun there. Not explained. It may be a shadow caster the traced scene lacks.
