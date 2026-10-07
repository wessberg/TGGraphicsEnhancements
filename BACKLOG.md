# Backlog - current work

Under 12 KB. Each round's story is in `docs/rounds/`; learnings and DEAD ENDS in `docs/PROJECT-CONTEXT.md`; every
change with its reason in `CHANGELOG.md`. The hard rules are in `AGENTS.md`.

## Goal and current priority

**TG Path Tracing** (Mono, prototype) is the work now: every pixel on screen lit by the tracer, looking right to the
owner in game, at a playable frame rate. TG Frame Generation and TG Better Upscaling are released (1.2.0 on Nexus):
keep them working and answer player reports.

State at R2 close (2026-09-28 ~02:15): committed and pushed on `fg-input-orientation` after 166bd58 (hair traced, DI
history 4, the harness); deployed to the game (Mono) at 02:02 and hash-verified, except the F8 "Characters along the
crosshair" section (built, not deployed). Versions 1.2.0 everywhere. ABIs: bridge 0x00010100, TGFG 0x00010100, TGUP
0x00010400, TGPT 0x00010F00.

## The owner's rulings (newest first)

- 2026-09-28, HARD RULE: "not to keep shading of anything in HDRP when PT is on", and "A solution to black eyes or
  weird shadows on hair isn't to exclude it from path tracing". Hair and eyes move into the tracer.
- 2026-09-28: fill lights keep lighting the game's fog: "these are just as much artist-placed fog volumes as they are
  artist-placed lights... allow the fog-part of them while preventing the light-part". Done: Fill Light Fog row, On.
- 2026-09-28: every rule, learning and insight lives in the repo harness (this file, ROUNDS, AGENTS, PROJECT-CONTEXT),
  never in one agent's memory.
- 2026-09-28: subagents only when the owner asks (earlier that day: "don't parallelize into subagents this time").
- 2026-09-28: Bounces default 1 (no difference between 1 and 4). Rough reflections are an A/B in the Video tab (the
  Reflections row). Thin cloth: realistic see-through, not glow. Eyes are "lit correctly now"; too small is the model.
  Reflection Boost is for stone and brick AND armour and metal: never exclude whole characters (skin is exempt).
- 2026-09-27: keep the game's fog ("core to the game's art style"). Denoising offers only None, NRD, Ray Regeneration
  and Ray Reconstruction ("don't suggest the built-in denoiser"). Reflection Boost default 0.1. "water reflections are
  not screen space I hope?" and "fix highlight bug, we don't want bugs". Boiling Filter: "jury is out", stays On.
  Subagents allowed that afternoon ("parallelize with subagents to build more stuff at a time"), not that night.
