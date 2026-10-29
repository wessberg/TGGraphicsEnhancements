# Grass agent, round 2: thin cloth under the sky (PT-39), and a leaf's light from behind (PT-03)

Written 2026-10-06 in the copy `wt\grass2` (base `wt\base4`). Nothing deployed, nothing committed, the game never started
by these agents. GPU tests ran only with no harness run going (`artifacts\harness\RUNNING`), the game closed and the GPU
free. The first agent was stopped at 20:58 (its history had grown too long), and a second one finished the job from the
files on disk.

## In short

- Built in the copy, and the offline harness passes: `tgpt_gpu_test` all checks pass (654 ok, 0 FAIL; DLL hash D9B1EED4E7D7, the same built and installed), `tests\source_checks.py` 78 of 78.
  Not seen in game.
- PT-39: thin cloth (canopies, awnings, tents, laundry, curtains, banners) now lets 0.3 of the sky's and bounced light
  through to what is under it, tinted by its colour, and a cloth pixel is lit by the sky behind it. Its partial pass for
  lamps and the sun came in round 1.
- PT-03: a leaf's or blade's light from behind uses HDRP's wrapped cosine. Straight from behind it is 0.795 of what the
  plain cosine gave, and a blade edge-on to the light now gets some. The total over all directions is the same.
- The second agent fixed two bugs in the new cloth code (see "What the failing check was"). Before the fixes, the new
  check failed: 0.21 of the light came through, where 0.30 was expected.

## What changed and why

1. **Thin cloth passes the sky's and bounced light** (`pt_common.hlsli`: `g_bounceRay`, `g_clothMet`, `cloth_walk`,
   `cloth_stops`, `trace_bounce`; `pt_gi.hlsl`; `pt_gi_spatial.hlsl`; `pt_rtxdi.hlsli`). The owner's candle corner in the
   `candle` scene stands in a building's shade at 10:47. No sun reaches its red canopy, so round 1's partial pass for
   lights had nothing to let through. Its light is the sky's, and every sky ray stopped at the cloth. Now:
   - The rays that gather light for a surface (pt_gi's first ray, a path's next ray, the radiance cache's ray) pass
     cloth by the same 0.3 chance as a light's ray. `cloth_walk` then tints what they bring by the colour of each cloth
     they passed.
   - A sample keeps its light divided by the chance its ray had of getting there (`held`: 1 / 0.3 for each cloth passed,
     1 / 0.7 where cloth stopped it). The ray that checks the sample is seen (`pt_gi_spatial`, and RTXDI's visibility test
     `rtxdi_gi_visible`) passes cloth by chance and takes the tint. A sample reused across a canopy's edge is dimmed by
     the canopy, as the pixel's own sample is.
   - `gathers_behind`: a cloth pixel drawn deferred gathers light from behind it as a leaf does, by the share it lets
     through. Under an open sky, a canopy's underside glows in its own colour. Hair is still excluded.
   - A character's clothes (a material named like cloth on an instance flagged `INSTANCE_CHARACTER`) stop lights and rays
     whole again. A tunic is no canopy, and the body under it is often not drawn.
   - Reflection rays still see cloth as solid.
2. **A leaf's light from behind, wrapped as HDRP wraps it** (`leaf_transmittance`, `leaf_cos` in `pt_common.hlsli`, used
   by `pt_di_spatial.hlsl` for lamps and the sun). The front is lit by the plain cosine. From behind, the light is the
   profile's transmittance times (w - N.L) / (1 + w)^2, with w = cos(75 degrees), as HDRP's Lit does. It applies only
   where HDRP's transmittance for the pixel is known (a deferred leaf with a foliage profile); everything else keeps the
   old path (`backTint`).
3. **Two fixes by the second agent**, both in the new cloth code:
   - `g_raySalt` (`pt_common.hlsli` `ray_seed`, set only in `pt_gi_spatial.hlsl`): the check ray gets its own random
     number at the cloth.
   - The firefly clamp now comes before `held` in `pt_gi.hlsl`: `held * clamp_radiance(...)`.

## What the failing check was

At the take-over, the build of 20:39 held the finished shaders. The last run had 653 checks passing and one failing: the
new check 10. It measured 0.21 of the sky's light through the red canopy (0.30 expected), and green was 0.328 of red's
share (0.107 expected). The test still held a block marked DIAGNOSTIC; it has been removed.

