# Cloth agent: every cloth found as cloth, and the wind's sway traced (PT-49, PT-39)

Status: done offline (2026-10-07, finishing the agent cut off at 00:05). Not seen in game yet.

## Summary

- Cloth is now found by what it is (`src/TGPathTracing/ClothRules.cs`): the game's whole `TG/Clothes/` shader family,
  a hanging sheet's name (tent, banner, curtain, canvas, awning, laundry, ...), or a cloth word (cloth, sheet, towel, rag,
  fabric, ...) on a material HDRP draws from both sides. Cloth lying on a floor, stuffed or thick (rugs, sacks, pillows,
  fur, nets) and stone drapery stay solid. Characters' clothes are left as they are. Over the census: 74 thin cloth
  materials, was 62; 16 found newly (laundry sheets, towels, rags, tablecloths), 4 dropped (a statue's stone cloth, a
  shrouded body, a shield's cover, the loading screen's canvas).
- The wind's sway is traced. The game's `TG/Clothes/Cloth_Wind` moves its vertices in its vertex shader; I read its
  maths from the game's compiled shader. The tracer now keeps its own copy of each swaying cloth's vertices near the
  camera, moves them each frame with the same maths and the same time (a compute pass, `pt_sway.hlsl`), refits that
  copy's BLAS, and traces it as a still object. So the camera's pixels, the shadows and the reflections all see the
  pose HDRP draws.
- One finding changes how to read the PT-39 evidence: the Surfaces debug view never showed the tracer's cloth decision.
  It shows HDRP's own material type, and HDRP draws the game's cloth as its standard material, so the canopy was grey
  whether or not the tracer took it for cloth. The view now paints what the tracer lights as thin cloth purple.
- Offline: the new GPU checks pass (a swaying sheet traced within 0.01 mm of where it is drawn at two times, its shadow
  the posed canopy's, a red canopy's underside lit through by the sky in red), the cloth rules check the census, the
  source checks pass, and every new check fails when the thing it guards is broken (section 5).

## 1. Cloth census over the harness's scenes

Source: every `material|` line in `artifacts/harness/*/*/1-game/audit-*.txt` (all runs to 2026-10-07 02:25), kept where
the shader or the name says cloth (`<scratch>/cloth_census.py`, `<scratch>/cloth_table.py`). 161 materials; AT = alpha
tested, DS = drawn from both sides (`_DOUBLESIDED_ON`). "Before" is the old rule (shader `Cloth_Wind`, or a name part
from a list, minus Floor, Rug, Carpet, Table, Pole, Rope, Wood, Metal).

