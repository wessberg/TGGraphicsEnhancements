# flame-near: what stands next to a flame, found on purpose and shown without burning out (PT-85, PT-40)

Worker `near`, 2026-10-09, copy `wt\near`, base `wt\base61`. Brief: `docs/agents/briefs/brief-flame-near.md`.
The job before: `docs/agents/flame-specks.md` (its four switches A to D still work; mine sit beside them).

STATE: see the last lines of this file. Compiled only. Nothing here was run on a GPU or in the game.

The copy held no edit against its base when I started (`merge_copy.py near --base=base61 --dry`: 0 changed).

## 0. HAND-OVER FOR THE GAME TRIAL

**Two trials, three hidden settings, section `Advanced`, all 0 by default (0 = today's picture).**
Neither moves a lamp. Neither takes light from a surface.

| Setting (`Advanced / ...`) | Trial | Values | What it does |
|---|---|---|---|
| `Lamp trial lit surroundings` | 1, the specks | whole number, cm. 0 off. Try **30**. (10 is the other one to try.) | What stands within that distance of a lamp (wax, holder, the wall behind it) is found ON PURPOSE by the rays that gather light, and the chance hit counts less by exactly as much. Same light, less noise. |
| `Highlight shoulder most` | 2, the burnt wax | number. 0 off. Try **3**, **6**, **12**. | The brightest traced light rolls off smoothly towards this value and never reaches it. Units: what the composite stores (1.0 shows at 0.62 of white after the game's tone mapping). |
| `Highlight shoulder knee` | 2 | number. 0 = where `Highlights kept` stops holding (1.0 unless set). | Where the roll-off starts. Nothing at or under it changes. Leave it at 0 for the first run. |

**The recipe is in the copy: `lamp-near`** (`tests/ingame/scenarios.json`, right after `lamp-trial`). 13 steps, one scene, NRD,
16 frames a step:
`.\tools\harness\Run-PtHarness.ps1 -Scenario tavern-room -Recipe lamp-near`

| step | what |
|---|---|
| 01 | the game's own picture |
| 02, 03 | as now: the picture, the Specular view |
| 04, 05 | trial 1 at 30 cm: the Specular view, the picture |
| 06, 07, 08 | trial 2 alone, most 3, 6, 12: the picture (the Specular view does not show it: it acts in the composite) |
| 09, 10 | both (30 cm, most 6): the picture, the Specular view |
| 11, 12 | the yardstick, switch A at 30 cm, in the same run: the Specular view, the picture |
| 13 | everything back to 0, off |

Trim it as you like. Steps 11 and 12 can go if the old run's column is enough.

**How to read the answer**

Trial 1 (steps 04, 05 against 02, 03 and against A in 11, 12):
- Good: the Specular view's spots fall from about 68 towards A's 19, the picture's from 35 towards 17.
- **The mean must not move.** Compare the mean brightness of step 05 with step 02, whole frame and round one wall candle.
  Step A (11, 12) is darker by design. Trial 1 must not be. If it is darker or lighter by more than the noise between
  two runs of the same step, the trial has a bug: section 1 lists where I would look, in order.
- Burnt pixels (over 240) should NOT fall with trial 1. It leaves the wax as bright as it is. That is trial 2's job.
- The frame time will rise. Each place that gathers light traces up to 3 more short rays. Read the frame time of 05
  against 02. Speed is not tuned; the direction comes first.

Trial 2 (steps 06 to 08 against 02):
- Good: the wax reads as bright cream with its shape, the flame stays the brightest thing, and the room looks the same.
- Burnt pixels fall from about 1,360. Anything under the knee is the same bit for bit, so the room must not change.
- The exposure must not move: the meter and the fog's histogram take the roll-off back out. Check the exposure and
  gain log line of step 06 against step 02. If the gain moved, that is a bug in the way back (section 2).

**The native log says once what is set:**
- "Small lamps on trial ... ; lit surroundings found on purpose within N cm (0: off)"
- "Highlights' shoulder on trial: knee K (0: the hold's), most M (0: off)"

**How it travels (no ABI change, no struct size, no option bit, no export):**
- Trial 1: bits 14-21 of the number flame-specks already sends (`TGPT_Frame.jitter[3]`, then `FrameConstants.wetGrid.w`).
  The tracer's clamp on that number went from 14 bits to 22 (a float holds them exactly).
- Trial 2: `TGPT_Frame.frameReserved` (was "0, keeps the record a multiple of 8 bytes"). One whole number: the knee in
  64ths in bits 0-11, the most in 64ths in bits 12-23. Then `FrameConstants.radiance.z`, which no shader read (it held
  the old radiance clamps' reference; the tracer still computes that and I overwrite the slot afterwards). For the
  D3D11 fog pass: `FogConstants.look[1]` and `look[2]`.
- **THIS JOB TOOK: bits 14-21 of `jitter[3]` / `wetGrid.w`; `frameReserved`; `FrameConstants.radiance.z`;
  `FogConstants.look[1..2]`.** Tell other workers.

**Files changed** (against base61):
- `native/pathtracing/shaders/pt_common.hlsli`: trial 2's three functions by `highlight_hold` (`highlight_shoulder`,
  `highlight_rolled`, `highlight_unrolled`) and one line in `traced_ungained`. Trial 1: `lamp_around`, `NearRay`,
  `near_ray_pdf`, `near_split`, `near_first` above `direct_light`; `direct_light` (the share the chance hit keeps);
  `lamp_around_light` after `subsurface_vertex`; `cache_sample`, `shade_path`, `shade_seen` (they arm it and add it).
- `native/pathtracing/shaders/pt_gi.hlsl`: trial 1 for a camera pixel's bounce rays (a candidate of ReSTIR GI's).
- `native/pathtracing/shaders/pt_specular.hlsl`: trial 1 for a camera pixel's reflection ray.
- `native/pathtracing/shaders/pt_compose.hlsl`: trial 2, one line after the hold.
- `native/pathtracing/shaders/d3d11.hlsl`: trial 2's way back in the fog pass's copy of `traced_ungained`.
- `native/pathtracing/src/tracer.cpp`: the clamp, two log lines, `radiance[2]`, `look[1..2]` of the fog pass.
- `native/pathtracing/include/tgpt_api.h`: comments only.
- `src/TGPathTracing/Settings.cs` (three hidden settings), `SceneLights.cs` (`LampTrial` packs one more),
  `HdrpPasses.cs` (`HighlightShoulder`, `FrameReserved`).
- `tests/source_checks.py`: one new check, `test_path_tracing_flame_surroundings_on_trial_default_off`. Three pins
  changed, each with its reason in a comment: the clamp `16383.f` is now `4194303.f`; `subsurface_vertex(` is called
  5 times, not 4; `g_mirrorRay = ` is written 3 times, not 1.
- `tests/ingame/scenarios.json`: the recipe `lamp-near`.
- `docs/agents/flame-near.md`: this report.

**With everything off the shaders do what they did before.** By reading the code, not by a picture:
- Trial 1 off: `lamp_around()` is 0. Nothing is armed, no ray is traced, no random number is drawn. The share the chance
  hit keeps is exactly 1.0, and the added light is exactly 0.
- Trial 2 off, or a pixel at or under the knee: the factor is exactly 1.0, and the way back returns its input.

## 1. Trial 1: found on purpose

**The problem in one line.** A candle puts I / d^2 on the wax 2 cm away. A bounce ray from across the room lands on
those few square centimetres about once in thousands of tries, and then brings back thousands of times the usual. That
is a speck.

**What the trial does.** It adds a second way to find that same light, and splits the light between the two ways so it
is counted once. This is standard: multiple importance sampling with the balance heuristic (Veach and Guibas, 1995), the
same idea the tracer already uses for emissive triangles (sampled as lights inside their reach, met by paths outside it).

1. On purpose (`lamp_around_light`). At the place a gathering ray leaves from:
   pick one lamp from that place's light lists; shoot one ray from the lamp's centre in a random direction;
   take the first thing it meets within the setting's distance; work out the lamp's light there exactly as a path
   vertex there would (same formula, its own shadow ray to the lamp's disc); trace one ray back to see if the place
   can see that point.
   Why this is quiet: a ray from the lamp lands most often where the lamp's light is strongest. The 1 / r^2 of the
   light and the r^2 of the chance cancel. What it brings is as large 2 cm from the flame as 20 cm.
2. By chance (as before): the place's own ray happens to meet such a point. Its lamp light now counts for
   `1 - s a / (a + b)` of the diffuse part. The on-purpose way counts `s b / (a + b)`.
   - `a`: how likely the lamp's ray was to land there (per area).
   - `b`: how likely the place's own ray was to land there (per area).
   - `s`: 1 up to half the setting's distance from the lamp, fading smoothly to 0 at the full distance.
   Close to the flame `a` is huge, so the chance hit keeps almost nothing: no speck. Far from it, or when the place
   is itself right next to the point, `b` wins and the chance hit keeps almost all.
3. The chance hit only gives up its share where the lamp's own ray could have found the point (`near_first`: one short
   ray from the lamp to the point). Where it could not (behind the holder, inside the lamp's own 1.6 cm), the chance
   hit keeps everything. That is what keeps the sum exact.

**Where it is switched in** (each place both adds the on-purpose light and tells the next vertex to count less):
- a camera pixel's bounce rays (`pt_gi`): the on-purpose light is one more candidate of ReSTIR GI's, its point the sample.
- a camera pixel's reflection ray (`pt_specular`): weighed with the pixel's own lobe, so a mirror keeps its chance ray.
- every later bounce of a path (`shade_path`).
- the rays that fill the radiance cache (`cache_sample`).

**Left to chance alone, on both sides, so nothing is lost or doubled:** see-through glass (met or in the way), thin
cloth (met or passed), a leaf lit from behind, water, a leaf's own bounce rays, a smooth metal's second reflection, the
water's path. The highlight a lamp makes on the point it lights is left whole too: only the diffuse part is split.

**Where I would look if the mean moves.** Honest list, most likely first. None of this was run.
1. **Wax and skin (things light walks inside).** For these the point the lamp lights and the point the light leaves from
   differ. The chance ray walks from where it lands to where the lamp shines. The on-purpose way walks the other way.
   The two agree only as far as the walk is the same both ways. For one candle of one wax color that should hold. Where
   the color changes across a walk's reach, or the mask says "part subsurface", it can differ a little.
   To test: the wax is exactly where trial 1 matters most, so compare the mean on the wax and on the wall next to it apart.
2. **ReSTIR GI's reuse** of the on-purpose sample. Its stored light carries the split's factor for the pixel that found
   it. A neighbour reuses it with that pixel's factor, not its own. The two ways' factors still add to 1 for every
   point, so by my reading the mean holds. It is the part I am least able to prove on paper.
3. **Textures read at another mip.** The point's color is read with the lamp ray's footprint, not the chance ray's.
4. **Two surfaces closer than half a millimetre** (a decal mesh on a wall): the lamp's ray finds the front one, the
   chance hit on the back one still gives up its share. A little light lost there.
5. **Eyes** use a smaller ray offset in a reflection; the on-purpose way uses the ordinary one.
6. Water was not looked at (none in the tavern).

**Cost.** Up to 3 short rays more at each place that gathers light (the lamp's, its shadow ray, the way back), and one
more where a chance hit lands within the setting's distance of a lamp. The shader passes also grew: see the compile
times in section 4. Nothing is tuned.

**What the picture may show instead of specks.** The on-purpose way takes ONE lamp and ONE direction a try. Many tries
find nothing (the lamp drawn is an emissive cluster, the lamp's ray meets nothing within the distance, the way back is
blocked). When one does find light it brings about as much as one sample of that lamp's direct light: never a speck,
but a fine, even grain in the bounced light and the reflections near lamps. If the game shows that grain where the
specks were, the direction is right and the next step is to let several lamps compete for each try (as `direct_light`
does with eight), not to give the idea up.

**What it does not do.** A mirror-like surface that reflects the wax still shows the wax as bright as it is. That is a
true mirror image, not noise. And the wax itself on screen is as bright as before: trial 2.

**Is this a fake?** No. Both ways trace real rays to the real lamp and the real surface. No light is clamped, moved or
invented. The setting's distance only says where the second way helps; outside it everything is as before.

## 2. Trial 2: the shoulder

**What it does.** In the composite, after the hold, a pixel's traced light L (as stored: radiance times HDRP's exposure
times the gain it kept) above the knee K becomes

`L' = K + (L - K) / (1 + (L - K) / (M - K))`

- At K it leaves with the same slope as the line: no ring.
- It never reaches M, and brighter stays brighter: shape is kept.
- Diffuse and specular light are scaled by the same factor: no change of color.
- At or under K the factor is exactly 1.
- What a surface emits itself is outside it, as it is outside the hold. A flame stays the brightest thing.

Numbers, K = 1: a pixel at 20 (wax under a flame) shows at 2.8 with M = 3, at 5.0 with M = 6, at 8.0 with M = 12.
A pixel at 2 shows at 1.67, 1.83, 1.92.

**The meter and Fog Adaptation.** Both read the picture back without the gain through `traced_ungained`. That function
now undoes the shoulder first (`highlight_unrolled`), then the hold as before. `pt_meter` uses it as it is. The fog
pass has its own copy in `d3d11.hlsl`, which got the same lines and reads K and M from `FogConstants.look`. So the
automatic gain should not notice the shoulder. Very near M the way back is held at 0.9999 of the room, so a pixel
that sat thousands of times past the knee is read back a little low: it lands in the meter's top bin either way.

**What to know before judging it:**
- It acts on each pixel as the composite sees it. With NRD that is after the denoiser: good. With Denoising "None" and
  with Ray Reconstruction it is before any cleaning, as the hold already is: a noisy pixel is rolled off sample by
  sample, so the mean of a noisy bright pixel comes out lower than the roll-off of its mean. Judge it with NRD.
- It is a display curve, in front of the game's own tone mapping. It changes how bright light is shown, not how much
  light there is: reflections and bounces of the wax elsewhere in the room are untouched.
- The Dark Adaptation row's tooltip already says "The brightest parts of the view are kept from turning white". With the
  shoulder that would become true above the game's own exposure too.

## 3. The coordinator's claim, and what HDRP does next to a light

**The claim: "lowering a candle's power alone cannot fix wax against wall". It is right.**
- A lamp's power scales everything it lights by the same factor. The wax 2 cm away and the wall 3 m away keep their
  ratio: (3 / 0.02)^2 = 22,500. The room's exposure follows the room, so the wax is as far past white as before.
- To bring wax at 1,000 times I under white by power alone, the room would have to be 1,000 times darker.
- One limit to the claim: it holds where the candles are what lights the room (the tavern at night). In a room lit
  mostly by something else (daylight, a hearth), a dimmer candle would burn its wax less against the room.

**But the game's numbers do mean something else than the tracer takes them to mean. This is the real finding.**
- HDRP does not light by I / d^2. It lights by `I / (d^2 + r^2)`, r the light's Radius. Checked in the HDRP source in
  the scratch folder (`hdrp-src/Lighting_LightEvaluation.hlsl`, `PunctualLightAttenuationWithDistanceModification`:
  "distance contains light size modification. See ModifyDistancesForFillLighting"). The project already had this
  (`docs/agents/lights-2.md`, CHANGELOG): the radius squared is stored "to simulate a fill light".
- The tavern's big candle lights are 21,149 to 24,775 candela with a Radius of 1 m (`lights-2.md`'s table).
  A real candle is about 1 candela. So these are not candles by their numbers. They are metre-wide soft balls of light
  that an artist tuned until the ROOM looked right.
- What that gives, for a light of intensity I:

  | distance | the game (r = 1 m) | the tracer (flame 0.6 cm) | tracer / game |
  |---|---|---|---|
  | 3 m | I / 10 | I / 9 | 1.1 |
  | 1 m | I / 2 | I | 2 |
  | 30 cm | I / 1.09 | 11 I | 12 |
  | 10 cm | I / 1.01 | 100 I | 101 |
  | 3 cm | I / 1.0009 | 1,111 I | 1,112 |

- So in the game nothing ever gets more than I from such a light. The wax is 10 times the wall at 3 m. In the tracer it
  is 10,000 times. No artist ever saw this wax under I / d^2.
- Beyond a metre the two agree within a factor of 2, so the room is lit about as the artist meant.
- HDRP's own floor on the distance is 1 cm (`PUNCTUAL_LIGHT_THRESHOLD`, also in that file). The tracer has the same
  (`light_incident`: 1.17 cm for a 0.6 cm flame). It is not what made HDRP soft. The radius is.
- A light layer that leaves the candle's own mesh out: NOT CHECKED. It needs the game (the rendering layers of the
  candle's renderer against the light's). `SceneLights.LightsWorld` reads a light's layers; nothing reads a candle
  mesh's yet. `lights-2.md` says the wax is in HDRP's G-buffer and was lit by this light, which speaks against it.

