# Round 1 (retroactive) - Frame Generation 0.3 to 1.2, Better Upscaling, the bridge, and TG Path Tracing up to RTXDI

Dates: 2026-09-18 to 2026-09-26 21:21 (commit c269d60). No rounds were kept before 2026-09-28; this summary was
written then from the agents' notes and `CHANGELOG.md`. The technical detail behind each item is in
`docs/PROJECT-CONTEXT.md` (section numbers below); the dated story of TG Path Tracing continues in round 2.

## Frame Generation, 0.3 to 1.0 (2026-09-18 to 2026-09-21)

- **2026-09-18, the load crash.** With 0.3.x (D3D11 rendering, a private D3D12 device presenting through Streamline)
  the game died at the first bridge Present: STATUS_STACK_OVERFLOW in `__chkstk`. The GetProcAddress hook had a 64 KB
  `wchar_t path[32768]`, and an NVIDIA driver worker thread with ~10 KB of stack called GetProcAddress. Fixed on
  2026-09-19 with heap-grown paths; `Build-Native.ps1` now builds Release on single-config generators (NMake had linked
  the debug CRT). Diagnosed without a debugger, from crash dumps and a PowerShell minidump parser.
- **2026-09-19, 0.4.0 (df55655).** Native rows in the game's Video tab, reverse-engineered from TG.Main.dll with a
  Mono.Cecil IL dump (section 3), and Reflex modes. Frame generation then never engaged. The new `Pipeline 5s:` log
  line found three blockers in turn: the end-of-camera marker queued in `endCameraRendering` never ran (HDRP submits
  before it; now `Graphics.ExecuteCommandBuffer`); the camera's rect is not bit-exact full screen (no camera packets);
  R32G32_FLOAT can't be shared from D3D11 on NVIDIA (motion now R16G16_FLOAT).
- **2026-09-19, compatibility.** ReShade + RenoDX HDR with FG: "works perfectly" once the real immediate context was
  tagged with private data. RenoDX's DLSS 5 add-on hooks `sl.interposer.dll` by name and killed our swapchain, so the
  interposer ships byte-identical as `TGFG.Streamline.dll` (found by disassembling the add-on with capstone).
- **2026-09-19, 0.5.0 (cbd5a74, f7e0b2b).** AMD FSR and Intel XeSS frame generation beside DLSS-G, one Low Latency row
  (Reflex, XeLL, Anti-Lag 2), capabilities from the runtimes. Avalon Mod Manager's Apply reverted Video-tab changes
  (Mode flipped to Off on every save): the mods blacklist themselves there for the session, and FoA Mod Manager shows
  the Video rows 1:1. HDRP's own depth and motion vectors as inputs, so FG runs without DLSS upscaling: DLSS-G, FSR and
  XeSS + XeLL confirmed in game with DLSS off. The HUD-less frame (the `rendersUIOverlay` override, a copy at event 2,
  a Present-time check). The IL2CPP port of the rows and the HDRP pass (section 7).
- **2026-09-20, the flicker hunt** (`docs/FSR-FLICKER-INVESTIGATION.md`; b6a02ce, c52fa75). FSR's HUD and first-person
  hands flickered. Real causes: the HUD-less capture was a frame late (now AfterPostProcess), duplicate Kandra history
  copies (the hands), and HDRP's Y-inverted depth and motion. The flip was first applied to FSR only; the owner then
  saw FSR look better than DLSS and XeSS, which proved the flip applies to all three. Owner: all three equally good.
  Also fixed: FSR's maxRenderSize (the context is rebuilt per render size) and the HUD-less check resetting on menus.
  A deployment trap cost a round trip: the game's DLL was a Vortex symlink to an older build.
