# PT-04 and PT-40: round things look stepped when traced (the surfaces agent, 2026-10-06)

Written by one of three parallel agents, in its own copy of the tree (the snapshot of 17:00). Built, installed to a scratch
prefix and checked offline (`tgpt_gpu_test` with the staged Agility runtime, `source_checks.py`, the managed build).
Never deployed, never run in the game. "Before" numbers come from the pristine snapshot built the same way.

## The answer in short

Round things looked stepped for four reasons. I changed all four. In the offline check each step is gone.

1. **Glass jars and props the game draws outside its G-buffer used flat faces.** The tracer draws these from its own copy
   of the mesh and shaded them by each triangle's flat direction. A round jar became a prism of flat mirrors. Now they use
   the mesh's own smooth (vertex) normals.
2. **A per-face fade on the direct light, on everything.** Near the shadow line it left each face its own flat light, and
   the face just before the shadow line went dark from edge to edge. So the shadow line ran straight along the mesh. Now
   only skin keeps the fade (the owner approved faces with it).
3. **The built-in denoiser stopped at every face's edge.** It filtered each face of a pot on its own into one tone. This
   is what the Direct, Indirect and Specular debug views showed on the jug: the debug views run this denoiser. Ray
   Reconstruction never did this, and the jug's Ray Reconstruction picture was smooth. NRD keeps its own guide.
4. **Closed see-through-light surfaces (candle wax) went black one face before the shadow line.** Their shadow rays
   started on the light's side of each flat face, which for a closed mesh is inside it. Now only where the smooth normal
   also faces away.

Candle wax is not taken for skin: it is lit like a leaf. Its dark sides and white wicks have other causes (PT-40 below).

## What I confirmed, with numbers

**In the harness's kept pictures (run `20261006-163946`, read only):**
- The glass jar at `refract-jar` is drawn from flat faces: across its body (rows 800-840, columns 1680-1940 of the 4K
  Normals view) its normal jumps 11 times by more than 0.03. Across the jug at `npc-day` (in the G-buffer, HDRP's own
  normals) it never does. A lamp's reflection on the jar is a block one face wide.
- The jug's faces are in the denoised debug views only. Its Normals view is smooth; its Direct, Indirect and Specular
  views show vertical bands; its undenoised traced picture and its Ray Reconstruction picture don't
  (`npc-day/views-0.76-0.80-0.96-1.00.jpg`; my crops: `scratchpad/surf/jug1.jpg`).
- The candle wax is red in the Surfaces view: pixels (158, 48, 48) and (189, 65, 63), green equal to blue. Red is HDRP's
  subsurface scattering with transmission through a **thin** profile. Skin (a solid profile) shows orange (green about
  half of red, blue near 0). So the tracer lights the wax as a leaf. The run's profile list has no wax profile (16 profiles:
  only Human_Eyes_Sclera, Human_Skin, Human_Teeth and Monster_ZombieSkin are thick).