Measured on the floor under a red cloth canopy, sky only, comparing the same pixels three ways: with the cloth, with a
solid canopy, and with no canopy. Through = (cloth - solid) / (none - solid), per channel. The right answer is 0.30 in
red and 0.032 in green (0.107 of red, the cloth's colour).

| Variant | red | green | blue |
|---|---|---|---|
| pt_gi's own rays alone (`GI_OWN_RAYS`, no clamps or boiling filter) | 0.303 | 0.032 | 0.024 |
| the reservoir, no reuse, before fix 1 | 0.365 | 0.036 | 0.025 |
| the same, after fix 1 | 0.298 | 0.032 | 0.024 |
| no reuse with the clamps, before fix 2 | 0.212 | 0.027 | 0.023 |
| no reuse with the clamps, after both fixes (the check as it stands) | 0.30 | 0.032 | |
| default pipeline (both reuses), after both fixes | 0.327 | 0.075 | 0.075 |
| the same, deep under the canopy only (solid below 0.1 of none) | 0.316 | 0.048 | 0.041 |
| with `TGPT_OPTION_GI_VISIBILITY` (Crevice Light row) | 0.342 | 0.038 | 0.027 |
| spatial reuse only / temporal only (no clamps or boiling filter) | 0.329 / 0.320 | 0.075 / 0.065 | 0.072 / 0.062 |

1. **The check ray decided the same way as the ray that found the sample.** In about one case in ten, pt_gi_spatial's
   check ray got the same random number at the cloth as pt_gi's ray. Its direction differs only in the last bits. The
   ray had passed, so the check ray passed too, and light through cloth came out 1.2 times too bright (0.365). Fixed by
   `g_raySalt`. It is 0 for every other ray, so nothing else changes.
2. **The clamp cut the samples that passed cloth.** Such a sample carries its light times 1 / 0.3. The firefly clamp
   (`LIMIT_INDIRECT`) came after that factor and cut the sky through a canopy to about two thirds (0.21). Fixed: clamp
   first, then multiply by `held`.
3. **With reuse, the reference is darker, not the cloth brighter.** ReSTIR's reuse with Basic bias correction (the
   default) darkens the floor near an edge that blocks light. Here it darkened the solid canopy's floor by 16 % in red
   and the floor with no canopy by 6 %. tracer.cpp already records this: "the canvas's front edge 14 % short". The cloth
   canopy's floor darkens less, because light through the cloth fills in what its samples lose. Cloth minus solid then
   also holds the solid canopy's untinted loss. With the ray-traced bias correction (`GI_VISIBILITY`) the colour is right
   again (green 0.111 of red). Deep under the canopy the default is close (0.316, green 0.15 of red). So check 10
   measures without reuse, and its comment records the reuse numbers.

## Offline numbers (final build, `%TEMP%\tgb\grass2-test-final.txt`)

- a blade edge-on to the sun (2037 pixels, its front 0.100 towards it): 4.89 5.50 1.82 times its front's light alone
  (4.89 5.50 1.82 expected).
- a blade lit from behind by the sun: 7.68 8.88 1.62 of the grey half (7.67 8.87 1.62 expected).
- a red cloth card's shadow (round 1's check, unchanged): 0.30 through in red, green 0.107 of red's share.
- a cloth sheet seen from below under the open sky: 0.33 0.29 0.29 of the sky's light from behind it (0.30 expected).
- the floor under a red cloth canopy, sky only, no reuse (26403 pixels): 0.30 through in red, green 0.107 of red's
  share (0.30 and 0.107 expected).
- Everything else as on the base: the run on base4 at 19:58 (`grass2-test-base.txt`) passed its 632 checks.

GPU times were not compared: two other agents ran GPU tests on the same GPU this evening, and AGENTS.md says timings
mean nothing then. The cost to watch in game: `cloth_walk` adds one query for each bounce ray that passed cloth.

## Mutation checks (break on purpose, see the check fail, put it back)

