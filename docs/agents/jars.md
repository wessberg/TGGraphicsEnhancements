# PT-35: why the glass jars are not see-through, and the fix (the jars agent, 2026-10-07)

Agent report. Built and tested offline in a copy of the tree (`wt\jars`, base `wt\base19`). Nothing ran in the game,
nothing was deployed or committed. A first agent did the digging and most of the shader work and was cut off at 05:15;
a second one checked its findings, finished the fix, wrote the tests and this report.

## In three lines

The jars were taken for see-through glass and seen through all along. But what showed through them was the tracer's
own flat copy of the wall (no stones), and the glass's own color was missing. Now the wall behind is shaded from
HDRP's own picture of it, and the glass is blended as the game blends it. Passes offline; not seen in game yet.

## Findings

Evidence: the state audit of run `20261006-212905` (`refract-jar/1-game/audit-a.txt`), that run's `LogOutput.log`, the
views run `20261006-172608` (Materials, Specular and no-denoiser frames), and the jar's own mesh and textures read from
the game's bundles (`scratchpad\jarscan`: `Mesh_Prop_Jars_01`, `Tex_Prop_Jars_01_Albedo`).

1. **The jars are taken for see-through glass.** Their material `Mat_Prop_Jars_01` is HDRP/Lit, transparent
   (`_SurfaceType 1`), blend mode Alpha with Preserve Specular Lighting (`_SrcBlend 1`, `_DstBlend 10`), no refraction
   model (`_RefractionModel 0`, no `_REFRACTION` keyword), no depth write, no transparent depth pre- or postpass,
   single-sided (cull back). Its name holds the word "Jar", so GlassFinish takes it, and the run's log says so:
   `Glass: "Mat_Prop_Jars_01" (HDRP/Lit) is transparent in the game: drawn by the tracer while tracing ... HDRP's own
   passes hidden`. So `TGPT_MATERIAL_SEE_THROUGH` was set and HDRP drew nothing of the jars while tracing.
2. **The "sheet" test in pt_specular was true for them.** The jars write no depth, so HDRP's depth at their pixels is
   the wall behind, 25 cm or more past the glass; the test asks for half a centimetre. The see-through path ran.
3. **The jar is a shell, and the walk through it worked.** The mesh is 27 cm across, its walls 4 to 5.5 mm thick, and a
   straight line crosses it four times: front face, back face (the inside of the near wall), front face (the inside of
   the far wall), back face. `behind_sheets` crossed those and reached the wall.
4. **What it showed of the wall was the tracer's own copy of it, which is flat.** The Crypt's stone wall is drawn by
   Entities Graphics; the tracer's copy of its material is a 64x64 tile with no normal map (the Materials view of run
   172608: grey blocks a few centimetres across, no stones). HDRP draws every stone, from its full texture and normal
   map. Seen through the glass, the traced wall came out a flat brown with faint squares: the "milky brown". The squares
   the PT-35 note put down to the right jar's own 64x64 tile are most likely the wall's tile seen through: the audit
   lists one jar material, so both jars read the same texture tile, and in run 212905 the left jar, whose stains are
   sharp, shows faint squares too.
5. **Not the denoiser, not the composite.** The no-denoiser frame of run 172608 shows the same flat inside. The composite
   puts the traced specular light (which holds the view through the glass) in place of HDRP's pixel, and HDRP's own jar
   passes were hidden, so nothing covers it.
6. **The texture's alpha says see-through, and the tint was the wrong model.** `Tex_Prop_Jars_01_Albedo` (1024x1024):
   half its texels have alpha 0.165 (the glass), the rest 1.0 (the lid and rim, wood). Base color (0.76, 0.70, 0.46) with
   alpha 1. HDRP blends it as `alpha x its lit color + its specular + (1 - alpha) x what is behind`: 83 % of the wall
   untinted plus 17 % of a lit pale olive, which is the greenish film in the game's picture. The tracer took it as a
   filter (`behind x lerp(1, color, alpha)`): no light of its own color, so the jar lost its green, and the lid (alpha 1)
   was brown glass the wall showed through, not wood.

So: the jars were seen through all along, but what was seen was a wall without its stones, and the glass's own color
was missing.

## What changed