**What follows, as I see it (a view, not a measurement):**
- The owner's ruling stands and physics agrees with it: a small flame does put 10,000 times more light on its own wax
  than on the far wall. A camera exposed for a candle-lit room does burn the candle's top white. An eye does not,
  because it adapts locally and the wax spreads the light inside it (PT-40). So the honest fixes are the two trials:
  find that light without noise (1), and show it as an eye would (2). Not a dimmer lamp, and not HDRP's soft ball.
- One thing worth a ruling later: the tracer is 2 times brighter than the game at 1 m and 12 times at 30 cm, on
  tables and walls near candles, not only on wax. That is "more realistic" by the ruling, but it is a change of the
  room's look that came with the flame-sized lamps, and the automatic gain hides part of it.

**Switch D (shadow rays at HDRP's disc): by reading the code, it works mostly through the same near light.**
- With D at 1 the shadow ray aims at a random point of a 30 cm disc round the lamp. For wax 3 cm from the lamp that
  point is up to 84 degrees off the lamp's centre, and the ray travels up to 30 cm sideways through the holder, the
  wall and the next candle. Many are blocked. The light's size on the wax is not changed (still I / d^2 from the
  centre), so the wax simply loses the blocked share.
- That fits the run: D took a third of the burnt pixels away. Softer shadow edges elsewhere cannot do that.
- So D is partly a hidden A. Do not read it as "shadow edges are guilty". D at 2 (a 3 cm disc) would be the clean test.