- 2026-09-26: Full mode only, no GI mode. "no fake lights, no fake bounce lights, no fake light volumes". Everything
  lit only by traced light (CLO's dialogue light is traced). The traced light gets automatic exposure. "Ray
  Reconstruction is essential for good PT, and it IS compatible with DLSS 5". Roadmap: ReSTIR with DLSS RR first, then
  SER and OMM, AMD Ray Regeneration too (all built).
- Standing (2026-09-19 to 09-25): the settings rules in AGENTS.md; players aren't technical; one shared D3D12 device;
  versions match Nexus; quality modes round down.

## Open, in priority order (R3)

1. **Hair and eyes shaded by the tracer** (the hard rule). Hair is traced since R2 (PROJECT-CONTEXT 9.7). Open:
   - far NPCs' hair traces grey (the owner: grey far away, colored up close, grey again walking away) while HDRP
     draws it colored. Deploy the F8 "Characters along the crosshair" section, F8 at a far NPC, find the material;
   - hair colour: RealHair's _RootColor/_EndColor along _RootMap and highlights A/B are not used yet; compare with
     vanilla; then a strand BSDF (the game's two shifted, tinted highlights);
   - eyes: read the eye shader's textures from the log ("TG/Character/Eye/Eye ... colors, values and textures"), then
     trace them (iris and sclera, the cornea's specular) and drop `pt_hdrp_shadow`, the Hair Shadows row and
     `TGPT_MATERIAL_HDRP_SHADED`;
   - eyebrows and lashes are transparent HDRP layers: raise with the owner, with transparents, particles and water.
2. **See-through glass** (the jars, 2026-09-28): HDRP transparent glass is invisible to rays, so no refraction, tint
   or shadow. Plan in PROJECT-CONTEXT 9.7. The owner: "let's fix that".
3. **Boiling.** Measured with the F8 Boiling line: on a cage's bars DI reuse was most of it; DI history 20 -> 4 cut it
   0.40 -> 0.14 (the default now; owner to judge). GI reuse is the rest (0.26 with GI from own rays): the capture now
   tests GI history 4. Also measured: not the radiance cache; Spatial Samples 15 helps, costly. Suspect for DI: HDRP
   radius 1 m lights (PointLight_Cookies_01) make wide noisy penumbrae.
4. **The tavern haze** is thinner than vanilla. Only 2 of its 24 fill lights had fog on (Fill Light Fog does matter in
   another interior, owner). Suspects: dark adaptation brightens the traced room (x5) against HDRP's haze; HDRP's fog
   ambient may follow the indirect multiplier the tracer zeroes. A research agent is on it.
5. **The saw on a door's metal strap.** Waiting for an F8 at the door: do the lights there say "no specular"? (HDRP's
   Affect Specular off is now honoured, `TGPT_LIGHT_NO_SPECULAR`.) Mirroring rays out (above_surface) did not fix it.
6. **Performance.** Tavern GPU 19.3 ms at Bounces 1 (bounced light 8.3, reflections 5.7, direct 3.7); the owner saw
   20-25 fps at 4K DLSS Performance. The F8 report has per-stage times. Not done: a per-frame static material-index
   upload with characters; the ~2.7 GB of tracer textures a first 4K frame once allocated.
7. **Caustics.** A research agent writes `docs/CAUSTICS-RESEARCH.md`. Read it, then propose to the owner.
8. **CLO's dialogue light in game.** It is now scaled by the inverse of the automatic traced gain (it was 3x too bright
   in a dark tavern). Needs the owner's look in a dialogue (this was the "Conversation spotlights too bright" report).
9. Eyes too small: the character model, not the lighting. Offered: a separate mod through blendshapes.

## Waiting for the owner's look

Boiling Filter On vs Off; Crevice Light with Ray Reconstruction; Reflections Glossy Surfaces vs All Surfaces; thin
cloth; the neck after skin's exemption from Reflection Boost; the cage and chairs at DI history 4; hair colors against
vanilla; whether the tavern is too dark since the "seen" meter (gain ~x2 where it was ~x4).

## Queue

Path tracing:
- From reviews, status unknown (check the code first): the tracer's reprojection ignores jitter; the composite's
  fallback keeps HDRP-lit pixels.
- Not done: blue noise; a firefly clamp; the built-in filters lag light changes ~1 s (maxLength 32; they still filter
  direct light and reflections under NRD).
- Glass: a painted label on very dark glass (the blackcurrant bottle) is lost.
- Latent (2026-09-27): HDRP's SSS features put profile|mask in GBuffer0.a, and profiles below 8 fall under
  pt_prepare's `a > 0.5` "deferred" test. Skin works today (it arrives as features 2/3 through GBuffer2.b).
- The game root holds the owner's own `nvngx_dlssd.dll` 310.7.129, and NGX also searches the exe folder: check which
  DLSS-RR DLL loads (TGBU's log names it; TESTING.md expects TGBU's 310.9.1).
- Never recorded as seen in game: DXR 1.2 (SER, micromaps; `docs/TESTING.md` step 8 says what to look for), NRD on
  AMD, AMD Ray Regeneration (needs an RX 9000). The D3D12 debug layer run needs Windows Graphics Tools installed.
- F8 is bound three times: TGPT's capture, TGFG's Emergency off (sets Frame Generation to Off) and AvalonAtlas's HUD
  editor. Asked the owner 2026-09-28 whether to move the capture (F7?); their keyboard has no Insert key.

Frame generation, upscaling, bridge:
- 2026-10-07 (round-07): native DLSS 5 spec: `docs/DLSS5-SPEC.md`. Direct NGX, no RenoDX/ReShade.
  Owner assigns implementation elsewhere. SDK access, D3D12 contract and runtime release rights unconfirmed.
- Hitches on fast pans with all three FG backends (Streamline: "Frame rate over 100.00ms"), open since 2026-09-20.
- `reflex.crossAdapterCopy=1.3ms` with `crossAdapter=0` (the 4060 laptop report): unexplained.
- Menu-text bloom with RenoDX's DLSS 5 add-on and our mods (the owner suspects the add-on's Present stage).
- Smooth Motion's DRS ID 0xB0D384C0 (from Profile Inspector): unverified.
- Exclusive Fullscreen switched into mid-game: the shipped fix moves the game's setting to borderless; the untested
  idea `DXGI_MWA_NO_WINDOW_CHANGES` remains.
- Deferred by agreement (2026-09-23): a backward-compatible extension ABI (M2); IL2CPP "hooked but never called"
  diagnostics (M5). Skipped with a reason (2026-09-25 review): TGBU's 20 s capability timeout (the swapchain exists
  before the first Update).
- Never recorded as confirmed: FSR 4 (RX 9000), Anti-Lag 2 (AMD), XeLL alone (Intel), XeSS 3x/4x (Arc), the Agility
  fix on a real Windows 10 PC, the 1.2.0 IL2CPP build in game, TGBU's quality modes and FSR pre-exposure in game.
- Docs: README's Status section still says 1.1.0 and ABI 0x00010100; `docs/TESTING.md`'s path tracing steps still
  say "Full" where the row now says On.