1. **The wall behind the glass is HDRP's.** Where a see-through view ends on the surface HDRP drew at that pixel
   (projected to the screen, within 3 cm + 1 % of HDRP's depth there, drawn deferred), that surface is shaded with
   HDRP's GBuffer color and normal (`hdrp_material_at`), its light still the tracer's (one path, as before). Elsewhere
   (behind a jar's bent edge, a forward-drawn surface, off screen) the tracer's own copy, as before.
2. **The glass is blended as HDRP blends it.** pt_prepare stores its color (base x texture) as the pixel's albedo and its
   alpha (the cover) in the specular color's green. pt_specular adds `cover x color x light` (the light: the DI and GI
   passes' raw direct and bounced light at the glass pixel) and lets `(1 - F) x (1 - cover)` of what is behind through,
   untinted. In front of the sky pt_compose hands the composite the same `(1 - F) x (1 - cover)`. The old filter
   (`behind x lerp(1, color, alpha)`) is gone for this glass; glass the game draws opaque (bottles) keeps its tint.
3. **A jar is crossed wall by wall** (`see_through_shell`, replacing `behind_sheets` and the "sheet" test): into a wall at
   a front face, out at its back face if one lies within 3 cm (else straight on, as a one-face pane), bent in and out by
   each face's smooth normal; each further front face (the far wall's inside) lays its cover over what is behind and
   adds its color lit by the near face's light. A thin wall bends what is behind it only near the outline.
4. **A log line per glass material** (cheap, once per name): `Glass: "<name>" (<shader>) is transparent in the game:
   taken for see-through glass ... base color alpha <a> (times its texture's), its texture's tile 512x512 (a detail
   tile)` (or `64x64 (no detail tile left: its stains in blocks)`); and for a transparent material named like glass
   that rays will not see: `... is transparent and named like glass, but not drawn as see-through glass: <why>; invisible
   to rays, HDRP draws it` (why: unlit shader, the NoRender shader, a "not glass" word in its name, or no rule took it).

The first agent wrote 2 and 3 and `hdrp_material_at`, and started the log lines; the second wired the log lines into
SceneCapture (the build was broken: `AddSeeThrough` had a new argument and no caller passed it), wrote the tests, fixed
the existing see-through test material (alpha 1 is opaque color under HDRP's blending; a clear pane is alpha 0), and
updated three source checks that quoted the old code.

## Files

- `native/pathtracing/shaders/pt_common.hlsli`: `Surface.seeThrough`, `Surface.cover` (load_surface), `see_through_cover`
  (was `see_through_tint`), `specular_guide_albedo`, `hdrp_material_at`, `g_firstKnown` / `g_firstMaterial` in
  `shade_path`.
- `native/pathtracing/shaders/pt_prepare.hlsl`: the see-through glass pixel's color, cover and bits.
- `native/pathtracing/shaders/pt_specular.hlsl`: `see_through_shell`, the cover light, the HDRP material behind.
- `native/pathtracing/shaders/pt_compose.hlsl`: the sky behind see-through glass by `1 - cover`.
- `src/TGPathTracing/GlassFinish.cs`, `src/TGPathTracing/SceneCapture.cs`: the log lines.
- `tests/pathtracing_gpu_test.cpp`: the Crypt's jar check (new), the clear test cylinder at alpha 0.
- `tests/source_checks.py`: `test_path_tracing_glass_is_a_shell` rewritten, `test_path_tracing_jars_as_the_game_blends_them`
  new, two quoted lines updated (85 checks).
- `docs/PT-DETAILS.md` ("See-through glass"), `CHANGELOG.md` (top of the path tracing part), this file.

## ABI and bits

No ABI change: `tgpt_api.h`, the material and option bits and FrameConstants are untouched. Internal to the shaders
only: in the per-pixel material texture pt_prepare writes, bits 2 and 16 together now mean see-through glass (bit 16
alone stays thin skin; no glass in the GBuffer sets 16), with the cover in the specular color's green byte and its
color in the albedo. `GlassFinish.AddSeeThrough` takes two more arguments (managed, internal).

## Tests

All on the GPU test (`tgpt_gpu_test`, built in `%TEMP%\tgb\jars-build`), run only while the GPU was free.

- The new check, the Crypt's case: a copy of the jar (27 cm, walls 5 mm, four crossings, smooth normals; the audit's
  material: base color (0.76, 0.70, 0.46) alpha 1, see-through glass; its texture's glass texels as the game's mean,
  sRGB (142, 162, 166) at alpha 45/255, through a detail tile) 5 cm in front of a wall HDRP draws checkered (5 cm
  squares) and the tracer plain grey; no lamp, no denoiser, 16 frames. Measured on the middle three fifths of the jar.

  | Build | Columns follow the bare wall | Light, of the bare wall's | Lid texture (alpha 1): follows |
  |---|---|---|---|
  | The fix | 0.97 (pass, > 0.8) | 0.71 (pass, 0.55 to 0.9) | -0.08 (pass, < 0.5) |
  | Before (base19's shaders) | -0.01 (fail) | 1.07 (fail: brighter than the wall, the "milky" look) | -0.09 |
  | Mutant 1: never HDRP's material behind | -0.11 (fail) | 1.11 (fail) | -0.08 |
  | Mutant 2: the alpha ignored | 0.97 | 0.83 | 0.82 (fail; the green pane checks fail too: x1.01, x0.99) |

  The light: two layers of 17.5 % cover less the Fresnel term leave 0.65 of the wall; the glass's own lit color and its
  reflection add the rest. Contrast through it 0.53 of the bare wall's. The lid check does not tell the old build
  apart (there the wall seen through was plain anyway); it guards the alpha (mutant 2).
- The existing shell check (a one-face cylinder 5 cm before the box's checker), with its material now clear (alpha 0):
  follows 1.00, contrast 0.96. With alpha 1 it failed (0.06), correctly: alpha 1 is opaque color to HDRP.
- The green pane at alpha 0.5: before the back wall green against red x1.94 (old model x1.66), against the sky x1.42,
  red x0.64 (old x1.61, x0.62): both checks still pass.
- Whole suite: all path tracing checks passed (`jars-gpu-fix2.txt`). `python tests\source_checks.py`: 85 pass.
  `dotnet build src\TGPathTracing -c Release` to a scratch folder: 0 errors.

## The next harness run

`.\tools\harness\Run-PtHarness.ps1 -Scenario refract-jar -Recipe views`, with this build deployed:

- Traced (RR and NRD): both jars show the wall's stones through them, at about two thirds of the wall's brightness,
  with a faint olive film and the stains darker; their outlines bend what is behind a little. The lids look like dull
  wood with a glint, not brown glass. No faint squares inside the jars.
- Albedo view: the jars show their own color (pale olive, stains darker), not near-white.
- Specular view and the no-denoiser frame: the stones inside the jars (noisier than the wall around them).
- Log: `Glass: "Mat_Prop_Jars_01" (HDRP/Lit) is transparent in the game: taken for see-through glass, ... base color
  alpha 1 (times its texture's), its texture's tile 512x512 (a detail tile)`. If it says 64x64, the jar's stains will
  be blocky (detail tiles ran out).
- If the jars still look flat: the stand-in wall the tracer hits lies more than 3 cm + 1 % off HDRP's depth there, and
  `hdrp_material_at` falls back to the tracer's copy. The Materials view would not show that; a quick check is the
  no-denoiser frame (flat inside the jars = fallback).

## Risks

- Noise: the view through the glass is one path a pixel (as before), not the ReSTIR-lit wall around it. Inside the
  jars it is noisier; Ray Reconstruction has to clean it and may soften the stones a little. The built-in denoiser on the
  NRD setting filters the specular signal and may blur them more.
- Ray Reconstruction's guides on see-through glass still say `(1 - F) x HDRP's albedo there` for the light let through;
  the cover's own color (17 %) is not in them.
- The cover light is the near face's raw direct and bounced light, taken for the far face's cover too.
- `hdrp_material_at` gives the diffuse color HDRP stores; a metal seen through glass loses its reflectance there.
- Every see-through glass now follows HDRP's blending: a tinted window lets through `1 - alpha` untinted plus its lit
  color, where the old filter tinted what was behind. Panes whose alpha is high will look more like colored film than
  tinted glass. That is what HDRP draws; the owner judges it in game. HDRP's refraction glass (`_REFRACTION`, the
  crafting bottle) also tints by `_TransmittanceColor`, which the plugin does not read.
- Lids (alpha 1) are shaded as a near-mirror (the glass finish: roughness 0.03), wood with a glint.
- The thin-shell walk looks 3 cm for a wall's other side; thicker glass (a solid lump drawn transparent) is gone
  through straight.
