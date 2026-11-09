# Project context - the engine picture, what each mod learned, DEAD ENDS, method traps

Read the section for the mod you touch and **DEAD ENDS** before you change code. Budget: about 700 lines. When you add
something, condense or archive something in the same edit. Dates are 2026. "Owner:" marks the owner's own words.
What changed when, and why, is in `CHANGELOG.md` (with harness numbers); how things fit together is in
`docs/ARCHITECTURE.md`; the in-game test steps are in `docs/TESTING.md`. This file keeps what those don't: facts about
the game, recipes, and what was measured not to work.

Contents: 1 Machine and tools. 2 The game and its engine. 3 Settings design. 4 Bridge. 5 Frame Generation. 6 Better
Upscaling. 7 IL2CPP. 8 Troubleshooting. 9 Path Tracing. 10 DEAD ENDS. 11 Method traps. 12 Build. 13 Agents. 14 Players.

## 1. Machine, install, tools

- The owner's PC: RTX 5080 (driver 616.92), 4K 240 Hz HDR screen, 32 cores, Windows 11 (System32 `D3D12Core.dll` is
  SDK 616), a MacBook keyboard (no Insert key). Its AMD iGPU (1002:13C0, RDNA2, 2 CU) runs the harness too
  (`TGPT_TEST_ADAPTER=1002`, ~250 ms a frame, numbers within ~0.3 % of the 5080's).
- Game folder: `C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA` (`Build.Local.props` names it). Mono
  branch with BepInEx 5.4.23.x now. The same Steam folder was switched to the IL2CPP branch (BepInEx 6 be.755) on
  2026-09-19 and 2026-09-23: check which branch is installed before a deploy.
- Vortex manages the folder with `symlink_activator_elevated`, staging in
  `%APPDATA%\Vortex\taintedgrailthefallofavalon\mods`. Frame Generation 1.0.2 was the Vortex mod "TGFrameGeneration -
  Mono 329 1.0.2-mono" (so its Nexus ID is likely 329). `Build.ps1 -Deploy` (Copy-TgReplacing) replaces links with
  real files; Vortex then reports them changed. A Vortex uninstall restores its `.vortex_backup` files, which once put
  an IL2CPP build back into the Mono folder.
- Other mods loaded on 2026-09-26: TG Volumetric Fix, Better Volumetric Fog Addon, TG Conversation Lighting Overhaul
  (CLO), Torchlight Rekindled, No Player Light, Tainted Texture Budget, TG Film Grain 0.6, AvalonAtlas, FoA and Avalon
  Mod Manager. On 2026-10-06, after the branch switches, only BepInEx 5 and the TG mods were deployed (their folders
  empty): read `LogOutput.log`'s `Loading [` lines. No ReShade/RenoDX since ~2026-09-27.
- The owner plays other games on this PC (Starfield), streamed with Sunshine, whose encoder shows as GPU use: AGENTS.md's
  rule on starting the game. Cyberpunk 2077 is not installed (its frame rate is from published tests, PT-HARNESS.md).
- Tools: Python at `C:\Python314` (capstone, pefile); `ilspycmd` (global dotnet tool) decompiles game DLLs; `dotnet
  run file.cs` with System.Reflection.Metadata reads metadata (PowerShell reflection-only loading of TG.Main fails);
  Mono.Cecil in `BepInEx/core`. No cdb/WinDbg (a PowerShell minidump parser worked; crash dumps in
  `%LOCALAPPDATA%\Temp\Questline\Fall of Avalon\Crashes`). Codex CLI for reviews. No Ninja, no Windows Graphics Tools.

## 2. The game and its engine

Build and HDRP's frame
- Unity 6000.3.21f1 as of the game's 1.31 (2026-10-30; 6000.0.64f1 before, the exe stub and BepInEx's log line still
  say 6000.0.41), HDRP 17. Renders D3D11; ships `D3D12/D3D12Core.dll`
  1.611.2 while the exe asks for SDK 610 (section 4). `shader-stripping.json` in the game root: every DXR variant was
  stripped (HDRP/Lit IndirectDXR, GBufferDXR, PathTracingDXR, all to 0). Game code:
  `Fall of Avalon_Data/Managed/TG.Main.dll`. Preferences: `HKCU\Software\Questline\Fall of Avalon`
  (`Screenmanager Fullscreen mode` 0 exclusive, 1 borderless). No Unity Player.log is written, and BepInEx's
  WriteUnityLog is off. HDRP SSGI probably shipped (named in globalgamemanagers.assets, unverified); resources.assets
  is full of ProbeVolume. `-force-d3d12`: the owner says the game takes it and the bridge breaks (PT-45); unmeasured.
- The gameplay camera is `Camera.main` "MainCamera"; its rect is not bit-exact 0/0/1/1, so pick it by pixel size (+-2
  px). AvalonAtlas's "AvalonAtlasDungeonCam" renders every frame: ignore non-main cameras, log them once. An autosave's
  thumbnail camera (DLSS 146x82 to 320x180) briefly leaves no game camera.
- `beginCameraRendering` fires from `TryCull`, before culling, and after `hdCamera.Update` (camera changes go in
  `beginContextRendering`). `endCameraRendering` fires after the camera's final Submit: context commands queued there
  never run that frame; use `Graphics.ExecuteCommandBuffer`. Requests run in reverse order; the main camera is last.
- HDRP renders Y-inverted and flips in its final blit: depth and motion are mirrored against the presented frame
  (measured). It sets `rendersUIOverlay = true` in `AdjustUIOverlayOwnership` every Render and draws overlay UI in the
  main camera's graph (`RenderScreenSpaceOverlayUI`) unless HDR output is active.
- Order: GBuffer -> opaque `RenderSSR` -> lighting -> `RenderSky` -> AfterOpaqueAndSky -> `RenderOpaqueFog` ->
  transparents (`RenderTransparency` calls `RenderSSR(transparent: true)` every frame) -> post. STP runs BeforePost in
  FoA; film grain is in the final pass after any upscaler. BeforePost still follows exposure/BeforeTAA custom effects.
- GBuffer2.a: material feature ID in the low 3 bits (`PackByte(encodedCoatMask | materialFeatureId)`); GBuffer2.b: the
  diffusion profile (PackFloatInt8bit, 16); SSS features: profile|mask in GBuffer0.a. HDRP bakes probe and lightmap
  light into GBuffer3 ("emission"; for skin before albedo).
- Internals: `HDCamera.mainViewConstants`, `GetMainLight()` public; STP's HDCamera fields (projectionParams,
  finalViewport, resetPostProcessingHistory, ...) internal on Mono. `GetSkyReflection` is a CubemapArray (take the
  Texture's pointer). `Light.intensity` is in native units (lux, candela). HDRP's previous view-projection takes
  positions relative to the CURRENT camera (`prevVP *= Translate(cur - prev)`): give denoisers 2D motion.
- Only HDRP's DLSS pass requests the exposure readback; anyone else must request it (GetExposureTexture /
  RequestGpuExposureValue / GpuExposureValue, internal on Mono). HDRP hands DLSS `Clamp(GpuExposureValue, 0.2, 2)`.
- Gameplay exposure is not automatic: outdoors a physical camera (log2 -12.66), in `Interior_Tavern_02` a fixed EV 14
  (compensation 1); values about 0.0001 to 0.006. Interiors float in the sky (the tavern at 500 m).

What draws what (for the path tracer's scene capture; the details, each renderer's API: `docs/PT-DETAILS.md`)
- Static world: Awaken's Drake ECS renderer. Castle sets: Awaken's Medusa BRG renderer. Castle walls, arches and
  battlements: Entities Graphics entities without Drake. Shadow-only `CM_*_Cluster_*` meshes. Vegetation: Leshy (grass,
  plants, ivy, trees) and Vegetation Studio Pro. Characters: TG's Kandra GPU skinning, world-space output every frame.

Shaders and materials
- `TG/Character/Kandra_Lit` keeps its GBuffer pass: characters and first-person hands are deferred.
  `TG/Character/Eye/Eye` is DepthForwardOnly, no GBuffer pass (the iris is its own texture, refracted under the
  cornea). `TG/Character/RealHair`: an alpha-tested FirstPass plus a transparent layer; far characters have only the
  FirstPass; eyebrows are transparent-only. Characters' material 0 is "Invisible" (cutoff 2, black); `TG/NoRender`
  hides body parts under armour.
- `DiffusionProfile_Human_Skin` is authored THIN ("Thin Object", as foliage); thick (Regular): sclera, teeth, zombie
  skin. Profiles: `DiffusionProfileList.diffusionProfiles.value[i]` ([0] HDRP's default; re-rented every frame, cache
  per profile); `DiffusionProfileSettings.profile` and `transmissionMode` are internal.
- Water: HDRP 17's Water System (`TG/Environment/Water/WaterSurface_Ocean` / `_River`, WaterGBuffer passes, drawn after
  opaque lighting, stencil 8|32); reflections from HDRP's transparent SSR, probes and sky.
