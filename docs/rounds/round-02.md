# Round 2 - TG Path Tracing in game: RTXDI, its own exposure, NRD, materials, water, characters

Dates: 2026-09-26 evening (after commit c269d60) to 2026-09-28 01:34 (commit 166bd58). Mostly in-game rounds: the owner
plays, reports and takes F8 captures; the agent fixes, runs the harness and deploys. Written on 2026-09-28 from the
agents' dated notes. Times are local unless marked UTC. Technical detail: `docs/PROJECT-CONTEXT.md` section 9; every
change with its numbers: `CHANGELOG.md` "TG Path Tracing".

## The RTXDI move (2026-09-26 evening, deployed 22:56)

- The owner chose NVIDIA RTXDI 3.1 ("Option 2"). ReSTIR DI, then GI, moved onto it; GI's history went from 1 to 8
  (RTXDI's default), with its permutation sampling for the denoisers.
- Claude Opus and GPT-6-astra reviewed the move and flagged the same top issue, the likely cause of the owner's
  hallway "SSGI" complaint: the radiance clamps were measured on screen, so a sunlit bounce into a dark hall (~80x over
  the limit) rose and fell with the exposure as the camera turned. Clamps now measure against a scene reference. A new
  harness check renders the same frames at exposures 0.25 and 25: old 14 % apart, new 0.025 %.
- RTXDI gotchas found and fixed (PROJECT-CONTEXT 9.5). DI visibility reuse measured: a lamp's shadow edge 0.8 -> 14 px
  for 13 % less night noise, so it stays off.

## Exposure, round after round (2026-09-26 23:00 to 2026-09-27 03:25)

- **23:00.** FoA's gameplay exposure is not automatic (a physical camera outdoors, a fixed EV 14 in the tavern), so
  the "SSGI" catch-up wasn't exposure. With only the real lights traced, interiors were black. The owner chose automatic
  exposure while tracing. Requirement: everything lit only by traced light, so placed lights get HDRP lightDimmer 0
  (their fog kept). "Darkens as I walk up" was a LOD switch.
- **00:1x deploy.** Owner: streaks fixed, auto exposure good, tavern better, characters mostly fixed. Forward pixels
  (hair, eyes) traced along a camera ray; within 30 m the finest LOD is traced.
- **00:35, judged worse.** Removing the fog's teal ambient took bright pixels out of the metered image, auto exposure
  climbed to its floor, and flames blew out; "Christmas lights" (leaks, glints, noise); red haze from fill lights' fog.
  Lesson kept: anything that removes light moves the exposure.
- **00:55 and 01:1x.** Still overblown (exposure pinned at its EV floor). The Surfaces view showed skin lit like a leaf:
  the game authors human skin as a THIN profile, so solid now means Regular mode or a name with "skin". Two NPCs sat
  dark under a hanging lamp; white seam lines; a candle with "SO much light".
- **~01:35.** F8 became a full capture: report.txt and JPGs of every view, so the agent reads the scene itself.
- **~02:00.** The boiling filter stopped counting its own sample; the GI Jacobian limits; bilinear alpha.
- **~02:20, decoupled exposure.** HDRP keeps the game's exposure; only the traced light gets a gain from its own
  histogram. 02:28 captures, still "extremely overblown": the gain was pinned at x64 and the light data (PROJECT-CONTEXT
  section 2) showed the game's lights balanced for EV 13 at physical falloff, with HDRP ranges cutting them short.
- **~03:10.** Lights by what they belong to (a candelabra's `PointLight_Ambience` is traced), physical reach (50 lux,
  up to 40 m), CLO's light found (DontSave), no sky indoors. The owner kept Reflection Boost at 0.2.
- **Overnight (~03:25; the owner: "implement all of the agent's feedback").** Light cookies, a 0.3 m flame radius cap,
  RR's hit-distance and colour-before-transparency guides, hair's transparent layer hidden, coverage-preserving alpha
  tiles, characters to 45 m, and more (CHANGELOG).

## RR back, and the gain's rules (2026-09-27 morning)

- **09:50.** Owner: "no longer completely overblown, but super boily", eyes white, oversmoothed, "big improvement".
  GPU 18-24 ms (was ~10): with physical reach every pixel has lights (11 a cell on average, 43 at most).
- **10:04 capture.** Ray Reconstruction had been OFF: TGBU refused every frame on the new R16_FLOAT hit-distance guide
  (read through a colour view: "Unsupported input format 54"), and after 5 refusals RR turned off for the session.
  Fixed; an on-screen notice now says when RR is chosen but not running. Lesson: the TGPT harness never runs RR.
- **~10:10.** Light-grid candidates drawn by luminance over distance squared: night DI error -8 %, GI -6 %.
- **10:13-10:20, RR on.** Owner: "light years better", "starting to look incredible". The tavern a bit bright,
  exteriors a bit dark (the median rule darkened sunny views): the gain never goes below 1 now; Exposure and Dark
  Adaptation rows. Eyes: traced they took the sclera texture (white); left out, the camera ray lit the socket skin
  behind them. They became camera-only + HDRP-shaded.
- The F8 JPGs had been sRGB-encoded twice (washed out). RenoDX was blamed first, wrongly: it isn't installed. Lesson:
  check a mod is loaded and check a theory with numbers before telling the owner.
- **11:00.** A candle-lit bedroom "too contrasty?": 42 % of the frame near-black. That is lighting design, exposure
  placement and the tonemapper's toe, not lost energy.

## NRD, Shadow Detail, an honest reference, AMD (2026-09-27 midday)

- The owner asked for a better fallback denoiser for GPUs without RR or Ray Regeneration (an RX 7900 XTX), citing DOOM:
  The Dark Ages. NRD 4.17.3 (RELAX) on the bounced light, built by a background fork, deployed 12:13. An astra review:
  2D motion, NRD's own units, camera cuts reset every history, license notices shipped.
- Shadow Detail (astra's design; the owner asked for an A/B row). TGBU's row names "FSR 3.1" / "FSR 4".
- **11:15.** An honest reference (no reuse, no clamps, no filters): ReSTIR was 21 % dark next to a light 10 cm from a
  wall, because of the GI boiling filter. The earlier "1.6x too bright" came from a bad reference. Boiling Filter row
  at 11:26.
- **11:5x.** One record per light for restoring the game's dimmers (the owner: the A/B wasn't clean). An astra review
  of AMD portability, all fixed. The harness also runs on this PC's AMD iGPU and under the D3D12 debug layer.

## Fog, the denoiser list, glass, skin, hair (2026-09-27 afternoon)

- **14:03, Sagremor Castle at night: the fog was gone.** `Add<Fog>(true)` had reset HDRP's fog to its default, off.
  Owner: "fog shouldn't go away just because people activate PT. It's core to the game's art style". Fixed by 14:30.
  On the Boiling Filter: "jury is out".
- **14:3x.** Owner: "don't suggest the built-in denoiser. Only off, the NRD one, and Ray Regeneration and Ray
  Reconstruction."
- **15:3x.** Effect lights keep HDRP's range (a portal lit a whole alcove white); fill lights' fog switched through
  `affectsVolumetric` (a zeroed `volumetricDimmer` stuck after tracing stopped); far hair kept HDRP's (traced grey);
  the "seen" meter, so fog counts as bright (castle mist had been lit through).
- **~16:3x to 17:0x.** Glass stage 1 (bottles pass light by chance, tinted), skin's subsurface scattering (the owner:
  "add subsurface scattering to skin"), glass stage 2 (see-through and refracting), far hair left to HDRP wherever the
  camera ray hits a character.

