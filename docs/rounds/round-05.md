# Round 5 - the game's FSR 2 update, TG Better Upscaling's fix, and 1.2.1

Date: 2026-10-06. The owner, back from vacation: a game update rolled out, players report that Better Upscaling
broke, Frame Generation maybe too; "The new game update shipped FSR support ... presumably we might consider hooking
into it differently now? For example, no need to use HDRP motion vectors for our own FSR 'Upgrade' ... What do you
think? Please look at the integration now and see how it is affected". Then: "Oh, the game shipped FSR 2? So, our FSR
will be so much better!"; one FSR entry with no separate "FSR 2" choice: "yes of course"; "Please build the new
artifacts for IL2CPP and Mono, deploy them to the game dir, nevermind the path tracing change, I'll tackle that
separetely, and then I'll verify and upload to nexus"; "for path tracing and the hair layer, I'll tackle it in a fresh
session". Everything below is uncommitted.

## What the update is (read from the game's files)

- Steam build 25629220, installed 2026-10-06 10:03 (Mono branch). New: `AMDUnityPlugin.dll` in the game root. Every
  managed DLL was rewritten; `UnityPlayer.dll` was not (still Unity 6000.0.64f1).
- `TG.Main.dll` (ilspycmd): `UpScalingType` has `FSR = 3`; `UpScaling` has a fourth row entry `UpScalingType_FSR`,
  `IsFSRAvailable` (HDRP's `FSR2Detected`) and forbids the entry without it; `UpScalingController.EnableFSR` sets the
  camera's `allowFidelityFX2SuperResolution` and takes the scale from the game's slider.
- It is FSR 2: `UnityEngine.AMDModule.dll` holds only FSR 2 types (`FSR2Context`, `FSR2Quality`), and HDRP's
  `FSR2Pass` (a stub in the previous build, by its IL2CPP interop left in `BepInEx/interop`) now has its code. No FSR 3,
  no FSR 4, no frame generation.
- HDRP's FSR 2 pass reads the same color, depth and motion vectors HDRP hands to `DoStpPasses`, plus a bias mask.
  There is no other source of motion vectors in the game: the game's FSR is HDRP's. So the mod keeps hosting in STP's
  slot.

## What it did to the mods

- **TG Better Upscaling 1.2.0: broken**, as players said. `UpscalerRow.Install` knew a row of 3 entries (or its own
  5) and left the game's new 4 alone: no XeSS, no Quality Mode rows, no FSR Sharpness. Its hook on the game's
  controller stayed in place, so DLSS and "FSR" were still set up from the config: the game's slider did nothing, and
  "FSR" ran the mod's FSR 3.1 at the config's mode. From the code; no log shows it (1.2.0 never ran on the new game here).
- **TG Frame Generation: nothing found broken.** It compiles against the updated game, the 67 private game and HDRP
  members the three mods reach by name are all still there (a Mono.Cecil check), and its inputs do not depend on the
  upscaler row. Not seen in game.

## The fix

`src/TGBetterUpscaling`: `UpscalerRow.cs`, `StpHost.cs`, `Settings.cs`, `UpscalingOverlay.cs`, `Diagnostics.cs`;
`tests/source_checks.py` (`test_games_own_fsr_entry`, 74 checks now). CHANGELOG "Unreleased" has the reasons.
- One FSR entry. The mod's FSR 3.1 / 4 takes the game's entry (same index 3, same ID, so saved choices carry over)
  where AMD's runtime reports FSR; XeSS follows at 4. Where it reports none, the game's FSR 2 keeps its entry and its
  slider, and the hook leaves it to the game.
- With the row left alone (a row the mod does not know), the hook leaves DLSS to the game's slider too.
- The camera's FSR 2 flag is cleared wherever the mod sets a camera up.

## 1.2.1 (built and deployed 11:25)

- Version 1.2.1 in every project, script, extension manifest, native log line and check (TGPT's sources too; its
  installed build stays 1.2.0). Not `rr_upscaler.cpp`'s "1.2.0 or newer" nor the install check's test data.
- Native: only `TGGraphicsBridge.Native`, `TGFG.Native` and `TGBetterUpscaling.Native` were built (one file each, the
  log line), then `cmake --install` (TGPT's DLL "Up-to-date", untouched). ctest without `tgpt_gpu_test`: 15 of 15
  pass, among them `tgup_gpu_smoke_with_pt` and `_all_mods` (the 1.2.1 bridge with TGPT's 12:45 native).
- Managed: `Build.ps1 -Runtime Both` with `$env:Il2CppCoreDir` set to BepInEx 6 be.755's core in Vortex's staging.
  The normal version (IL2CPP) compiles against the interop left in the game folder, which is the PREVIOUS game
  build's; it has run nowhere.
- Packaged per mod and runtime. `artifacts/Nexus`: `TGFrameGeneration.zip`, `TGFrameGeneration - mono.zip`,
  `TGBetterUpscaling.zip`, `TGBetterUpscaling - mono.zip` (each: BepInEx, its manifest, the Vortex mod type). The
  `TGPathTracing - mono.zip` there is from 2026-09-28, not part of this.
- Deployed to the game (Mono) with `Package.ps1 -Runtime Mono -Mod <mod> -Deploy`, TGFG then TGBU. Hashes: build tree,
  `artifacts/Native` and the game match for the three native DLLs (bridge 6192B9DDB70D, TGFG 11C3C98C9CD3, TGBU
  5E54DBECF15C); `artifacts/Mono` and the game match for the six managed ones (TGBetterUpscaling.dll 7DEF4DA202F0).
  Backups under `BepInEx/<Mod>/Backups/20261006-0925...-Mono`.
- TG Path Tracing in the game was not touched (plugin 588E1DC307BE, native 867115848B90, its own 1.2.0 bridge copies).
- 1.2.1 is built from the tree as it stands: it holds whatever changed in the two mods since the Nexus 1.2.0 zips were
  made, not this fix alone.
- Checks: source checks 68 of 74 pass (the 6 that fail are path tracing's, see below); autoload integration 12 of 12.

## After the build: the Mono run, then the normal version (11:33 to 11:37)

- The owner ran the Mono 1.2.1 at 11:33 (BepInEx 5.4.23.5; `LogOutput.log` of that run was read before the switch).
  TG Better Upscaling: "HDRP's upscaler order: DLSS, FSR2, STP" (FSR 2 stands before STP, as inferred); "FSR available,
  XeSS available in the game's Upscaling row; this game build has an FSR entry of its own (FSR 2), which this mod's FSR
  takes the place of", with the quality modes of DLSS, FSR and XeSS listed; DLSS Performance, then DLSS Ray
  Reconstruction for TG Path Tracing 1.2.0 on the 1.2.1 bridge: 610 frames upscaled, 0 refused, one frame at its start
  without guides. All four plugins loaded. The owner's verdict on what they saw is still to come.
- 11:34: the owner switched Steam to the normal version ("public", build 25593237; BepInEx 6 by Vortex). Its
  `global-metadata.dat` has `UpScalingType_FSR`, `IsFSRAvailable` and `EnableFSR`: the same update.
- 11:37: `Package.ps1 -Runtime IL2CPP -Mod All -Deploy` (TGPT has no IL2CPP version and is skipped, so All is the two
  mods). Hash-verified: `artifacts/IL2CPP` and the game match for the six managed DLLs (TGBetterUpscaling.dll
  74A3EF81EEDF), the natives as before. The two normal-version zips in `artifacts/Nexus` were made again in that run
  (the same files, 11:37). The game folder now holds the IL2CPP builds of both mods; the Mono ones are in
  `artifacts/Mono` and the mono zips.
- `BepInEx/interop` was still the previous game build's (2026-09-19) at the deploy: BepInEx 6 makes it anew on the
  first start. The mods are compiled against the old one.
- TG Path Tracing's Mono files are still in the folder (plugin, preloader, extension manifest). The bridge's preloader
  loads a mod's native DLL when its preloader and plugin files exist, whatever they were built for, so on the normal
  version TGPT's native part loads with nothing to drive it: a state no player has. `disable-autoload.txt` in
  `BepInEx/TGPathTracing` stops it (and must go again for Mono). Left to the owner.
- TG Frame Generation against the Nexus 1.2.0 (the owner asked): this round changed its version only. After the commit
  that set 1.2.0 (c93c648, 2026-09-25) came `ExtraFramerateLimit.cs` (the 45 fps entry), `FrameRateCheck.cs`,
  Streamline's plugin path for Ray Reconstruction, and in the bridge the Agility SDK for path tracing, narrower VSync
  alerts and per-step lines in the report. Which of these the Nexus zips have depends on when they were built.

## The normal version's first run (11:34 to 11:47, read from `LogOutput.log`)

- BepInEx 6.0.0-be.755 made its interop anew (233 files, 11:46). Bridge, TG Better Upscaling and TG Frame Generation
  1.2.1 loaded; every type registered ("Registered mono type ..."); "HDRP's upscaler order: DLSS, FSR2, STP".
- TG Better Upscaling: the saved choice FSR came back. First "The game sets up its own FSR 2 ...: the runtimes have
  not reported to this mod yet", then "Upscaling with FSR Ultra Performance at 33,4% render scale (HDRP's STP pass is
  taken over)", the row's status line with the game's FSR entry replaced, "active FSR (version 3.1.5) 1284x724 ->
  3840x2160", 2450 frames upscaled, 0 refused; later DLSS Performance from the row.
- TG Frame Generation: rows registered, DLSS FG "OptionsAccepted | mode Fixed/Fixed", native errors 0/0, the 45 fps
  entry added.
- TG Path Tracing's native DLL was registered as an extension there (as predicted; no disable file).
- One warning, from the bridge, not from this round's changes: "The mods' problems could not be shown on the Video
  rows ...: System.InvalidOperationException: Collection was modified" in `VideoTab.FocusableViews` (the game's set of
  settings views, walked through the interop's enumerator). `RowAlerts.Tick` then sets `failed` and shows no alert
  icon for the rest of the session. First written here as a one-off while the owner switched upscalers: wrong. It
  came right after the settings screen first built its rows (the first call of `FocusableViews`), and the same
  warning is in both normal-version troubleshooting reports of 2026-09-25 (10:04, 10:07): every normal-version log
  there is has it, so the alert icons most likely never show on the normal version, since 1.2.0. The problems still
  reach the player through the performance overlay, the messages in play and the report. A fix: find the views
  without that enumerator (the settings screen's `GetComponentsInChildren<VFocusableSetting>`, say), and never give
  up for the session on one failure. A bridge change: both mods, both game versions, a look in game.

## The alert-icon fix (the owner: "OK, fix that bug too before release"; built and deployed 12:03)

- `src/TGGraphicsBridge/VideoTab.cs`, the IL2CPP `FocusableViews` only: the row views come from Unity
  (`UnityEngine.Object.FindObjectsByType(Il2CppType.Of<VFocusableSetting>(), FindObjectsSortMode.None)`, then
  `TryCast`), not from `Model.Views` (an `IEnumerable<IView>` over a HashSet). A failure gives no views that time and
  one warning; the first success logs "The settings screen's rows are listed for the mods' alert icons (N rows)".
  `RowAlerts` is unchanged. The cause of the old error was not proven (a boxed struct enumerator stepped through the
  interop is the suspicion); the fix does not go that way at all.
- Nothing outside `#if BEPINEX_IL2CPP` changed, so the Mono build is the same instructions: the mono zips of 11:25 stand.
- Compiled against the game's fresh interop (11:46), which also shows the whole normal-version build compiles
  against the new game build. `Build.ps1 -Runtime IL2CPP`, `Package.ps1 -Runtime IL2CPP -Mod All -Deploy`; hashes
  match (`artifacts/IL2CPP` and the game: TGGraphicsBridge.dll C4AFEED81104, TGBetterUpscaling.dll C7B708748D38).
  `artifacts/Nexus/TGFrameGeneration.zip` and `TGBetterUpscaling.zip` are from 12:03.
- Source check `test_settings_rows_are_listed_without_an_il2cpp_enumerator` (75 checks; mutation: the committed
  VideoTab.cs fails it on all four points).
- The owner's run at 12:05 (normal version, the 12:03 build): "The settings screen's rows are listed for the mods'
  alert icons (26 rows)", no "could not be shown" and no "could not be listed"; both mods and the bridge loaded at
  1.2.1, the Upscaling row extended, the Frame Generation rows registered. The run stayed in menus (frame generation
  suspended, 0 input pairs, 74 log lines): gameplay on the normal version was seen at 11:47 with the 11:37 build,
  which was compiled against the old interop; the 12:03 build (fresh interop, this fix) has not been played yet.
  An icon itself was not seen: it needs a problem to show (NVIDIA VSync at half refresh is one).

## Two player reports from Nexus (relayed by the owner, analysed from the code, nothing changed)

1. "sometimes gets the 'framerate not increased' error with FG, even though his subjective experience is that it is".
   `FrameRateCheck.cs` (the same rules in 1.2.0 and 1.2.1): frames on screen with FG (the median of five one-second
   samples, from 3 s after it starts generating) against the rendered rate sampled while FG was Off, which may be up
   to 180 s old; under 1.15x is "No FPS gain"; one measurement, and the alert stays until FG is turned off or changed.
   It can only fire after FG went from Off to On (or its settings changed) within those 3 minutes. Where it can be
   wrong while FG works: (a) the old rate is from another view or place; (b) for FSR the frames on screen are not
   measured but taken as rendered x 2, so the check is "rendering fell below 57 % of the old rate"; (c) the first
   sample after a menu closes averages menu frames in (`FrameRates.Rendered` is a trailing second); (d) a single
   five-second window right after the settings close decides for the session. Which one hit him is not known: the
   message carries its three numbers (before, on screen, rendered), and the troubleshooting report has them too.
   Options: tighten it (the old rate at most ~30 s old and only from uninterrupted play; two windows before an
   alert), or make it certain (on a first "no gain", pause FG for two seconds on the spot, measure the same view
   without it, and alert only if that agrees). Not changed: the owner's call, and it needs a look in game.
2. "in-game V-Sync ... feels less smooth than when forcing V-Sync via the Nvidia app. I think somehow the in-game
   option forces a 60fps limit, even though my TV supports 165Hz. I am not using GSync"; and forced VSync "shows as a
   'problem' in the overlay" though it works.
   - The "problem" is 1.2.0's alert "NVIDIA Vertical Sync: Forced on" (any forced driver VSync, at commit c93c648). The
     tree since 2026-09-27, and so 1.2.1, alerts only for a sync to 1/2, 1/3 or 1/4 of the refresh rate. Fixed by
     1.2.1. It also shows that the 1.2.0 on Nexus was built before 2026-09-27: 1.2.1 is not a no-op for TGFG.
   - The 60 fps: not established. The mod's own notes on the game's code (`ExtraFramerateLimit.cs`) say the game's
     Framerate Limit row holds, with VSync on, rates of the refresh rate over a VSync count (`ARRefreshRate.vSyncCount`):
     on 165 Hz a third is 55 fps. NVIDIA's DLSS-G guide (section 22, Streamline 2.14.1): "SyncInterval > 1: Not
     supported. Will be clamped to 1"; for FSR and XeSS the bridge hands the game's interval to the presentation
     swapchain as it is (`output_->Present(sync, ...)`), so generated frames would be held to the divided rate too.
     Driver VSync is always every refresh. The game's code could not be re-read (the folder is on the normal version;
     the Mono DLL is gone until the branch is switched back). To know: his Framerate Limit entry with VSync on and
     his FG technology; the troubleshooting report has both. A fix in the owner's style, if that is it: while FG runs
     and the game's VSync is on, hold the Framerate Limit row at the full refresh rate and grey out the divided ones.

## A correction

Earlier this round the agent told the owner, and wrote here, that TGPT's ABI was 0x00011400 in the sources against
0x00011300 in the game, and that this blocked the deploy script. Wrong: `tgpt_api.h` got 0x00011400 at 12:31 and the
native DLL in the game was built at 12:45, after it (file times; the ABI was not read out of the DLL, and no log line
names it). What is true: `SceneCapture.cs`, `CharacterCapture.cs` (13:02) and `tests/pathtracing_gpu_test.cpp` (13:03)
were edited after that deploy and never built into the game, and 6 path tracing source checks were not brought along
with the hair layer (ABI, FrameConstants' size, the shader list, three source lines). Method: a claim about what is
installed needs a number from the installed file or its log.

## Open (R5)

1. The owner's look in game (Mono). In `BepInEx/LogOutput.log`, from TG Better Upscaling: the load line's "HDRP's
   upscaler order: ..." (is FSR2 before STP?) and the row's status line ("this game build has an FSR entry of its own
   (FSR 2), which this mod's FSR takes the place of"). In game: the Upscaling row reads None, STP, DLSS, FSR 3.1, XeSS;
   Quality Mode under DLSS, FSR and XeSS; FSR Sharpness under FSR; a saved FSR or XeSS choice comes back. TGFG: the
   `Pipeline 5s:` line in `TGFG.Native.log`. Path tracing still starts (1.2.0 on the 1.2.1 bridge).
2. The normal version's zips before they go to Nexus: switch the Steam branch, let BepInEx 6 make new interop, install
   the zips, look. Rebuilding against the new interop is the stricter check.
3. Nexus pages and README still say the game has no FSR (the owner's text). Not compared in game: the mod's FSR
   against the game's FSR 2.
4. Path tracing: the owner's fresh session (the hair layer, its 6 source checks, a round file for round 4).
5. `docs/PROJECT-CONTEXT.md` is over its ~700 lines (it was before this round): condense when next adding.

## ▶ Starter prompt (R6)

Read AGENTS.md's START HERE: BACKLOG.md (the two state paragraphs and Open item 0), PROJECT-CONTEXT sections 6, 11 and
12, and this file. 1.2.1 of TG Frame Generation and TG Better Upscaling is built for both game versions and deployed
to the Mono game (2026-10-06 11:25); it fixes TG Better Upscaling for the game's FSR 2 update. Nothing is committed.
First ask the owner what they saw in game, and read `BepInEx/LogOutput.log` for the lines in Open 1 and
`TGFG.Native.log`'s `Pipeline 5s:` line; fix what they report before anything else, then rebuild and redeploy the same
way (PROJECT-CONTEXT 12: a release of some mods only). The normal version's zips have run nowhere (Open 2). If this
session is the owner's path tracing one instead: the tree's TGPT holds the hair layer (`pt_hair.hlsl`, ABI
0x00011400, deployed 2026-09-28 12:45) plus later edits to `SceneCapture.cs`, `CharacterCapture.cs` and the GPU test
that were never built; 6 source checks wait for it; round 4 has no round file; TGPT's sources say 1.2.1 now. Commit
and push only when the owner asks.