| Group | Materials (scenes they appear in) | Before | Now |
|---|---|---|---|
| The game's cloth in the wind, `TG/Clothes/Cloth_Wind` (16) | Cloth_Wind (the canopy family), Mat_BannerWind and its HoS, Kamelot, RedCloth variants, Mat_Banner_RedPriest_01_Wind, Mat_Prop_Cloth_PlaneBig_04_HoS_moving (candle-up's red canopy), Mat_Prop_Drakkar_Ruin_02_Cloth, Mat_Prop_Laundry_01/02_Moving, Mat_Sagremore_Banner_01..04_Cloth, Mat_Ulfr_Banner_01 | thin | thin, and swayed as the game sways them |
| Tents and canvas, HDRP/Lit | Mat_BigTent_Plague_01, Mat_DestroyedTent_01..04_Cloth, Mat_Prop_Tent_01, Mat_Prop_TentBig_02, Mat_Tent_03DalRiata, Mat_tent_03 | thin | thin |
| Wagon, boat and stall covers | Mat_Cloth_Wagon_01..03, Mat_Wagoncloth_02, Mat_Vech_Cage_Wagon_Cloth, Mat_BoatCloth_A, Mat_MerchantStall_Cloth_* (5) | thin | thin |
| Curtains and wall hangings | Mat_Prop_Curtain_grey/red/red_light, Mat_Prop_Curtain_Medic_01/02, Mat_Prop_Cloth_Curtain(_02), Mat_Prop_Cloth_Arras_01(_v3), Mat_Tavern_Cloth_Tilt_01 | thin | thin |
| Cloth planes, lines, towels on a line | Mat_Prop_Cloth_Plane_02/03_HOS/06, Mat_Prop_Cloth_PlaneBig_04/05/KAM_01, Mat_Prop_Cloth_Line_01, Mat_Prop_Cloth_Towel_01/02, Mat_Prop_Cloth_Bench_01, Mat_Prop_Cloth_Circle_01, Mat_Prop_Cloth_Plague_01 | thin | thin |
| Laundry sheets, towels, rags (DS, AT) | Mat_Hos_sheet_01, Mat_Prop_Sheet, Mat_Prop_Sheet_2_Red, Mat_Prop_Towel_01, Mat_Prop_Rags_01/02/13, Mat_Prop_Rags_Low_12, Mat_Prop_Plane_Rag_03 | solid | **thin** (new: "clothes hanging on a drier") |
| Tablecloths, bed fabric, a cradle's sheets (DS) | Mat_Prop_Tablecloth_01/02/02_v1, Mat_Fabric_Bed_01/01_Red/02, Mat_Cradle_Sheets | solid | **thin** (single sheets; they hang over edges) |
| Cloth wrapped round something (one side drawn) | Mat_grave_body_cloth, Mat_Prop_Shootshield_03_Cloth, Mat_Prop_Bed_Sheets_01/02 | thin, thin, solid | solid |
| Stone drapery | Mat_Statue_HarpLady_Cloth_01 | thin | **solid** |
| Lying, stuffed, thick, or no cloth | Mat_Fabric_Floor_01/05, Mat_Prop_FloorRag_*, Mat_Prop_AnimalRug_*, Mat_Prop_Sack_*, Mat_Prop_CoinsBag_*, Mat_Prop_Sleeping_Bag_01, Mat_Prop_Pillow_*, Mat_Prop_Wyrddear_Fur, fishing nets, Mat_Prop_Bandage_01, window glass | solid | solid |
| The interface | TransitionScreen_Canvas (TG/UI) | thin (by its name) | not a surface |
| Characters' clothes (TG/Character/*, 8) | Mat_ChMHosM_Daragh_Arms/Torso, Mat_Sveinn_Fur(_v2), Ribbon, Mat_Wolf_Fur_Base, Mat_Corax_Fur | solid | solid (left as they are) |

Thin cloth over the census: 62 before, 74 now. The rows changed: 16 solid to thin, 4 thin to solid (above, in bold or
in the wrapped and interface rows). A name the census never saw is judged the same way, by its word parts.

Which material the candle-up canopy is: the scene holds `Mat_Prop_Cloth_PlaneBig_04_HoS_moving` (Cloth_Wind, AT, DS)
and the HDRP/Lit stall cloths; the next run's log line "Cloth in the wind ..." names every swaying material with its
wind, and the purple in the Surfaces view shows which pixels the tracer lights as cloth.

## 2. How cloth is recognised now

`ClothRules.IsThinCloth(shader, material name, drawn from both sides)`, free of the game, so `tests/cloth_rules_tests.cs`
(`tests/ClothRules.Tests.csproj`) checks it against the census without it:

1. Shader `TG/Clothes/*` (any of the family): thin cloth, whatever the material is called.
2. Characters (`TG/Character/*`) and the interface (`TG/UI/`, `UI/`, `GUI/`, `Sprites/`, `TextMeshPro`): never.
3. The name split into word parts (at anything not a letter, and where a lower-case letter meets an upper-case one:
   "Mat_BigTent_Plague_01" is mat, big, tent, plague). A part from the "not" list (floor, rug, carpet, statue, stone,
   rock, wood, metal, pole, rope, sack, bag, pillow, fur, leather, hide, net): solid.
4. A hanging word (banner, curtain, canvas, tent, flag, sail, awning, tarp, laundry, drape, canopy, arras, tapestry,
   pennant, and plurals): thin.