- Wind: `Vegetation_Grass`, `Vegetation_Leaves`, `Cloth_Wind` bend vertices (`Lit_VA` is crabs). `KF_Architecture`,
  `Cloth_Wind`, `Lit_No_VA` tile textures through Shader Graph properties. `HDRP/Unlit`'s colour is `_UnlitColor` /
  `_UnlitColorMap` (`_Color` / `_MainTex` are white). Per-shader mask-map layouts come from `resources.assets`.

Lights
- Candles: "Point Light_Single", HDRP range 0.5 m. Table candlesticks: `PointLight_Cookies_01`, ~25k cd, radius 1 m,
  range 3.3 m. Wall candelabras' room light: `PointLight_Ambience`, 74k cd, range 10 m (a candlestick whose only light
  had that name lit nothing when names were a blacklist). Chandeliers: `PointLight_Cookies` 16384 cd x lightDimmer 16,
  range 2.72 m. Fill lights: plain "Point Light_N", 125k-367k cd, radius 2.4-5 m, 3971/5254 K, reaching up to 46 m.
  A portal: two 131k cd lights, range 0.6 m. The tavern has 43 lights kept, 32 placed; 24 fill lights made its haze.
  The game's lights are balanced for EV 13 at physical falloff; HDRP ranges are artist cut-offs.
- `LightWithOverride` (time of day, triggers) caches `volumetricDimmer` when an override starts and writes it back when
  it ends. Only the dev `LightsDebugger` writes `affectsVolumetric`. HDRP 17: `volumetricLightDimmer = affectVolumetric
  ? volumetricDimmer : 0`; `lightDimmer` isn't part of it. Some lights have `affectSpecular` off.
- CLO's dialogue light is "TGConversationLightingOverhaul_Light" with HideFlags.DontSave: only
  `Resources.FindObjectsOfTypeAll` finds it. Its source is in `../TGConversationLightingOverhaul`.

Saves, loading, the title screen, game time and the HUD, driven from a plugin (read from TG.Main 1.30.012, 2026-10-06):
`docs/PT-HARNESS.md` "What the game's code offers". The game does write a log of its own, with times:
`%USERPROFILE%\AppData\LocalLow\Questline\Fall of Avalon\Logs\<date>.txt` (loads, saves, the file verifier); saves are
in `...\Fall of Avalon\<Steam id>\Saved`. No save on exit; automatic and quick saves keep 3 files per character.

Keys: F9 is the game's QuickLoad. F8 is bound three times: TGPT's capture, TGFG's "Emergency off key" (sets Frame
Generation to Off) and AvalonAtlas's HUD editor (every F8 logs twice and pauses the game; white boxes in captures).

## 3. Settings design (the owner's rules, and the recipe)

Rules, with their source:
- Capabilities from the runtime (maximum multiplier, modes), clamped on switch; one selector per concept (a single
  "Low Latency" row with Reflex / XeLL / Anti-Lag 2). Low Latency Off is blocked while frame generation is on; a row with
  one choice left is greyed out (2026-09-19).
- FoA Mod Manager shows exactly the Video tab's rows (2026-09-19, after seeing "02 Menu/03 Safety/04 Diagnostics/
  General.Enabled" entries there). Hidden config entries only for things without a Video row.
- Never offer what the runtime didn't report (2026-09-23: an RX 9060 XT player on 1.0.2 got DLSS FG with multi-frame
  and no FPS change). Without a native report the FG row is Off only, greyed, with the reason
  (Session.UnavailableReason); a later report rebuilds the rows in place (VideoRowsSlot). The FG row is there whenever
  TGFG runs (2026-09-25): it waits only while the bridge's native part is loaded, never while the settings are open.
- Auto-fix over buttons (2026-09-25, "Players aren't technical"): HybridVsync, DisplayMode. With VSync forced off,
  Target FPS goes to Unlimited; no cap of the mod's own.
