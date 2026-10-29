# Glass that looks like glass: clear jars, thin rims, the tint of coloured glass (PT-13, PT-35, PT-76)

Worker copy: `wt/glass3` (base `wt/base65`). Build: `%TEMP%\tgb\glass3` (NMake, Release, `-DTGPT_WITH_NRD=OFF`).
**Nothing here was seen in game.** "Built" and "checked offline" are said where they hold; the rest is reading and numbers.

## State (2026-10-09 11:10): ready to merge

- Step 1 (why the jars are cloudy): done, with numbers from the game's own mesh and material and an offline render.
- Step 2 (the trial): the hidden setting `Advanced / Glass trial`, 0 by default. **7 is the one to try**; the recipe
  `glass-trial` is in `tests/ingame/scenarios.json`. Checked offline. Not seen in game: that is the next step.
- Step 3 (the tint, PT-76): built as the rule, no switch. Two of the four failing checks pass again in my run, the
  third could not run here (whole test only) and should pass, the fourth is not the tint and is still open.
- Builds: three; the copy holds build 3's source. Its 40 shader headers and both binaries are newer than every
  shader source. `python tests/source_checks.py`: 102 of 102. The plugin (net472) compiles with 0 warnings
  (`%TEMP%\tgb\glass3-plugin`). The glass fixtures' own mode (`TGPT_TEST_GLASS_ONLY`) on build 3: every glass check
  and every one of the room's coloured-glass checks passes (`%TEMP%\tgb\glass3-build3\gpu.log`, pictures beside it).
- NOT run by me, by the rule (whole test only): everything in "What the whole test will tell" below.
- No ABI, struct size, export or option bit changed. Two reserved words of `TGPT_Material` and one unused float of
  `TGPT_Frame` got a meaning (below). `tools/Build-PtShaders.ps1` in the copy is the coordinator's own replacement
  of 10:00, not this job's.

## 1. Why the jars are cloudy with dark blotches (no denoiser)

### What a camera ray meets on a jar

The Crypt's jar (`Mesh_Prop_Jars_01`, 27 cm across, 49 cm tall, walls 4 to 5.5 mm, material `Mat_Prop_Jars_01`) is ONE
submesh with two pieces (measured on the game's mesh, `<scratch>\glass3-tools\jar_closed.py`, by the tracer's own rule
`scene.cpp mark_closed`): the body, 764 triangles, closed; and the lid, 325 triangles, open (29 edges lack their twin).
`pt_common.hlsli glass_solid` asked whether the WHOLE submesh closes a volume. It does not, because of the lid. So the
tracer takes the whole jar for open sheets, and a ray through the middle of a jar meets four "thin panes": the outer
and the inner face of the near wall, the inner and the outer face of the far wall. At each one
`pt_dielectric.hlsli dielectric_follow`:

1. stops at paint with the chance the game's alpha gives the texel (`glass_cover`), whichever side it comes from;
2. else reflects as a whole pane does (`2F / (1 + F)`: 7.7 % at 1.5, more at a slant), into the material's rough lobe;
3. else goes straight on: not bent, not blurred (a thin pane).

### The numbers (the game's own material and textures: `<scratch>\jarscan`, `glass3-tools\jar_stats.py`)