5. A cloth word (cloth, sheet, towel, rag, linen, fabric, veil, blanket, and plurals, and compounds ending in cloth,
   sheet, towel, curtain, banner, canvas: "tablecloth", "wagoncloth") on a material HDRP draws from both sides: thin. Drawn
   from one side it is a closed shape (a shrouded body, a shield's cover, bed sheets on a mattress).

Replaces `SceneCapture.ThinCloth` (a substring list: "Table" in "Tablecloth" made it solid; "Cloth" in a statue's
stone made it thin). Rays of thin cloth still pass characters by (`pt_common`: `INSTANCE_CHARACTER`), as before.

## 3. The wind sway: the game's maths, and the traced copy

### Where the maths came from

The game's `TG/Clothes/Cloth_Wind` shader, unpacked from `resources.assets` (`tools/research/unity_shader_blob.py`) and
its 24 vertex shaders disassembled (`tools/research/dxbc_split.py`; `<scratch>/clothsh/vs_*.txt`). The depth pass's
(`vs_000aba`) and the GBuffer pass's (`vs_00a8aa`) do the same. The constant buffers have no names in the game's DXBC, so
UnityPerMaterial is read by the properties' order (`<scratch>/cloth_props.py`): `_Speed` cb2[0].x, `_Wind` cb2[0].y,
`_Intensity` cb2[1].xyz, `_NoiseScale` cb2[1].w, three texel sizes cb2[2..4], `_Alpha_Clip` cb2[5].x, `_Direction`
cb2[5].yz, `_Tiling` cb2[6].xy, `_Offset` cb2[6].zw, `_UseWorldSpace` cb2[7].x, `_UseVertexPaint` cb2[7].y. Every read the
code makes fits that layout. HDRP's global buffer: cb0[80] is `_TimeParameters` (offsets from the game's own HDRP
assembly), whose x HDRP sets to `Time.time`.

What one vertex does (object space in and out):

```
uv    = (cb2[7].y != 0) ? position.xy : texcoord0.xy
n     = SimpleNoise(uv + time * speed, noiseScale)      Shader Graph's: Hash_Tchou_2_1 (0x41C64E6D, 0x27D4EB2D),
                                                        value noise with a smoothstep, octaves at scale, /2, /4
                                                        weighted 1/8, 1/4, 1/2
push  = uv.y * wind * (direction.x, 0, direction.y)      in world space
local = position + worldToObject3x3 * push
mask  = (cb2[7].x != 0) ? 2 * vertexColor.rgb : 1
moved = position + intensity * n * local * mask
```

Two things to know. The switches are the other way round from their names: `_UseWorldSpace` (x) switches the vertex
color mask, `_UseVertexPaint` (y) switches the noise to the position. The plugin follows the compiled code
(`TGPT_WIND_VERTEX_PAINT` from `_UseWorldSpace`, `TGPT_WIND_POSITION_UV` from `_UseVertexPaint`). And the sway is not
centred on the still pose: the noise is 0 to 1, so the cloth is always pushed out from its object's origin by up to
`intensity` of its position. For a canopy whose origin sits at its poles' tops, the drawn cloth hangs lower or higher
than the still mesh by tens of centimetres all the time, not only when the wind blows.

### The traced copy

- The plugin reads each Cloth_Wind material's values when it builds the material (`SceneCapture.AddClothWind`, logged
  once each as "Cloth in the wind ...") and sends them with the time before every frame (`SceneCapture.SendClothWind`
  in `HdrpPasses`, beside the characters, through the new `TGPT_SetClothWind`).
- `GpuScene::record` picks the instances with such a material within 60 m of the camera, the nearest first, at most 96
  and 1,048,576 vertices (`kMaxSwayCopies`, `kSwayReach`, `kSwayVertexBudget`). Each gets a copy of its own: a positions
  buffer and a BLAS built for refitting, kept while its instance is traced (dropped 120 frames after the last use).