- Alerts: RowAlerts puts an amber AlertIcon (procedural, a child of the row's name TMP label) after the FG row's and the
  game's Dynamic Resolution row's name, tooltip "Source: Label. Summary". The FG row reads "Off", not "Unavailable";
  a stand-in FG row appears when TGFG is installed but not loaded.
- Upscalers: only their own options (2026-09-22: no shared RCAS; DLSS sharpening is ignored since 2.5.1/310.x, XeSS has
  none). No FSR version row (AMD: default or the player's pick, never the app's; the default is the newest, 3.1.5 on
  the RTX 5080). FSR Sharpness 0.5. Upscaling runs before lens effects. The row says "FSR 3.1" / "FSR 4" (config: "FSR");
  no DLL numbers for DLSS or XeSS (owner: marketing names only, then "nevermind" for those two).

Recipe (TG.Main). Before polling or overriding a game setting, read its type (ilspycmd) for the mechanism the game
already has for the case (Exclusive Fullscreen: `SetForbiddenOptions`, not a poll that fights the value).
- Subclass `Setting` (abstract `SettingName`, virtual `Options`, protected `AutoApplyOnInit`, `OnApply()`). Rows:
  `ToggleOption`, `EnumArrowsOption` (OptionInt, SetForbiddenOptions, EnsureCurrentOptionNotForbidden, onChange),
  `SliderOption`, `DependentOption(base, Func<bool> show, PrefOption[] others)` (VDependentOption re-evaluates `show`
  every frame, spawns and discards the nested views), `PrefOption.SetInteractabilityFunction`, `AddTooltip`.
- Register: `World.Any<SettingsMaster>()`, then the non-public `SettingsMaster.AddGraphicSetting(Setting)` by
  reflection; reorder the private `List<ISetting> _graphicSettings`. Never while `World.Any<AllSettingsUI>() != null`.
- Values persist in the game's PrefMemory by option ID on change. `EnumArrowsOption` saves an INDEX into `_options`;
  each entry is a ToggleOption with its own saved bool. Extending a game row (the 45 fps entry of the Framerate Limit
  row, ExtraFramerateLimit, 2026-09-27) means keeping the saved index in the game's own list space in onChange.
  `ScreenResolution.Init` resets RefreshRateOption when none of its entries is on.
- `DependentOption` is not sealed: TGFG's SwitchRowsOption (ModeRowsOption) swaps the private
  `<OtherOptions>k__BackingField` on a mode change and returns false for that frame to rebuild the nested rows.
- `ScreenResolution` (`World.Any<ScreenResolution>()`): public `FullscreenOption`, private `_fullScreenModes`;
  `set_Option` writes PrefMemory and invokes `onChange` (Cancel and RestoreDefault route through it). Public
  `Setting.Apply(out bool)` applies ALL of that setting's options (resolution, refresh, VSync) and calls
  `Screen.SetResolution`: never while the Options screen may hold unapplied edits. `SetForbiddenOptions` really blocks
  (Next/Previous skip); the game uses it only on RefreshRateOption; RestoreDefault bypasses it. `Setting.Apply(bool,
  out bool)` is private; the public `Apply(out)` refreshes only when a row changed.
- FoA Mod Manager (`kane.tgfoa.mod-manager`) reads `plugin.Config`: string/int/float entries with
  `AcceptableValueList<T>` (enum entries list every name), tags through a `ConfigurationManagerAttributes` class
  (DisplayName, DisplaySection, SortOrder, ChoiceLabels "value=label;...", Hidden/Browsable). FoA reads `Order` then
  `SortOrder` ascending; BepInEx ConfigurationManager sorts `Order` descending: never set `Order`. FoA reads tags only
  when it builds its catalog: call `FoAModManagerApi.Refresh()` on the next Update after a change (changes arrive
  inside its OnGUI). BepInEx swaps a value outside the choice list for the first choice: set the technology before the
  values whose choices depend on it, and read migrated values from `OrphanedEntries` before `Bind`.
- Avalon Mod Manager (`com.avalonmodmanager`, MODS tab in Options) writes back on Apply the values it staged when
  Options opened, reverting Video-tab changes (TGFG's Mode flipped to Off on every save, 2026-09-19). Each mod appends
  its GUID to `AvalonModManager.Plugin.BlacklistedMods` in Awake with SaveOnConfigSet off (ModManagers.KeepOutOfAvalon;
  a soft dependency guarantees load order).

## 4. TG Graphics Bridge (shared)

- Layout and election (`docs/AUTOLOAD-INTEGRATION.md`): each mod ships `patchers/<Mod>.Autoload.dll`,
  `plugins/<Mod>/TGGraphicsBridge.dll`, `BepInEx/<Mod>/Native/TGGraphicsBridge.Native.dll` and
  `BepInEx/<Mod>/graphics-bridge-extension.json` `{"name","native"}`. The preloader bundled with the newest bridge
  version starts it (marker via AppDomain.SetData); BepInEx 5 and 6 keep only the highest version per plugin GUID.
  A leftover FG 1.0.x preloader (`patchers/TGFG.Autoload.dll` without the json) makes every copy refuse.
  `disable-autoload.txt` in a mod's folder stops that mod; in `BepInEx/TGGraphicsBridge` it stops everything.
- The D3D12 device is created inside the game's swapchain creation: ReShade's guard lets a nested device creation
  through only there. On NVIDIA with TGFG installed it is created through Streamline. Up to 3 swapchains are tried
  (not after an LUID mismatch).
- Hook order against ReShade (`dxgi.dll` proxy): it hooks dxgi's exports as dxgi loads and forwards the game's
  factory calls past later hooks. So `prime_swapchain_hooks()` runs before extensions load (`ExportsAsOnDisk` restores
  CreateDXGIFactory/1/2's first bytes for one call, LIFO). Hooks run last-installed first; ReShade leaves D3D12
  objects unwrapped only while its swapchain hook is on the stack. Test: ReShade's `dxgi.dll` beside tgup_gpu_smoke.exe.
- ReShade and NVIDIA's Unity plugin use ReShade's proxy context/device: tag the real immediate context with private
  data (own GUID; ReShade forwards it) and accept any device whose immediate context carries it.
- Shared textures (616.92): D3D11 NT-handle shares work for R32, R16G16, R16, RGBA16F, RGBA32F, R10G10B10A2 and RGBA8,
  not R32G32_FLOAT. D3D11-created shares open on D3D12; most D3D12-created ones don't open on D3D11.
- Fences: a producer fence signalled only by D3D11 and a consumer fence signalled only by D3D12. D3D11 waits per
  resource. The game renders every frame into ONE back buffer, so `d3d11Waits buffer=` counts every present.
- Agility SDK (0x887E0003): Windows' d3d12.dll reads the request from the exe WHEN IT LOADS. On Windows 10 with an
  older System32 runtime it loads the game's 611 file, sees 611 != 610, and every `D3D12CreateDevice` fails with
  D3D12_ERROR_INVALID_REDIST (a player on Win10 IoT 21H2 + RX 9060 XT). `native/bridge/src/agility.cpp`
  `align_agility_sdk()` patches the exe's export before anything loads d3d12.dll. TGPT ships Agility 1.619
  (`Native/D3D12`); the bridge points the request at it when newer than both Windows' and the game's, only from inside
  the game folder, Microsoft-signed. A failed device with it is remembered (registry GPU identity; no early dxgi.dll).
  Not yet confirmed on a real Windows 10 PC. If a report shows 0x887E0003, read the bridge log's Agility line ("too
  late" means another tool, such as OptiScaler, loaded d3d12.dll first).
- NGX keeps one session per D3D12 device: Streamline creates the device and inits NGX first, so a later
  `NVSDK_NGX_D3D12_Init_with_ProjectID` joins Streamline's session and NGX searches only its PathListInfo (plugin
  folder + `pathsToPlugins`) and the exe folder (0xBAD0000B otherwise). TGFG's slInit adds
  `BepInEx/TGBetterUpscaling/Native/DLSS` as `pathsToPlugins[1]` when that folder has `nvngx_dlssd.dll` and
  `TGPathTracing.Native.dll` exists (file checks: extensions bootstrap one at a time). TGBU starts NGX for RR only on
  `TGUP_EVENT_PREPARE_RAY_RECONSTRUCTION` (3). Test with `--installed` and the game's `UnityPlayer.dll` beside the exe.
- Native DLLs link the CRT statically (/MT) since 1.2.0: /MD with MSVC 19.44 imported `_Mtx_lock` without
  `_Mtx_init_in_situ` and crashed on msvcp140 older than 14.40 (the game ships none). /MT DLLs must not call
  DisableThreadLibraryCalls. The "Visual C++ runtime: Missing" row covers the vendor DLLs, which need the redist.
- Unity replaces the swapchain on a display-mode change (it doesn't resize): `Dx11Swapchain::retire()` hands the
  shared pipeline over. `add_hook` is idempotent (keyed by target address).
- NVAPI driver settings: driver 616.92 rejects NVDRS_SETTING at nvapi.h's size 12328 (-9); it wants 12320 (4-byte
  packing). DLSS IDs: SR preset 0x10E41DF3 (12 = L on the owner's profile), SR override 0x10E41E01, SR mode 0x10AFB768
  (3 = the game's), DLAA 0x10E41DF4, scaling ratio 0x10E41DF5. Smooth Motion 0xB0D384C0 (+ APIs 0xB0CC0875) is from
  Profile Inspector and UNVERIFIED (616.92 names no such setting).

## 5. TG Frame Generation

Frame-generation inputs, orientation, backends and compatibility.
Moved unchanged to `docs/PROJECT-CONTEXT-MODS.md`, section 5.

## 6. TG Better Upscaling

Upscaler integration, vendor modes, exposure and DLL selection.
Moved unchanged to `docs/PROJECT-CONTEXT-MODS.md`, section 6.

## 7. IL2CPP (the normal version)

The normal version's interop, menu lifetimes, crashes and validation.
Moved unchanged to `docs/PROJECT-CONTEXT-MODS.md`, section 7.

## 8. Troubleshooting report and player reports (1.2.0)

The troubleshooting report, player evidence and the Defender report.
Moved unchanged to `docs/PROJECT-CONTEXT-MODS.md`, section 8.

## 9. TG Path Tracing

Current code in base20i, checked against round 20's 2026-10-12 01:25 state. Older measurements and design history
moved unchanged to `docs/PT-DETAILS.md` (the dated archive at its end); open work is in `docs/PT-REQUESTS.md`.

### 9.1 Shape of it
- Mono only: `native/pathtracing`, `src/TGPathTracing` (net472), `src/TGPathTracing.Autoload`; GUID
  `com.wessberg.tgpathtracing`. The bridge owns the shared D3D12 device.
- ABI `0x00012700` on both sides (`tgpt_api.h`, `NativeTracer.cs`). Sizes in bytes: Frame 624, Status 232,
  Material 204, MeshDesc 192, CpuMeshDesc 56, Instance 68, Moving 116, SkinnedInstance 100, Light 80, Decal 928,
  WaterCaustics 288, Guides 64, SharedTexture 96, HairLobes 48, ClothWind 48, PlantWind 48, PlantWindFrame 32,
  Rain 64, FogBox 144, Yardstick 632. `FrameConstants` 9024 (`tracer.cpp`); `NativeTracer.Validate` checks managed sizes.
- `start_passes` makes pipelines off the render thread; `TGPT_Status.starting` holds `Session.WantsFrames`.
  A cold driver cache takes minutes on the large passes (PT-111), not the seconds of round 3. A warm cache is faster.
- Capture hooks opaque `HDRenderPipeline.RenderSSR` after the G-buffer; the transparent call is excluded.
  The composite CustomPassVolume runs at AfterOpaqueAndSky. Frame N waits on the GPU for its own trace; inputs
  use parity buffers, tracing an async compute queue. There is no one-frame reprojection.
- `pt_prepare` starts each pixel with `pt_primary.hlsli`'s `primary_all`; HDRP is the yardstick, its old starting
  route still behind `GBUFFER_SURFACES` (PT-60 cleanup owed).
- PT-112 A: `GamePasses.cs` empties the gameplay camera's opaque draw lists and disables HDRP decals. The tracer
  writes its own depth and motion into HDRP's depth buffer, motion vectors and depth pyramid (`OWN_DEPTH`).
  Traced decals follow the game's setting recorded before the cut. Yardstick, the old route and hidden
  `HDRP draws under the traced picture` restore HDRP's drawing for tests.
- HDRP's AO, contact shadows, SSR (opaque and transparent), SSGI, screen-space shadows, SSS, transmission,
  shadow maps, volumetric grid, deferred lighting and camera motion pass are cut under that rule.
  Traced fog is the rule too: HDRP's opaque fog and screen-space fog blur are skipped (9.4).
  Still to migrate: the sky and transparent effects (PT-112 C). HDRP keeps post, upscaling and the HUD.
- SM 6.6 inline rays (DXR 1.1), bindless resources, `pt_root.hlsli`, DXC `-WX`; DXR 1.2/SM 6.9 via Agility 1.619
  adds micromaps/reordering where reported by the device. Plain twins are measuring tools; reordering stays on.
- The 14 timed stages, in `tracer.cpp` `kStageNames` order: scene, sky, caustics, prepare, bounced light,
  bounced light spatial reuse, direct light, reflections, radiance cache, denoising, fog cells, fog walk, fog lamps,
  composite. PT-130 moved caustics before every reader on 10-11; seen in
  `20261011-215257`, the owner's moving-water check still owed.
- HDRP's indirect diffuse multiplier stays 0 (its GBuffer3 contains baked light); distortion stays off.
  `ReplacedRows.cs` greys the game's replaced rows with reasons. Volume overrides use `Add<T>()`, never `Add<T>(true)`.

### 9.2 Scene capture
- `SceneCapture.cs`: scan every 2 s, or after 0.5 s when movement reaches 1 m or LOD scale changes.
  Pending arrivals shorten the interval; mesh uploads are spread across frames. Geometry outside a fixed scene
  radius is no longer discarded. PT-87 tracks memory; PT-124 tracks slow arrival and texture stutter.
- Sources: GameObjects, Drake, Medusa, Entities Graphics without Drake, Leshy, VSP, terrains and Kandra characters.
  Water gets displaced traced geometry. Renderer APIs and shader decodes: `docs/PT-DETAILS.md`.
- `WorldRules.cs` follows each renderer's LOD rule: Drake/Entities use scaled distance, Medusa squared distance,
  Leshy metres, Unity groups their last-level cull. Read HDRP's resolved LOD bias during culling. Drake's
  `MaterialMeshInfo` says whether it is loaded; do not substitute its range. Keep the old LOD until the new is ready.
- Shadow-only and other-layer Drake objects are included; cluster stand-ins remain where Entities resolution needs
  them. Do not restore the old list excluding brows, lashes, scalp shells, transparent eyes or water.
- Kandra's world-space skinned output is copied each frame; posed bottom-level ray structures are refitted/rebuilt.
  Solid submeshes keep their opaque marks even beside glass (PT-110 M3). Character micromaps were removed after
  two clean runs showed no gain (M4, `docs/agents/ommout.md`); plants' maps stay.
- Cloth is posed per instance (`TGPT_SetClothWind`); plants per 16 m cell (`TGPT_SetPlantWind`, `PlantGroup`).
  The game's wind maths include view-dependent fades. `docs/agents/world.md` 4.1 explains the decode.
  Plants store previous-minus-current vertex moves (`kMeshPreviousDelta`), cloth its previous pose. Character
  motion uses differences of nearby floats; transforming two world positions separately caused soldier flicker.
- Shared game textures and material programs are the normal route (`TextureShare.cs`, `MaterialPrograms.cs`).
  Old alpha/colour/detail atlases still have readers and capacity limits; their presence is a migration gap, not
  evidence that every material reads its original texture. PT-45/46/51/71 and the material reports track the rest.
- Micromaps settle only proven coverage. Plants use their maps in the wind pose; leaves/grass bake at level 6.
  Covered leaves are opaque to other rays, but light rays test their transmission separately (M1/M2/M7).
  Hair's camera lookup uses changing mips and anisotropic footprints; a bare top-mip sample proves nothing about
  every reader. Any new hair map must bound all readers, including the probe. See M4's dead end before trying again.
- Limits still in code include 256 skinned renderers, 8 materials per skinned mesh, 256 cloth records,
  4096 plant-wind records and four active water slots. These are gaps under the owner's no-picture-limits rule.
- Remaining migration work includes terrain/material fidelity, VFX Graph particles, trails and transparent effects.
  The game can stop simulating effects off screen: tracing only the visible buffers would not satisfy PT-112 C.

### 9.3 Lights
- `SceneLights.IsPlacedLight` classifies by attachment, effect names and what emits light nearby. A lamp's name
  alone is not proof; a glowing crystal or magic source can keep an otherwise unnamed light. Artist fill stays out.
  Inventories and reasons are logged. CLO's dialogue light is kept and scaled by `AutomaticGainInverse`.
- Flames and lamps carry `LIGHT_NO_RANGE`: their near-list reach is a sampling boundary, not where light stops.
  Effects and glowing-source lights can retain HDRP's range. Size comes from the emitter where found; kind-sized
  fallbacks are logged. `LIGHT_AT_EMITTER` marks placement at the flame; `LIGHT_NO_SPECULAR` respects affectSpecular.
- Cookies are sampled; near light lists and far strata provide candidates without the old 40 m light cutoff.
  The lamp search tree reduces candidate work (`lamp_tree.hpp`).
- Emissive surfaces are sampled too, except surfaces represented by a kept light (`OWN_SURFACE`), which would
  otherwise emit twice. Exposure weight and Albedo Affects Emissive are material terms; front-only emission stays
  front-only. Moving emitters and selection limits remain part of the correctness audit.
- HDRP light overrides restore on stop. Fill Light Fog is inert until fog-only lights enter the traced medium.

### 9.4 Exposure, fog and traced gain
- HDRP keeps the game's exposure. `pt_meter` meters 4 x 4 block means of traced light (since 10-11), so raw noise
  does not lower its median and win extra gain against NRD. Compare denoisers at the same traced gain.
- `HdrpPasses.AutomaticGain`: brighten = max(0, min((-3.06 - p50) x Dark Adaptation, 0.5 - p98)), limited to
  game EV minus the darkest-scene EV (7), at most 12 stops. Exposure adds -3..3; ease up 1.5 EV/s, down 3 EV/s;
  hold in debug views and F8. A 128 x 72 seen meter every fourth frame lets bright fog count as bright.
- `highlight_hold` reduces automatic gain on already-bright pixels; it is not one multiplier everywhere.
  Read the exposure/gain line before judging a change that removes light. RR's highlight hold remains PT-127 review 17.
- Radiance storage uses an eased reference from sun/lamp strength to fit physical units into FP16: the sun's white,
  or 1 m from the brightest lamp, a lamp counting for no more than 64 times the median lamp and a glowing mesh for no
  more than its own radiance (PT-136, 2026-10-12: one effect light of 1.8e9 cd put a vista's reference 20,000 times
  over the sun's white; dim light was lost in 16-bit targets and the dark end crushed: darkest 5 % 29, the game's 38,
  38 with the bound). The log's "The reference radiance" and "The lamps the reference is taken from" lines say what
  it is. PT-50 removed the old clamps and the 25 cm lamp floor; `NO_CLAMPS` remains reserved.
- The sky probe scales HDRP ambient/reflection remnants; interiors suppress sky leakage. Shadow Detail and
  Fog Adaptation are gone.
- `pt_medium.hlsli`: height fog, local fog boxes, the lamps along a reflected or refracted ray's way (equiangular,
  PT-132), multiple scattering from world cache cells. Fog is composed after surface denoising; NRD does not
  denoise it. Sky haze, fog-only lights and box fidelity remain open.
- The camera's fog is in cells (PT-118, the rule since 2026-10-12; RTX Remix's layout, carried on past its 20 m):
  /16 screen size, 96 slices (48 to 20 m by a square, 48 more to the far distance by a factor), one sample a cell
  a frame where the camera's rays read, 254 frames, RGBA32F, no clamp. Each pixel walks them (`pt_fog_integrate`:
  the medium exact per ray, the cells' light read between centres). Near-lamp light is split: the smooth share in
  cells, the rest per pixel (`pt_fog_tail`, `fog_lamps_residual`).
- Game evidence, frozen, old per-pixel route / cells (`20261012-051553`, `-053455`): flicker 0.40 / 0.36 tavern, 0.99 /
  0.46 vista, 1.85 / 0.20 night, 0.33 / 0.17 mist; under NRD 0.40 / 0.18, 0.94 / 0.27, 1.92 / 0.16, 0.32 / 0.16; the
  same light to 1 %; 0.6 to 3.3 ms more. Open: PT-137, PT-138. Learned: a per-pixel pass with no history IS the
  grain where most fog lies past the cells; read a view's own numbers (reference, storage units, steps) before
  blaming an estimator for a percent; a frozen run cannot judge what shows only in motion.
- PT-132 (reflected and refracted rays' fog alone now): one anchor's light lists miss ranged lamps on long rays.
  PT-133: the SM 6.9 cells' pass gave huge finite values without a sun; splitting its sums cured the white tavern:
  keep the `KEEP THIS SHAPE` code until the cause is understood; other no-sun branches still need checks.

### 9.5 ReSTIR on RTXDI 3.1
- DI: weighted light candidates, temporal history 4 (hidden 0 means 4), Basic spatial reuse, a fresh permutation
  each frame. Final visibility is traced; spatial results do not feed next frame by default. Own boiling filter
  weighs W x targetPdf, excludes itself once at least four neighbours are lit; naive-sample discount is off.
- GI: history 8, max age 30, permutation, a search around reprojection, reconnection Jacobian and NaN rejection.
  Fallback sampling stays off. Multi-ray own means and the cache supplement reused samples.
- Reuse across frames is separate from the reordered path pass (`pt_gi_reuse`); spatial reuse has its own stage.
  Material-key regrouping after the first bounce ray is the rule. Do not put screen-order work after that call.
- PT-127 still audits previous-scene temporal visibility (review 21) and departures from vendor parameters (22).
  History 4, discount off and fallback off have measured reasons; the owner decides changes.
  Energy, crevice and boiling measurements, including their limitations: `docs/PT-DETAILS.md`.

### 9.6 Denoisers
- Choices: None, NRD, Ray Regeneration, Ray Reconstruction; best available RR > RRG > NRD. NRD serves when RR
  waits for DLSS or RRG cannot run; the tracer's filters remain a failure fallback and cover glass/water under NRD.
- Since 10-11, NRD 4.17.3 takes all opaque diffuse/specular light, demodulated with `NRD_MaterialFactors`,
  real first-bounce/reflection distances, shading normals, 2.5D motion with previous depth, and strand material IDs.
  RELAX DIFFUSE_SPECULAR is the rule; hidden `NRD trial` 2 selects REBLUR. History is set in seconds.
  Direct light and caustics belong in the input. The bounced-light-only route is removed.
- History confidence is the rule since 21:52 on 10-11, no switch (frame bit 21 free). The moving-candle run changed
  more because NRD followed the candles sooner; frozen checks found no added boil. PT-127 keeps both measurements.
  Diffuse pre-pass blur is off, firefly suppression on. RELAX versus REBLUR remains the owner's choice.
- NRD embeds DXIL in a static library; its heap/root signature run on our list. See `nrd_denoiser.cpp` and PT-127.
- Primary surface replacement is the rule for still, flat, pure mirrors with own camera rays and NRD:
  hand NRD the reflected surface's virtual depth/motion, mirrored normal, material, signals and hit distances.
  `pt_psr_record.hlsli`: 64-byte serial-stamped records; `pt_specular` captures them, carrying one word across rays.
  Merged game run `20261012-005626`: mirror detail 2.53 (old route 0.47); the owner's look remains owed.
- Not covered: moving/curved/nested mirrors, hair/skin/transparency in mirrors, glass and water's two branches.
  Mirror records allocate about 127 MB at 1924 x 1084 even with no mirror visible. Glass/water options for the owner:
  two complete NRD layers, one denoised layer, or the vendor sample's screen lookup (conflicts with a ruling).
  `docs/agents/psr-2.md` gives costs and gaps. Our reflection filter stays until that choice.
- RR runs in TGBU on the shared D3D12 device. Preset Default is F in SDK 310.9.16, as the owner requested;
  recheck after a DLL update. RGB specular guides, independent reflection distance, material diffuse guides
  and RR's own optimal render size are in source (`docs/agents/vendor-rr.md`).
  TGBU's part is not yet deployed/tested in game in the 01:25 state; the TGPT half is merged.
- RR hair guides and glass reflection-distance changes from worker psr are not merged: they need the game
  comparison in psr-2 section (c). Steady hair guides were deliberate. RR highlight hold is still review 17.
  Pre-exposure follows HDRP's 0.2..2 clamp; guide 3.7 does not support exposure inputs.
  A refused RR frame returns to the tracer's denoiser for 0.5 s with fresh history.
- RRG: FidelityFX 2.4 runtime support query; demodulated radiance, sqrt albedos, world oct normals, 2.5D motion.
  Fixed history scale, matching specular factors, gap resets, background context creation with retained device
  and fences are merged; `TGPT_RRG_DEBUG=1` enables validation. Direct-specular separation remains review 7.
  It still needs supported AMD hardware; do not call it game-tested.
- Cuts reset histories: camera change, HDRP reset rising edge, >60-degree turn or >20 m jump. Cache survives cuts.

### 9.7 Surfaces
- Camera/secondary rays share hit/material code: `MaterialPrograms.cs` captures values, `pt_common.hlsli` reads them.
  Decals have their own records and a spatial tree. Terrain layers and remaining shader gaps stay on PT-71's list.
- GGX keeps its small-denominator guard, `max(PI d^2, 1e-30)`; adding 1e-7 capped sharp peaks. HDRP Unlit uses
  f0 0 and roughness 1; Schlick f90 is saturate(50 f0). Reflection Boost exempts skin and foliage, not armour.
  The rough-surface cache branch belongs to Glossy Surfaces; All Surfaces is the default.
- Translucency uses material terms where decoded; fallback leaf and cloth terms still exist
  (`TRANSMISSION` 0.5, `CLOTH_TRANSMISSION` 0.3). These are gaps under PT-51, not proof of complete material fidelity.
- Skin/solid scattering uses traced walks with the material's diffusion profile and mask (`pt_walk`).
  The old screen-space Burley blur is gone. Skin's reflection exemption and shadow-origin history remain relevant;
  use shading normals for NRD, not the old per-triangle guide.
- Glass uses the shared `pt_dielectric.hlsli` walk: Fresnel, GGX, Snell/TIR, absorption along real segments,
  material IOR and compensated roulette. No old 15 m eligibility, 1 m exit search or guessed wall thickness.
  `GlassFinish.cs` still classifies glass and suppresses HDRP duplicates. Mixed labels/lids and open geometry
  need correct material/volume rules. The older shadow-connection caustic solver has separate limitations.
- Water is displaced traced geometry using the game's wave data; camera and secondary paths reflect/refract
  through it. It is not the old HDRP depth/normal reflection pass. Four active slots remain a gap (PT-116).
  The second ground ray in `water_medium` stays after its camp theory failed; water and tavern checks found
  no measured change. It does not solve every submerged/overhang case (PT-131).
- Caustics use traced wave surfaces, not HDRP's ready-made pattern or a flat-mirror glow. The frame rebuilds them
  before readers (PT-130). Glass solver history and the removed water stand-ins are dated in `docs/PT-DETAILS.md`;
  PT-14/51/114 track remaining correctness and cost. A matching camera view alone does not prove reflected light.
- Hair, brows and lashes are camera-ray surfaces with their own depth/motion, not a layer after the denoiser.
  Stochastic coverage uses the material's remapped alpha; shading uses strands and Kajiya-Kay highlights.
  HDRP hair depth holding (`HoldLayerDepth`) was removed in R20. Remaining atlas/guide work is still a gap.
  Eyes/cornea are traced material surfaces; dark oblique eyes remain PT-119. Shader decodes are in PT-DETAILS.
- Legacy surface offsets and the G-buffer route still need removal (PT-60). In particular, psr-2 found
  non-character reflection origins still using `ray_offset`'s centimetres instead of the camera ray's exact hit.
  Do not describe the renderer migration or mirror fidelity as complete.

### 9.8 Radiance cache
- Own world hash grid: 2^20 slots, buckets of 8; incoming indirect E/pi, 0.1 m cells growing with distance,
  sides by dominant normal. First/extension rays feed it; last vertices read it. No SDK source copied.
- Existing limits: 512 samples per cell/frame, channel cap 2000, EMA at least 1/16, expiry after 240 frames.
  These remain correctness-audit facts, not owner-approved picture limits. Clears on first use, re-enable and
  scene clear, not camera cuts. The older furnace and memory measurements are archived in PT-DETAILS.

### 9.9 Rows and defaults (`src/TGPathTracing/Settings.cs`)
Path Tracing On; Denoising best available; Exposure 0 (-3..3), Dark Adaptation 35 %, Rays Per Pixel 1 (1-16),
Bounces 1 (1-4), Radiance Cache On, Indirect Color 1 (0-4), Reflections All Surfaces, Caustics On,
Fill Light Fog On (currently inert), Spatial Samples 4 (0-16), Boiling Filter On, Emissive Lights On,
Crevice Light Off, Sky Intensity 1, Reflection Boost 0.1, Debug View None. Shadow Detail, Water Reflections,
Ray Distance and Scene Radius are gone. Hidden histories 0 mean DI 4/GI 8; visibility reuse and spatial
feedback are off. Saved choices persist except explicit migrations (`OrphanedEntries` before `Bind`).
Trial defaults: fog grid off, NRD RELAX (trial 2 REBLUR), Mega Geometry 0. Check Settings before assigning a trial.

### 9.10 F8 capture, debug views, logs
- F8 (`DebugCapture.cs`, hold still about 24 s) writes `BepInEx/TGPathTracing/Captures/<time>/report.txt` and JPGs.
  Read it and inspect the JPGs yourself. Contents: PT-DETAILS. F8 also hits TGFG's emergency off and AvalonAtlas.
- View names: Settings; colour meanings: shaders and `docs/agents/hair-6.md`. Yardstick compares first hits.
- Logs: BepInEx `Path tracing hook 10s:` / `Path tracing:`, native `Path tracing 10s:`; stages as in 9.1.
- `report_gpu_stop` names the last/stalled GPU stages and device-removal reasons; compare Windows nvlddmkm logs.
- Descriptor slot misuse is logged since 10-11 (double return or writing a free slot); PT-129's second-load
  reflections hang remains unreproduced. The owner stopped reproduction work; do not treat the guard as its fix.

### 9.11 The offline harness
- `tests/pathtracing_gpu_test.cpp` loads the real bridge/extension with synthetic HDRP-like buffers; same render
  events, upright PNGs in `build/native/tgpt-images`. In-game recipes and acceptance: `docs/PT-HARNESS.md`.
- Motion uses unjittered matrices and HDRP's previous-camera translation. `objects[]` draws only the G-buffer;
  SetScene supplies traced geometry. Wait for startup; sky off is 1e-6 (0 means default).
- Match A/B frame indices/seeds; raw references, cache-off shadow sums, multiple frames, debug views twice.
- It never runs RR: use `tgup_gpu_smoke_with_pt`, then the real game. Worker checks are small `_ONLY` modes;
  an early exit placed late still runs everything above it. Whole GPU suite belongs to the coordinator.
- Last whole-run baseline: 44 failures on R19 and early R20 (PT-113), before the NRD rewrite; no current pass.
  Switches: PT-DETAILS. No GPU test beside an owner's game; AGENTS.md's guard applies.

### 9.12 Bits (check before adding one; assign workers' bits first)
Values below are decimal masks unless called bit positions. Checked against `tgpt_api.h` and `NativeTracer.cs`;
shader masks/samplers against `pt_common.hlsli`, `pt_root.hlsli` and `scene.hpp`. Native-only options stay reserved.
- `TGPT_OPTION_*`: NO_SPATIAL 1, NO_TEMPORAL 2, NO_DENOISER 4, NO_SPECULAR 8, RAY_RECONSTRUCTION 16,
  NO_MICROMAPS 32, RAY_REGENERATION 64, NO_PASS_THROUGH 128, DI_VISIBILITY 256, SPATIAL_FEEDBACK 512, NRD 1024,
  NO_BOILING 2048, NO_CLAMPS 4096 (inert, reserved), EXACT_JACOBIAN 8192, OFFSET_RECEIVER 16384, NO_GI_BOILING 32768,
  NO_RADIANCE_CACHE 65536, EMISSIVE_LIGHTS 131072, GLOSSY_REFLECTIONS 262144, GI_OWN_RAYS 524288,
  DI_NO_REUSE 1048576, GI_VISIBILITY 2097152, CAUSTICS_LIT 4194304, NO_CAUSTICS 16777216, FACETED 33554432,
  ATLAS_TEXTURES 67108864, REFLECTION_TOP_MIP 134217728, NO_SUBSURFACE 268435456, DRY_GROUND 536870912,
  CAUSTICS_SPLIT 1073741824, GBUFFER_SURFACES 2147483648. Free: 8388608 (bit 23). New trials prefer frame flags.
- Frame flag bit positions: RESET 0 (mask 1), GUIDES 1 (mask 2), Mega Geometry 2-5 (shift 2, mask 15),
  Micromaps trial 6-13 (shift 6, mask 255), OWN_DEPTH 14, TRACED_FOG 15, FOG_WALK 16,
  NRD trial 17-18 (shift 17, mask 3), the caustics' grid 19, 25, 26 (PT-114). Free: 20-24, 27-31 (10-12).
- `TGPT_MATERIAL_*`: DOUBLE_SIDED 1, ALPHA_TEST 2, VERTEX_COLOR 4, ALPHA_TILE 8, CAMERA_ONLY 16, GLASS 64,
  EMISSIVE_ALBEDO 128, TRANSLUCENT 256, THIN_CLOTH 512, MASK_TILE 1024, SEE_THROUGH 2048, STRANDS 4096, SWAYS 8192,
  EYE 16384, HAIR_LOBES 32768, HAIR_LAYER 65536, OWN_SURFACE 131072, SHARED 262144, FIRST_PERSON 1048576,
  SUBSURFACE 2097152, NO_DECALS 33554432. Free bit positions: 5, 19, 22-24, 26-31.
- `TGPT_INSTANCE_*`: ALPHA_TEST 1, HIDDEN 2, STAND_IN 4, CHARACTER 8, SWAYS 32, WATER 64; water slot in bits 8-9.
  There is no current public MOVING define; moving records are `TGPT_Moving`. Bit 4 is unassigned in this header.
- Instance masks: plain still 0x21, foliage 0x40 (combined still 0x61), sways 0x02, see-through 0x04, level water
  0x08, strands 0x10, waves 0x80. Ray masks: ALL 0xF7, STILL 0xF5, WATER 0x08, WAVES 0x80, SOLID 0x77,
  GROUND 0x21, STRANDS 0x10, FOLIAGE 0x40, SWAYS 0x02. Instance flags are not these ray masks.
- Light flags: cookie low 16 bits; HDRP_SHADOWS 0x10000, NO_SPECULAR 0x20000, AT_EMITTER 0x40000,
  NO_RANGE 0x80000, trial HDRP radius in millimetres in bits 20-31. Public render events: TRACE 1, COMPOSITE 2, MARK 8.
- Static samplers: s0 linear clamp; s1 linear wrap U/W, clamp V; s2 point clamp; s3 linear wrap;
  s4 anisotropic 16x wrap; s5 anisotropic 16x clamp. Dynamic material samplers also use the sampler heap.
  Atlas/key history: PT-DETAILS. Packed bytes/spare fields: read prepare/load_surface, the header and managed writer.

### 9.13 Performance (RTX 5080; dated evidence, not a new baseline)
- First speed and memory (PT-99/87), then rendering gaps. Quote only foreground stretches with no other program on
  the card BUT THE OWNER'S STREAM, which stays on (2026-10-12): speed is measured with it (it slows the two
  reordering passes by 2 to 3 ms each in the forest, the reflections by 13 ms at the beach), named so, and held
  against runs with a stream alone; decisions by an A/B/A within one run. Baselines before 10-12 08:26 had none.
- R19: the game's D3D11 device and our D3D12 device take turns on this card; a frame is both devices' work plus
  waits (the 10 s CPU/GPU clocks and `measure-waits` split them). Never subtract today's trace from an older frame.
- Reordering: keep little across one material-key call after the first bounce ray; RTXDI reuse is outside it. Check
  the driver's stack report and DXIL local arrays before blaming the technology. Both sort choices still reorder.
- PT-131 ("a camp at 3 fps") was the harness's walking camera inside a hill: walk frame times before 2026-10-12 are
  of it. Real, from it: water in view asks the sun through the waves at every scatter point; a patch no finer than
  the asking ray is wide took the beach from 132 to 73 ms of the tracer. The scene's record on the CPU (the GPU
  waits for it; "PT record CPU 10s") is 6 to 7 ms a frame; left: the water's pose (2.6 ms), characters, plants.
- PT-114: in an interior the map's water is not traced nor drawn (an `Interior*` scene loaded means the player is in
  it; `WaterCaustics.LeftOutside`): the tavern 49.6 to 43.7 ms, the same picture. Outdoors the caustics' 4 to 5 ms
  where no water is in view remain: the grid's wave/level work (the shadow rays' cut saved nothing: DEAD ENDS).
