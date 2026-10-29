# Lights round 2: candles that glow too strongly, and "torch" lights with no torch (PT-47, PT-34)

Agent: lights2, 2026-10-07. Work copy `wt/lights2`. Nothing deployed, nothing committed. Not seen in game yet.

## The answer in short

- The tavern's candles have their big light (21,149 to 24,775 cd, HDRP radius 1 m) inside the wax, about 10 cm under the
  flame. HDRP softens a light near its centre by that radius (I / (d^2 + r^2)), so the wax 3 cm away got about I. Round 1
  traced the light at the flame's size (1.2 to 3 cm): the wax got I / d^2 = I / 0.0009, 1,100 times HDRP's light and 100
  times what the tracer gave it before round 1. The wax is drawn as see-through-light material (a thin diffusion
  profile), and the tracer's rays from a surface pass its own mesh's back faces within 15 cm and nothing within 5 cm and
  2 cm of a light, so the whole candle was lit from inside, up to the direct-light clamp: white-hot.
- Now a flame's or a lamp's light is traced at the centre of its measured flame (or lantern glass). There its shadow rays
  are tested up to its own size plus 1 cm, and a surface lit from behind lets a lamp's light through back faces within
  1 cm only (a thin thing's thickness). The wax's top then shadows its sides; the top itself gets what physics gives it.
- A light kept by a flame's or a lamp's name needs a flame, lamp or glowing thing within 1 m, or it is left out as a fill
  light. That drops the mineral cave's six "torch_06" lights (and the other torch groups' fills).
- Hidden key "Lights sized as their emitters" (default on): off traces every light at HDRP's place and radius, as before
  round 1. Harness recipe `lights` A/Bs it.

## 1. Why the candles overexposed (numbers)

Evidence: `20261006-234757/tavern-room` (audit-a.txt, the 1-game and 2-traced-rr frames), lights projected into the frame
with the scenario's camera (`<scratch>/l2_candle_px.py`; 2.43 px per cm at the table candle, 7.9 m away).

| Candle | Light | cd | HDRP radius | Where it sits |
|---|---|---|---|---|
| Table, `TableStuff_05/SM_Candlestick_02` | `PointLight_Cookies_01` (cookie Cookie_Candelabra_02B, 2,600 K, no shadows) | 21,149 | 1 m | inside the wax: the flame's brightest rows 9 to 11 cm above it, the wax from 7 cm above to 4 cm below it |
| Balcony, `SM_Candlestick_01 (2)` | `Point Light_Single` | 1,611 | 0.1 m | at the flame's tip |
| same | `PointLight_Cookies_01` | 24,775 | 1 m | 10 cm lower: inside the wax |

The light the wax gets from the big light, the wax about 3 cm from it:

| | Formula | Light on the wax |
|---|---|---|
| HDRP 17 | `I / (d^2 + r^2)`, r = its radius (`ModifyDistancesForFillLighting` in `PunctualLightCommon.hlsl`; `HDGpuLightsBuilder.Jobs.cs` stores `shapeRadius^2` as `size.x`, "to simulate a fill light"), 1/d held under 100 (`PUNCTUAL_LIGHT_THRESHOLD`, 1 cm) | I / 1.0009: about I |
| Tracer before round 1 | `I / max(d^2, r^2)`, r = HDRP's radius held to 0.3 m | I / 0.09 = 11 I |
| Tracer, round 1 | the same, r = the flame's 0.6 to 1.5 cm | I / 0.0009 = 1,100 I |
| Tracer, round 2 | the light at the flame's centre, 3 cm over the wax's top | the top: I cos / d^2 (a 5 cm top: 676 I on average); the sides: shadowed by the top, except its top centimetre |

- HDRP's near clamp is 1 cm (`PUNCTUAL_LIGHT_THRESHOLD`); the tracer already has the same (`max(d^2, r^2 + 1e-4)`). What
  made HDRP soft is its fill-light trick, which Unity's own comment calls "not physically plausible". Not copied: the
  owner's ruling is "whatever is most realistic".
- The wax (`MI_Candle_01`, HDRP/Lit with subsurface scattering and transmission) is in HDRP's GBuffer with a thin profile,
  so the tracer lights it from behind too. Three rules let the light out of the wax: rays from a camera surface pass back
  faces of a non-alpha-tested mesh within 15 cm (`passes_through`, `near_back`: the traced mesh's level of detail may
  differ from HDRP's), shadow rays end 5 cm before a light (`shadow_ray`, `light_ray_end`: a light inside its lamp's mesh),
  and the camera's rays also end the surface's 2 cm offset short (`ray_offset` = 2 cm + 0.2 % of the distance).
- The direct-light clamp (`LIMIT_DIRECT`, 48 times the reference) caps the brightest wax; the top of the column below
  takes 98.5 with it, 105.6 without.

Offline, `tgpt_gpu_test` 2b: a 2 cd light, a wax column 5 x 12 x 5 cm drawn with transmission and traced as a closed box,
the Direct view (E / pi times the exposure 0.25), undenoised, no reuse, no clamps:

| Case | The column's side facing the camera (22,080 px) | Below its top 2 cm (18,432 px) |
|---|---|---|
| Light inside the wax at the flame's size (round 1, the game's place) | 37.17 | |
| Light inside at HDRP's 0.3 m (before round 1) | 0.349 | |
| Light 3 cm over the top, no flag (by hand, as a thin leaf with nothing blocking: 5.242) | 5.201 | 2.777 |
| Light 3 cm over the top, at its emitter (round 2) | 2.217 | 0.432 |

The top of the wax (a 5 cm square 3 cm under the light): 105.6 against 107.5 by hand (I x 4 asin(a^2 / (a^2 + 4 h^2)) / a^2,
0.982).

- After the fix the side is 17 times darker than with the light inside the wax as in the game today, and below its top 2 cm
  6 times darker than without the flag. The remaining light is the top centimetre (rays that cross less than 1 cm of
  wax) and a sixth of the light below the top 2 cm that I could not trace to its cause in this round.
- Still bright, by physics: the wax's top 2 to 4 cm under the flame takes I / d^2 straight on, about a fifth of the flame's
  own brightness, held by the direct-light clamp. The flame effect HDRP draws covers most of it.

## 2. What changed

- `LightRules.FlameCentre`: a Candle, Torch, Lamp, Brazier, Fire, Ember, Flame or flame-by-look light whose measured thing
  (the flame's particles or effect, a lantern's glass, within 0.5 m, 1 m for fires) is compact (no side more than 25 cm
  from its centre, 50 cm for braziers and fires) is traced at that thing's centre, kept in the light's own space (a carried
  lantern's light stays at its flame). Effects, glowing things (crystals) and lava stay where the game has them.
- Such a light carries `TGPT_LIGHT_AT_EMITTER` (0x40000). Native, `pt_common.hlsli`: `light_own_reach` (its shadow rays are
  tested up to `min(radius + 1 cm, 5 cm)` from it; every other light keeps 5 cm), used by `shadow_ray` (path vertices,
  HDRP-shaded surfaces, hair) and `light_ray_end` (`pt_rtxdi.hlsli`, the camera's direct light, where the surface's offset
  is no longer taken off as well for such a light: `RAB_LightSample.ownReach`, `.atEmitter`).
- `near_back_towards` (`pt_common.hlsli`): a surface lit through from behind passes back faces on the way to a lamp within
  1 cm (`THIN_THROUGH`), not `near_back`'s 15 cm. Used by `rtxdi_visible` and `rtxdi_transmittance` (lamps only; the sun's
  ray is unchanged). Alpha-tested leaves, grass and cloth never passed back faces anyway, so they are unchanged.
- Hidden key `Lights sized as their emitters` (Advanced, default true). Off: every light at HDRP's place, HDRP's radius
  held to 2 cm - 30 cm (fires 1 m), no flag: the tracer as before round 1. Recipe `lights` in `tests/ingame/scenarios.json`
  (game, traced, traced with the key off, Direct view, off) and a line in `docs/PT-HARNESS.md`.
- The size log line says how many lights of each kind moved to their flame and how far, e.g.
  `Candle x36 0.012-0.03 m (33 measured, 33 moved to their flame by 0.08-0.11 m)`.

## 3. "Torch" lights with no torch

- `LightRules.EmitterAt`: the nearest glowing thing (claimed or not) within 1 m (`GlowReach`) of a light. A light kept by a
  flame's or a lamp's name (fire, flame, torch, candle, candela, chandelier, brazier, hearth, forge, furnace, lamp, lantern,
  sconce, ember, lava) with none is left out as a fill light (`Class.noEmitter`), logged once per name and parent:
  `Lights: "Point Light_35" under "torch_06" left out (its names say "Torch" ("torch_06"), but no flame, lamp or glowing
  thing stands within 1 m of it: a fill light (nearest glowing ... m away); color ...)`; the capture's report says the same.
- Once its flame was seen, a light stays kept while its parent is the same (a flame effect between bursts).
- Not judged: lights kept by look (the campaign map's warm flickering ones), effect lights whose names say only an effect
  (VFX, Magic, Spell, Portal...: the light is a child of the effect object), the dialogue light, and lights beyond the
  gathered things. The glowing things are now gathered out to the scene radius + 40 m (as far as a traced light reaches).
- The mineral cave (audit `20261006-162322/glow`): each torch group (`torch_04`, `_05`, `_06`) holds one torch
  (`Light_Int_Torch_Big_01_Lit_02`: PointLight_Cookies at the flame, PointLight_Ambience 0.4 to 0.5 m above it) and 3 to 6
  warm "Point Light_NN" 3 to 8 m from it, at one height; their mirror images in the wet floor were the yellow dots. The
  torch's two lights are within 1 m of its flame, the fills are not. Round 1's size line for that run, "Torch x25 (11
  measured)", counts the 15 fills among the 14 torch lights with nothing within 0.5 m.