- `pt_sway.hlsl` (one dispatch per copy to copy the still positions, then one per swaying submesh over its indices)
  writes the pose; the BLAS is built once and refitted after. A pose already made for this time (the game paused) is
  kept. The copy is traced as a still object (no `kMaskSways`, no `TGPT_INSTANCE_SWAYS`), with a mesh record of its own
  after the registered ones (its positions, the mesh's texture coordinates, colors, indices, normals).
- Copies get no opacity micromaps (they were made for the still mesh): they are alpha-tested per ray.
- The 30 cm window of 18:50 (`CLOTH_SWAY_REACH`) stays for swaying cloth beyond 60 m or over the budget.
- Fixed in the stopped agent's code: a copy whose transform had no inverse was skipped by the sway pass while its
  instance still used the copy's BLAS (never built). Now the push is dropped and the copy is built.

The status line (native log) says "cloth: N thin, M in the wind's pose (V vertices, C copies)".

## 4. The canopy seen from below

What was found:

- The PT-39 runs read "the canopy is grey in the Surfaces view, so the tracer does not take its pixels for thin cloth".
  That reading was wrong: the Surfaces view (`pt_compose`) only paints HDRP's GBuffer material type, and the game's
  cloth is HDRP's standard type. Grey says nothing about the tracer's cloth. Now purple marks thin cloth there.
- Its Coverage view (run `20261006-232601/candle-up`) shows the canopy traced with holes where the game's are, so the
  still copy is near the drawn one there; whether it is within the camera ray's window (3 cm + 1 cm per metre) is not
  known. With the copy swayed as HDRP draws it, it is matched exactly.
- The red under the canopy is the composite's: the pixel takes light from behind (`gathers_behind`, 0.3 of it), and
  the cloth's own color (its albedo) tints it.

Offline (GPU check 13, section 5): a red Cloth_Wind-style canopy, swayed, under the open sky with the sun off, seen from
below. Its pixels take the sky's light through it as thin cloth, and that light is red in the picture. Traced still (the
PT-39 case: the drawn cloth 0.3 to 0.7 m from the traced one) the pixels miss it.

## 5. Offline checks and mutation runs

New GPU checks, `tests/pathtracing_gpu_test.cpp` (the leaf section's 11 to 13). The drawn pose comes from
`cloth_wind_reference`, written from the game's compiled shader (not from `pt_sway`); the tracer gets the still mesh and
the wind. A wind as the game's materials set it (speed 0.18, wind 0.77), noise scale 4, direction (0.8, 0.6).

| Check | Result | Broken (mutation) |
|---|---|---|
| 11. A 1.5 by 1 m sheet (16 by 16 cells) hangs facing a camera 3 m off (3.0 mm a pixel). Where camera rays find the traced sheet against where it is drawn, along its outline (1,951 pixels long) | 0.001 px (0.00 mm) at 3 s, 0.002 px at 7.5 s; traced still 36.8 px (112 mm); it moved 8.2 px (25 mm) between the two times | wind direction's components swapped in `pt_sway`: 8.14 mm and 6.82 mm, fails. BLAS not refitted after its first build: 25.00 mm at the second time (the first pose), fails |
| 12. The sheet hung 1 m under its origin as a 2 by 2 m canopy 4 m up in the sun, solid: the floor's direct light, swayed by the tracer against the same canopy registered already posed (2,248 pixels where its shadow moves) | mean difference 0.0000; the still canopy's 0.2523, a later pose's 0.0475 | direction swapped: 0.0109, fails. No refit: the later pose's shadow 0.0000 against the first (it never moved), fails |
| 13. That canopy red thin cloth, drawn facing down in its swayed pose, seen from 3 m below, the sky alone (241,470 pixels) | light from behind it 0.27 of the sky's above it (0.30 expected); traced still 0.02; in the picture it adds 0.0164 red, 0.0024 green (6.8 to 1; the cloth's color 0.75, 0.08) | traced still is the PT-39 case, checked in every run |

The older cloth checks hold: a red card's shadow 0.30 in red, green 0.107 of it; cloth drawn 25 cm off its traced place
lit through 0.73; thin cloth under the sky 0.33, 0.29, 0.29; the floor under a red canopy 0.30, 0.107. The whole run
(build `%TEMP%\tgb\cloth-build`, installed to `%TEMP%\tgb\cloth-native`, finished about 03:15 on 2026-10-07): "all path tracing checks
passed", 18,397 frames traced, none failed, 241 s. (The first run failed only "the wind moved the sheet between the two
times": its bar was 10 pixels and the sheet moved 8.2, 25 mm; the bar is now 3 pixels, 9 mm.)