| Set | Broken on purpose | Result |
|---|---|---|
| A | `leaf_cos` back to the plain cosine | edge-on blade 1.00 1.00 1.00 of the front's light (4.89 5.50 1.82 expected): FAIL "a leaf's light from behind is wrapped past the terminator as HDRP wraps it". The blade lit from behind passes either way (7.79 against 7.67 expected): the test's sun is nearly straight behind it, where the two cosines differ by 1.5 % |
| A | `gathers_behind` for leaves only, not cloth | the cloth sheet under the sky takes 0.01 of the sky's light from behind (0.30 expected): FAIL "thin cloth is lit by the sky behind it, as much as it lets through" |
| A | `g_raySalt` removed (the check ray decides as the sample's ray) | 0.37 through, green 0.098 of red: FAIL "the sky's light passes thin cloth to what lies under it, tinted by its color" (bound 0.36; a narrow margin) |
| B | clamp after `held`, and the check ray's tint removed | 0.17 through (the clamp), green 1.309 of red (no tint): FAIL check 10 |
| C | `CLOTH_TRANSMISSION` 0 | does not compile: DXC rejects `pow(1.0 / 0.0, ...)` in pt_gi ("No indefinite logarithm"). Run with 0.01 instead: 5 checks fail, among them round 1's card shadow (0.01), check 9 (0.03 0.01 0.00) and check 10
(0.01); the leaf checks pass |
| D | bounce rays stop at cloth again (`g_bounceRay` out of `alpha_opaque`'s cloth test) | check 10: 0.00 through: FAIL; check 9 (0.32) and round 1's card shadow (0.30) still pass |

Set B's run also showed set A's edge-on failure. My restore put back the files' old timestamps, so the build kept set A's
compiled pt_di_spatial. Set B's own check (check 10) is not affected: its mutations were in pt_gi and pt_gi_spatial, and
check 10 has no sun or lamp. After that, restores give files a fresh time, and every shader was touched before sets C and
D and the final build.

## Files changed (against `wt\base4`)

- `native\pathtracing\shaders\pt_common.hlsli`: `g_bounceRay`, `g_clothMet`, `g_raySalt`; `alpha_opaque`'s cloth test
  for bounce rays and not characters; `leaf_transmittance`, `leaf_tint`, `leaf_cos`; `gathers_behind` with cloth;
  `cloth_walk`, `cloth_stops`, `trace_bounce`; `cache_sample` and `shade_path` through `trace_bounce`.
- `native\pathtracing\shaders\pt_gi.hlsl`: bounce rays through cloth, `held` and `arrives`, the clamp before `held`.
- `native\pathtracing\shaders\pt_gi_spatial.hlsl`: the check ray through cloth by its own chance, tinted.
- `native\pathtracing\shaders\pt_rtxdi.hlsli`: `rtxdi_gi_visible` through cloth by chance.
- `native\pathtracing\shaders\pt_di_spatial.hlsl`: `leaf_cos` for lamps and the sun on a leaf with HDRP's transmittance.
- `tests\pathtracing_gpu_test.cpp`: check "a blade lit from behind" now expects the wrapped cosine; new checks "a leaf's
  light from behind is wrapped past the terminator as HDRP wraps it" (in 2), 9 "thin cloth is lit by the sky behind it,
  as much as it lets through", 10 "the sky's light passes thin cloth to what lies under it, tinted by its color";
  `accumulate` takes extra options.
- `tests\source_checks.py`: the new lines pinned (cloth for bounce rays, not characters, `trace_bounce` used three times,
  `held` and `arrives`, the clamp before `held`, `g_raySalt`, `leaf_cos` used twice), and the three check names.
- `CHANGELOG.md`: the two entries, with the numbers.
- `docs\agents\grass-2.md`: this report.

## What to look at in the next in-game harness run

- `candle` and `candle-up` (PT-39): the market's candle corner under the red canopy. In game after round 1 it was
  32.7 of 255 against the game's 92.4. Expect the canopy's underside to glow red and the corner under it to rise, but not
  to 92: a red canopy passes 0.3 of the sky in red, about a tenth of its brightness, while the game's corner is lit by
  an ambient light that nothing shades. Look for red light spreading past the canopy's edge, and for speckles or boiling
  under the cloth (a sample that passed cloth carries 3.3 times its light).
- `forest` (PT-03): the bright yellow-green blades with the sun behind them should be up to a fifth dimmer, and blades
  edge-on to the sun a little brighter. Compare the blades' brightness and colour with the game's picture.
- `vista`: grass and trees seen far away. The total light from behind is unchanged, so the overall exposure should not
  move. Read the exposure and gain log line anyway.
- `npc-day`: characters' clothes. A material named like cloth on a character now stops lights whole again (round 1 let
  0.3 through), so capes and sleeves cast full shadows as before round 1. Check that NPC clothing looks as it did before
  round 1.

## Risks: other cloth that will look different

Every material the plugin marks as thin cloth (by name, `SceneCapture.cs`) on a non-character instance:
- Tents: the wall seen from outside glows from the sky behind it, and the inside gets the tinted 0.3 of the day. Expect
  tent interiors to be brighter and coloured by the canvas.
- Awnings and market stalls: the goods under them get 0.3 of the sky through the cloth, in its colour.
- Laundry on a line, flags, sails: lit from behind by the sky, so they look lighter against the sky.
- Curtains: a room behind a curtain gets 0.3 of what lies beyond it. A dark room behind a sunlit curtain gets brighter.
- Banners on a wall: barely change, because behind them is the wall in its own shade.
- Noise: a sample through cloth carries 1 / 0.3 of its light, and 1 / 0.3^n for n layers (`cloth_walk` stops at 4).
  Since the clamp now comes before that factor, a sample under four layers can be 123 times the clamp's limit, at a
  chance of 0.3^4. Watch stacked cloth (a tent's two walls, layered laundry) for speckles.
- Not covered: `pt_hdrp_shadow.hlsl` (the bounced light for the surfaces HDRP shades) and `pt_probe.hlsl` still see cloth
  as solid.
- With the default reuse, the floor near a cloth canopy's edge is lit a little more evenly than near a solid one. That
  is right: it is the solid canopy's floor that reuse makes too dark (cause 3 above).