- PT-109: NVIDIA's layout 6 % behind plain in the town (`20261012-000233`); off. PT-110: level-6 plant maps stay.
- Baseline 2026-10-12 06:20 (`20261012-061203`, world running, no denoiser, clean card), fps / frame / tracer ms:
  tavern 21.5 / 46.5 / 35.7, town 12.9 / 77.5 / 59.5, forest 11.2 / 89.3 / 75.0, vista 18.0 / 55.5 / 41.0, beach
  11.2 / 89.3 / 73.1. Not measured since: the fog's rule, PT-114 (the tavern about 43 ms).

## 10. DEAD ENDS - measured not to work; don't re-attack without a new reason, in writing

Path tracing:
- The fog cells' light weighed by the medium's scattering (2026-10-12): the same picture in four scenes, 8 to 20 %
  more of the cells' pass. Remix's reservoir reuse and Gaussian filter over the cells: the same picture in every
  FROZEN scene, which cannot judge them (PT-138).
- Water's separate pass and regrouping inside its loop were dropped (R19 section 2, `20261010-173151`, `-185439`); both runs had GPU contention, so no clean speed claim.
- (NOT A DEAD END, the owner 2026-10-12: RTX Mega Geometry is on and stays on; "slower" is our fault to find. What was measured: its clusters cost the bounced light's rays 6 ms more and its 10,000 plant structures a frame 13 to 15 ms of CPU in the town, PT-109, `megageo-2.md` to `megageo-6.md`, `20261012-000233`.)
- Caustcut's exact cut of caustic shadow rays saved nothing: 4.20 / 4.30 / 4.05 ms off/on/off (`20261011-213004`, PT-114, `docs/agents/caustcut.md`); trial dropped.
- Resetting the harness clock backwards rolls the game's date forward a day; freeze through compilation instead (R20 night, `docs/PT-HARNESS.md`, `20261011-235717`).
- The camp's camera was not underwater: the ground-ray change did not fix its cost (`20261011-223913`, PT-131); special glass/water paths remain the measured lead.
- HDRP's own ray tracing: every DXR variant is stripped from the shipped build.
- A GI or hybrid mode: removed at the owner's word; Full only.
- Excluding hair or eyes from tracing as a fix for dark eyes or odd hair shadows (owner, 2026-09-28).
- Scaling the lights' `volumetricDimmer` for Fog Adaptation: the game's LightWithOverride caches it when an override
  starts and writes it back when it stops (a gained value would stick after tracing). The fog pass works on the image.
- Cycles' per-vertex "no progress" failure in the MNEE walk: it fails every flat pane under a directional light.
- Opacity micromaps for skinned characters (PT-110 M4, 2026-10-11): 421,395 cut-out triangles mapped in the tavern,
  its tracer 36.20 / 36.25 / 36.20 ms (off, on, off), the town's 67.81 / 68.17 / 67.40, both clean. Hair stays
  unknown under bounds that hold for every reader. Before a new try: the "Micromaps trial" 2 as the bound (9.2).
- Raising ReSTIR history caps on harness numbers: the tracer's own ReSTIR at cap 20 was grainy in game (1 was chosen
  there). Since the RTXDI move: the direct light's history is 4 (the plugin's default; 20, RTXDI's own, boiled on thin
  geometry) and the bounced light's 8; review-vendor-astra 22 lists these departures for the owner.
