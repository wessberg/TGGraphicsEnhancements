# Lights at glowing things, the crystals' glow, and each light's real size (PT-47, PT-34)

Agent: lights, 2026-10-06. Work copy `wt/lights`. Nothing deployed, nothing committed. Not seen in game yet.

## The answer in short

- The cave turned orange for two reasons, both ours. (1) No glowing surface was ever traced as glowing: we read HDRP's
  "EmissiveIsBlack" flag as "no glow", but HDRP sets it on every material whose Baked Emission toggle is off. Every
  harness run's native log says `emissive: 0 submeshes` (39,669 status lines, all scenes); not one "Emissive material"
  line was ever logged. (2) The crystals' own blue lights were dropped as fill lights: each pickable crystal
  (`Prefab_Prop_ForeCrystalMold_Lowpoly_01`) carries a plain "Point Light" (13,416 K, blue, 185,364 cd, range 8 m), and its
  names say no flame, lamp or effect.
- Now: HDRP's own shaders' glow is read whatever the flag says; a light the names leave out is kept when a glowing thing
  stands within 1 m of it; the glowing surface it stands for then lights nothing itself (no double light). Fill lights
  with nothing glowing at them stay out.
- Separate change: every traced light is the size of what emits it (candle flame 1.2 cm, torch 8 cm, ...), never
  HDRP's artist radius. Candle and torch shadows get sharp, highlights in puddles become points.

## 1. What is at each left-out light (the census, `left_out.py` over all harness runs, 45 names)