**Offline, a 12-sided cylinder** (1.2 m across, smooth vertex normals, 3.5 m from its camera, the room's lamp 0.9 m from
its left side). Measure: across its middle, the largest jump between neighbouring columns beyond the profile's own slope.
"Before" is the hidden option `TGPT_OPTION_FACETED`, which reproduces the old build exactly (a whole run with it forced
matched the pristine build's every measured line within 2 %).

| check | before | after |
|---|---|---|
| see-through glass: step in its normal (n.x) | 0.518 | 0.001 |
| prop drawn forward: step in its normal | 0.518 | 0.001 |
| in the G-buffer, lit by a lamp: step in the direct light (of its brightest) | 0.141 | 0.020 |
| the same as skin (keeps the fade) | 0.141 | 0.141 |
| closed, alpha-tested, translucent (wax-like): step in the direct light | 0.121 | 0.016 |
| denoised bounced light (built-in denoiser): step | 0.037 | 0.001 |

The direct light before: the profile falls 0.10 to 0.00 in one column at the face edge before the shadow line. After:
0.10, 0.09, 0.08 ... to 0 over 20 columns, as the smooth cosine does. The translucent one without an alpha test was
smooth already (0.013): the black face needs a closed mesh that stops rays from inside.

**Mutation checks** (break it on purpose, the check must fail): (a) the smooth normal never taken, the filter's edge by
faces alone, skin never marked: 4 checks fail (glass 0.518, forward 0.518, skin 0.020, bounced light 0.037). (b) the fade
on everything again and the old translucent rule: 2 checks fail (direct 0.141, wax 0.121). Restored, all pass.

**The whole offline test, before and after:** the same one known failure ("layer hair covers the wall...", the hair
agent's). Every measured line equal within 2 %, except the four "bumpy" grain cases (normal-mapped walls): their error
against the undenoised picture rose 3.5 to 4.6 % over the whole image (bumpy, history 1, moving 0.1069 to 0.1106; bumpy
night 1: 0.1308 to 0.1370; bumpy night 20: 0.1411 to 0.1471; NRD bumpy night: 0.1458 to 0.1512), floor 0.6 to 3.5 %;
grain the same or lower (0.00821 to 0.00752), flicker the same. Cause, measured: it stays with the old filter put back, so
it is the fade's removal: normal-mapped walls keep their full bump shading at grazing light (the fade had flattened
bumps that face the light more than the wall), and the denoiser smooths a little of that detail. HDRP has no such fade.

**Source checks:** 77 tests, the one known failure (`test_path_tracing_no_placed_lights`, the hair agent's).

## Files changed (in `wt/surfaces`)

- `native/pathtracing/shaders/pt_common.hlsli`: `OPTION_FACETED`; `Surface.skin` (from the color byte's alpha, 0.25);
  `terminator_softening` returns 1 unless skin; `Hit.rounded` in `fetch_hit` (the mesh's vertex normals, as
  `caustic_vertex` reads them); `facing_normal`; `shadow_origin(Surface, dir, offset)` (the light's side only where the
  shading normal turns away too).
- `pt_prepare.hlsl`: see-through glass's normal from `rounded` (its geometric normal stays the triangle's); props drawn
  forward take `rounded` (a new line after the hair and eye line, which is untouched); the skin mark.
- `pt_specular.hlsl`: the way out of glass bends by the far side's `rounded`.
- `pt_atrous.hlsl`: an edge is where both the face and the smooth normal turn.
- `pt_di_spatial.hlsl`, `pt_rtxdi.hlsli` (two lines), `pt_compose.hlsl`: the surface form of `shadow_origin`.
- `include/tgpt_api.h`: `TGPT_OPTION_FACETED 33554432u` (the bit I was given). No ABI change.
- `src/TGPathTracing/Settings.cs`: hidden "Smooth round surfaces" (default true; false sends the option).
  `NativeTracer.cs`: `OptionFaceted`. `SceneCapture.cs`: a comment (the normals now serve shading too).
- `tests/pathtracing_gpu_test.cpp`: the cylinder checks (after the throughput block; images 48-50);
  `Object.drawn` so the raster pass can draw a test's own mesh; `TGPT_TEST_SURFACES_ONLY=1` stops after these checks
  (about 20 s).
- `tests/source_checks.py`: new `test_path_tracing_round_surfaces`; `test_path_tracing_shadow_terminator` pins the skin
  rule and the new `shadow_origin` calls (the old sun-origin line changed); the glass exit `refract` pin changed.
- Docs: `CHANGELOG.md` (top section), `docs/PT-REQUESTS.md` (PT-04, PT-40), `docs/PROJECT-CONTEXT.md` 9.7 and 9.12 (the
  bit; next free 67108864), `docs/PT-DETAILS.md` (the switch), this file.

For the merge: `pt_prepare.hlsl` line 1906 of the source checks pins an older hair line (`!passed`) that this snapshot no
longer has; I did not touch that line. The hair agent may rename `behindShown`; my new line uses `eyeSurface` and
`strands` only.

## PT-40, the candle wax

- **Not skin.** Red in the Surfaces view: HDRP's subsurface scattering with transmission through a thin profile. The
  tracer lights it as a leaf: light from behind at half strength, its shadow rays from the light's side. The main
  session's note ("the tracer takes it for skin") read red as orange.
- **Faceted:** (3) and, if the wax mesh is alpha-tested, (4) above. Both fixed.
- **Dark olive-brown:** its Direct view is black on the candles' sides with lit tops and rims. Each candle's light
  ("Point Light_Single", 10 cm radius, under Light_Int_Candle_02_Lit) sits above its own wick, so it lights the top and
  only grazes the sides. The rest is bounced light, warm from the stone, times a cream albedo: olive-brown. The game's
  cream comes from light the tracer leaves out on purpose (no fake lights) and from HDRP's subsurface blur. The tracer runs
  HDRP's subsurface scattering for solid profiles only (`pt_sss.hlsl` `scattering_profile`), so the wax gets no blur.
  Changing that touches grass and leaves (thin profiles too): the owner's call, and the grass agent's area.
- **White wicks:** each wick sits inside its candle light's 10 cm sphere, where the traced light is capped at I / r^2
  from every side; the Specular view shows the wicks white, so these lights make highlights. Not fixed. Next: an F8 at a
  candle (the wick's material and finish, the light's affectSpecular).
- Which profile the wax has is not in the logs; an F8 at a candle tells (the pick's GBuffer2).

## Not changed, and why

- **What reflections and glass show** is still lit by triangle normals (`shade_path`, path vertices). Smoother there
  needs the shading normal for the cosine and the triangle's for the ray's start in `direct_diffuse`, every path vertex:
  a larger change. Look at the near surfaces in game first.
- **Characters** bring no vertex normals (their meshes come as CPU meshes, `TGPT_RegisterSkinnedMesh`, positions each
  frame). They are in the G-buffer, so they shade with HDRP's normals; fixes 2 and 3 apply to them (cloth folds,
  shoulders). Only what is reflected of them stays faceted.
- **NRD's guide** stays the per-face normal (a measured choice: normal maps broke its history). With NRD the bounced
  light can still show faces on coarse props. Ray Reconstruction (the default) has HDRP's smooth normals as its guide.
- **The hollow jar as a lens:** the game's jars are hollow (PROJECT-CONTEXT 9.7: an inner wall), so the ray leaves the
  glass at the inner wall a few millimetres on, bent back by about as much as it was bent in: no strong lens. A solid
  glass object would bend strongly, as real solid glass does. What the
  jar then shows is its own far wall, lit as a solid surface by flat faces: that is PT-35 (one glass crossing followed),
  not changed here. So the jar's far side will still look panelled, while its near side's reflection and bending are
  smooth.

## What to look at in game (the main session)

Deploy, then the harness with the `views` recipe and one A/B step: add
`{"name": "faceted", "set": [{"key": "Smooth round surfaces", "value": "false"}], "traced": true, ...}` after the traced
step, and set it back to `true` in the last step.

- `refract-jar`: Normals view smooth on the jars (was vertical strips); the lamp's reflection a thin vertical streak
  (was a block one face wide). The far wall behind may still show panels (PT-35).
- `glass-jars`: the cups and bottles at the shelf; the bottles' far side no longer in panels.
- `npc-day`: the jug's Direct, Indirect and Specular views smooth (were vertical bands); the shadow line on its body a
  curve. Ray Reconstruction's picture should look as before or smoother.
- `candle`: wax faces in the debug views gone; the wax stays dark (above).
- `metal`, `npc-day` characters: armour and cloth near the shadow line without bands; skin unchanged.
- Normal-mapped walls at grazing light: a little more bump contrast than before (closer to the game). Say if it reads as
  noise.