- A denoised harness reference: it scored blur as a win (sun merged into the diffuse signal: 3.0 % -> 0.9 % while every
  sun shadow blurred). Grade against an undenoised reference. (Its old last sentence, "the sun and lamps are their own
  signal", went with PT-127: NRD takes all the diffuse light, as documented.)
- A 3x3 raw spatial-variance fallback in the filter: it read shadow edges as noise. Dividing reflections by specular
  albedo: worse.
- Un-zeroing HDRP's indirect diffuse multiplier (on the false premise that hands are forward-lit): flat ambient filled
  every shadow and skin glowed like a lamp.
- Leaving out shadow-only / other-layer objects wholesale: a stairwell lost its roof. Dropping the `CM_*_Cluster_*`
  meshes before Entities Graphics was captured: stairwells and tunnels broke.
- Light blacklists by name: candlesticks whose only light was `PointLight_Ambience` went dark. Whitelist by attachment.
- HDRP ranges for traced lights: rooms dark. HDRP's own auto exposure over the whole image (floors tried at EV 2, 5,
  then 7): everything HDRP draws blew out 6-7 stops. Removing the fog's teal ambient: auto exposure climbed to its
  floor. Median-only metering of the traced light: sunny exteriors at 0.5-0.8x the game's exposure.
- Radiance clamps measured on screen, against exposure or its 10 s average: light followed the camera's direction.
- `Add<Fog>(true)` to dim fog ambient: turned HDRP's fog off. `volumetricDimmer` 0 for fill-light fog: stuck at 0 after
  tracing stopped (LightWithOverride caches it).