- **2026-09-20/21, Exclusive Fullscreen** (d7359d3 and following). A cold start into it works once an sRGB view bug
  was fixed, but switching into it mid-game leaves the game's window deactivated (paused, 60 fps). The game's own
  display setting now moves to borderless through its own "unavailable choice" mechanism (owner: "Then the settings
  won't lie."); a replacement swapchain takes over from a live bridge. Released on Nexus as Frame Generation 1.0.x
  (repo 0.5.0).

## Three mods on one bridge, 1.1.0 (2026-09-22 to 2026-09-23)

- **2026-09-22.** The owner asked whether ReSTIR path tracing could come to FoA: a feasibility read only (every DXR
  shader variant is stripped from the game; our own D3D12 tracer is possible; capturing the world is the risk).
- **2026-09-22, the split** (first committed with 1.1.0 and 1.2.0 in c93c648, 2026-09-25; 545aeed on 09-22 is
  packaging only). TG Graphics Bridge (the D3D11-to-D3D12 bridge and preloader) became a host for
  extensions; TG Frame Generation one of them; TG Upscaling new: FSR and XeSS as entries 3 and 4 of the game's Upscaling
  row, run where HDRP runs STP (section 6). The first in-game run hung at loading: reading `UpScaling.IsSTPAvailable`
  ran its localizing static constructor too early. A FSR/XeSS jitter override was added and removed (STP's jitter was
  already right). "DLSS ignores render scale" was the DLSS 5 add-on's RequireDlss setting. The install turned out to be
  Vortex-managed with symlinks.
- **2026-09-23.** Renamed TG Better Upscaling. The bridge is bundled in each mod's zip, not a download of its own; all
  versions 1.1.0 to match Nexus ("keep version numbers consistent so they match with nexus"). F7 comparisons at 33 %
  render scale (STP best standing, DLSS best moving), then the tool was removed for production (owner: "pretty
  solid"). NVIDIA's DLSS 310.9.1 DLL bundled through a LoadLibraryExW redirect, with the DLSS-set rule for DLSS 5
  (the owner's idea, confirmed in game). FSR's mip bias and pre-exposure. Quality modes per upscaler instead of the
  slider (migration rounds down). The Performance Overlay. Two reviews (architecture, frame generation) with fixes;
  the ReShade hook-order fix; the owner rejected a second D3D12 device. The Agility SDK mismatch behind a Windows 10
  player's 0x887E0003 (section 4). IL2CPP: UpScaling's static constructor again, and an interop bug that leaked ~3 GB/s
  of commit (section 7). A Radeon player was offered DLSS FG: never offer what the runtime didn't report.

## 1.2.0, the troubleshooting report (2026-09-24 to 2026-09-25)

- **2026-09-24.** After Nexus reports (Windows 10 + RX 9060 XT without FG, a 4060 laptop with laggy DLSS-G, FG halving
  a 9060 XT's frame rate): the troubleshooting report and test run, problem rows, install and compatibility checks, the
  no-framerate-gain check (section 8). Three GPT-6-astra reviews, all applied: per-resource D3D11 waits, a PlayerLoop
  frame start, the static CRT (/MD crashed on older msvcp140), producer and consumer fences (the review caught a fence
  that ran backwards, which the first fix introduced), XeSS/XeLL gating, error handling (16 findings, 15 fixed).
- **2026-09-25.** Owner: players aren't technical, so hybrid laptops get VSync turned off and greyed automatically
  (no button), no frame cap of the mod's own, problems as alert icons on the rows they concern, the Frame Generation row
  always present, "normal version" / "Mono version" in player text. A config key with an apostrophe killed TGFG with no
  log line: a source check and "Didn't start" alerts now catch that. Commit c93c648 (15:01) holds 1.1.0 and 1.2.0.

## TG Path Tracing, first build to the RTXDI move (2026-09-25 to 2026-09-26 21:21)

- **2026-09-25.** The owner asked for "proper ReSTIR path tracing", a prototype mod, no subagents. Built as the third
  bridge extension, Mono only: inline ray tracing on the bridge's D3D12 device, ReSTIR DI and GI of its own, an
  SVGF-style denoiser, composited one frame late so D3D11 never waits for D3D12 (section 9.1).
- **2026-09-26 01:18 to 02:20, first in-game runs.** Nothing traced (HDRP's transparent RenderSSR call disarmed the
  capture); then nothing on screen (ReShade's proxy device); then warm fireflies (a uniform pick of 1 of ~884 lights,
  fixed with a light grid and resampling); then grain. The owner found Temporal History 1 "a lot better" than 20.
- **03:00 to 04:00.** Built, not yet seen in game: DLSS Ray Reconstruction in TG Better Upscaling on D3D12 (D3D11 NGX
  DLSSD crashed), DXR 1.2 (Shader Model 6.9, Shader Execution Reordering, opacity micromaps) with Agility 1.619 shipped,
  AMD FSR Ray Regeneration. A GPT-6-astra review, six fixes. The micromap test passed while broken until it was
  mutation-checked.
- **Morning.** The Path Tracing row and its Denoising row in the Video tab. Grain round 2: the harness's reference must
  be undenoised (a denoised one scored blur as a win and the owner lost chair and rope-net shadows); direct light became
  its own signal. RR never engaged (an int.MinValue overflow in TGBU's watch). The HDRP indirect multiplier must stay 0
  (un-zeroing it made skin glow like a lamp). An agent raised TemporalCap to 20 on a harness number, against in-game
  notes; the owner caught it: "It scares me a little you don't remember or can't see it in our notes."
- **Owner, ~12:00:** "Ray Reconstruction is essential for good PT, and it IS compatible with DLSS 5". NGX joins
  Streamline's session on the shared device, so TGFG now lists TGBU's DLSS folder for it (section 4).
- **12:30 to 16:00, characters and the castle.** Kandra characters traced from the game's skinning buffer (owner: "more
  grounded to the bench"); their hair and alpha parts traced as black cards until dropped. Black arch and vault patches:
  a shadow-only filter took a stairwell's roof away (reverted); the castle's real geometry is Medusa's and, for the
  walls, Entities Graphics without Drake (found 15:40); the shadow-only cluster meshes are stand-ins until then.
- **Owner (2026-09-26):** "Don't waste energy on anything else than 'Full' mode", then "drop the GI option for good ...
  Full or nothing": the GI mode left the row and the code.
- **Owner, 15:00:** "no fake lights, no fake bounce lights, no fake light volumes". Fill lights by name left out; at
  15:48 lights became a whitelist by what they belong to (a flame, candle, lamp, magic); CLO's dialogue light kept.
- **18:28 UTC.** Radiance clamps measured against a 10 s average of HDRP's exposure (from an astra review). Research on
  published ReSTIR + RR practice (section 9.5). Commit c269d60 at 21:21: "Add TG Path Tracing and its Ray
  Reconstruction link, before moving ReSTIR to RTXDI".

## The owner's rulings made in round 1

Settings: capabilities from the runtime, clamp on switch, one selector per concept, FoA Mod Manager 1:1, never the
banned word (see AGENTS.md), names "TG <Name>" (2026-09-19); native sharpening only, no shared RCAS (09-22); no FSR
version row; never offer unreported technology, the bridge bundled in each mod, versions match Nexus, one D3D12
device, quality modes round down, STP keeps the slider (09-23); a single troubleshooting .txt beside LogOutput.log
(no clipboard, no subfolder) (09-24); auto-fix over buttons, no mod frame cap, alerts on rows, no "IL2CPP" in player
text (09-25); deploy after every fresh build (09-26). Path tracing: Full only; no fake lights; RR is essential and
works with DLSS 5 (09-26).

## Left open at the end of round 1

TG Path Tracing: the RTXDI move (the owner chose RTXDI, "Option 2"), RR and DXR 1.2 never seen in game, grass not traced
yet (Leshy), white characters and odd hands (asked for debug-view screenshots), menu-text and exterior bloom. Frame
generation: hitches on fast pans; the 4060 laptop's report items (section 8). Unconfirmed: Anti-Lag 2, XeLL alone, FSR 4,
IL2CPP 1.2.0 in game, the Agility fix on a real Windows 10 PC.
