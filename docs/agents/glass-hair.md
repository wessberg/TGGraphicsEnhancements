# Bottles that turn black, hair that is dark and striped, armour that lost its silver (PT-73 f, i)

Worker copy: `wt/glasshair` (base `wt/base45`, ABI 0x00012100). Evidence: run `artifacts/harness/20261007-224654`
(read only) and the coordinator's whole GPU test of the base build (`<scratch>/gpu-test-main-235041.txt`).
State: code written and compiled; the offline checks and the whole GPU test are noted at the end as they finish.
**Nothing here was seen in game.** "Fixed" below means: built, and checked offline with the numbers given.

## In short (plain words)

1. **Bottles.** The shelf's bottles are not glass in the game. The game draws them opaque: a glazed bottle with a
   painted label and a cork, all in one texture. The tracer took them for glass because of their NAMES ("Bottle"), and
   since yesterday's glass rewrite glass has no colour of its own. So each bottle became clear glass from cork to
   base, showing the dark wall behind it. Fixed in the plugin: only what the game itself draws see-through is glass.
   For glass the game does draw see-through (the alchemist's jars), the label and lid are back too (the game's alpha
   says how much of each spot is paint).
2. **Hair.** Its colour is read right, and it is not darker than the rest of her (numbers below). The "stripes" are
   two things. (a) The frames in the crop were taken while the camera slid sideways, and the game's own motion blur
   smears every frame along the slide: on a noisy picture that draws lines along the rows. The game's own moving
   frames are blurred the same way. (b) The dark lines that follow the strands are the hair UNDER the top cards, seen
   between their strands; it gets almost no light. One real cause of that is fixed: the underside of a lit card of
   hair was black to every ray but the camera's, so no light went through a card to what is under it.
3. **Armour.** The metal is read right (the yardstick: its colour differs on 0 to 1 % of its pixels, its roughness on
   1 to 3 %). It is brown because the only lights left are candle flames (2500 K, orange). The game lights her with
   its "hero light": a white lamp (4900 K) carried at the camera, with no flame behind it, which the tracer leaves out
   by the owner's "no fake lights" rule. Her cloth, her skin, her hair and the floor shift by the same amount: it is
   the light, not the metal. Nothing changed in code. The owner's call (it was raised before: PT-REQUESTS, Ron's face).