- TAKEN BACK (2026-10-11, PT-127): "full RELAX blurs sun shadows and flickers; hand NRD the bounced light only". That
  test (round 2) handed RELAX a hit distance of 1 and the triangle's normal. With NVIDIA's documented inputs RELAX takes
  all the light and is the rule. The lesson: when a vendor's way looks worse, look for a wrong input of ours first.
- DI visibility reuse: lamp shadow edge 0.8 -> 14 px (Basic spatial) or 4.4 px (with RTXDI's ray-traced correction)
  for 7-13 % less night noise. Spatial results feeding the next frame: +6-10 % error. Both hidden, off.
- RTXDI pairwise DI spatial reuse (counted empty neighbours: black at light-range edges); RTXDI's boiling filter (out
  of bounds below 32 lanes; counts itself, so it can't fire with <= 41 of 64 lit); RTXDI GI fallback sampling (biased
  dark on disocclusion).
- A uniform light pick (1 of 884, x884): fireflies. Even light-grid candidates: a chandelier as rare as the faintest
  candle, boiling. A 10 cm light-radius floor: 4 px lamp penumbrae instead of 0.8.
- Flipping the shadow origin inside for every surface facing away (jagged face shadows); deciding pass-through by the
  instance's alpha flag (hair made a head opaque from inside). Hair cut out at 0.2 coverage: hard card shadows.