## 4. Builds, commands

- Native: `%TEMP%\tgb\near` (NMake Release, `-DTGPT_WITH_NRD=OFF`), script `scratchpad\near-work\near-build.cmd`
  (configure, `Build-PtShaders.ps1 -Throttle 8`, then the bridge and the tracer). Logs: `%TEMP%\tgb\near-*.log`.
- Plugin: `dotnet build src\TGPathTracing\TGPathTracing.csproj -c Release -f net472 -p:TargetFrameworks=net472
  -o %TEMP%\tgb\near-plugin`. 0 errors, 0 warnings.
- Source checks: `python tests/source_checks.py`.
- The recipe was added by `scratchpad\near-work\add_recipe.py`.
- **`d3d11.hlsl` is not compiled by the build.** Its text is put into the DLL and compiled when the game starts
  (`make_cs("TGPT_FOG")`, FXC, `cs_5_0`). So I compiled its fog pass by hand with the Windows SDK's `fxc.exe`
  (`/T cs_5_0 /E main /D TGPT_FOG=1`), the base's and mine: both compile, mine with no warning. No GPU needed.
- **Compile times rose** (one core each, 8 side by side; trial 1 adds its code to every pass that gathers light):

  | pass | the job before (s) | now (s) |
  |---|---|---|
  | `pt_walk` | 131 to 157 | 169 to 183 |
  | `pt_gi` (three kinds) | 282 to 320 | 467 to 510 |
  | `pt_specular` (three kinds) | 433 to 446 | 554 to 570 |
  | all 39, side by side | 446 | 570 |

  Expect the game's first start with this build to take longer by about as much (it makes the same passes). If trial 1
  becomes the rule, its three call sites in a path should become one.