4. **The coordinator's two failing checks** (the whole GPU test of the base build):
   - "clear glass casts no shadow": the test's box of clear glass has no bottom. Since the glass rewrite every glass
     face is the wall of a solid: a ray that enters is dimmed to 1 / 1.5^2 = 0.44 of its light (right, inside glass) and
     gets it back when it leaves. Through the missing bottom it never leaves. The same happens to every glass mesh
     that is one open skin: a window of one quad, a shard, a cup, a cornea. What is behind it comes out bent and at
     0.44 (an eye under its cornea: 0.53). Fixed: glass has an inside only where its mesh closes a volume; an open
     sheet is a thin pane (it reflects as a pane's two faces do, the rest goes straight through).
   - "a character's eye drawn forward is shaded by the tracer, not left to HDRP": the eye is fine (0.0912, traced).
     The check also asked that a PLAIN forward face keeps HDRP's shading, which the glass and eyes migration removed
     on purpose (nothing keeps HDRP's shading while tracing). The check was out of date; it now asks for both traced.

## 1. Bottles and jars

### What the pictures show
`glass-jars/1-game` against `2-traced-none` (crops made from the raw frames: `<scratch>/glasshair-tools/`
`shelf-left.jpg`, `shelf-right.jpg`, `jars-zoom.jpg`): the tall turquoise bottle with the "cider" label, two small
red-brown bottles and a small barrel mug are gone (a darker shape where the tall one stands); the white jar and the
cream bottle are dark bodies with bright speckles.

### The cause, with evidence
- `LogOutput.log` of the run: `Glass: "Mat_Prop_CiderBottle_01" is a surface of camera and secondary rays; index of
  refraction 1.5 (material), absorption per metre 0, 0, 0 (no volume absorption data; clear)`, and the same for
  `Mat_Prop_MeadBottle_01`, `VodkaBottle_01`, `WineBottle_Blackcurrant_01`, `BottleBeer_01`, `AlchBottle` and
  `MI_Window_Glass_01`. None of them has the second line "is transparent in the game".
- The run's audit (`glass-jars/1-game/audit-a.txt`): every one of them is `shader=HDRP/Lit; queue=2225` (2000 for the
  alchemist's bottle) with `_SurfaceType=0`: OPAQUE in the game. `MI_Window_Glass_01` too (a painted window pane).
- `SceneCapture.ReadMaterial` flagged a material as glass by `GlassFinish.IsGlass(m)` alone: a name list ("Bottle",
  "Glass", "Jar", ...), whatever the game does with it.
- `pt_dielectric.hlsli dielectric_follow` (the glass rewrite of 2026-10-07) uses nothing of a glass material's colour:
  no tint, no label, no cork. With "absorption 0" the whole bottle is perfectly clear.
- So an opaque, painted bottle became a clear glass body. A smooth one shows the dark room bent through it (the black
  shape). A rough one (stoneware has a rough finish) scatters at each face, is kept inside by total reflection for many
  bounces and loses light at each (the dark body with speckles). Each such bounce also costs extra ray queries
  (`optical_medium`): not measured here, a note for the speed worker.
- The mug that "nearly vanishes" right of the lantern, and the dark cups: not glass. They stand on the shelf above the
  lantern, in the shelf board's shadow. The game's lantern light casts no shadows (`PointLight_Ambience`, `shadows
  None` in the audit), so in the game the board does not shade them. Real shadow, not a bug of theirs.
- Not the cause: light being cut off behind glass. A light's shadow ray passes clear glass whole (`glass_walk`).

### What was changed
- `src/TGPathTracing/SceneCapture.cs`: glass (`TGPT_MATERIAL_GLASS`) only for what the game itself draws see-through
  (`seeThrough`: a transparent material named like glass, a cornea). A bottle the game draws opaque is traced as the
  opaque Lit surface it is, with its own colour, label, cork and finish. `GlassFinish.ExplainOpaque` logs each once:
  `Glass: "..." is named like glass but opaque in the game (surface type 0, queue ...): traced as the opaque surface
  the game draws, with its own color, label and finish; no ray passes it`.
- `GlassFinish.ReadOptics`: (a) the absorption was asked for under `_AbsorptionDistance`, a name HDRP does not have
  (HDRP's is `_ATDistance`): nothing was ever read. (b) HDRP Lit uses `_Ior`, `_TransmittanceColor` and `_ATDistance`
  only with a refraction model on; every glass material of this run has `_RefractionModel=0` (134 of 134 in the
  audit). So the jars' and the broken glass's `_Ior 1.01` is a number the game never drew with: read as the index, it
  gave glass that reflects 0.0025 % (no highlight at all). Now: the material's `_Ior` and absorption where its shader
  uses them, else the index of the 4 % mirror reflection the game's shader does show (1.5), a cornea's 1.376. The log
  line says which, and names the unused `_Ior`.
- Paint on see-through glass (PT-35's rule, lost in the glass rewrite; `see_through_cover` was still in the code, with
  no caller): the game's alpha is the share of a texel its colour covers (the alchemist's jars: 0.165 on the glass, 1
  on the lids and labels, in the same texture). `pt_common.hlsli glass_cover`; a ray meets that paint with that chance
  (the ray's own draw, a new one each frame, as hair's cover) and the glass's own interface otherwise:
  `pt_dielectric.hlsli` (every ray), `pt_primary.hlsli write_traced` and `pt_specular.hlsl` (the camera's own ray: the
  pixel is the label in that share of the frames, lit like any other surface). A light's ray is stopped by the covered
  share (`glass_color`: until now it passed a wooden lid as "brown glass"). A cornea carries no paint (its base alpha is
  sent as 0), so eyes are as before.

### What this costs (say it to the owner)
- **A bottle the game draws opaque no longer bends light or throws a light pattern (caustic).** It is a painted thing
  now, as in the game. The Caustics row still works for glass the game draws see-through and whose mesh is closed.
  If the owner wants the opaque bottles to be real glass, the game's data does not say how (no thickness, no tint, and
  the label and the glaze are one painted picture): that would be a new design, not a number to read.
- The owner's save `PT caustics-glass` ("a glass bottle on a harbour table"): if that bottle is one the game draws
  opaque, it has no caustic any more. To check in game (the log line "named like glass but opaque" names it).

## 2. The two failing checks of the whole GPU test

### "clear glass casts no shadow" (base build: clear 55.0 against a solid box's 131.4; allowed 24.8)
- The fixture: a character's box (sides and top, no bottom) with material 10, clear glass, index 1.5 by default.
- Before the glass rewrite every ray but the camera's passed clear glass untouched (`base37 pt_common.hlsli` line 1131:
  "sky rays it stopped every time left clear glass half a solid shadow"). Since the rewrite a bounce or sky ray MEETS the
  glass (`alpha_opaque` returns true) and `dielectric_follow` treats the face as the wall of a solid:
  front face: refract in, light times (1/1.5)^2 = 0.444; back face: refract out, light times 2.25, or total reflection.
  For a closed solid the two cancel. This box is open: a ray in through the top and out through the missing bottom keeps
  0.444; a ray up from the floor meets the top from inside "glass" it never entered (times 2.25 inside a cone of 42
  degrees, mirrored back outside it). (The sun's own shadow rays are not the cause: `glass_walk` and the caustic
  solver are the same as when the check passed, `caustic_seed` line for line against base37.)