## The afternoon's list, with five subagents (2026-09-27 16:47 to 20:02)

The owner, flying in game, gave a list (radiance cache, crevices, glass polish, material maps, emissive lights,
performance, "reflections on water and grass") and asked to "parallelize with subagents to build more stuff at a
time" (recipe: PROJECT-CONTEXT section 13).
- **16:47 radiance cache** (the main session's own design; furnace 0.994 with one bounce).
- **17:53 materials** (mask maps, per-shader tiling). At the merge, metals were black at path vertices: they now send
  their reflected light on as diffuse light of their base colour.
- **18:25 crevices and glass polish.** The beach-canvas pocket is mostly real darkness (1/150 of sunlit sand); Crevice
  Light row, off by default. Glass tints shadows per channel.
- **18:45 water, 19:17.** Traced water reflections replace HDRP's SSR entirely (the owner: "water reflections are not
  screen space I hope?"). The GGX peak cap fixed (the owner: "fix highlight bug, we don't want bugs").
- **18:50.** Acceleration structures created in their own resource state: ~5100 debug-layer errors a run to 0.
- **19:49 emissive surfaces as lights; 20:02 characters:** hair strands stop rays by chance, thin-skin transmission,
  Hair Shadows (traced shadows on the hair and eyes HDRP shades), characters to 100 m.
- At 17:40 every agent hit a session limit (HTTP 429). Nothing was lost: their edits were files in the copies, and
  messaging each agent resumed it.

## Reports and 13 review findings (2026-09-27 22:30 to 23:40)

- The owner's reports (22:30-22:57) and a GPT-6-astra review of the afternoon's diff: 13 findings, all fixed. Finding 1
  was the dark hair and eyes (the traced-shadow share darkened the sun's and the ambient light too).
- Also: every alpha tile stops rays by chance (straw was a solid mat to rays), wind-swayed grass and laundry pass rays
  near their drawn copy, translucent path vertices, the fog fades the automatic gain, Unlit colours, Rays Per Pixel to
  16 with each pixel's own mean blended in (the boiling measure 0.34 / 0.17 / 0.08 at 1 / 8 / 16 rays), Reflection
  Boost default 0.1 (owner). The owner: no subagents for now. Deployed 23:40 (ABI 0x00010E00).

## GPU time by stage (2026-09-28 00:15)

- Per-stage GPU timestamps in the F8 report and the log; Spatial Samples up to 16; a portal's glowing orb was the
  reflection of a black Unlit disc (Unlit now reflects nothing); the saw on a metal strap first blamed on reflection
  rays drawn into the triangle (mirrored out now); hair and eyes got back the ambient HDRP takes from them (hair's part
  was reverted at 01:15). The bottle works (the blackcurrant one is very dark glass). ABI 0x00010F00.
- The owner on boiling: not the cache; Spatial Samples 0 much worse, 8 better; 8-15 rays better; nothing removes it.

## The last round (2026-09-28 ~01:15; deployed, hash-verified, committed and pushed as 166bd58 at 01:34)

The owner's answers: Bounces 1 as the default, yes (no difference between 1 and 4); rough reflections as an A/B from
the Video tab; cloth lit through as realistic see-through, not glow; eyes "lit correctly now", too small is the model.

Built and deployed:
- **Reflections row:** Off / Glossy Surfaces (rough surfaces reflect the radiance cache's light) / All Surfaces.
- **Thin cloth lit through from behind** (`TGPT_MATERIAL_THIN_CLOTH`, CLOTH_TRANSMISSION 0.3).
- **Lights with HDRP's Affect Specular off make no traced highlight** (`TGPT_LIGHT_NO_SPECULAR`); the F8 report's light
  lines say "no specular". The hypothesis for the saw on the door's metal strap (candle highlights on the strap's
  facets). Mirroring rays out had not fixed it. Waiting for an F8 at the door.
- **Spatial Samples above 8 never reached `tracer.cpp` until now:** an edit script with two blocks for the same file
  dropped the first. Fixed.
- **Bounces defaults to 1.** The chair probe's bounced light: 27.8 ms -> 8.3 ms.
- **The F8 stage timings** split out "bounced light spatial reuse".
- **The F8 capture measures boiling:** the 8x8-block frame-to-frame change as traced, with the bounced light from 16
  own rays only (`TGPT_OPTION_GI_OWN_RAYS`), with the direct light without reuse (`TGPT_OPTION_DI_NO_REUSE`), and with
  neither: the "Boiling" line under "== Capture". Waiting for the owner's F8 at a boiling chair or ceiling.
- **Reflection Boost (default 0.1) leaves skin alone.** The neck "rash" was glossy skin (roughness 0.18) reflecting
  the chin. The owner: the boost is for stone and brick AND armour and metal, so characters otherwise keep it (a
  character-wide exclusion was reverted).
- **Conversation Lighting Overhaul's light** is scaled by the inverse of the automatic traced gain (3x too bright in a
  dark tavern).
- **Eyes take the traced bounced light** for HDRP's lost ambient; hair's version was reverted (blonde hair glowed).
- **Fill Light Fog row (On):** fill lights keep lighting the fog (the tavern's haze came only from its 24 fill lights).

Also in 166bd58, for TG Frame Generation and TG Better Upscaling (from the first player report): the 45 fps Framerate
Limit entry, narrower driver alerts, the report's per-step Pipeline lines, no pause for an autosave's camera, FSR's
version in the Upscaling row.

## The owner's rulings this round (2026-09-28)

1. **HARD RULE:** "not to keep shading of anything in HDRP when PT is on", and "A solution to black eyes or weird
   shadows on hair isn't to exclude it from path tracing". Hair and eyes are still HDRP-shaded (`pt_hdrp_shadow`'s
   traced-shadow share, `TGPT_MATERIAL_HDRP_SHADED`) and must move into the tracer. Also still HDRP-shaded, to raise
   with the owner: hair's transparent layer, transparents and particles, water's own shading.
2. Fill lights keep lighting the game's fog: "these are just as much artist-placed fog volumes as they are
   artist-placed lights... allow the fog-part of them while preventing the light-part". Done: the Fill Light Fog row.
3. All rules, learnings and insights go into the repo harness (BACKLOG, ROUNDS, AGENTS, PROJECT-CONTEXT), not an
   agent's memory. This harness was written for it at the end of the round.
4. Subagents: allowed when the owner asks (earlier that day: "don't parallelize into subagents this time"). A caustics
   research agent is running and writes `docs/CAUSTICS-RESEARCH.md`.

## State at close

Commit 166bd58 on `fg-input-orientation`, pushed; deployed to the Mono game folder and hash-verified. Versions 1.2.0;
ABIs: bridge 0x00010100, TGFG 0x00010100, TGUP 0x00010400, TGPT 0x00010F00 (TGPT_Frame 568 B, FrameConstants 1632 B).
ctest 16/16 and 71 source checks passed at the 00:15 deploy.

## Open (R2 close), in priority order

1. Hair and eyes shaded by the tracer. Design: drop the HDRP-shaded path; hair colour and tint from the hair shader's
   properties (the traced hair material lacks HDRP's tint); a hair BSDF with strand tangents; camera rays that miss thin
   hair cards on far characters and hit the head behind; the eye's cornea specular over the iris and sclera texture.
2. The boiling: waiting for the F8 "Boiling" line. Lights with HDRP radius 1 m (e.g. `PointLight_Cookies_01`) make huge
   noisy penumbrae: a DI suspect.
3. The saw: waiting for "no specular" on the lights at the door.
4. Performance: tavern GPU 19.3 ms at Bounces 1 (bounced light 8.3, reflections 5.7, direct 3.7).
5. Caustics: the research agent's `docs/CAUSTICS-RESEARCH.md`.
6. The CLO light, checked in game.
7. Conversation spotlights.
8. Eyes too small: a model issue; a separate mod through blendshapes was offered.

## ▶ Starter prompt (R3)

Read AGENTS.md's START HERE: BACKLOG.md (the rulings and the Open list), PROJECT-CONTEXT section 9 and DEAD ENDS, and
this file's "The last round" and "Open". Under the owner's hard rule of 2026-09-28 (no HDRP shading while tracing;
excluding a surface from tracing is not a fix), hair is traced since late R2 and the eyes are next. First read the
owner's newest F8 captures in `BepInEx/TGPathTracing/Captures/`: the "Characters along the crosshair" section at a far
NPC says which hair material HDRP draws there (far hair traces grey while HDRP's is colored), the "Boiling" line now
tests GI history 4 against a settled control (DI history is 4 since R2), and the door's lights may say "no specular".
Then: fix far hair; trace the eyes from the eye shader's textures BepInEx logged ("TG/Character/Eye/Eye ... colors,
values and textures") and drop `pt_hdrp_shadow`, the Hair Shadows row and `TGPT_MATERIAL_HDRP_SHADED`; build the
see-through glass for HDRP's transparent jars (plan in PROJECT-CONTEXT 9.7, the owner: "let's fix that"). Ask the owner
about eyebrows and lashes (transparent HDRP layers), transparents and particles, water's own shading, and whether to
move the F8 capture off F8 (it also turns TGFG off). Read `docs/CAUSTICS-RESEARCH.md` and the tavern-haze finding if
they arrived, and propose a next step. Deploy after each round of fixes (`Build.ps1 -Runtime Mono -Deploy`, game
closed, hash-check). At the end, write round 3 and update BACKLOG, PROJECT-CONTEXT and ROUNDS.