- NOT RUN: any GPU test, ctest, the game, NRD's own pass (not in this build), Ray Reconstruction. No fixture was
  built (the brief: the game first).

## Workarounds met

None removed (this job adds two trials). Met and left:
- `light_incident`'s `+ 1e-4`: a 1 cm floor on every lamp's near light.
- The hold and now the shoulder act before Ray Reconstruction and on undenoised pixels with Denoising "None".
- pt_gi's cache feed holds the ratio of the two normals' cosines at 4. I kept the same hold for the on-purpose light.
- `FrameConstants.radiance.z` still gets the old clamps' reference computed for it, then my number over it. If the
  shoulder stays, that computation (`clampReference`) can go, with its source check's pin.
- The trials themselves: all of it goes or becomes the rule once the game has answered (three settings, `lamp_around*`,
  `near_*`, `NearRay`, `highlight_shoulder` and its two helpers, the recipe, the check).

## State (newest last)

- Read the brief, flame-specks.md, AGENTS.md's rules, AGENT-WORKERS.md. Read the code both trials live in.
- Both trials written: shaders, tracer, plugin. A source check and the recipe `lamp-near` added.
- The plugin compiles: 0 errors.
- Source checks: 99 pass (98 and the new one).
- First native build failed at once: I had named a parameter `point`, a reserved word in HLSL. Renamed.
- Second build compiled all 39 passes. Three small rewrites followed while it ran (to keep three lines other source
  checks pin word for word), so I did not count it.
- 04:37 Third build, from the final text: 39 shader passes compiled, 0 failed (570 s, 8 at a time); the bridge and the
  tracer built, 0 errors (`%TEMP%\tgb\near\TGPathTracing.Native.dll`, SHA-256 starts `BB4B866D116FD7E`). No NRD in
  this build: its pass was not compiled here. The fog pass of `d3d11.hlsl` compiled by hand with FXC (section 4).
- Source checks again: 99 pass. Plugin again: 0 errors, 0 warnings. `tests/harness_analyze_test.py`: 6 pass.
- `merge_copy.py near --base=base61 --dry` against the main tree: 12 changed, 1 new, 0 conflicts
  (`pt_common.hlsli`, `tracer.cpp` and `source_checks.py` merge three ways: the main tree moved on in them).
- **HANDED OVER for the game's trial.** Compiled only. Nothing was run: no GPU test, no ctest, no game.
  Whether each switch does in a picture what section 0 says is not checked; the first run of `lamp-near` is that check.
  It took about 2 hours, not the 45 minutes the brief aimed at. Trial 1 is the reason: it has to be exact in four
  places to keep the mean, and each native build is 10 minutes. Stopped here, as the brief says.