The cloth rules (`tests/ClothRules.Tests.csproj`): all pass. Broken on purpose: the family matched by its exact name, 1
fails (a `TG/Clothes/` variant); the both-sides rule dropped, 3 fail (the shroud, the shield's cover, bed sheets);
"statue" taken off the "not" list, 1 fails.

Source checks (`python tests/source_checks.py`, 81): all pass. Each new one fails when its line is broken: the frame's
`SendClothWind`, the sway dispatch before the emitters, the two switches swapped, `ClothRules.cs` dropped from the test
project.

The plugin compiles (`dotnet build src/TGPathTracing -c Release`, to a scratch folder).

## 6. Files, ABI, bits, FrameConstants

Files:
- New: `src/TGPathTracing/ClothRules.cs`, `native/pathtracing/shaders/pt_sway.hlsl`, `tests/ClothRules.Tests.csproj`,
  `tests/cloth_rules_tests.cs`, this report.
- Changed: `native/pathtracing/include/tgpt_api.h`, `src/scene.cpp`, `src/scene.hpp`, `src/tracer.cpp`,
  `src/extension.cpp`, `src/runtime.hpp`, `shaders/pt_shaders.hpp.in`, `shaders/pt_compose.hlsl` (Surfaces view),
  `CMakeLists.txt` (the `sway:pt_sway` pass), `src/TGPathTracing/SceneCapture.cs`, `HdrpPasses.cs`, `NativeTracer.cs`,
  `tests/pathtracing_gpu_test.cpp`, `tests/source_checks.py`, `docs/TESTING.md`, `CHANGELOG.md`.

ABI: this copy says 0x00011800 (from 0x00011700). The main tree is already at 0x00011800 (texture sharing phase 2), so
the merge needs the next number for this: it adds the export `TGPT_SetClothWind(float time, const TGPT_ClothWind*,
uint32_t count)`, the struct `TGPT_ClothWind` (48 bytes; `NativeClothWind` in C#, size-checked), `TGPT_MAX_CLOTH_WIND`
(256), `TGPT_WIND_VERTEX_PAINT` (1) and `TGPT_WIND_POSITION_UV` (2). The source check's ABI comment line "1.18 (PT-49)"
moves with it.

No new material bit, option bit, instance flag or FrameConstants field. The sway pass reads no frame constants (it runs
inside `GpuScene::record`, before they are filled), only its records through the pass constants (`pass.a`).

## 7. GPU cost of the sway pass

Not measured alone: the test's sheets have 289 vertices, too few to time. Per frame, for each copy near the camera: one
dispatch over its vertices (copy the still pose), one per swaying submesh over its indices (about 150 arithmetic steps
a vertex: three octaves of four hashes), then one BLAS refit. For a market with ten canopies of 5,000 vertices that is
50,000 vertices of arithmetic and ten refits; I expect well under 0.1 ms. The refits dominate. It shows in the F8 report's
"scene" stage. Caps: 96 copies, a million vertices, 60 m. A paused game (the same time) does no work. Memory: 12 bytes a
vertex for the positions, plus one BLAS a copy.

## 8. What the next harness run should show

`-Scenario candle-up -Recipe views`, then `candle`, `npc-day`, `tavern-room`:

- The log: "Cloth in the wind \"...\" (material N): speed ..., intensity ..., world space ..., vertex paint ..." for each
  swaying material, and the native status line "cloth: ... in the wind's pose (... vertices, ... copies)" above 0 where
  a canopy or banner is near.
- candle-up, Surfaces view: the canopy purple (thin cloth), where it was grey. Its holes in the Coverage view where the
  game's are, to the pixel.
- candle-up, the traced picture: the canopy's underside red, lit through from the sky above it (it was near black). If
  the Surfaces view is purple and the underside still black, the light from behind is what is missing, not the match.
- candle: the corner as before (the owner closed its darkness: no light source there).
- npc-day and tavern-room: laundry, towels, sheets and tablecloths now thin cloth (purple); nothing else changes colour
  in the Surfaces view except those.

## 9. Risks

- The UnityPerMaterial layout is read by property order, not by name (the game's DXBC has no names). Every read fits,
  but if a material's switches look wrong in game (cloth swaying where the game's hangs still), swap the two flags.
- `Time.time` is HDRP's time only while the camera animates materials (always in the game's own camera). The F8 capture
  and a paused game keep the last pose.
- A canopy's material set on a renderer by a MaterialPropertyBlock (per-object wind values) would be missed; none seen.
- Refitting keeps the BLAS of the first pose; a sway of tens of centimetres refits well, but a cloth whose copy lives
  for hours traces a little slower. Rebuilding every few hundred frames is the fix if it shows.
- The copies cost memory: positions (12 bytes a vertex) and a BLAS each, at most 96 of them and 1 M vertices.
- The vertex color mask uses the colors as the tracer stores them (8 bits each, as Unity's Color32); a mesh with float
  colors is rounded to 8 bits.
- Swaying vegetation (grass, leaves) is still traced still (PT-49's "vegetation after cloth").