- The same in the game: any glass mesh that is one open skin. What lies behind it was seen bent once and at 0.44 of its
  light; an eye behind a cornea (1.376) at 0.53.
- The rule now: **glass has an inside only where its mesh closes a volume.**
  - `scene.cpp mark_closed` (at mesh conversion, on the worker thread): a submesh is closed when every edge of each of
    its triangles is shared with exactly one other triangle of the mesh running the other way. Vertices at the same
    place count as one (seams repeat them). The flag goes in the submesh record's spare word
    (`GeometryRecordGpu.pad0` is now `flags`, bit 0; same size, nothing public changes).
  - `pt_common.hlsli glass_solid`, `pt_dielectric.hlsli`: a closed mesh is a solid as before. An open sheet is a thin
    pane: it reflects 2F / (1 + F) (a pane's two faces, with the light going to and fro between them: 7.7 % at 1.5),
    the rest goes straight on, unbent and undimmed; no point is "inside" it (`optical_medium`); the caustic solver
    leaves it alone (`caustic_through`: its light comes straight through).
- A character's mesh comes without positions (they arrive each frame), so its vertices are told apart by their indices
  alone: a character's glass with repeated vertices along a seam counts as open (a thin pane). That is the safe side
  (no light lost), and right for a cornea drawn as one shell. To know it exactly the plugin would have to hand over a
  rest pose (`CharacterCapture` registers indices and UVs only): not done.

### "a character's eye drawn forward is shaded by the tracer, not left to HDRP"
- Printed by the base build: `a plain material 0.0806 (HDRP's shading kept), an eye 0.0912 (traced)`; the check was
  `plain<0.005 && eyeFace>0.02`. The eye passes. The plain face is 0.0806 because the glass and eyes migration made every
  forward surface the camera's ray finds the tracer's (its report: "Forward opaque and unlit surfaces ... shade from
  their traced material, with no retained HDRP colour share"; the owner's ruling "not to keep shading of anything in
  HDRP when PT is on"). The check still asked for the old behaviour. Changed to `plain>0.02 && eyeFace>0.02`, renamed
  "a character's face drawn forward is shaded by the tracer, not left to HDRP: an eye and a plain material alike"
  (`tests/source_checks.py` names it too).
- Her dark eyes in the frames: not this check. What I can say without the game: (1) the cornea rule above (if her
  cornea's mesh is an open shell, or closes around the eye, the eye was seen at 0.53: fixed for the open case; a cornea
  that closes around the whole eye still dims it, which is physics for a thing inside glass but not what the game's
  painted iris expects: open point for the eyes' owner); (2) the candle light and the missing hero light, as for the
  rest of her (section 4); (3) her fringe and lids really shade the sockets from flames that stand to the side.

## 3. Hair

### The stripes
- `hair-moving/6-traced-none-slide`: f00 is sharp, f01 and later are smeared along the rows; the game's own frames
  `4-game-slide` f01, f02 are smeared the same way and f00 is sharp (`<scratch>/glasshair-tools/game-zoom.jpg`,
  `hair-zoom.jpg`, `face-zoom.jpg`). The audit has `camera|frame.MotionBlur true`. The camera moves 2.2 cm a frame
  with her 1 m away: 21 traced pixels a frame, which is the length of the streaks. So: HDRP's motion blur, laid over a
  picture that has noise in it. In a frame from a still camera the noise is round (`jars-zoom.jpg`).
  For the harness: switch motion blur off for the slide steps, or judge moving frames by f00 only.
- In the sharp frame (`h-both.jpg`) the lines that remain run ALONG the strands (the hair lies sideways from the parting
  at the top, as in the game's frame): dark gaps between clumps of strands, where the camera's ray passes a card and
  stops at the card or the scalp under it, which is nearly unlit. In the game most of the tavern's lights cast no
  shadows at all (`shadows None` in the audit), so the hair under hair is as bright as the hair on top and the gaps
  do not show.
- "Coarse": the run traced at 1924x1084 and showed it at 3840x2160. With Denoising None nothing upscales: TG Better
  Upscaling logs "enlarged without upscaling: TG Path Tracing wrote no Ray Reconstruction guides this frame". And the
  tracer reads every texture one level sharper than its pixels (`HdrpPasses.cs` Forward[1] = log2(1924/3840) = -1, an
  upscaler's bias) although no upscaler gathers the frames. So a None picture at half size is a half-size picture with
  aliased strands, enlarged. For judging without a denoiser: run those steps at 100 % render scale.

### The tone
- The yardstick names her hair `FirstPass_Mat_H_f001(Clone)`; the log has its colours: root (0.71, 0.63, 0.51), end
  (0.87, 0.72, 0.50): light. Measured in the two frames (linear light, patches on her; `4-game-slide/f00` against
  `6-traced-none-slide/f00`, the camera's 4 cm between them taken into account):

  | patch | traced / game, brightness | green over red, game -> traced | blue over red, game -> traced |
  |---|---|---|---|
  | hair, right of the parting | 0.93 | 0.27 -> 0.21 | 0.08 -> 0.05 |
  | hair, left of the parting | 0.63 | 0.26 -> 0.22 | 0.08 -> 0.06 |
  | armour, chest | 0.57 | 0.63 -> 0.40 | 0.40 -> 0.12 |
  | armour, shoulder plate | 0.59 | 0.55 -> 0.38 | 0.31 -> 0.12 |
  | shoulder cloth | 0.48 | 0.54 -> 0.37 | 0.37 -> 0.12 |
  | cheek | 0.45 | 0.32 -> 0.15 | 0.17 -> 0.05 |

  The hair is the part of her that lost the LEAST. Everything on her is darker and redder by about the same factor:
  the light changed, not the hair (section 4).

### What was changed
- `pt_common.hlsli hit_material`: a card of hair is lit through from behind where ANY ray meets it (a bounce, a path,
  a mirror), as the camera's own pixel of it has been (`CLOTH_TRANSMISSION`, 0.3, near RealHair's own
  `_TransmittanceMultiplier` 0.33). It was 0 there: the underside of a lit card was black to the bounce rays of the
  hair and scalp under it, so no light went through a card to what is under it.
- Not changed: a light's own ray is still stopped by a card as much as its strands cover (no light straight through a
  strand). So the hair under hair gets the lit-through light only as bounced light. Real light hair is brighter
  inside than this (light scatters from fibre to fibre many times); cards cannot do that. An open design point.

## 4. Armour (and the tone of everything near the player)

- The yardstick, same run: `Mat_ChFL_TavernAlisa_Torso (TG/Character/Kandra_Lit, its mask map traced)`: of its pixels,
  another colour 0 to 1 %, another roughness 1 to 3 %, another normal 19 to 21 % (a character's mesh brings no
  normals; that turns reflections a little, not their colour). So metallic and smoothness are read as HDRP has them.
- The lights (the audit of the same tavern): at the camera `Misc/HeroLight/Light_HeroLight_Indoor`: point, on,
  intensity 8192, dimmer 16, 4900 K, range 6 m, no shadows. The log: `"Light_HeroLight_Indoor" under "HeroLight" left
  out (placed to fake light ...; color 0.42/0.32/0.25)`. What is traced near her: candle flames, logged colour
  0.70/0.25/0.05 (2500 K). Green over red of the light: 0.76 -> 0.36; blue over red: 0.60 -> 0.07.
- The table above: armour, cloth and skin lose the same share of green and of blue; the floor too (0.75 -> 0.42).
  A metal shows what it reflects, and what is there to reflect is a dark room lit orange.
- Nothing to fix in the metal. The decision is the owner's: the game's look of anything within 6 m of the player is
  that white lamp; without it a room lit by flames is orange and darker. (The lantern's brightness and the exposure
  are other workers'.)
- Offline check for "a metal ball in a known room reflects it": none added; the whole test's mirror checks stand
  (their lines are quoted at the end once it has run).

## 5. In game: what the coordinator should see, and where to look

- `glass-jars@quick` (still the stand-in save `AutoSave_0`): the shelf's tall bottle turquoise with its tan "cider"
  label and cork, the two small ones red-brown, the white jar and the cream bottle pale: as in the game's own frame,
  only lit by the lantern. In `LogOutput.log`: one line `named like glass but opaque` for each of cider, mead, vodka,
  wine, beer, the alchemist's bottle and `MI_Window_Glass_01`; no `Glass: ... is a surface of camera and secondary
  rays` line for them. The yardstick's "another colour" for those materials should be near 0.
- The measured region sat on the chair. In my copy `tests/ingame/scenarios.json`: `glass-jars` roi
  `[0.51, 0.24, 0.83, 0.40]` (the shelf, for the stand-in save's view; left, top, right, bottom). The bottles are
  small there (the tall one 1.6 % of the screen's width): a better place is 1 to 1.5 m in front of the shelf, the
  shelf in the middle. The real `PT glass-jars` save (the alchemist's see-through jars) does not exist yet; that is
  the scene for paint on glass (labels, lids) and for thin panes.
- `hair-moving`: roi `[0.385, 0.60, 0.50, 0.76]` (her hair in the still view; it sat on the ceiling). Her armour:
  `[0.36, 0.82, 0.55, 1.0]`, cut by the screen's edge: tilt the camera 10 to 15 degrees down, or step back half a
  metre, and both fit. During the slide her head moves from x 0.46-0.56 to 0.34-0.45.
- Her hair should show fewer black lines between strands in a still frame with no denoiser (the hair under it is lit
  a little now). It will still be orange: candle light. Judge stripes on a frame with the camera still, or with
  motion blur off.

## 6. Workarounds met (removed or not)

| Where | What | State |
|---|---|---|
| `SceneCapture.ReadMaterial` | glass by a material's name, whatever the game draws | removed for opaque materials; the name list still picks, among TRANSPARENT materials, which are glass and which stay invisible to rays (particles and other transparents are another job) |
| `GlassFinish.ReadOptics` | `_AbsorptionDistance` (no such property); `_Ior` read although the shader ignores it | fixed |
| `pt_common.hlsli see_through_cover` | PT-35's cover, left without a caller by the glass rewrite | in use again |
| `glass_color`, see-through branch | paint let through as coloured glass for shadow rays | replaced: the covered share stops the light |
| `pt_dielectric.hlsli` | every glass face the wall of a solid, whatever the mesh | fixed (closed meshes only) |
| `glass_color`'s other branch, `glass_walk`'s square root, `emission_coverage`'s glass branch | glass's base colour as a filter for shadow rays ("a closed object's two crossings its colour") | NOT removed. The plugin no longer sends such glass (glass without the see-through bit); only the offline test's old fixtures do (materials 9, 10, the green cube and sheet: about a dozen checks). Removing it means rewriting those checks to absorb by distance. For the coordinator to assign |
| `glass_walk`, `transmittance` | a light's ray through glass takes no Fresnel loss and no absorption by distance (radiance rays do) | not mine; open (no material of the game has absorption today: all have refraction off) |
| `dielectric_follow` | a rough glass face loses light at every bounce (one scattering only) | open: rough solid glass comes out too dark; thin panes pass their unreflected light straight (a frosted pane of one quad is clear) |
| `pt_primary.hlsli`, `pt_prepare.hlsl`, now `hit_material` | hair lit through by a fixed 0.3 where RealHair has `_TransmittanceMultiplier` | not removed (the material's own number is not in the record) |
| `HdrpPasses.cs` Forward[1] | an upscaler's texture bias sent whatever upscales | open (see Hair, "coarse") |
| `SceneLights.cs` | HeroLight left out as a fake light | the owner's ruling; raised again above |

## 7. Files changed (against base45)

Plugin: `src/TGPathTracing/GlassFinish.cs`, `SceneCapture.cs`.
Native: `native/pathtracing/src/scene.cpp`, `scene.hpp`; shaders `pt_common.hlsli`, `pt_dielectric.hlsli`,
`pt_primary.hlsli`, `pt_specular.hlsl`.
Tests: `tests/pathtracing_glass_checks.inl` (four new checks; its glass fixture now has alpha 0: "no paint"),
`tests/pathtracing_gpu_test.cpp` (the eye check's expectation; the pane's sun reflection is a pane's, 2F/(1+F)),
`tests/source_checks.py` (one new test, three lines updated; 94 tests pass), `tests/ingame/scenarios.json` (two rois).
Shared with other workers: `pt_common.hlsli`, `SceneCapture.cs`, `scene.cpp`, `pathtracing_gpu_test.cpp`,
`source_checks.py` (small, local edits).

**Structs and ABI:** no public struct, export or option bit changed; ABI stays 0x00012100. Internal only:
`GeometryRecordGpu.pad0` and the shader's `GeometryRecord.pad0` are now `flags` (bit 0: closed); same size (16 bytes).
The plugin sends a cornea's base alpha as 0 (it meant nothing for glass before).

## 8. Checks and the whole GPU test

Build: `%TEMP%\tgb\glasshair-build` (NMake, Release, `-DTGPT_WITH_NRD=OFF`); tracer DLL SHA-256 `9DB03AD056E5...`.
Plugin (net472) and the harness plugin compile with 0 warnings. `python tests/source_checks.py`: 94 tests, OK.
`python tests/harness_analyze_test.py`: 6 tests, OK.

### The four new checks (`tests/pathtracing_glass_checks.inl`; 320x180, Denoising None, no radiance cache, 48 or 64 frames)
"Before" is the SAME test program run on the base build's tracer (`build/native`, 54165A49E5FD, the one in the game);
"after" on this worker's. Logs: `%TEMP%\tgb\glasshair-before\gpu.log`, `glasshair-after2\gpu.log`; pictures beside them.

| Check | Before (base tracer) | After | Expected |
|---|---|---|---|
| paint on see-through glass: a sheet wholly painted green, lit by a lamp, in front of the glowing backdrop (0.100, 0.150, 0.200) | 0.100, 0.150, 0.200: the backdrop, the paint is not there at all | 0.008, 0.055, 0.008 (lit green paint) | its own lit colour |
| the same, half painted (alpha 0.5) | 0.100, 0.150, 0.200 | 0.0542, 0.1025, 0.1043 | half of each: 0.0541, 0.1023, 0.1041 |
| an open sheet of glass, index 1.5: the backdrop seen through it | 0.427 of it | 0.927 of it | 0.923 (a pane's two faces) |
| an eye under an open shell (a cornea's index, 1.376) against the bare eye | 0.517 | 0.953 | 0.951 |
| a floor under a lit card of hair, against the open floor (an opaque card: 0.000) | 0.000 | 0.128 | 0.129 (the card's lit-through share 0.3 x its colour 0.8 x what the floor sees of it) |

The base numbers are what the causes predict: 0.427 = 1/1.5^2 x 0.96 (entered, never left); 0.517 = 1/1.376^2 x 0.975.
The eleven older optical checks pass unchanged (same digits as on the base tracer, the jar's apart: see below).

- The jar check's numbers moved in the first run (red 0.536 -> 0.556, rim 0.133 -> 0.171): not the optics. Its mesh is
  registered just before it is measured and built on a worker thread; two of the 48 frames had no jar yet
  ((46 x 0.536 + 2 x 1) / 48 = 0.555; rim 0.169; green 0.889: all three match). The check now runs 12 frames first.
- **Start-up, for the PT-74 worker:** the first run of a new tracer DLL failed at "the passes were made": the test waits
  180 s and the passes were not made yet (a second run: 154.7 s; the base DLL, whose passes the driver had cached:
  3.3 s). Not caused by this job; it is the 100 s start the owner reported, worse on a busy machine.

### The whole GPU test, once

(running; its FAIL lines come here)