| What | The game's value | What the tracer makes of it |
|---|---|---|
| Alpha on the glass texels | 0.175 (59 % of the texture; the lid 1.0, 29 %; stains between, 12 %) | paint at 4 faces. The game draws front faces only (`_CullMode 2`, no back-face pass): 2 layers |
| Faces a wall has | one sheet of glass 5 mm thick: two boundaries, 4 % each | two "panes" of 7.7 % each, a wall: 4 reflections a wall |
| Smoothness on the glass texels | mask map 0.93 mean (median 0.996) remapped to 0 .. 0.640: 0.60 to 0.64 | roughness 0.36 to 0.40: the reflections are a broad, milky lobe (the game's highlight has the same roughness) |
| Refraction | `_RefractionModel 0`: the game's shader neither bends nor blurs what is behind | nothing bent either: no rim, no thicker glass at the outline |

Per ray through the middle of a jar, by those chances (paint 17.5 %, else a reflection, per face):

| | sees the wall behind | stops on the jar's own film | reflected by the glass |
|---|---|---|---|
| The tracer today (4 painted faces, 4 panes) | **34 %** | **49 %** | 18 % |
| The game (2 layers of alpha 0.175, its highlight on top) | 68 % | 32 % | (its highlight) |
| Trial 2 (paint on the 2 drawn faces) | 49 % | 30 % | 20 % |
| Trial 7 (and the body a solid shell: 4 boundaries of 4 %) | 58 % | 30 % | 12 % |

So the cloud: half of every jar pixel is the jar's own lit film (a third in the game) and a sixth more is a rough
reflection of the room. Only a third is the wall. The dark blotches: the film where it is in shadow (the lamp is traced
with shadows; the game's lights there cast none), and the jar's own shadow on the wall behind it, seen through it: a
light's ray is stopped by the film at all four faces too (`glass_walk`: 0.825^4 = 46 % gets through; by the game's two
layers 68 %).

A second cause waits behind the first. Once the body is taken for the solid it is (it should be: that is what bends
the view at the outline and makes the thin rims), the walk through solid glass bends each ray at a random microfacet of
the material's roughness (`ggx_sample` about the normal-mapped normal, at each of four boundaries): 0.36 to 0.40
spreads the view some 7 degrees, 3 to 4 cm on a wall 27 cm behind, and the normal map (6.7 degrees of tilt on
average, 11.5 at the 90th percentile) wobbles it by about as much. That is frosted, embossed glass. The game's
shader, with no refraction model, uses the roughness and the normal map for the highlight alone. So bits 1 and 4 of
the trial go together.

### Measured offline (the glass fixtures' own mode, build 3; `tests/pathtracing_glass_checks.inl`, "the Crypt's jar")

A shell of the jar's proportions (27 cm, walls 5.4 mm), roughness 0.38, alpha 0.175, index 1.5, in front of glowing
bars 4 cm apart, 27 cm behind it; no lamp, so the film is black; over the jar's middle, against the bare bars.
Pictures: `%TEMP%\tgb\glass3-build3\glass-11*.png`.

| The jar | Glass trial | the bars' light that comes through | the bars' contrast kept |
|---|---|---|---|
| a closed shell alone (a solid to the walk already) | 0 | 0.384 | **0.069**: the bars are gone, a grey frosted slab |
| | 1 (the view crosses the smooth face) | 0.403 | 0.390 |
| | 2 (the film on the drawn faces) | 0.568 | 0.064 |
| | 3 | 0.600 | 0.593 |
| **with an open lid in its submesh: the game's jar** | **0 (open sheets: today)** | **0.335** | **0.330** |
| | 2 (the film once a wall) | 0.497 | 0.486 |
| | **7 (and its body a solid, seen through clear)** | **0.600** | **0.593** |

The light is what the table of chances predicts (34 %, 49 %, 58 %). At 7 the bars also bend at the jar's outline
(the rim); at 0 they run straight through it.

Also read on the way: the material carries `_TransmittanceColor (1, 0.996, 0.711)`, `_ATDistance 0.25` and `_Ior 1.01`,
all unused by the game (refraction model off), so they are not read (the glass-hair job's rule). `_MetallicRemapMax`
0.888 with a mask map of 0.48 on the glass texels: the film is shaded 42 % metallic. Not looked into.

## 2. The trial: hidden setting `Advanced / Glass trial` (an integer, 0 by default: everything as before)

A bit each; add them up.

| Bit | What it does | Where |
|---|---|---|
| 1 | What goes THROUGH see-through glass crosses the mesh's own smooth face (no microfacet, no normal map), where the game's shader shows what is behind neither bent nor blurred (HDRP Lit with no refraction model). The reflection keeps the material's roughness and normal map. Glass whose shader does refract (a refraction model on: the crafting bottle) keeps its rough view: the plugin marks it (`TGPT_Material.opticalReserved[0]` bit 0) | `pt_common.hlsli glass_clear`, `pt_dielectric.hlsli`, `pt_primary.hlsli write_traced`, `pt_specular.hlsl`, `GlassFinish.ReadOptics` |
| 2 | The paint on see-through glass only on the faces the game draws: the front faces of a material drawn with its back faces culled. For the camera's and every other ray, and for a light's shadow ray | `pt_common.hlsli glass_painted`, `glass_cover`, `glass_walk` |
| 4 | Glass has an inside where its PIECE of the mesh closes a volume, not only where its whole submesh does (the jar's body with its open lid). Uses what `scene.cpp mark_closed` already records for wax (`piece_solid`) | `pt_common.hlsli glass_solid` (three arguments), `pt_dielectric.hlsli`, `caustic_through` |
| 8 | Not part of the trial: brings a fixed fault back to see it (section 3: a lamp's light through glass counted twice) | `pt_dielectric.hlsli g_walkGathers` |

**7 is the one to try.** 4 without 1 would frost the jars (see above); 1 without 4 changes nothing on the Crypt's
jars (they are open sheets until 4).

How it travels (no option bit: all 32 are used; no size or ABI change): the plugin puts the number in
`TGPT_Frame.cameraPosition[3]` (was "w unused"), `tracer.cpp` hands it to the shaders in the frame flags' bits 8 to 15
(`FrameConstants.size.w`, whose three readers mask their own bits), `pt_common.hlsli glass_trial()` reads it. The
tracer logs `Glass on trial (7): ...` when it changes.

**The one run to make:** `.\tools\harness\Run-PtHarness.ps1 -Scenario refract-jar -Recipe glass-trial` (the recipe is
in the copy's `tests/ingame/scenarios.json`): the game; no denoiser at 0, at 2 (the film once a wall) and at 7 (all
three); NRD at 7 and at 0; 16 frames each. What to look for at 7: the wall's stones through the jars about as clear
as in the game's picture, a thin bent rim at each jar's outline, the film a third of the pixel instead of half, the
jars' shadow on the wall lighter. The tracer's log line: `Glass on trial (7): the view through see-through glass
crosses the mesh's smooth face; its paint only on the faces the game draws; glass has an inside where its piece of the
mesh closes a volume; `.

With bit 1 the camera's own glass pixel stores the mesh's smooth normal as its normal (the view through is bent by
it; with the normal-mapped one the camera's first face still embossed the view), and the normal-mapped one, which the
reflection is drawn about, travels in 16 spare bits of the glass pixel's two words (half a degree: a rough lobe's
normal). So with bit 1 a denoiser's guide normal on such glass is the smooth one. With the trial off nothing of this
changes.

## 3. The tint (PT-76): built as the rule

### What was wrong, with numbers

**A lamp's light through glass was counted twice.** A ray that gathers light for a vertex (a path's bounce, the
radiance cache's, a pixel's own bounce ray) and meets glass walks through it (`dielectric_follow`). That walk added
every lamp's ball and the sun's disc it then met. But the vertex's own shadow ray had already brought that light
through the glass (tinted along the straight line, or bent: the caustic). Offline (`glass-12*.png`): a floor under a
sheet of clear glass, lit by a lamp above it, with every bounce: **1.885 times** the open floor. That second, untinted
helping is why "grey glass's shadow" was 0.17 of a solid one's and why no tint could show. Now: **1.026 times** (the
0.026 is the pane's own reflections of the floor's light).

The fix (`pt_dielectric.hlsli g_walkGathers`, set by `shade_path` and `cache_sample`): such a walk counts a lamp or
the sun only where it reflected off glass on its way, which no shadow ray finds. The camera's own view through glass
and a mirror's count what they meet whole, as before: nothing samples for them. No switch (it is a double count, not
a look); the trial's bit 8 brings it back to see what it did, and is the offline check's own mutant.

**What this changes in the game** (say it to the owner): every surface lit by a lamp or the sun THROUGH see-through
glass (a jar between a candle and the wall, a window) gets that light once. Such places come out darker than today,
by up to half, and lose the bright specks the second helping made (a bounce ray that found a lamp's small ball
through glass). The picture the owner accepted this morning (the tavern's candles) has no see-through glass in the
light's way that I know of (the tavern's bottles are opaque to the tracer).

### The tint itself

The glass's own tint is its material's absorption (HDRP's transmittance color over its absorption distance, which the
plugin already reads where the game's shader uses it: a refraction model on):
- where the glass closes a volume: over the ray's way inside it. A radiance ray had that; a light's shadow ray
  (`pt_common.hlsli glass_walk`) and a caustic's solved path (`caustic_tint`) have it now;
- an open sheet: a thin pane as thick as the material says (HDRP's refraction thickness in metres, its Thin model's
  5 mm; `TGPT_Material.opticalReserved[1]`, a float's bits; `GlassFinish.ReadOptics`), slanted as the ray is inside
  it (`pane_transmittance`), for radiance and shadow rays alike;
- paint on see-through glass stays paint, by its alpha;
- glass's base color is no filter any more (`glass_color` is gone, `glass_open` is what the paint leaves). Only the
  offline test still sent glass without the see-through mark, and only a light's ray was filtered by it.

Offline (build 3): under a tinted pane 10 cm thick (0.621, 0.928, 1.005) of the open floor, under a closed slab of
the same glass (0.622, 0.930, 1.008); their absorption leaves (0.607, 0.905, 0.980).

In the game today one material has an absorption (`Mat_Crafting_Bottle_08_Glass`, HDRP's sphere model, with a
thickness MAP, which is not read: the material's number alone). The jars' transmittance color is unused by the game
and stays unread.

### The four failing checks

| Check | Before (the coordinator's whole test of 08:36) | Now |
|---|---|---|
| "light through green glass reaches path vertices green (bounced light)" | green/red under the sheet 0.65 times the open room's | **1.60 times** (passes; glass-only mode) |
| "grey glass leaves the bounced light's color" | 0.52 times | **1.01 times** (passes) |
| "grey glass lets light through as bright as its color (its shadow about 0.64 of a solid one's)" | 0.17 | not run here (it is in the whole test's character block); its glass is the grey one below, a pane at each crossing: 0.64 to 0.69 expected |
| "glass bends light by its material's own index of refraction (another index lays the light elsewhere)" | `1.14 of the light through`, the limit 1.1 | **untouched, still open.** Direct light alone (the Direct view), so neither the tint nor the double count: the caustic solver gives a ball of index 1.2 14 % more light than its opaque twin blocks. It asks the right thing. It passed on 10-08 04:23, so something merged since changed it. Not looked into |

The first three asked for a tint by glass's BASE COLOR. Their glass is now tinted by its absorption, as the brief
asks: material 9 (grey): 0.8 m of it leaves 36 %, and as an open sheet it is a pane 0.4 m thick (60 % a crossing);
the green one the same way for (0.15, 0.6, 0.15). The cube's check compares with the absorption over each ray's way
inside the cube, worked out in the test (no longer one number for the whole shadow).

So that they can run alone, the room's coloured-glass checks moved into `tests/pathtracing_glass_tint_checks.inl`:
the whole test includes it where they always stood, and the glass-only run includes it after the glass fixtures.

## 4. What the whole test will tell (not run by me)

- The character block's two glass checks ("grey glass lets light through ...", "clear glass casts no shadow").
- The room's coloured-glass checks at full size (they ran at the glass-only mode's 320x180, where the cube's two
  checks need a looser limit: the shadow's outline, where the way inside the glass is shortest, is left out by the
  check's own 70 % rule and is a large part of a 12-pixel shadow. Measured there: a lamp, grey 0.443 against 0.524
  worked out; the sun, 0.544 against 0.573).
- The caustic ball's checks and the see-through cube's (their glass is clear: nothing should move).
- Everything built with NRD (my build has `-DTGPT_WITH_NRD=OFF`).
- In the glass-only mode six checks of OTHER fixtures, which run before the glass block, fail at that mode's 320x180:
  "the scene has thin things and edges ...", two of the reuse check's, "a closed wax cylinder drawn from both sides
  ...", "a skin panel's pixels are counted", "coloured or white bulk slab ...". The same six in all three of my
  builds; none has glass in it; five of them pass at full size in the coordinator's whole test of 08:36 (the sixth,
  the bulk slab, fails there too). I could not show that they fail on the unchanged tracer in this mode (a run with
  the main tree's own DLL would not start: "SKIP: path tracing unsupported here").

## 5. Notes (not built)

- **Why NRD smears and boils what is seen through a jar.** A jar pixel's own record flips from frame to frame: the
  camera ray's draw makes it the film in some frames (an opaque surface: its normal, roughness, material, a diffuse
  signal) and glass in the others (`pt_primary.hlsli write_traced`: `glass = false` by chance). Every guide a
  denoiser is handed for that pixel changes with it, so no history holds. Hair's layer already has the answer: steady
  guides whichever way this frame's ray went (`write_guides`). For glass: always the glass's record, the film's light
  as a share of the same pixel's signal. Not built.
- **The lantern's milky glass** (`glass-jars`): not found. That scene's audit has no transparent material named for
  glass on the lantern, so its panes are either a material the plugin does not take for glass (invisible to rays, and
  HDRP still draws it over the traced picture) or opaque and painted. The F8 pick on a pane says which.
- **The missing windows** (PT-83): `MI_Window_Glass_02` is traced as see-through glass, `MI_Window_Glass_01` and the
  stained glass materials are opaque in the game and traced opaque. Nothing of this job explains a window that is
  missing altogether; not looked into.
- **The film's cover in the camera's pixel is still a per-frame draw** (a third of the frames the film, at trial 2):
  with no denoiser it is noise, as before.

## 6. Workarounds met

| Where | What | State |
|---|---|---|
| `pt_common.hlsli glass_color`, `glass_walk`'s square root, `caustic_tint` | glass's base color as a filter for a light's ray (test-only glass) | removed; the tests' glass absorbs instead |
| `pt_dielectric.hlsli` | the walk's lamps and sun added for rays whose vertex samples them itself | fixed (above) |
| `pt_common.hlsli glass_solid` | a glass thing is a solid only where its whole submesh closes (a jar with a lid is sheets) | on trial behind bit 4 |
| `pt_specular.hlsl` | the camera's glass pixel asked "is it a solid" of submesh 0, triangle 0 of the instance (the record keeps neither) | fixed: the record carries the answer (bit 30 of the glass pixel's first word) |
| `pt_dielectric.hlsli` | the view through glass frosted and embossed by the finish of its highlight | on trial behind bit 1 |
| `glass_cover`, `glass_walk` | paint on faces the game does not draw | on trial behind bit 2 |
| `glass_walk`, `transmittance` | a light's straight ray through glass takes no Fresnel loss (a pane passes 100 %, where 92 % is due; the check "clear glass casts no shadow" asks for it) | NOT removed; the owner's or the coordinator's call |
| `pt_common.hlsli emission_coverage`, glass branch | an emissive glass's sampled light is scaled by 1 minus the root of its base color's brightness (a white glowing glass is sampled at 0): left from "glass passed by chance" | NOT removed (the emissive lights' side; not looked into) |
| `shade_seen` (a mirror's ray that then meets glass) | a lamp behind glass seen in a glossy opaque surface is counted by the surface's own highlight and by the walk | NOT looked into (only the rays that gather diffuse light were fixed) |
| `GlassFinish.ReadOptics` | a material's thickness map is not read (its number alone); the Thin model's 5 mm is from HDRP's description of it, not checked against the game's compiled shader | open; no material of the game needs it today |
| `caustic_through` | where the solver finds no path through a closed glass thing the light is 0 there (a ball of 1.5 passes 0.67 where about 0.9 is due); until today the second helping hid part of it | NOT mine; open |

## 7. Files changed (against base65)

Plugin: `src/TGPathTracing/Settings.cs` (the setting), `HdrpPasses.cs` (one line: the frame's word), `GlassFinish.cs`
(the refracts mark, the pane's thickness, the log line).
Native: `native/pathtracing/include/tgpt_api.h` (a comment), `src/tracer.cpp` (the trial into the frame flags, its
log line); shaders `pt_common.hlsli`, `pt_dielectric.hlsli`, `pt_primary.hlsli`, `pt_specular.hlsl`.
Tests: `tests/pathtracing_glass_checks.inl` (the jar, the lidded jar, the floor under glass),
`tests/pathtracing_glass_tint_checks.inl` (new: the room's checks, moved), `tests/pathtracing_gpu_test.cpp`
(`glassTrial`, materials 9 and 10, the two includes), `tests/source_checks.py` (two new tests, seven quoted lines),
`tests/ingame/scenarios.json` (the recipe `glass-trial`).
Not mine: `tools/Build-PtShaders.ps1` (the coordinator's replacement).
Shared with other workers (water, grass): `pt_common.hlsli` (local edits: the trial's helpers before `glass_color`'s
place, `glass_walk`, `caustic_tint`, two lines in `cache_sample` and `shade_path`, `pack_normal16`),
`tracer.cpp` (one block after `k.size`), `pathtracing_gpu_test.cpp`, `source_checks.py`.
Line endings as each file had them (`source_checks.py` and `pt_specular.hlsl` CRLF, the others LF in this copy).

**Words that got a meaning** (say so to whoever else takes spare fields):
- `TGPT_Frame.cameraPosition[3]`: the glass trial's number (was unused).
- `FrameConstants.size.w` bits 8 to 15: the same, for the shaders (bits 0 and 1 were the only ones used).
- `TGPT_Material.opticalReserved[0]` bit 0: the game's shader refracts by this material's finish.
- `TGPT_Material.opticalReserved[1]`: an open sheet's thickness in metres, a float's bits.
- The glass pixel's two words (the hair texture): instance in 24 bits, material in 20; bit 31 front face, bit 30
  its triangle has an inside, bit 29 with 16 more bits: the normal-mapped normal (trial bit 1 only).

Size: the tracer's DLL 29,146,112 bytes (build 1, the trial's first two bits alone: 28,928,000). The second shader
build took 35 minutes where the first took 16: the PC was full of other compiles, not the code (1 % more of it).

## 8. Tools left in the session's scratch folder (`<scratch>\glass3-tools`)

`jar_closed.py` (is a mesh closed, piece by piece, by the tracer's rule), `jar_stats.py` (a material's alpha,
smoothness and normal map from the jar scan), `build.cmd` and `build-loop.ps1` (the build; never falls back to
NMake's one-at-a-time shader compile), `gpu.ps1` (the glass-only run behind the GPU gate), `eol.py`, `changed.py`.
