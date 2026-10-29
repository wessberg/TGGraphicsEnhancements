# flame-specks: why lamps placed in their flames make speckles (PT-85, PT-40)

Worker `flame`, 2026-10-09, copy `wt\flame`, base `wt\base60`. Brief: `docs/agents/briefs/brief-flame-specks.md`.

STATE: HANDED OVER at 01:12 for the game's trial. Compiled only: nothing here was run on a GPU or in the game.

## 0. HAND-OVER FOR THE GAME TRIAL

**Four hidden settings, section `Advanced`, all integers, all default 0 (off = today's picture).**
The brief asked for three. I added a fourth (D) because the code showed a fourth use of the lamp's size.
The lamps stay in their flames whatever these say. No switch moves a lamp.

| Setting (`Advanced / ...`) | Suspect | Values | What it changes, and nothing else |
|---|---|---|---|
| `Lamp trial near light` | A, the wax | 0 off. 1 to 255: a distance in cm. Try **30** and **10**. | No surface takes more of a lamp's light than it would at that distance. A diagnostic, never a fix: it takes real light away from what is right by a flame. |
| `Lamp trial reflected size` | B, the lamp's own reflection | 0 off. **1**: the radius HDRP gave the light. 2: five times the flame's. | The glowing ball a reflection ray meets a lamp as. Its total light is the same, spread over a larger ball. |
| `Lamp trial highlight size` | C, the highlight | 0 off. **1**: HDRP's radius. 2: five times the flame's. | The size of the lamp for the highlight its light samples make on glossy surfaces. |
| `Lamp trial shadow size` | D, the shadows (my addition) | 0 off. **1**: HDRP's radius. 2: five times the flame's. | The disc a lamp's shadow rays aim at: how soft its shadows are. |

Bold is what the recipe runs. They are independent bits: any mix works.

**The recipe is in the copy: `lamp-trial`** (`tests/ingame/scenarios.json`, after `watch-lights`). 16 steps on one scene,
NRD, 16 frames each, the picture and the Specular view for: as now, A at 30, A at 10, B, C, D, all four together.
Run: `.\tools\harness\Run-PtHarness.ps1 -Scenario tavern-room -Recipe lamp-trial`. Trim it as you like.

**How to read the answer** (against the run `20261009-004410`: picture 28.2 spots a frame at the flames, 14.8 at the
game's place and size; Specular view 67.7 and 23.5; pixels over 240 round a wall candle 388 and 6):
- A is guilty if the picture's spots fall towards 14.8 and the wall candle's count towards 6. A at 30 is what HDRP's
  30 cm did to the near light in the old run. A at 10 says whether the wax alone (closer than 10 cm) is enough.
- B is guilty if the Specular view's spots fall, and they sat on bottles, glasses and windows. B does nothing anywhere else (section 1).
- C is guilty if the spots fall on glossy things that are not glass (metal, polished wood, wet stone).
- D is guilty if the spots that fall sit along shadow edges of thin things.
- "All four" is the control. With all four on, the only thing left that differs from the old run is the PLACE.
  If all four together do not bring the count down, no use of the lamp's size is the cause: it is what a lamp
  outside its prop newly lights and shadows. No switch changes that, by the owner's ruling.

**Read before judging the A steps:** A takes light out of the picture, so the exposure may move (AGENTS.md: "anything
that removes light from the image moves the exposure"). Read the exposure and gain log line of those steps. A speck
count that fell only because the picture got darker is not an answer.

**D at 1 has a known flaw:** a 30 cm disc round a lamp that hangs 10 cm from a wall puts some of its points inside
the wall. Shadow rays to those are blocked, so things near such a lamp get a little darker. The old run had the same
flaw. D at 2 (a 3 cm disc for a candle) has none of it and is the cleaner test if D at 1 looks odd.

**B at 1 has one too:** glass closer than 30 cm to a candle is then INSIDE the lamp's ball, and every reflection ray
from it sees the ball: the whole glass glows a little. The old run did the same. B at 2 (a 3 cm ball) keeps that to
glass within 3 cm.

**The native log says once which it is:** "Small lamps on trial: all four switches off (lamps as before)", or
"Small lamps on trial (for finding causes): A near light held to N cm (0: off); B the reflected ball at ...; C ...; D ...".

**How it travels (no ABI change, no struct size, no option bit, no export):**
- plugin to tracer, the four settings: one number in `TGPT_Frame.jitter[3]` (was "w unused"; 0 from every other
  caller): A's centimetres in bits 0-7, then two bits each for B (8-9), C (10-11), D (12-13).
- tracer to shaders: the same number in `FrameConstants.wetGrid.w` (was unused; `sizeof(FrameConstants)` stays 7920).
  THIS JOB TOOK `jitter[3]` AND `wetGrid.w`: tell other workers. (`look.yzw` and `jitter[2]` are the reuse job's.)
- **the second radius** (the brief asked how it reaches the shader): HDRP's radius for each light rides in the top 12
  bits of the light's own flags (bits 20-31, millimetres, 1 to 4095; `TGPT_LIGHT_HDRP_RADIUS_SHIFT`). It is the same
  number the old run traced with (`LightRules.HdrpRadius`: HDRP's, held to 2 to 30 cm, a fire's to 1 m). The plugin
  fills those bits ONLY while B, C or D is at 1, so with everything off the lights' records are byte for byte as
  before. The light's own radius stays its flame's. Cookies use bits 0-15, the four flags bits 16-19: no clash.
  THIS JOB TOOK bits 20-31 of `TGPT_Light.flags` for point and spot lights.

**Files changed** (against base60):
- `native/pathtracing/shaders/pt_common.hlsli`: the helpers (`lamp_trial`, `lamp_trial_mode`, `lamp_trial_radius`,
  `light_near2`, `lamp_disc_dir`); `light_incident` (A); `reflected_lamps` (B); `direct_light` (C); `shadow_ray` (D);
  the comment on `wetGrid`.
- `native/pathtracing/shaders/pt_rtxdi.hlsli`: `RAB_LightSample.highlightDir`; `RAB_SamplePolymorphicLight` (C, D);
  `rtxdi_shade` reads `highlightDir` for the highlight (C).
- `native/pathtracing/src/tracer.cpp`: `wetGrid[3]` from `jitter[3]`, one log line, the struct's comment.
- `native/pathtracing/include/tgpt_api.h`: comments, `TGPT_LIGHT_HDRP_RADIUS_SHIFT`.
- `src/TGPathTracing/Settings.cs`: the four hidden settings. `HdrpPasses.cs`: `frame.Jitter[3]`.
  `SceneLights.cs`: `LampTrial` (the packing), HDRP's radius into the flags. `NativeTracer.cs`: one constant.
- `tests/source_checks.py`: one new check, `test_path_tracing_small_lamps_on_trial_default_off` (the defaults are 0,
  the plumbing is there). `tests/ingame/scenarios.json`: the recipe `lamp-trial`.
- `docs/agents/flame-specks.md`: this report.

**With everything off the shaders compute what they did before:** each switch reads 0 and takes the old branch
(`lamp_trial_radius` returns the lamp's own radius; `light_near2` returns `radius^2 + 1e-4`; `highlightDir` is `dir`;
no random number more is drawn). That is by reading the code, not by a picture.

## 1. What the code says about each suspect (read, not measured)

Numbers here are arithmetic from the code and its comments. A tavern candle's flame is traced 1.2 cm across (radius
0.6 cm). HDRP gives those candles a radius of 1 m (`SceneLights.cs`, the comment above `Size`), which the old run held
to 30 cm.

**A, the near light.** `light_incident` gives a surface `I / max(d^2, radius^2 + 1e-4)`. For a 0.6 cm flame the light
stops growing only 1.2 cm from the centre. At HDRP's 30 cm it stopped growing at 30 cm. So the old run did not only
move the lamp into the wax: it also cut the light on EVERYTHING closer than 30 cm. By how much: 100 times at 3 cm,
9 times at 10 cm, about 660 times at 1 cm. That is the wax, and also the wall behind a wall candle and the table
under a table candle.

**B, the lamp's own reflection. Narrower than the brief thought.** `reflected_lamps` is called from two places only:
`pt_dielectric.hlsli` (see-through glass) and `pt_water_path.hlsli` (water). Nowhere else does a reflection ray meet a
lamp as a ball. On wood, metal and stone a lamp is not in the traced world at all: its reflection there is the
highlight its light samples make (C). On glass the ball shows `I / (pi r^2)`: at 0.6 cm that is 2,500 times brighter
than at 30 cm, over 1/2,500 of the area. A mirror ray hits it or misses it. So B can only be guilty on bottles,
glasses, windows and water.

**C, the highlight.** Two routes, and they differ:
- A camera pixel's direct light (`rtxdi_shade`) evaluates the highlight towards ONE point of the lamp's disc, with a
  roughness floor of 0.045 (lobe width alpha 0.002). A lamp of radius 0.6 cm seen from 3 m fills an angle of 0.002
  radians from its centre to its edge: as narrow as the narrowest lobe. At 30 cm it was 0.1 radians, 50 times wider.
  So on smooth surfaces the small lamp's highlight is a needle, where the old run had a blob.
- What a reflection shows, and path vertices (`direct_light`), evaluate the highlight towards the lamp's CENTRE,
  whatever its size. That is a pinpoint lamp in both runs. C widens it there too, so C can also show whether that
  route makes specks. It was not a difference between the two runs.

**D, the shadows (added).** The shadow ray aims at a point of the lamp's disc (`RAB_SamplePolymorphicLight`,
`shadow_ray`). A 0.6 cm disc casts knife-sharp shadows where 30 cm cast soft ones. The reuse job found 96 to 99 % of
the Direct layer's specks at thin things and outlines (`docs/agents/reuse-shift.md`, section 3). Sharp shadow edges of
thin things are more of the same. So it gets a switch.

**Other things that depend on the lamp's size or on `LIGHT_AT_EMITTER`, with no switch:**
- `light_own_reach` (`pt_common.hlsli`): a lamp at its flame is tested for shadows up to its radius + 1 cm, not 5 cm.
  So its own candlestick, sconce and wax now cast shadows into the room. They are large, because the thing is right
  by the lamp. This is part of the PLACE; the "all four" step is the control for it.
- `light_ray_end` (`pt_rtxdi.hlsli`): for a lamp at its flame the surface's offset is not taken off the ray's end.
- `pt_emissive.hlsl`, line 113: a glowing triangle within the lamp's radius + 25 cm of a lamp is that lamp's own
  surface and lights nothing itself. With 30 cm that reached 55 cm, now 25.6 cm. A flame's own mesh sits at its
  lamp, so it should still pair. A chandelier's or a candle group's parts between 26 and 55 cm from every lamp would
  now be lights of their own. Not checked in the tavern. Worth one look at the Emissive view if A to D are all innocent.
- `pt_caustics.hlsl`, lines 63 and 178: where a caustic's light starts on the lamp, and where its ray ends.
- `scene.cpp`, line 2231: the radius is in the hash that rebuilds the emitters. No effect on the picture.
- A does not reach `pt_caustics.hlsl` line 170 (`intensity *= dist * dist`): with A on, a caustic from a lamp closer
  than A's distance to the water would be a little dim. No water in the tavern.

## 2. After the game's answer (not started; the brief's step 3)

Views, not measurements:
- **B guilty:** sample the lamp's ball on purpose from the glass surface (by the solid angle it fills) and weigh that
  against the mirror ray, so the reflection is found every frame at its right brightness. On a perfect mirror only
  the mirror ray can find it, so there the honest answer is the pixel's footprint: the ball seen through the
  pixel's cone, not through one line.
- **C guilty:** the same for the highlight: integrate the lobe over the lamp's ball (and the pixel's cone) in place
  of one point of it. And give `direct_light` the lamp's size at all: today it has none.
- **A guilty:** the lamp's light near the flame is right for a small bright thing; a real candle's wax top is its
  brightest part after the flame. What is wrong is that a few bright square centimetres are found by chance by
  reflection and bounce rays. I would look at PT-40 (wax scatters light inside) and at sampling such small bright
  surfaces on purpose. I would not move the lamp and not dim the wax by a rule.
- **None, and "all four" innocent too:** it is the place. Then look at the lamp's own prop's shadows and at the
  Emissive view (the list above).

## 3. Builds, commands

- Native: `%TEMP%\tgb\flame` (NMake Release, `-DTGPT_WITH_NRD=OFF`), script `scratchpad\flame-work\flame-build.cmd`
  (configure, `Build-PtShaders.ps1 -Throttle 8`, then the bridge and the tracer). Logs: `%TEMP%\tgb\flame-trial-*.log`.
- Plugin: `dotnet build src\TGPathTracing\TGPathTracing.csproj -c Release -f net472 -p:TargetFrameworks=net472
  -o %TEMP%\tgb\flame-plugin`. The project has no deploy step; the log names no game folder.
- Source checks: `python tests/source_checks.py`.
- NOT RUN: any GPU test, ctest, the game, NRD (not in this build; its pass includes `pt_common.hlsli`, which the other
  passes compiled), Ray Reconstruction. No fixture was built (the brief: the game first).

## Workarounds met

None removed (this job adds switches). Met and left:
- `light_incident`'s `+ 1e-4` under the square root: a 1 cm floor on every lamp's near light, whatever its size.
- `direct_light` treats every lamp as a point for its highlight (section 1, C).
- `roughness_alpha`'s floor of 0.045 for a lamp's highlight.
- The trial itself is a workaround by design: all of it goes once the game has answered (the four settings,
  `lamp_trial*`, `highlightDir` if C is innocent, the flags' top bits, `jitter[3]`, `wetGrid.w`, the recipe, the check).

## State (newest last)

- Read the brief, AGENTS.md's two rules, AGENT-WORKERS.md, reuse-shift.md. Read the code the suspects live in.
- The four switches written: shaders, tracer, plugin. A source check and the recipe `lamp-trial` added.
- The plugin compiles: 0 errors (`%TEMP%\tgb\flame-plugin-build.log`).
- Source checks: 98 pass (97 and the new one).
- 01:11 Native build ended well: 39 shader passes compiled, 0 failed (446 s, 8 at a time); the bridge and the tracer
  built, 0 warnings, 0 errors (`%TEMP%\tgb\flame\TGPathTracing.Native.dll`, SHA-256 starts `0B4515EBE8E49165`).
  This build has no NRD: its passes were not compiled here.
- 01:12 **HANDED OVER for the game's trial.** Compiled only. Nothing was run: no GPU test, no ctest, no game.
  Whether each switch does in a picture what section 0 says is not checked; the first run of `lamp-trial` is that check.
  The job took about 17 minutes from the copy's creation (00:55). Stopped here, as the brief says.