- Traced eyes: the sclera texture over the whole eye. Eyes left out: the camera ray lit the socket skin behind.
  Hair's bounced-light add (2026-09-28): blonde hair glowed. Reflection Boost off for whole characters: the owner
  wants armour and metal boosted.
- Hair (or anything) as a layer laid over the picture after the denoiser, with HDRP's own hidden: no denoiser sees it,
  and the depth and motion vectors at its pixels are the surface's behind it (flicker, sliding: PT-53 to PT-57).
- Glass stage 1 "finish" (smoothness to 0.95): did nothing on mask-mapped materials (their remap max is already 1).
  The GI "1.6x too bright" finding: an artifact of a reference that still had clamps and filters.
- Boiling: the radiance cache is not the cause (Cache Off didn't remove it); Spatial Samples 0 is much worse, 8 better,
  8-15 rays better; nothing tried removes it (2026-09-28).
- Mirroring reflection rays out of the triangle (above_surface) did NOT fix the saw on the door's metal strap. Bounces
  above 1: no visible difference in game (owner: 1 vs 4) at up to 3x the cost. D3D11 NGX for RR: crashed in its DLL.

Frame generation, upscaling, bridge:
- "DLSS-G and XeSS need no flip because they get the projection matrix": wrong, all three need it.
- FSR "resetting every frame": its reset indicators showed none. Ruled out as the flicker's cause: film grain,
  vignette, chromatic aberration, motion blur, base framerate, Reflex, VSync, FSR motion scale / jitter / near-far,
  HDR MaxLuminance.
- Exclusive Fullscreen by a 10 Hz `Screen.SetResolution` override (Options lied, the game wrote exclusive back) or by
  fighting the value back. A FSR/XeSS jitter override (STP's jitter was already right). A shared RCAS pass. A FSR
  version row. A second D3D12 device. An own DXGI factory through realFactory1 (went through ReShade, wrapped our
  device). Deferring the DLSS-G capability query to the present thread (reverted, aa5bb3f). Harmony-patching
  UpScaling at plugin load (hung the game). The 0.4.0 "DLSS layout" fallback for unknown capabilities.
- A clipboard copy or subfolder for the troubleshooting report (owner rejected). Alerts for NVCPL VSync On and Max
  Frame Rate (overbroad). "No RR while the DLSS 5 add-on is loaded" and "DLSS 5 pauses" (both wrong).

## 11. Method traps

- A NUMBER KEPT ACROSS FRAMES names something of its owner's own, never a place in a buffer laid out anew every
  frame, nor an address that a later copy moves (R20, PT-125: the plants' per-vertex range was such a place; after
  a walk every plant was posed from other plants' records, gone from the picture, at times a GPU reset; PT-134 and
  PT-135 the same kind, found by an audit). A read through a released view shows as DEVICE_HUNG, as a timeout
  does. The log's "The GPU stopped" line names the stage, the stamp and, for the plants, the step and the group.
- The in-game harness (R20): a step's pictures are taken as its walk STARTS (`walk-end.png`: where it ended); a
  running step moves the game's clock: compare light in frozen steps of one run; a recipe after `@` holds for its
  one scene; the tracer's log runs on across starts; queued and trial runs go through `Queue-PtRun.ps1`. A SCENE'S
  LAMPS DEPEND ON THE SCENES LOADED BEFORE IT (PT-147: a town lamp 0.125 or 524,288 by the game's own state; scenes
  run in `scenarios.json`'s order). A moving step needs `"disableMotionBlur": true` (PT-HARNESS).
- Workers' offline GPU tests starve while the owner plays on and off (a build's first start: 4 to 10 minutes of
  driver work): they hand over a trial DLL instead. Storage Sense empties `%TEMP%` on a nearly full disk (AGENT-WORKERS).
- Mono/normal branch switches (R17): `Build.Local.props` points Mono at the normal version's Steam folder, whose
  Managed assemblies and BepInEx core Mono C# builds read. Hold PT deploys and runs during NR testing; back up BepInEx
  and the loader files (`winhttp.dll`, `doorstop_config.ini`, `.doorstop_version`) as real files, not Vortex links;
  restore the Mono setup by replacement, not merging.
- Deploys: the game folder's DLL can be a Vortex symlink to an older staged build ("exactly like before", 2026-09-20).
  `Build.ps1 -Deploy` copies from `artifacts/Native`, which only `cmake --install` refreshes. Hash all three places.
- WRL's `&comptr` is a `ComPtrRef`: converted to `ComPtr<T>*` it EMPTIES the ComPtr (it is meant for out parameters).
  `const ComPtr<X>* list[]={&a, &b}` threw three passes away silently (R19). Use `std::addressof`.
- Never change Unity's `QualitySettings.maxQueuedFrames` while the game runs: setting it back from 3 to 2 mid-run hung
  the game (R19, run `20261010-192506`; Windows' Application Hang, no driver reset). Read it; change pacing by config.
- D3D11 binds no render targets (and depth buffer) of different sizes together: a larger texture as a second target
  dropped its writes silently (R19, HDRP's depth pyramid). Count writes by what lands, not by draws. A shared texture
  between the devices needs a format the game's device can share: R32G32_FLOAT failed (0x80070057) and 30 failed frames
  turn the tracer off for the session; R32_FLOAT, R16G16_FLOAT, R16G16B16A16_FLOAT, R16_FLOAT, R8G8B8A8_UNORM share.
- HDRP (R19, PT-112): its opaque fog and transparents read the DEPTH PYRAMID (`prepassOutput.depthPyramidTexture`, a copy
  made after the opaque passes), not the depth buffer. Turning `FrameSettingsField.OpaqueObjects` off makes the game's
  ECS re-upload its object data every frame (Unity's `EndWrite`, 800 ms frames); empty the opaque draw lists instead
  (`HDRenderPipeline.CreateOpaqueRendererListDesc`). `DecalCapture` follows HDRP's Decals frame setting.
- A raw buffer view (ByteAddressBuffer) must start on a multiple of 16 bytes (`D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT`). One
  that does not removes the device (`DXGI_ERROR_INVALID_CALL`) with nothing said: the next creation fails (R6, the
  plants' groups: several views into one buffer).
- R20: CMake makes EVERY pass depend on EVERY `.hlsli`: an edit to one marks all 57 headers stale (30 to 60 min; three
  trees at once take hours: a worker that changes no shader copies main's headers). Never start the game on a build
  with a cold driver cache while dxc compiles run (the harness's 600 s wait a step runs out); a measuring build's
  reordering pass took the driver over 20 minutes: give such a run a warm-up start of its own.
- A world-space float position (a character's skinned vertices, kilometres from the origin) placed through a transform
  and compared with a ray's own hit differs by up to half a millimetre: take MOVES as differences of nearby floats (R20,
  the town's soldier "moved" a few hundredths of a pixel and DLSS dropped its history).
- `d3d11.hlsl` is embedded at CMake configure time (CMAKE_CONFIGURE_DEPENDS now forces a reconfigure), in 8000-char
  chunks (MSVC's 16380-char literal limit). `small` is a Windows macro (rpcndr.h).
- `Package.ps1` packages existing builds only (`Build.ps1 -Package`), and refuses without `build-manifest.json`.
- Offline checks that passed for nothing: a flat room cannot show grain (bumps in the G-buffer normal do); a leaf quad
  in a wall's shadow; the micromap test with every micro-triangle unknown (until mutation-checked). Total variation is
  blind to a blurred edge: use squared differences of block averages. Shadow sums: cache off.
- A plugin logging `Loading [X]` and nothing else died in Awake (no Unity log). A mod folder is not an installed mod.
- After a game update: decompile the changed types (`ilspycmd -t`), compile every mod against it, and check the members
  reached by name with Mono.Cecil (a compile can't see them). A leftover `BepInEx/interop` is the older build's API.
- Windows Python in text mode keeps CRLF; never pass `newline=""` when writing.
- The Bash tool eats backslashes even inside a quoted heredoc: a printf's \n became a real newline and a Python pin lost
  its escape (R3). Anything with a backslash goes through Write/Edit or a spec file.
- A harness check can pass on the wrong model: numpy re-implementations of the shader math (`tools/research/`) found
  the MNEE walk's pane failure and that a cube's caustic misses its straight shadow, which the GPU numbers alone hid.
- `Build.ps1 -Mod <one> -Deploy` refuses when another installed mod carries a different bridge build (two trees'
  bridge DLLs differ by build stamps alone, R3). Compare `artifacts\Native\TGGraphicsBridge\TGGraphicsBridge.Native.dll`
  with the game's `BepInEx\TGFrameGeneration\Native` copy; if they differ and the bridge's sources did not change, copy
  the installed build back into artifacts, rerun `tools\Write-NativeManifest.ps1`, then deploy. Else `-Mod All`.
- A mutant restored with Copy-Item (or `cp -p`) keeps the copy's older time: the build keeps the mutated shader and
  the "restored" run fails again (R4). Write the file anew, or touch it.
- Unity's `JsonUtility` in a BepInEx plugin reads and writes only the top object's plain fields: nested classes and
  arrays of them come back empty, with no error (R6: a request's scenarios, the list of saves). Use the game's Json.NET.
- `Get-Counter` on the GPU counters fails for the whole set when one process ends between its two readings: a failed
  reading is "unknown", never "free" (R6). `CopyFromScreen` from the agent's shell captures a DPI-scaled corner.
- Line endings are mixed in the working tree (`core.autocrlf=true` normalizes the index): keep each file's own: read
  bytes, look for CRLF, write back the same.

## 12. Build recipes

- A worker with read-only SDK junctions must not configure NRD from source: NRD's CMake writes its
  `Shaders/NRDConfig.hlsli` into the SDK. `TGPT_NRD_PREBUILT_LIBRARY` and `TGPT_NRD_PREBUILT_DEPS` can link the
  coordinator's already built matching NRD/ShaderMakeBlob libraries without that write (fog-froxel report).
  Keep this a worker build configuration; normal packaging uses the source-build path and its notices.

- In vcvars64 (AGENTS.md). `build/native`: NMake, Release, `FETCHCONTENT_FULLY_DISCONNECTED=ON` (NRD's deps in
  `build/native/_deps`). Configure from the repo root (`<repo>`: its absolute path, forward slashes):
  ```
  cmake -S . -B build\native -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DTGFG_BUILD_NATIVE=ON -DBUILD_TESTING=ON
    -DSTREAMLINE_ROOT=<repo>/Streamline-2.14.16 -DNGX_INCLUDE_DIR=<repo>/Streamline-2.14.16/external/ngx-sdk/include
    -DFIDELITYFX_ROOT=<repo>/FidelityFX-SDK-2.4.0 -DXESS_ROOT=<repo>/XeSS-SDK-3.0.2
    -DANTILAG2_ROOT=<repo>/AntiLag2-SDK-2.0.0a -DDLSS_ROOT=<repo>/DLSS-SDK-310.9.16 -UTG_DXC
  ```
- One command (join the lines). Then build, install, ctest and `tools\Write-NativeManifest.ps1` as in AGENTS.md;
  `Build-Native.ps1` does it all (`-Generator "NMake Makefiles"` reuses `build/native`); runtimes: `Stage-Runtime.ps1`.
- Ninja (~15 s on 32 cores; NMake is serial): `pip install --target <scratch>/pytools ninja` on PATH (ShaderMake's
  ExternalProject ignores CMAKE_MAKE_PROGRAM), a build folder `%TEMP%\tgb\<name>` (long paths break MSVC PDBs, C1041),
  `-G Ninja -DMINHOOK_ROOT=... -DFETCHCONTENT_SOURCE_DIR_{SHADERMAKE,MATHLIB,DXC}=<repo>/build/native/_deps/*-src`.
- A release of some mods only (1.2.1, 2026-10-06: TGFG and TGBU, TGPT left alone): bump the version everywhere (the
  deploy's bridge check passes for another version, and the newest bridge starts), build only those native targets
  (`cmake --build build\native --target TGGraphicsBridge.Native TGFG.Native TGBetterUpscaling.Native`), install,
  `ctest -E tgpt_gpu_test`, `Write-NativeManifest.ps1`, `Build.ps1 -Runtime Both`, then `Package.ps1 -Mod <one>` per mod
  and runtime (`-Deploy` for the installed one). IL2CPP on a Mono game folder: `$env:Il2CppCoreDir` = be.755's core.
- Offline smoke with all mods: `ctest -R tgup_gpu_smoke` (`_with_pt`, `_fg_installed`, `_all_mods`); copy the game's
  `UnityPlayer.dll` beside `build/native/tgup_gpu_smoke.exe`, or Streamline refuses ("Cannot read UnityPlayer.dll").

## 13. Parallel agents (only when the owner asks)

- The whole routine is `docs/AGENT-WORKERS.md`, its tools `tools/agents/`: `New-TreeCopies.ps1` (a copy of the tree a
  worker and a pristine base; SDK folders as junctions, so a copy is removed with `-Remove` alone),
  `Start-CodexWorker.ps1`, `merge_copy.py` (three-way; `--dry` lists what a worker left), `repair_copies.py` (after
  Windows swept the temp folder). Build folders in `%TEMP%\tgb\<name>`.
- Assign option, material, instance and view bits up front (9.12); ABI, FrameConstants and test lists are merged by
  hand; read each diff (the materials merge made metals black at path vertices). Never resume a worker with a long
  history: a fresh one finishes from its report. Main has no snapshot but the newest base: a trial DLL is run in
  the game BEFORE its merge (`Queue-PtRun.ps1 -TrialDll`), and `scenarios.json` is kept aside (a conflict writes
  markers into it).
- A rebuild can relink the DLL with no source change (a new hash): install the very DLL that was tested
  (`Deploy-PtBuild.ps1 -Native -NativeFrom <dll>`). One build of `build\native` at a time (LNK1104 beside a running
  tgpt_gpu_test.exe of it).

## 14. Writing for players

- NR R17/NR-34: README and `docs/nexus-neural-rendering.md` now describe automatic official/community DLL
  selection, no per-file allowlist, adapter included, DLL supplied by player (create Native/DLSS if absent).
  Keep official in-game SDR/ReShade-RenoDX HDR evidence distinct from community standalone5080 checks;
  community in-game and RTX20/30/40 hardware checks remain pending. Do not restore signed-only/SDR-only claims.

- Nexus pages: BBCode (`[size=5][b]Title[/b][/size]`, `[size=4][b]Section[/b][/size]`, `[list][*]...[/*][/list]`,
  `[quote]`, `[code]`), plain first person, then Installation (Requirements, a directory tree indented with alternating
  non-breaking and plain spaces), Configuration, "Recommended mods". Drafts: `docs/nexus.md` ("Frame Generation"),
  `docs/nexus-upscaling.md` ("FSR and XeSS Upscaling"); no Graphics Bridge page. Tooltips: one short line.
- The owner's other FoA mods on Nexus: Global Illumination 134, All Lights Cast Shadows 133, Contact Shadows 135, Film
  Grain 136, Chromatic Aberration 137, Animation Framerate Unlock 138, Conversation Lighting Overhaul 139, Volumetric
  Fix (ID unknown). BepInEx 5.4 for Mono is Nexus mod 50.