- Could not be checked offline: the audits hold no flame positions (a proxy by cookie lights is useless: most candles'
  lights carry none). Round 1 found a flame within 0.5 m for 33 of 36 candle lights and all 12 torch lights in the tavern,
  65 of 65 candle lights in the cave; now the reach is 1 m. One likely casualty: `Light_Torch_Big_01_Lit_01`'s
  PointLight_Ambience in the cave, 1.46 m from its own cookie light: by the ruling a fill light (the torch keeps its
  PointLight_Cookies at the flame). The next run's log names every one dropped.

## 4. Files, ABI, bits

- Managed: `src/TGPathTracing/LightRules.cs` (EmitterAt, MovesToEmitter, FlameCentre, HdrpRadius), `SceneLights.cs`
  (Classify's name rule, Size's place and flag, logs, gather radius), `Settings.cs` (the hidden key), `NativeTracer.cs`
  (`LightAtEmitter`).
- Native: `include/tgpt_api.h` (`TGPT_LIGHT_AT_EMITTER 0x40000u`), `shaders/pt_common.hlsli` (`LIGHT_AT_EMITTER`,
  `light_own_reach`, `shadow_ray`, `THIN_THROUGH`, `near_back_towards`), `shaders/pt_rtxdi.hlsli` (`RAB_LightSample`
  `ownReach` and `atEmitter`, `light_ray_end`, `rtxdi_visible`, `rtxdi_transmittance`).
- Tests and docs: `tests/light_rules_tests.cs`, `tests/pathtracing_gpu_test.cpp` (grass section, 2b),
  `tests/source_checks.py`, `tests/ingame/scenarios.json` (recipe `lights`), `docs/PT-HARNESS.md`, `CHANGELOG.md`.
- No export, no ABI bump, no material or option bit, no FrameConstants change. New: TGPT_Light.flags bit 0x40000 (above the
  cookie's 16 bits, beside 0x10000 and 0x20000). An older native ignores it (the light still at the flame, 5 cm as before).

## 5. Checks

- `python tests/source_checks.py`: 81 tests, OK.
- `dotnet build src/TGPathTracing` (to `%TEMP%\tgb\lights2-managed`): OK.
- `tests/LightRules.Tests`: all pass; 4 mutations (reach ignored, every kind moved, no compactness, HDRP radii unheld) each
  make it fail.
- `tgpt_gpu_test` (Ninja build `%TEMP%\tgb\lights2-build`, installed to `lights2-inst`): all path tracing checks passed (147 s), with the new check 2b: the top 0.982 of the hand value; the side below its top 2 cm 0.155 of the unflagged light (must be under 0.3), the whole side 0.43 of it (under 0.6) and 0.06 of the light in the wax (under 0.1).
- Mutations of the native rules (each installed to its own prefix, the source restored after): `light_own_reach` always 5 cm (`lights2-mutA`), `near_back_towards` always `near_back` (`lights2-mutB`), the surface offset taken off a flame light's ray again (`lights2-mutC`): each gives 5.20 at the flame and 2.78 below the top 2 cm (the unflagged values), and check 2b fails, the only failure.

## 6. What the next harness run should show

Run `-Scenario tavern-room,candle,npc-candle -Recipe lights`, then `-Scenario glow -Recipe views`.

- BepInEx log: `Lights: traced at the size of what emits them (across), at its flame where measured: Candle x.. (.. measured,
  .. moved to their flame by 0.08-0.11 m)`; with the key off (step 3) `traced at HDRP's place and radius`.
- Candles (`tavern-room` `table-candle.jpg`, `balcony-candles.jpg`; `candle`, `npc-candle`): the wax cream again with a
  bright top centimetre right under the flame, no white body and no halo; the holder under it no longer lit from 4 cm
  (the light was in the wax beside it). Step 3 (key off) should look like the pre-round-1 traced picture (`212905`).
  Candle shadows on the table now start at the flame: the candle's own body casts one.
- `glow`: the six warm lights of `torch_06` (and `torch_04`'s six, `torch_05`'s three) logged "left out (its names say
  "Torch" ...)"; the yellow dots in the wet floor on the left gone, the one real torch's reflection still there.
- Watch the log for real candles, torches, braziers or lanterns logged as left out: each line names the nearest glowing
  thing and its distance.

## 7. Risks

- A real flame light whose flame is not found within 1 m (a flame drawn as a mesh with an unrecognised material, an
  effect's bounds empty until it plays) is now left out until its flame shows up. The log names it.
- A flame effect whose bounds are off-centre (padded upward for rising sparks) puts the light a little above the flame.
- A light moved to its flame lights its own prop as a real flame would: candle shadows on tables, and the top of the wax
  very bright (held by the direct-light clamp).
- `near_back_towards`: closed meshes with a thin profile (wax, perhaps mushrooms or slime) lit from behind by a lamp no
  longer glow through their whole body; a mesh whose traced LOD is more than 1 cm off HDRP's could take a lamp's shadow
  from its own far side when lit from behind. Leaves, grass and cloth are alpha-tested and unchanged.
- The cookie's pattern now centres on the flame instead of HDRP's place (10 cm apart for the tavern candles).