Evidence: the state audits (`audit-a.txt`: each light's path, colour, temperature, strength, range, radius, position),
the "Lights by name" lines, the debug views of `20261006-220318/glow`. The audits hold no renderer positions, so "what
glows near it" is measured only from now on: the new log lines (section 8) name it per light.

| Kind (census name, parent) | What is at it | Verdict | Evidence |
|---|---|---|---|
| "Point Light" in `Prefab_Prop_ForeCrystalMold_Lowpoly_01(Clone)` (8 in the cave's audit) | the pickable crystal it sits in (HDRP/Lit, `_EMISSIVE_COLOR_MAP`) | keep: the crystal's light | blue 0.39/0.69/1.0 at 13,416 K, 185,364 cd, range 8, one per crystal prop (audit `20261006-162322/glow`) |
| "Point Light" under `VFX_Explosion_Aard` | a spell's effect | keep (already by its "VFX" name) | census parent; the "*" on "Point Light" came from the crystal props |
| "Point Light_17B" x31 under "Lights" (cave) | 11 blue (9,094-12,062 K), the rest white or warm (2,768-7,417 K); 17B_15 2.2 m and 17B_13 5 m from a crystal prop | the rule decides per light: kept where a crystal glows within 1 m, the white and warm ones are the cave's fill | audit; wall crystals' positions unknown until the next run's log |
| "Spot Light" x2 under "Lights" (cave) | nothing: 9.6-11.5 m up, 8.4 and 16.7 million cd, 12,062 K, one with hard shadows, radius 12.4 m | drop: a sky shaft from the opening, fake moonlight | audit |
| "Point Light_01Vol" x2 under "Lights" (cave) | 3,543 K, 1.5 m radius, 2 m range ("Vol": light for the fog) | drop unless something glows at it | audit |
| "Light_Stonehenge_Decal" x14 under `StonehengeDynamicDecals(Clone)` | the stone circle's glowing rune decals | keep: if the decal is emissive (the decal rule) | red 1/0/0, 26,616 cd, range 8, 0.025 m; the parent holds the decals |
| "Light_Ambience" x23 under "SecondVisual" | unknown (SecondVisual is the root of the stone-circle location) | the rule decides; the log will say | census |
| "Light_Monster_Wyrdstalker" under "Spine" | a creature's glow; characters' emission is not read by the tracer | stays out for now; ask the owner | census; the log names the nearest glowing thing |
| "Point Light_Neck" under "HeadHolder" (night, the masked figure) | a character; maybe a glowing mask | stays out for now; ask the owner | census |
| "PointLight_Ambience" x272, "PointLight_Cookies", "PointLight_Shadows", "Point Light_Single" etc. (the left-out ones of these groups) | the campaign map's lights under no prop that are not warm flickering flames, and candles' extra lights | drop: fill, or a candle's extra light (its flame is claimed by the candle's own light) | ruling (each candle keeps one); `FlameByLook` keeps the flames |
| "Point Light_N" under "Lights" (tavern), "Spot_Light", "Point Light_15 (2)", "PointLight_1 (70)", "Point Light (1-4)", "Ambient_Light", "Point Light_Ambience" under "Chapel", "Point Light_Shadows" (vista: 20,000 K, 4 million cd) | room and sky fills, radii 2-25 m | drop | audits, rulings |
| "Light_CandelabraGallahad_Volumetric", "PointLight_Volumetric" | light for the fog | drop (Fill Light Fog keeps their fog) | AlwaysPlaced "Volumetric" |
| "Light_HeroLight_*", "Light_Inventory_*", "Default HDAdditionalLightData" | the player's light, the inventory screen's, Unity's default | drop | names |

## 2. What changed in the classifier (`SceneLights.cs`, `LightRules.cs`)

- By names and look as before (`ByNames`). Where they leave a light out (and it isn't HeroLight, Volumetric or Fake),
  `LightRules.GlowAt` looks at the glowing things near the camera (`GatherGlows`, every scan):
  - traced emissive instances (`SceneCapture.Glows`: world bounds from the mesh's bounds, colour, a key per instance),
  - effects drawn glowing: `ParticleSystemRenderer` and `VFXRenderer` whose material is additive (`_DstBlend` One, or
    HDRP's transparent Additive), glows by its own colours (`_EmissiveColor` > 0, `_EmissionColor` above 1) or is named
    for fire or light ("Additive", "Fire", "Flame", "Glow", "Emissive", "Ember", "Spark", "Magic", "Light", "Firefl",
    "Orb"),
  - emissive decals (`DecalProjector` with Affect Emission, or an emissive colour).
- Kept: the nearest such thing within 1 m of the light's position (distance to its bounds), at most 6 m across, not
  claimed. Claimed: within 1 m of a light kept by names or look (a candle's flame, a lantern's glass), so a candle's
  extra lights stay out as the ruling says.
- A kept-for-a-glow light keeps HDRP's colour, strength and range (it counts as an effect: `Reach` = HDRP's range). It
  stays kept while its parent is the same (`glowKept`), because particles come and go between bursts.
- No double light: the glowing surfaces kept lights stand for are marked their own surface (`SceneCapture.OwnSurfaces`,
  the next scan): the kept-for-a-glow light's surface, and every surface within 1 m of a flame's or a lamp's light. Their
  emissive materials get a copy with `TGPT_MATERIAL_OWN_SURFACE`; the native emitter pass marks such triangles
  EMITTER_PAIRED (they light nothing, are never sampled) and reflections and the camera still see them glow. The old
  radius rule (light radius + 25 cm) stays.

## 3. Why the crystals' own glow lit nothing

- Not a strength question: the glow was never read. `SceneCapture.ReadMaterial` required
  `(globalIlluminationFlags & EmissiveIsBlack) == 0`. HDRP's material editor writes EmissiveIsBlack whenever Baked
  Emission is off (the default for new materials), and HDRP's Lit shader adds `_EmissiveColor` (times the emissive map
  and, with Albedo Affects Emissive, the albedo) regardless. So every HDRP/Lit glow of the game was dropped: the
  status line "emissive: 0 submeshes" in every run, no "Emissive material" log line ever, and in `glow`'s Emissive view
  (`20261006-220318/glow/views.jpg`) the crystals show only HDRP's own GBuffer emission (that view adds it), never the
  view's green/blue/red marks of a traced emitter; the Materials view shows them at their albedo.
- Fixed: HDRP's own shaders (`HDRP/...`) are read whatever the flag says; other shaders keep the flag's word.
- Not proven yet: that the crystal's `_EmissiveColor` is above 0 (the game might set it per renderer). The next run's
  log says either `Emissive material "Mat_Prop_CrystalMold_Lowpoly_01" ... nits ..., Baked Emission off (read all the
  same)` or `... has an emissive map but _EmissiveColor ...: traced dark`; the audit's material line now carries
  `_EmissiveColor=(...)` and `gi=` (4 = Baked Emission off). I tried the game's bundles for the material's values; the
  crystal prefab is addressed by GUID only and I stopped there.
- Strength once read (unchanged, documented in CHANGELOG): HDRP's exposure weight is applied (at weight 0 a glow is its
  colour over the exposure: about 19,700 times it at the cave's fixed EV 14), Albedo Affects Emissive, front face only
  for single-sided materials, a 16,384-triangle budget brightest first. Expect much more glowing light in many scenes.

## 4. Glowing things in traced reflections

- Traced emissive surfaces: yes. A reflection ray that meets one counts its emission whole (`emission_weight` returns 1
  for rays that don't gather), own surfaces included. Before this fix none were traced as glowing, so none showed.
- Lights: their highlight comes from the direct light's specular sample (and, on see-through glass, the light's sphere,
  `reflected_lamps`). With the real sizes the highlight is small, as HDRP draws it.
- Missing (not built): everything HDRP draws over the picture: particles and visual effects (flames, spells,
  fireflies, sparks), decals, transparent and unlit glow cards. They are invisible to rays (`ReadMaterial`: transparent or
  unlit), so no reflection shows them; only the light kept for them makes a highlight. Characters' glow (the Kandra
  shaders' emission) is not read at all.

## 5. Each traced light's size (a separate change, `SceneLights.Size`)

| Kind (by the most specific word in the light's or its 5 parents' names) | Size across when nothing measures it | Range a measured size is held to | Measured from |
|---|---|---|---|
| Candle (Candle, Candela, Chandelier) | 1.2 cm | 0.8-3 cm | the nearest glowing thing within 0.5 m (the flame's particles) |
| Torch (Torch, Sconce) | 8 cm | 4-16 cm | same |
| Lamp (Lantern, Lamp) | 10 cm | 4-30 cm | the glowing glass or flame within 0.5 m |
| Brazier (Brazier, Hearth, Forge, Furnace) | 30 cm | 16 cm-1 m | within 1 m |
| Fire (Bonfire, Bonefire, Campfire, Fire) | 50 cm | 20 cm-1.2 m | within 1 m |
| Lava | 1 m | 20 cm-2 m | within 1 m |
| Ember | 4 cm | 1-40 cm | within 0.5 m |
| Flame, a flame by its look (campaign map) | 6 cm | 1-40 cm / 1-16 cm | within 0.5 m |
| Effect (VFX, Magic, Spell, Projectile, Portal, Explosion, fireflies) | 20 cm | 1 cm-2 m | its particles within 0.5 m |
| Kept for a glowing thing | (always measured) | 1 cm-2 m | that thing's bounds |
| A mod's own light (the dialogue light) | HDRP's radius, 2-30 cm as before | | |

- "Measured": the middle of the thing's three half extents (a flame is taller than wide; a crystal cluster on a wall
  thinner than wide), held to the kind's range. The sizes are cached per light and made again when the glowing things
  change.
- What it does: shadows of small flames get sharp (a candle was a ball 20 cm across, its HDRP radius 0.1 m); a
  lamp's highlight in water or on wet stone is a small point, not a long smear; right beside a flame the light is no
  longer capped at I / r^2 of a big ball (path vertices still take it as at least 25 cm away). The own-surface radius
  rule (radius + 25 cm) shrinks with the radius; the 1 m claim (section 2) covers lanterns' glass and braziers' embers.

## 6. Files, ABI, bits

- `src/TGPathTracing/LightRules.cs` (new, pure rules), `SceneLights.cs` (classifier, sizes, logs, own surfaces),
  `SceneCapture.cs` (emission read, glowing instances, own-surface materials), `NativeTracer.cs` (MaterialOwnSurface),
  `TGPathTracing.csproj` (ParticleSystem and VFX module references), `src/TGPathTracing.Harness/Audit.cs` (material
  line: `_EmissiveColor`, `gi`).
- Native: `include/tgpt_api.h` and `shaders/pt_common.hlsli` (`TGPT_MATERIAL_OWN_SURFACE` / `MATERIAL_OWN_SURFACE` =
  131072, the bit assigned to this agent), `shaders/pt_emissive.hlsl` (own surface -> EMITTER_PAIRED), `src/scene.cpp`
  (an own surface is never sampled).
- Tests: `tests/LightRules.Tests.csproj` + `tests/light_rules_tests.cs` (new), `tests/pathtracing_gpu_test.cpp` (6b),
  `tests/source_checks.py`, `docs/TESTING.md`.
- No export, no ABI bump, no FrameConstants change, no option or view bit. An older native ignores the new material bit
  (the surface would light as well as its light).

## 7. Checks

- `python tests/source_checks.py`: 80 tests, OK.
- `dotnet build src/TGPathTracing -c Release` (to a scratch folder): OK; the harness plugin too.
- `tests/light_rules_tests.cs`: all pass; 6 mutations (reach ignored, claims ignored, walls count, least specific kind,
  candle 3 cm, no clamp) each make it fail.
- `tgpt_gpu_test` (Ninja build in `%TEMP%\tgb\lights-native`, installed to `lights-inst`): all path tracing checks
  passed, 137 s. New check 6b: a light 1.5 m beside a glowing quad marked as its own surface, the room's light 0.01017
  row on and off, as with the quad dark (0.01017); unmarked 0.02989. Mutant (the own-surface line removed from
  `pt_emissive.hlsl`, installed to `lights-mut`): 0.03007 / 0.03013, the check fails, the only failure.

## 8. What the next harness run should show

Run `-Scenario glow -Recipe views`, then `night`, `vista`, `tavern-room`.

- BepInEx log, `glow`: `Emissive material "Mat_Prop_CrystalMold_Lowpoly_01" (HDRP/Lit): ... nits times its emissive map
  ..., Baked Emission off (read all the same)` (or "traced dark": then the glow comes per renderer and needs another
  look). `Lights: "Point Light" under "Prefab_Prop_ForeCrystalMold_Lowpoly_01(Clone)" kept: at a glowing surface ...`,
  the inventory line `+ "Point Light" ...`, and per "Point Light_17B" name either kept (at a crystal) or "left out ...
  nothing glowing within 5 m" / "nearest glowing ... m away". A line `Lights: traced at the size of what emits them`.
- Native log: `emissive: N submeshes` with N > 0, some "sampled as lights" (the wall crystals no light stands for).
- Pictures (`glow`): the crystals glow blue in the Emissive view's marks (blue: a light's own surface; green: sampled),
  the cave right of the crystals lit blue in Direct and the traced picture, blue-white dots back in the wet floor,
  the lamps' reflections small points, not streaks; candle and torch shadows sharper.
- `night`: the stone circle's runes (if emissive decals) keep their red lights; the Wyrdstalker and the masked figure's
  lights stay out (log line says what is near).
- `vista`, `tavern-room`: fill lights still out (`* ` lines), and any surface that now glows (windows, embers, magic)
  should look right, not blown out.

## 9. Risks

- Scenes get brighter wherever HDRP/Lit materials glow: every such surface now lights the world (sampled as a light
  with the Emissive Lights row on). With exposure weight 0 they are bright (colour over the exposure); a large
  glowing mesh could flood a room. Watch `tavern-room` and `vista` for that, and the exposure log line.
- A fill light that happens to sit within 1 m of an unclaimed glowing thing is now kept (a window, a glowing sign).
  The log names each such light with its thing; the rule is in `LightRules` (1 m, 6 m).
- The particle rule leans on material settings and names; a glowing-named dust or smoke volume under 6 m could keep a
  fill light. The log will show it.
- Small lights: very sharp shadows and tiny highlights may show more noise in the first frames, and candles right next
  to a surface make a hot spot (physically right).
- Moved or carried glowing things count as things (a carried lantern) but their surfaces are never sampled (moving).
