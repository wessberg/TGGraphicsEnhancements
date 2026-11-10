# Backlog - current work

Under 12 KB. Each round's story is in `docs/rounds/`; learnings and DEAD ENDS in `docs/PROJECT-CONTEXT.md`; every
change with its reason in `CHANGELOG.md`. The hard rules are in `AGENTS.md`.

## Goal and current priority

**1.2.4 of TG Frame Generation, TG Better Upscaling and TG Neural Rendering** for the game's Unity 6000.3 is the work now
(1.2.3 on Nexus since 10-09 has FSR, XeSS and NR broken on that game build). The six zips in `artifacts/Nexus` are tested on both versions. LEFT: the owner's upload.
TG Path Tracing (Mono, prototype): TABLED 10-30; everything below about it waits for the owner's word.

READ FIRST: the end of `docs/rounds/round-21.md` ("▶ Starter prompt (R22)").
The in-game harness: `docs/PT-HARNESS.md`. The shortcuts the owner found on 10-07, each ruled on:
`docs/PT-SHORTCUTS.md`. Uncommitted since d31dff9.

The game's 2026-10-06 update (Steam build 25629220) ships Unity's FSR 2; 1.2.3 holds the fix for TG Better
Upscaling. The game folder is on **Mono**. TG Path Tracing is installed only by `tools\harness\Deploy-PtBuild.ps1`
(what is in the game: the round's state block).
A SECOND SESSION works in this tree again since 10-09 evening (round 18, the Defender report below): its lines
here and its files are not this session's to change.

## The owner's rulings (newest first)

- 2026-10-12 (AGENTS.md has each): RTX MEGA GEOMETRY ON AND STAYS ON ("Do not turn it off ... there must be
  something we've done wrong if it's not making things faster"; PT-109). The stream stays on: speed is measured
  with it. Our water need not look like the game's, "so long as it looks amazing". "Anything can light the mist -
  not just the sun and moon"; the fog's light in every order. Old workers' builds go (`Clear-PtHarnessRuns.ps1`).
  The open choices, with pictures: https://claude.ai/artifact/JbWmoPqCgAS7em2nN1aSFt
- 2026-10-12 ~00:30: a worker's offline test reset the GPU beside Starfield ("the GPU couldnt handle it"): NO offline
  GPU test while a game of the owner's is open (AGENTS.md).
- 2026-10-11 ~21:40 (AGENTS.md): "you can up to 3 Opus 5.5 xhigh subagents at a time again, the old restriction has
  lifted".
- 2026-10-11 ~18:10: Ray Reconstruction's preset: "Use NVIDIA's Default, if that is currently preset F. If it's not, use
  preset F" (SDK 310.9.1: Default is F; rr_upscaler asks for Default).
- 2026-10-11 ~14:50: "Obviously we want to 100% follow NVIDIA's and AMD's implementation guidelines for these
  technologies" (AGENTS.md; PT-127).
- 2026-10-11 ~10:15: HDRP's AO, contact shadows, SSR, shadow maps, deferred lighting and SSS "must not render at all"
  while tracing: the rule, no switch (GamePasses).
- 2026-10-11 ~07:40, ~09:45: "why is fog and translucents untraced ... get that brought over": the sky, fog,
  translucents and HDRP's leftover passes move to the tracer; HDRP keeps post, DLSS and the HUD. No distance caps.
- 2026-10-11 02:55: RTXDI, reordering, micromaps, Mega Geometry used "absolutely" right (audit-4.md).

- 2026-10-10 10:30 (AGENTS.md): Shader Execution Reordering is never switched off; where a pass is slower with
  it, the fault is in our design: "Do not turn that off ... due to us not designing our tech with it in mind".
- 2026-10-10 01:05, THE ORDER (AGENTS.md): first "getting this under control" (speed, video memory), then the
  remaining rendering issues and missing features (Mega Geometry "not as a first step": overruled 10-12, above).
- 2026-10-06 to 10-08 (AGENTS.md): the game is the first test; one thing at a time; full suite never in subagents;
  every request and defect an entry in `docs/PT-REQUESTS.md` and a line here; never a new job to an old agent.
- 2026-10-06: grass casts soft, partial shadows; metal reflects more than stone and brick; the tracer lights the fog
  (PT-37); the target is 55 fps, "As performant as Cyberpunk's PT"; the in-game harness (`docs/PT-HARNESS.md`).
- 2026-10-06, HARD RULE (AGENTS.md): never start the game while another program uses the GPU.
- 2026-09-28: while iterating on a look, deploy without the test suites (AGENTS.md). Reviews by GPT-6-astra on request.
- 2026-09-28: hair like the game's: fine, soft strands; brows thin. No HDRP shading while tracing (AGENTS.md). Fill
  lights keep lighting the fog (Fill Light Fog, On). Bounces 1. Thin cloth see-through, not glow.
- 2026-09-26/27 (AGENTS.md): keep the game's fog; Denoising None, NRD, Ray Regeneration, RR only; Full mode only; no
  fake lights; RR is essential and works with DLSS 5; Reflection Boost 0.1; Boiling Filter On.
- Standing (2026-09-19 to 09-25): the settings rules in AGENTS.md; players aren't technical; one shared D3D12 device;
  versions match Nexus; quality modes round down.

## Open, in priority order

Path tracing: every request of the owner's and every defect found has an entry in **`docs/PT-REQUESTS.md`** (the
owner's words, the evidence, what comes next). Add one there and a line here the moment something comes up.

FOCUS (R20, 10-12; the state: the newest "State at" of `docs/rounds/round-20.md`). PAUSED BY THE OWNER 10-12
~11:40 ("pause until I give you to go-ahead"): no worker, run or test until the owner says go.
1. SPEED (PT-99; first). Baseline 10-12 06:20 (no stream; one with it owed), fps: tavern
   21.5, town 12.9, forest 11.2, vista 18.0, beach 11.2. Since: rooms -6 ms (PT-114), the water in groups (-1 to
   -2.7 ms CPU), the caustics' grid -2.2 to -2.6 ms (default; the old way's removal owed), Mega Geometry on
   (PT-109, PT-148: megageo7's report). NEXT: PT-145 the light one frame late (worker framelate's report); the
   census. PT-87.
2. FIXED 10-12: PT-125 the crash (the plants' pose read stale records after a walk; 40 walks, no reset), PT-134,
   PT-135, PT-129's four endless loops, PT-136 (a portal's lamp: bounded; a save at the gate: the owner's).
3. PT-127 NRD and RR as NVIDIA documents them. THE OWNER CHOSE 10-12: RELAX; BOTH FILTERS IN MOTION OWED (recipe
   `nrd-motion`, motion blur off; then the page); NVIDIA's SH form for the lost detail; glass under NRD way B
   first, then A; RTXDI's defaults tried beside ours, shown. Left: RR 17, AMD 7.
4. PT-118 THE FOG IN CELLS, THE RULE. PT-37: every lamp lights the mist, every order (the owner; the look
   CONFIRMED in game 10-12). PT-147: the town's lamps bright or off by the game's own state: explained; the game's piece not found. PT-138: the check mended (fog3), ITS RUN IN THE MAIN TREE OWED.
   PT-144 smoke in fog a dark blob. PT-112 THE SKY, ANSWERED: light from the sun and moon one sees, real haze,
   cloud shadows: a job. Transparents (step C), PT-116, PT-130, PT-137, PT-133.
5. PT-129 (not reproduced; the owner: "nevermind trying to reproduce that crash"): a hang after a second load.
6. THE LOOK. PT-81 THE BEACH (the bed a dark slab: `brief-water-12.md`, next slot). The owner's session of 10-11: PT-119 eyes dark but straight on, PT-120 bottles, PT-121 smoke and haze,
   PT-122 smooth planks and ground and PT-123 contact shadows (better under NRD: the owner to look), PT-124 slow textures and stutter, PT-126 a cyan fog patch in rain. NEW 10-12: PT-139 a mirror's lamps a noisy glow; PT-146 a flame's steady reach (chosen; pictures
   owed); small: PT-140 to PT-143. Older: PT-104, PT-108, PT-28, PT-105, PT-106, PT-13, PT-81, PT-40.
The whole list: `docs/agents/inspection-20261008.md` (123 defects). One at a time, one scene each.
FINISH THE MIGRATION to our own renderer (the owner, 10-07: "I expect visual regressions until the renderer is
fully migrated ... then we can A/B with vanilla"). The list: PT-71. "Do. not. fake. stuff."

The rest:
- PT-39 candle corner (dark is right; PT-49 canopy red; PT-06 the glow sprite). Hair: PT-53 to PT-56. The owner to
  look: PT-33, PT-38, PT-32, PT-05, PT-12, PT-03, PT-04. PT-72 (step A gives NR/RR the tracer's depth and motion).
  PT-69 two review findings. PT-19 NRD noisier than RR. PT-27, PT-11, PT-12, PT-16, PT-21, PT-10. PT-71's list: PT-28,
  PT-29 to PT-31, PT-41, PT-68. PT-23, PT-26; the owner's PT-17, PT-18, PT-22, PT-25.
- Not `-force-d3d12` unless Direct3D 11 cannot deliver PT-45/46 or it is much easier (then FG and BU follow).

Frame generation and upscaling:

- 2026-10-30 (the owner: "Switching back and forth is not acceptable"): DLSS is created at the size of the first frame and stayed that large. Fixed: the mod has it made anew by itself, 76 to
  104 fps as launched on the normal version, about 80 to 112 on Mono (`docs/rounds/round-21.md`, Open 2).
- 2026-10-30: the new TGFG.Native.dll scans clean in Defender, three builds in a row. Whether Microsoft's false alarm on the
  released 1.2.3 is settled is still open, below.
- URGENT 2026-10-09: Defender blocks FG 1.2.3 in BOTH release zips (`83947A2A...143CB73A`).
  R18: exactly SIX bytes differ from the clean October 7 DLL, only build timestamps; all code identical.
  Same Defender scans: old FG, BU, bridge, NR clean; released FG `Wacatac.H!ml`. Owner restored game
  copy; public fix OPEN. Microsoft report prepared, not submitted. `build/nr/TGFG.Native.dll` was
  quarantined. Also open: error 225 gives misleading reinstall advice. See `docs/rounds/round-18.md`.

0. **1.2.3 is ON NEXUS since 10-09** (the owner; the notes: CHANGELOG's top): TG Frame Generation, TG Better
   Upscaling, TG Neural Rendering, each for the normal and the Mono version. The six zips: `artifacts/Nexus`,
   copies in `artifacts/Nexus-1.2.3-backup`. `TGPathTracing - mono.zip` there is an old 1.2.0 of 09-28: not for
   upload. Open: on the normal version the bridge loads TGPT's native part when its Mono files are in the folder
   (`disable-autoload.txt` stops it). The builds are of uncommitted work (git says 1.2.0 at 655d5dd).

## Queue

Frame generation, upscaling, bridge:
- TG NEURAL RENDERING IS DONE (the owner, 10-09: "the mod is done"). Every request, NR-01 to NR-45, with its
  state: `docs/DLSS5-FEEDBACK.md`; the story: rounds 07 to 17 (`docs/rounds/round-17.md` is the last); design and
  tests: `docs/DLSS5-SPEC.md`, `-RESEARCH.md`, `-VALIDATION.md`, `-COMPATIBILITY.md`. As that session left them
  open: NR-14 the picture is unstable while walking (the owner rejected an extra temporal filter: NR-27); NR-30
  to NR-32 the community runtime on RTX 2000/3000/4000 cards, never confirmed on such a card (NR-33: a 5080
  cannot prove it); NR-19 motion markers of FG and Better Upscaling; NR-29 what FSR frame generation costs;
  NR-44 the photoreal close-up of Ron's face, not reproduced; PT-72. No HDR mod of our own (deferred).
- Later (the owner, 2026-10-08): the bridge keeps ONE list of every installed mod's NVIDIA, AMD and Intel folders
  and every mod uses it; files stay in each mod's folder (zips work alone). Replaces the hand-made lists in
  `sl_runtime.cpp`, `rr_upscaler.cpp`, NR's `extension.cpp` and `tgb/ngx_session.hpp`. Bridge bump, every mod mix.
- NR's player DLL, found 2026-10-08, undecided: the zip has no `Native/DLSS` folder (R17 README/Nexus now explain
  creating it); not found in TGBU's `Native/DLSS`; stray README `+` fixed in R17; `Build.ps1 -Deploy` never copies
  it (only untracked `.testruns/nr/deploy.ps1`, from `DLSS-NR-Runtime/`).
- 2026-10-06 (round-05, the owner's call): a false "No FPS gain" alert (its old rate may be 3 minutes old); in-game
  VSync "capped at 60" on a 165 Hz TV (the game's divided VSync rate? unverified).
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
