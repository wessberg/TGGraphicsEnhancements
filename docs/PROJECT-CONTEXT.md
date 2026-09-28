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
- Other mods loaded (LogOutput.log, 2026-09-26): TG Volumetric Fix, Better Volumetric Fog Addon, TG Conversation
  Lighting Overhaul (CLO), Torchlight Rekindled, No Player Light, Tainted Texture Budget, TG Film Grain 0.6, AvalonAtlas,
  FoA Mod Manager, Avalon Mod Manager. TGGlobalIllumination and TGContactShadows folders hold leftover configs only.
  Since ~2026-09-27 no ReShade/RenoDX (a leftover `ReShade.ini`; no `dxgi.dll`, no `.addon64`).
- Tools: Python at `C:\Python314` (capstone, pefile); `ilspycmd` (global dotnet tool) decompiles game DLLs; `dotnet
  run file.cs` with System.Reflection.Metadata reads metadata (PowerShell reflection-only loading of TG.Main fails);
  Mono.Cecil in `BepInEx/core`. No cdb/WinDbg (a PowerShell minidump parser worked; crash dumps in
  `%LOCALAPPDATA%\Temp\Questline\Fall of Avalon\Crashes`). Codex CLI for reviews. No Ninja, no Windows Graphics Tools.

## 2. The game and its engine

Build and HDRP's frame
- Unity 6000.0.64f1, HDRP 17 (Graphics repo branch 6000.0/staging). Renders D3D11; ships `D3D12/D3D12Core.dll`
  1.611.2 while the exe asks for SDK 610 (section 4). `shader-stripping.json` in the game root: every DXR variant was
  stripped (HDRP/Lit IndirectDXR, GBufferDXR, PathTracingDXR, all to 0). Game code:
  `Fall of Avalon_Data/Managed/TG.Main.dll`. Preferences: `HKCU\Software\Questline\Fall of Avalon`
  (`Screenmanager Fullscreen mode` 0 exclusive, 1 borderless). No Unity Player.log is written, and BepInEx's
  WriteUnityLog is off. HDRP SSGI probably shipped (named in globalgamemanagers.assets, unverified); resources.assets
  is full of ProbeVolume; `-force-d3d12` is untested.
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
  FoA; film grain is in the final pass after any upscaler.
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

What draws what (for the path tracer's scene capture)
- Static world: Awaken's Drake ECS renderer (entities with `DrakeMeshMaterialComponent` + `LocalToWorld`; mesh and
  material from `DrakeRendererManager.Instance.LoadingManager.MeshLoadingData[i].loadingHandle`; LOD by LODRange x
  2 tan(fov/2) / lodBias; visible ranges are SQUARED distances; per-chunk `RenderFilterSettings`).
- Castle sets: Awaken's Medusa BRG renderer (`MedusaRendererManager` per scene; EditorAccess structs are public:
  Renderers, TransformsCount, BrgRenderer.Xs/Ys/Zs/Radii/LodDistanceSq/TransformIndices/BasePath). Matrices in
  `matrices.medusa` (PackedMatrix, 48 B) inside `medusa.arch`, read with AsyncReadManager, not System.IO.
- Castle walls, arches, battlements: Entities Graphics entities WITHOUT Drake, meshes and materials registered at
  runtime (`World.GetExistingSystemManaged<EntitiesGraphicsSystem>().GetMesh/GetMaterial`; `IsRuntimeMaterial` is
  internal, use `Material >= 0`). They use MaterialMeshInfo RANGES into the game's own shared component
  `Unity.Entities.Graphics.RendererRange { MaterialMeshInfo* }`; no RenderMeshArray.
- Shadow-only cluster meshes `CM_*_Cluster_*` (GameObjects): coarser copies of the castle the game casts shadows with.
- Vegetation: Leshy BRG renderer in TG.Main (`LeshyManager`, a cells catalog, `Matrices.bin`, PrefabRuntime) for grass,
  plants, bushes, ivy and trees; Vegetation Studio Pro trees and large objects (`LoadedVegetationCellList`). Grass has
  no colliders (a raycast probe can't hit it).
- Characters: TG's Kandra GPU skinning. All registered renderers skin at `beginContextRendering` into one
  `OutputVerticesBuffer` (structured, 20 B CompressedVertex, 2.75 M capacity, never recreated, WORLD space). UVs sit in
  `MeshManager._additionalVerticesDataBuffer` (half2 at sharedStartVertex). `KandraTrisCuller` culls body triangles under
  clothes (`rendererData.RenderingMesh`). Kandra draws min(materials, submeshes) submeshes.
- FoA keeps `QualitySettings.lodBias` equal to the camera's HDRP LOD bias (LodBiasWatcher).

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

- Inputs: the DLSS SR NGX evaluation when the game's DLSS runs (the game ships only NVUnityPlugin; feature flags 43 =
  HDR|MVLowRes|DepthInverted|Sharpening), else HDRP's own: HdrpInputs.cs, a global CustomPassVolume at
  BeforePostProcess, `TGFG_SetHdrpInputs` (depth and motion RT pointers cached per RT, refreshed on resize), render
  event 3. Constants as HDRP's DLSSPass: jitter = -taaJitter, mvScale = -(render size), MVJittered 0. DLSS inputs win
  while DLSS evaluated the previous frame. Motion is copied as R16G16_FLOAT (lossless: HDRP's own format).
- Orientation: the flip is unconditional for DLSS-G, FSR and XeSS. `native/include/camera_orientation.hpp` turns the
  four clip-space matrices by F = diag(1,-1,1,1) at `TGFG_SetCamera` (Streamline and XeSS read them; FSR FI 3.1.6
  ignores camera vectors and matrices). On the packet's transposed storage (F*M)^T negates column 1 and (M*F)^T row 1.
  The bug hid in DLSS-G because its optical flow blends with the game's vectors; the owner found it when FSR looked
  better than both others. Owner, 2026-09-20: DLSS, FSR and XeSS equally good after the fix.
- HUD-less frame: `HudlessUi.BeginGameplayCamera()` sets `rendersUIOverlay` false each frame while FG runs and HDR
  output is off, so event 2 copies a back buffer without the HUD, for DLSS-G, FSR and XeSS. `TGFG_HUDCHECK` compares
  every 4th pixel at Present (3-deep staging ring): <= 50 % differing for 30 checks = verified; still frames are
  skipped, since menus reset the streak.
- Backends. Streamline 2.14.1 (the newest release, 2026-09): eDynamic ignores `numFramesToGenerate`;
  `dynamicTargetFrameRate` 0 = refresh, ignored with VSync. FSR SDK 2.3.0: the loader finds effect DLLs by bare name, so
  preload `amd_fidelityfx_framegeneration_dx12.dll` by full path; 1 generated frame, no dynamic mode; FSR 4 ML FG on RX
  9000 only; maxRenderSize is fixed at create (the context is rebuilt per render size); FSR 3.1.6 wants >= 60 fps input.
  XeSS SDK 3.0.2: `libxess_fg.dll` loads `libxell.dll` by name (preload it); 3x/4x on Arc only; needs an XeLL context and
  no other latency tech; XeLL alone only on Intel; `xellSetSleepMode` needs an idle GPU; Win10 19043+ and msvcp140 >=
  14.40.33810. Anti-Lag 2 is header-only (driver amdxc64.dll), initialized on the bridge's D3D12 device while the game
  renders D3D11 (unvalidated on AMD).
- Switching technology recreates only the private presentation swapchain, inside Present (a bridge-owned waitable).
- Read the `Pipeline 5s:` line in `TGFG.Native.log` first when FG doesn't engage (frames, camera packets, events,
  NGX evals, captures, ready/fgOn, DLSS-G status, gpuWait, sdkPresent, handoff). `d3d11Waits inputs=` shows D3D12 lag.
- Exclusive Fullscreen: a cold start into it works (DXGI refuses the game's SetFullscreenState and the game carries
  on); switching into it mid-game leaves Unity's window deactivated (TimePaused, 60 fps). Shipped: DisplayMode.cs moves
  the game's own setting to borderless (`FullscreenOption` + SetForbiddenOptions). Owner: "Then the settings won't lie."
- RenoDX's DLSS 5 add-on (`renodx-dlss.addon64`, closed source) finds a game's Streamline only by
  `GetModuleHandleW(L"sl.interposer.dll")` and returns E_UNEXPECTED for swapchains ReShade didn't see, so TGFG ships
  NVIDIA's interposer byte-identical as `Native/Streamline/TGFG.Streamline.dll`. The add-on runs DLSS-NR through NGX on
  its own D3D12 proxy; ReShade.ini [RENODX-DLSS] `DirectNeuralRenderingHookStage` 0 Render / 1 Upscaled / 2 Present
  (the owner used 2), `DirectNeuralRenderingRequireDlss=1`. Its NGX EvaluateFeature hook can skip an evaluation
  (0xBAD00000). With ReShade + RenoDX HDR, FG "works perfectly" (2026-09-19; the game then calls ResizeBuffers every
  10-30 s).
- Hybrid laptops (HybridVsync.cs, DLSS-G guide 22.6): DLSS FG + `display.crossAdapter=1` -> the game's VSync off and
  greyed, Target FPS once to "RefreshRate_Unlimited", toast, opt-out; `TGFG_SetNoTearing` while it holds.
- FrameRateCheck.cs: < 15 % gain after 3 s settle + 5 s measure -> toast and a "No FPS gain" row. VRAM full and RAM full
  rows while FG generates. FrameStart.cs: a PlayerLoop entry after `WaitForLastPresentationAndUpdateTime`; frameId is a
  rising counter, not Time.frameCount. (Open items, such as the hitches on fast pans: BACKLOG.)

## 6. TG Better Upscaling

- FSR and XeSS take STP's place: appended as entries 3/4 of the game's Upscaling row (`EnumArrowsOption._options`;
  PrefMemory key "UpScalingType" restores them); a Harmony prefix on `UpScalingController.OnSettingChanged` sets the
  camera up as for STP; a prefix on `HDRenderPipeline.DoStpPasses` records one CustomPass via
  `CustomPass.ExecuteInternal` (colorBufferRG = output, nonMSAAColorBufferRG = source) and returns the output handle.
  Pass HDRP's `m_CustomPassColorBuffer` / `DepthBuffer` Lazies, never null (the render func reads IsValueCreated).
  A refused frame is enlarged; five in a row hand back to STP for the session.
- Jitter is HDRP's own: STP.Jit16 is Halton(2,3) over HDRP's 1024-frame `taaFrameIndex`; FSR and XeSS get it negated
  (tgup_jitter_probe confirmed -x,-y against the real runtimes).
- Quality modes from the runtimes (DLSS `GraphicsDevice.GetOptimalSettings`, (DLSSQuality)5 = DLAA; FSR's
  GETUPSCALERATIOFROMQUALITYMODE query; XeSS `xessGetOptimalInputResolution`, 7 presets). DLSS gets the mode as its NGX
  level, so the model follows (K DLAA/Quality/Balanced, M Performance, L Ultra Performance). The game's slider is
  100/(2.99 - 1.99 s) % (33.4-100) and never gave DLAA. Migration rounds DOWN (owner, 2026-09-23). STP keeps the slider.
- FSR mip bias: HDRP applies log2(render/output) wherever STP runs (Intel's rule); AMD wants 1 more, so
  `HDAdditionalCameraData.materialMipBias -= 1` while FSR is selected.
- FSR pre-exposure: StpHost requests HDRP's exposure readback and sends Clamp(value, 0.2, 2) as HDRP's DLSS does; native
  FSR uses it as preExposure and as a 1x1 R32F exposure texture (8-slot upload ring). XeSS's multiplier stays 1.
- Bundled DLSS DLL: `nvngx_dlss.dll` 310.9.1 (github.com/NVIDIA/DLSS tag v310.9.1, `DLSS-SDK-310.9.1/`) in
  `Native/DLSS`. The driver's `_nvngx.dll` loads `<appdir>/nvngx_dlss.dll` with LoadLibraryExW(flags 0) during NGX Init;
  `native/upscaling/src/dlss_dll.cpp` hooks kernelbase!LoadLibraryExW and returns the bundled DLL when the requested
  file is older (newest wins). DLSS-set rule (owner's idea, confirmed 2026-09-23): the game folder's DLL is kept when
  `nvngx_dlssnr.dll` sits beside it, or dlssd/dlssg of the same major.minor (with our 310.9.1 in place of DLSS 5's
  310.8, the add-on's fps stopped following render resolution). Unity's NVIDIA module `appDir` is NGX's log path, not a
  DLL path. The RTX SDK license 7.1(b) asks for DLSS credit.
- F7 comparisons (2026-09-23, tavern, 4K at 33.4 %; PSNR / SSIM / detail): STP 43.0/0.989/0.98, XeSS 42.8/0.985/0.84,
  DLSS 39.9/0.988/0.92, FSR 38.7/0.953/0.65; in motion DLSS best, STP worst. FSR's softness at 3x is FSR itself. The tool
  was removed for production (owner: "pretty solid"). NGX can't report the active DLSS preset: the overlay reads the
  NVIDIA App's override (section 4), else 310.9's default per mode. FSR FG output = rendered x 2, not "an estimate".

## 7. IL2CPP (the normal version)

- The same C# sources. Subclasses of game types (FrameGenerationVideoSetting : Setting, SwitchRowsOption :
  DependentOption, custom passes : CustomPass) are thin shells registered with `Il2CppTypes.Register<T>()`; overrides
  are `new` members with the base's exact name and parameter types (property getters as get_X). Logic lives in plain
  classes so lambdas aren't injected.
- `ClassInjector.DerivedConstructorPointer` does not run the IL2CPP base ctor: `Il2CppTypes.Construct` calls it through
  the interop's private `NativeMethodInfoPtr__ctor_*` field + `il2cpp_runtime_invoke`.
- Registering a Setting subclass threw "Couldn't find System.Type for Il2Cpp type: T, TG.Main": `Register<T>(genericBase:
  true)` patches that method with a HarmonyX finalizer during the call. A failed registration can't be retried.
- Il2CppInterop be.755 bugs: (1) a converted `System.Func<string>` returns the address of the MANAGED string (crash
  opening Options): tooltips use `Func<IntPtr>` + `IL2CPP.ManagedStringToIl2Cpp`; never pass managed delegates returning
  strings/objects through op_Implicit. (2) Generated wrappers pass `ref`/`in` non-blittable struct params
  (`CustomPass.RenderTargets` in ExecuteInternal) as the BOXED pointer: HDRP read an object header as targets, the pass
  never executed and the process leaked ~3 GB/s of commit (233 GB virtual, dwm crashed, 2026-09-23). StpHost.Record calls
  `il2cpp_runtime_invoke` itself with unboxed args, plus a guard after 3 recorded-but-unexecuted passes.
- Il2CppInterop runs a game class's static ctor on the FIRST use of its wrapper type (a `World.Any<T>()` type
  argument, TryCast<T>). UpScaling's cctor translates labels before localization loads -> SEHException, the class stays
  failed for the process and the game hangs at loading. `VideoTab.GraphicSetting<T>("UpScaling")` finds it in
  `SettingsMaster._graphicSettings` by IL2CPP class name, then TryCast.
- Interop details: attributes like [DefaultExecutionOrder] aren't System.Attribute (#if); every read returns a new
  wrapper (compare Pointer, key by GetInstanceID); no LINQ on Il2CppSystem IEnumerable; `Il2CppSystem.Nullable<T>`
  params need `new Il2CppSystem.Nullable<float>()`, not null; interop exposes HDRP's `in RenderTargets` as `ref`;
  net6.0 lambdas touching nullable-annotated BCL signatures fail with CS0656 (use named methods); PlayerLoop edits
  through Il2CppReferenceArray copy semantics, keep objects alive in statics.
- Crash triage: the Windows Application log's fault offset in GameAssembly.dll, mapped with LibCpp2IL (code sits in the
  "il2cpp" PE section). Mono compile check without the Mono branch: Cpp2IL signature DLLs + BepInEx unity-libs + BepInEx
  5.4.23.5 core, `dotnet build -f net472 -p:GameDirMono=<dir>`; IL2CPP: `-p:Il2CppCoreDir=<be.755>/BepInEx/core`.

## 8. Troubleshooting report and player reports (1.2.0)

- Built 2026-09-24 after Nexus reports (Win10 + RX 9060 XT no FG; 4060 laptop bad DLSS-G; 9060 XT FG halves fps).
  Details: `CHANGELOG.md` "Troubleshooting (1.2.0)", `docs/TESTING.md`. One file, `BepInEx/TG Troubleshooting Report
  <date>.txt` (~25 KB), for pasting on Nexus; the button sits under Performance Overlay. Test steps never write the
  player's config; a pause restores the player's settings until play resumes.
- Notices have Label (<= 16 chars: "Wrong folder", "Conflict", "No FPS gain", "Extra copy"), Summary (tooltip, <= 240)
  and Detail (report only). Install check: "correct" only when BepInEx loaded the plugin from the right path;
  `InstallCheck.Tests` has a read-only `check <exe> <loaded>` mode for a real game folder.
- First player report (2026-09-26, RTX 4060 laptop, 1080p144, Mono): 1.2.0 works, all three FG techs 2.0x, DLSS-G the
  cheapest. Fixed 2026-09-27: per-step Pipeline lines in the report, the autosave camera blip, overbroad driver VSync /
  Max Frame Rate alerts (only 1/2, 1/3, 1/4-refresh VSync alerts now), a 45 fps Framerate Limit entry.

## 9. TG Path Tracing

### 9.1 Shape of it
- The third bridge extension: `native/pathtracing` (TGPathTracing.Native.dll), `src/TGPathTracing` (net472 only),
  `src/TGPathTracing.Autoload`. GUID `com.wessberg.tgpathtracing`. Mono only (`Runtimes=@('Mono')` in
  `tools/Package.Common.ps1`). ABI 0x00010F00: TGPT_Frame 568 B, TGPT_Status 208 B, TGPT_Light 80 B, TGPT_Material
  80 B, TGPT_Guides 64 B; FrameConstants 1632 B (`tracer.cpp` static_assert).
- HDRP hook: a Harmony prefix on the opaque `HDRenderPipeline.RenderSSR` (right after the GBuffer; the transparent call
  returns first) records a CustomPass through `ExecuteInternal` with GBuffer0-3, depth + stencil, motion vectors and
  exposure. The composite is a global CustomPassVolume at AfterOpaqueAndSky, before fog.
- Native: Shader Model 6.6 inline ray tracing (DXR 1.1), bindless `ResourceDescriptorHeap`, root signature in
  `pt_root.hlsli`, DXC at build time (`-WX`). With DXR 1.2 (raytracing tier 1.2 + SM 6.9 via the shipped Agility
  1.619): SM 6.9 builds of every pass, opacity micromaps (`pt_omm_bake.hlsl`), Shader Execution Reordering raygen state
  objects when D3D12_OPTIONS22 says it actually reorders (feature 65, not in the 26100 SDK header). No d3d12.lib: only
  the bridge's device.
- One frame late on purpose: a D3D11 wait on D3D12 work of the same frame cost +7.6 ms (direct queue) / +5.7 ms
  (compute). Frame N composites frame N-1's traced light, stored divided by albedo, reprojected with N's motion and
  depth-checked, multiplied by N's albedo on D3D11. Inputs and outputs double-buffered by parity; async compute queue.
  About +2.3 ms total, ~1.5 ms of it fixed.
- Passes (`native/pathtracing/shaders`): pt_prepare (G-buffer, camera rays, glass), pt_di(_spatial), pt_gi(_spatial,
  _boil), pt_specular, pt_hdrp_shadow, pt_cache, pt_emissive, pt_skin, pt_sss, pt_temporal + pt_atrous (built-in
  filters), pt_nrd_prep, pt_rrg_guides, pt_compose, pt_meter, pt_probe, pt_water, pt_omm_bake; `d3d11.hlsl` (composite,
  Shadow Detail, water fill). Managed: HdrpPasses.cs (hooks, volumes, gain), SceneCapture.cs, CharacterCapture.cs,
  LeshyCapture.cs, SceneLights.cs, GlassFinish.cs, DebugCapture.cs (F8), PathTracingRow.cs, ReplacedRows.cs.
- While tracing: `IndirectLightingController.indirectDiffuseLightingMultiplier` = 0 always (GBuffer3 carries baked
  indirect as "emission"); HDRP's AO, contact shadows and SSR off; HDRP distortion off (its resampling of undenoised
  light made candle air boil); the game's Shadows, Contact Shadows, Ambient Occlusion, Reflections and Subsurface
  Scattering rows greyed with the reason (ReplacedRows.cs wraps `_isInteractable` / `_tooltipConstructor`). Every
  Volume override uses `Add<T>()` and sets only the named parameters. To scale a game value: a second global volume
  with value 0 and weight 1 - factor (HDRP lerps: game x factor).

### 9.2 Scene capture
- Gathered every 2 s, and 0.5 s after the camera moves 4 m. Each mesh is read back once, compacted on a worker thread,
  one BLAS each. Past 2.5 GB of traced geometry the scene starts over near the camera.
- Sources: GameObject mesh renderers; Drake; Medusa (once per scene, at its LOD); Entities Graphics without Drake (the
  cluster stand-ins stay while more than 10 % is unresolved); Leshy (grass and plants within 20 m, at most 60,000
  instances, nearest cells first, no billboards); VSP trees; terrains as heightmap grids; characters (below).
- LOD: within 30 m the finest LOD whatever the game draws; registered from 40 m; the old LOD stays until the new one
  is ready ("darkens as I walk up" was a LOD switch).
- Left out: statically batched renderers, particles, water surfaces (GPU-displaced; rays start from HDRP's water depth
  and normals), water materials on mesh renderers, hidden layers (RainSeaMesh_Ocean), the cluster stand-ins while
  Entities Graphics resolves, scalp and beard shells, lashes, eyebrows, transparent character parts, TG/NoRender.
  Shadow-only and other-layer Drake entities ARE traced ("Traced though the camera does not draw it").
- Characters (CharacterCapture.cs): copies of Kandra's output buffer each frame for renderers within 100 m (nearest
  first, at most 64); BLAS refit every frame and rebuilt every 4th, staggered (64 x 8192 triangles: 0.8 ms rebuilt
  every frame, ~0.3 ms staggered). Character instances were FORCE_OPAQUE, so hair and alpha parts must be dropped at
  registration or per-material tested.
- Rays start where HDRP drew the surface, which can sit centimetres inside the traced mesh. Pass-through: a ray whose
  first hit is the inside of a mesh within 3 x near_back (~0.45 m + 3 % of distance) passes out. Stand-ins
  (`TGPT_INSTANCE_STAND_IN`) are skipped within that reach whatever their facing. `occluded_from_surface`: closest
  hit within 3 x nearBack, any-hit beyond.
- Wind: `TGPT_MATERIAL_SWAYS` gives an instance mask 0x02 (still ones 0xFD). Rays from the G-buffer query all masks
  first, then still-only below 20 cm, then all only if the hit is `TGPT_INSTANCE_SWAYS` within reach.
- Alpha: 256x256 alpha tiles in their own atlas, coverage-preserving (each texel holds how much passes the cutoff over
  16 samples of the finer mip), filtered bilinearly. Every alpha tile stops non-camera rays by chance, as much as it
  covers (strands, straw). Opacity micromaps settle only coverage 0 and 1. Hair (`TGPT_MATERIAL_STRANDS`) gets none.
  Colour tiles are 64x64 in a shared atlas; mask maps share it (up to 3/4).

### 9.3 Lights
- Traced by attachment (SceneLights.IsPlacedLight): a light inside a source prop (Fire, Candle, Candela, Brazier,
  Lamp, Lantern, Torch, VFX, Magic, Portal... in it or 5-6 parents) is traced whatever its name; HeroLight, Volumetric
  and Fake always stay out; fills outside source props stay out. CLO's light is kept (and holds its slot while fading
  in). Skipped: affectDiffuse off, lights off the default light layer. Inventories are logged once per session.
- Reach is physical: as far as the light still gives 50 lux (sqrt(I/50), at most 40 m), not HDRP's range; no HDRP
  distance fade. Effect-only lights (VFX/Magic/Portal with no flame or lamp word) keep HDRP's range.
- Size: HDRP's shape radius (>= 2 cm) is the disc; flames capped at 0.3 m (fires 1 m); a new disc point every frame.
  Cookies are traced (atlas 1024^2, 64 px octahedral tiles). `TGPT_LIGHT_HDRP_SHADOWS`: HDRP renders a shadow map for
  it. `TGPT_LIGHT_NO_SPECULAR`: HDRP's affectSpecular is off, so no traced highlight either.
- Left-out lights while tracing: HDRP `lightDimmer` 0; fog via `affectsVolumetric = original && Fill Light Fog`. Kept
  lights: HDRP lightDimmer x traced gain (so what HDRP still shades matches). One `originals` record per light,
  restored together when tracing stops.
- CLO's light is scaled by `HdrpPasses.AutomaticGainInverse` (at gain x3.1 its spot was 3x the game's).
- Light grid: CPU-built, 8 m cells, 48x20x48 around the camera snapped to 32 m, uncapped CSR lists, rebuilt on a hash
  change. Entries hold (index, cumulative weight), weight = luminance / max(squared distance to the cell box, 1 m^2);
  systematic CDF candidates in pt_di and direct_diffuse, invPdf = 1/p_i. Tavern: 11 lights a cell on average, 43 max.
- Path vertices take a light's light as if at least 25 cm away; GI vertices at least 25 cm from lights.
- Emissive surfaces are lights too (Emissive Lights row): clusters of 32 triangles, a 16,384-triangle budget
  (brightest and largest kept). A glowing surface within a kept light's radius + 25 cm is that light's own surface and
  lights nothing. Moving instances (`TGPT_INSTANCE_MOVING`), characters and stand-ins aren't sampled. Traced emission
  applies HDRP's exposure weight (at 0 the light is colour / exposure, ~10,000x at EV 13) and Albedo Affects Emissive;
  single-sided surfaces glow from the front only.

### 9.4 Exposure and the traced gain
- HDRP keeps the game's exposure (no Exposure override). Only the traced light is brightened: `pt_meter.hlsl`
  (histogram of every other traced pixel, centre-weighted, log2 -16..+8, 64 bins, before gain) -> p50/p90/p98 ->
  `HdrpPasses.UpdateTracedGain` -> `frame.tracedGain` -> compose multiplies.
- Rules now (`HdrpPasses.AutomaticGain`): brighten = max(0, min((mid tone -3.06 - p50) x Dark Adaptation, 0.5 - p98)),
  so the automatic gain never darkens and the brightest fiftieth stays under ~1.4x white; at most (game EV - darkest
  scene EV 7) stops, and 12; 1.5 EV/s up, 3 EV/s down; frozen in debug views and during F8; the Exposure row adds +-3.
- The "seen" meter: a 128x72 copy of HDRP's image before post, read back every 4th frame; Dark Adaptation's mid =
  max(traced p50, seen p50 - gain), so fog counts as bright.
- Fog: `traced_gain_at` fades the AUTOMATIC gain by HDRP height-fog transmittance (1/meanFreePath, baseHeight,
  H = layer x 0.144765); the player's Exposure stays.
- Radiance reference: signals are stored relative to max(sun lux, brightest light at 1 m)/pi, eased (FP16 then holds
  HDRP's physical units); the indoor clamp uses the brightest light at 25 cm. Clamps vs the reference
  (`pt_restir.hlsli`): LIMIT_INDIRECT 4, LIMIT_REFLECTION 8, LIMIT_DIRECT 48.
- Camera light (`pt_probe.hlsl`): 256 sky-only rays from the camera against the open sky -> `cameraLight`, eased
  0.5 s; it scales HDRP's forward ambient, `reflectionLightingMultiplier` and `Fog.globalLightProbeDimmer`. Interiors
  (no sun, or an Interior* scene): sky 1e-6 and cameraLight 0.01, so no sky leaks through wall seams.
- Shadow Detail row: a D3D11 curve on HDRP's image after the denoiser and upscaler, before post (postfix on
  `BeforeCustomPostProcessPass`, event 3). Below log2 0.12 it lifts s x min(3, 0.4 h(d)) EV; s = row x activation;
  activation = smoothstep(0, 2, auto gain EV) x smoothstep(4.5, 6.5, p98 - p50), eased 1 s, 0 after cuts.
- RR's pre-exposure is HDRP's value clamped 0.2-2, as HDRP's DLSS pass does (the real ~1e-4 would overflow RR's FP16
  history). RR guide 3.7: exposure inputs are unsupported.

### 9.5 ReSTIR on RTXDI 3.1
- DI: the tracer's light-grid candidates go into RTXDI reservoirs; temporal reuse (history 4 since 2026-09-28, RTX
  Remix's: RTXDI's 20 boiled on a cage's thin bars, F8 change 0.40 at 20, 0.14 at 4, 0.11 without reuse; hidden "Direct
  light history"), Basic spatial reuse, permutation sampling with a new random number every frame. The final
  shadow ray is always traced; spatial results never feed the next frame. Own boiling filter (`pt_boil.hlsli`,
  weighs W x targetPdf, excludes the sample itself once >= 4 others are lit). discountNaiveSamples 0. Previous-frame
  lights + light instance IDs for `RAB_TranslateLightIndex`.
- GI: history 8 + maxReservoirAge 30 (RTXDI's defaults; hidden "Bounced light history"); temporal reuse with
  permutation, a search around the reprojected pixel and the reconnection Jacobian (rejected beyond [1/10, 10],
  clamped to [1/3, 3]); NaN check on unpack; RTXDI's fallback sampling off. With N rays a pixel its own mean is
  blended (N-1)/(N-1+16) (30 % at 8, 48 % at 16).
- Decorrelation for the denoisers (RR guide 3.5, AMD denoising.md 5.1.2): permutation sampling; none on complex
  surfaces (a neighbour's normal more than 35 degrees off: strands, slats).
- Published practice (2026-09-26 research): RTXDI DI 20 (permutation, boiling 0.2), GI 8 + age 30; RTX Remix DI 4, GI
  30 frames in its RR preset; course notes 5-30; Solari 8. Randomized GI temporal lookup (Remix up to 80 px), relative
  boiling filter, trace final DI visibility, don't demodulate for RR, albedo < 0.5/255 -> 0, sky albedo 0.5, >= 32
  jitter phases (Remix 64), unjittered motion. Sources: `docs/SOURCES.md`.
- Honest energy reference (`TGPT_ENERGY=1`): plain path tracing with `NO_BOILING | NO_CLAMPS` (+ EXACT_JACOBIAN,
  OFFSET_RECEIVER for attribution), 8 rays x 256 frames, SE over 8 batches. 24 torches: ~0.99. One light 10 cm from a
  wall: 0.794 as shipped (21 % dark), 1.013 without the GI boiling filter (strength 0.2 drops samples > 41x the
  neighbours' average). Hence the Boiling Filter row (default On; the owner can't tell On from Off).
- Crevices (canvas 40 cm over sand, a 12 cm slit; `TGPT_CREVICES=1`): ReSTIR is 14 % short at the mouth (RTXDI's
  normalization) and 9 % over deeper in (30-frame age restarts). Crevice Light (GI ray-traced bias correction, no age
  limit, clamp instead of drop) fixes it for +15-30 % denoised error: default Off. Deep sand gets ~1/150 of sunlit
  sand; 2 bounces hold 53-72 % of it; with the radiance cache 0.97-0.98 of an honest 4-bounce.
- Boiling measures: 8x8-block error lag-1 correlation (0.34 / 0.17 / 0.08 at 1 / 8 / 16 rays). The F8 capture's
  "Boiling" line: 8x8-block frame-to-frame change as traced, GI from 16 own rays only (`TGPT_OPTION_GI_OWN_RAYS`), DI
  without reuse (`TGPT_OPTION_DI_NO_REUSE`), neither, GI history 4, and "as traced again, settled": compare with the
  settled one (the first pair follows the report and read 0.48 against a settled 0.17 once). [crosshair] is the spot.

### 9.6 Denoisers
- Choices {None, NRD, Ray Regeneration, Ray Reconstruction}; best RR > RRG > NRD. NRD's option is set for every
  choice but None, so NRD serves while RR waits for DLSS upscaling or RRG failed. The tracer's own SVGF-style filters
  still filter direct light and reflections under NRD, and take over if NRD fails. A refused RR frame gives the
  tracer's denoiser back for 0.5 s with fresh history.
- Ray Reconstruction runs in TGBU on the D3D12 device (D3D11 NGX DLSSD crashed in `nvngx_dlssd.dll`), preset F
  ("Default model RR2", SDK 310.9.1). Guides written by the composite: specular hit distance (R16_FLOAT, read as
  `ReadAs::Depth`: a Color view has no single-channel formats and refused every frame), colour before transparency,
  diffuse albedo leaning to the light's colour on emissive and sky (as RTXPT), albedo floor 0.05, input clamp 256x
  white, glass (no diffuse albedo, specular albedo covering transmission, roughness 0.03, hit distance behind). Extras
  NGX refuses are dropped, never the frame. TGPT warns on screen when RR is the setting and hasn't run for 5 s of
  traced, guided frames. TGBU's RR watch starts at -1000 (int.MinValue overflowed and RR never counted as running).
- NRD 4.17.3, RELAX, on the bounced light only (full RELAX blurred sun shadows, floor error 0.145 vs 0.049, and its
  specular flickered on bump maps). A static library with DXIL embedded, its own root signature and a small heap on the
  tracer's list; 2D motion (mvScale.z 0); its own radiance units (restart when the reference moves more than 4x). DLL
  1.25 -> 3.95 MB. CMake refuses releases other than 4.17.
- AMD FSR Ray Regeneration (FidelityFX 2.3 signed DLLs in `Native/FidelityFX`, RX 9000 only, untested): demodulated
  radiance, sqrt albedos, world octahedral normals (AMD's doc examples of the encoding are wrong; its sample's
  `common.hlsl` is standard).
- Camera cuts reset every history: a new camera, HDRP's `resetPostProcessingHistory` rising edge, a > 60 degree turn,
  a > 20 m jump. The radiance cache survives cuts.
- DLSS 5 with tracing: with RR, upscaling is TGBU's RR on the bridge device, and the add-on's neural rendering binds
  to its own proxy ("BindDevice rejected a distinct live D3D12 device"): an add-on limitation, told to the owner. The
  public DLSS SDK 310.9.1 has no neural-rendering API, so a DLSS 5 of our own isn't possible. `ReShade.log` sits in the
  game root and stays open while the game runs: copy it to read it.

### 9.7 Surfaces
- Metals: path vertices are diffuse-only, so `SurfaceMaterial.reflectance` = base colour (a metal's specular energy
  sent on as diffuse); pixels HDRP draws forward use the diffuse albedo. Mask maps per shader, remapped as HDRP does.
- GGX: `ggx_d = a2 / max(PI d^2, 1e-30)` (a +1e-7 capped peaks below roughness ~0.12). HDRP Unlit: f0 0, roughness 1.
  Schlick f90 = saturate(50 f0). A reflection drawn into the surface's own triangle is mirrored back out.
- Reflections row: Off / Glossy Surfaces (rougher than 0.6 uses the cache's light x split-sum albedo) / All Surfaces.
- Translucent (`TGPT_MATERIAL_TRANSLUCENT`: grass, leaves, HDRP Lit `_MaterialID` 5, HDRP transmission features 2/3):
  light from behind at pt_common's TRANSMISSION share; the shadow ray from the light's side only for these. Thin
  cloth (`TGPT_MATERIAL_THIN_CLOTH`, CLOTH_TRANSMISSION 0.3): the owner wants realistic see-through, not glow.
- Skin: a profile is solid when HDRP transmits it as a thick object OR its name says skin. `pt_sss.hlsl`: HDRP's
  Burley after compose, 24 samples on a tangent-plane disc projected to the screen (a screen disc under-weighted
  slanted surfaces), centre weight = CDF(half a pixel). Thin skin (ears, fingers): HDRP's transmittance of the GBuffer
  thickness, 15 degree wrap, the ray must leave within 3 cm. Terminator: Chiang softening; opaque shadow rays start
  outside. Skin is exempt from Reflection Boost (the neck "rash": glossy skin, roughness 0.18, mirrored the chin).
- Glass: `GlassFinish.cs` names (Bottle, Glass, Flask, Vial, Potion, Phial, Decanter, Carafe, Beaker, Jar; a NotGlass
  list) -> `TGPT_MATERIAL_GLASS`. Rays pass by chance, sqrt(luminance) per crossing; final shadow rays carry
  per-channel tint. Glossy texels (HDRP roughness < 0.5, < 15 m) are see-through: refract at IOR 1.5, find the far
  side within 1 m, shade behind with 1 + Bounces vertices. Its reflection ray meets lamp spheres and the sun's disc.
  The blackcurrant bottle is simply very dark glass.
- Water (`pt_water.hlsl`): GGX rays from HDRP's water depth and normals, 12-frame accumulation + 5x5 edge-stopped
  filter. The D3D11 fill replaces HDRP's SSR entirely (traced x 1/multiplier, weight 1), right after the transparent
  `RenderSSR` (postfix, event 4, `TGPT_SetWater`). The water's own shading is still HDRP's.
- Forward pixels: pt_prepare traces a camera ray (mat alpha = forward flag; the composite matches forward pixels to
  flagged texels). `TGPT_MATERIAL_CAMERA_ONLY` (eyes, opaque HDRP Unlit decals < 60 m) is seen by camera rays only.
- Hair is the tracer's since 2026-09-28: RealHair's albedo = _DiffuseMap x _DiffuseColor x _DiffuseStrength (read
  again every frame), camera rays stop at MATERIAL_STRANDS whatever the alpha, lit through from behind (mat bit 4,
  CLOTH_TRANSMISSION), no Reflection Boost. Not yet: _RootColor/_EndColor along _RootMap, highlights A/B, a strand BSDF.
  The game colors hair per NPC: CharacterHairColor.ApplyTo -> HairController.SetHairColor on the Kandra renderer's
  instanced materials (KandraRenderer.UseInstancedMaterials; UseOriginalMaterials returns the shared default, grey
  _DiffuseColor 0.698); BodyFeatures.RefreshDistanceBand makes hair transparent below band 2, opaque beyond. Far NPCs'
  hair traces grey while HDRP draws it colored: the F8 section "Characters along the crosshair" lists each renderer's
  materials to find out which one HDRP draws. Eyes are still HDRP's (`TGPT_MATERIAL_HDRP_SHADED`, `pt_hdrp_shadow`'s
  share R = (K + D' + A) / (K + D + A), Hair Shadows row, plus albedo x cache light x (1 - cameraLight)); their shader's
  textures are logged ("colors, values and textures") to trace them next. Hair's transparent layer is hidden while
  tracing (SetShaderPassEnabled). HDRP transparent glass (the alchemist's jars) is invisible to rays: planned
  `TGPT_MATERIAL_SEE_THROUGH` 2048 + instance mask 0x04 + a glass-only camera ray in pt_prepare + its passes hidden.

### 9.8 Radiance cache
- Own SHaRC-like design, no SDK code: a world hash grid of 2^20 slots (buckets of 8; keys, sums, light = 36 MB);
  cells 0.1 m within 5 m of the camera, doubling with each doubling of distance; a side per dominant normal axis; it
  stores cosine-mean incoming indirect light (E/pi). A path's last vertex adds throughput x albedo x cache light
  (jittered lookup). Fed by 1/4 of the 8x8 tiles each frame (whole tiles), the pixels' first rays and an extension ray
  at the last vertex; 512 samples a cell a frame, per-channel cap 2000; EMA >= 1/16; ages out after 240 frames.
  Cleared on first use, re-enable and scene clears, never on `TGPT_FRAME_RESET`. Furnace: 0.994 with 1 bounce.

### 9.9 Rows and defaults (`src/TGPathTracing/Settings.cs`)
Path Tracing Off/On (On), then nested: Denoising (best), Exposure 0 (-3..3), Dark Adaptation 50 %, Shadow Detail
50 %, Rays Per Pixel 1 (1-16), Bounces 1 (1-4), Radiance Cache On, Indirect Color 3 (0-4), Reflections All Surfaces,
Water Reflections On, Hair Shadows On, Fill Light Fog On, Spatial Samples 4 (0-16), Boiling Filter On, Emissive Lights
On, Crevice Light Off, Ray Distance 500 m, Scene Radius 250 m, Sky Intensity 1, Reflection Boost 0.1, Debug View None.
Hidden: Denoising chosen, Direct/Bounced light history (0 = 4/8), Direct light visibility reuse (off), Spatial
reuse feeds the next frame (off), Auto exposure darkest scene EV 7, Trace toggles, keys. Saved values stay when a
default changes; migrations read `OrphanedEntries` before `Bind`.

### 9.10 F8 capture, debug views, logs
- F8 (DebugCapture.cs, hold still ~22 s; F8 also triggers TGFG's emergency off and AvalonAtlas) writes
  `BepInEx/TGPathTracing/Captures/<time>/`: report.txt (exposure, EV,
  gain and what it metered, every light within 40 m with slot, colour, dimmers, keep/leave-out reason and "no
  specular", the probe, diffusion profiles, tonemapper, GPU time by stage, the Boiling line, the pick under the
  crosshair with HDRP's and the tracer's roughness and "glass seen through", each Drake mesh's material and whether rays
  see it, "Characters along the crosshair") and JPGs (final, each debug view, sky off,
  pass-through off, the game's own with tracing off, water reflections off, a 2-frame difference, the boiling variants).
  JPGs are CPU box downscales of raw bytes (before 2026-09-27 ~10:47 they were sRGB-encoded twice: washed out).
- Debug views: None, Albedo, Normals, Coverage, Materials, Indirect, Indirect raw, Direct, Surfaces (cyan: forward,
  traced; magenta: HDRP-shaded; orange: solid transmission, i.e. skin), Specular, Emissive (green sampled, blue a
  light's own surface, red rays only).
- Logs: BepInEx's `Path tracing hook 10s:` and `Path tracing:` lines; the native `Path tracing 10s:` line; GPU time by
  stage (9 stages + water, `TGPT_GetStageTimes`). Other logs: README "Diagnostics".

### 9.11 The harness (`tests/pathtracing_gpu_test.cpp`, ctest `tgpt_gpu_test`)
- Boots the real bridge and extension, rasterizes a synthetic HDRP-like GBuffer, traces and composites through the same
  render events; PNGs flipped upright in `build/native/tgpt-images`. Motion from unjittered view-projections; the
  previous one x T(cam - prev), as HDRP. `objects[]` only draw the GBuffer; traced-only objects go through SetScene.
- Switches: `TGPT_ENERGY=1`, `TGPT_CREVICES=1`, `TGPT_TEST_ADAPTER=1002|10DE|8086`, `TGPT_TEST_DEBUG_LAYER=<folder with
  the Agility 1.619 d3d12SDKLayers.dll>` (+ `TGPT_TEST_GPU_VALIDATION=1`; needs Windows Graphics Tools),
  `TGPT_TEST_STAGE=<name>` for a second concurrent run (copy the exe too). Sky off needs skyIntensity 1e-6 (0 = 1).
- Its frame 0 runs without the cache (so A/B runs stay identical); checks that sum darkened pixels run cache-off;
  single-frame errors vary with the seed (average 4 frames). Debug views are a frame late: run each twice. It never runs
  Ray Reconstruction (`tgup_gpu_smoke_with_pt` does).

### 9.12 Bits (check before adding one; parallel agents get theirs assigned)
- `TGPT_OPTION_*`: NO_SPATIAL 1, NO_TEMPORAL 2, NO_DENOISER 4, NO_SPECULAR 8, RAY_RECONSTRUCTION 16, NO_MICROMAPS 32,
  RAY_REGENERATION 64, NO_PASS_THROUGH 128, DI_VISIBILITY 256, SPATIAL_FEEDBACK 512, NRD 1024, NO_BOILING 2048,
  NO_CLAMPS 4096, EXACT_JACOBIAN 8192, OFFSET_RECEIVER 16384, NO_GI_BOILING 32768, NO_RADIANCE_CACHE 65536,
  EMISSIVE_LIGHTS 131072, GLOSSY_REFLECTIONS 262144, GI_OWN_RAYS 524288, DI_NO_REUSE 1048576, GI_VISIBILITY 2097152,
  NO_HDRP_SHADOWS 4194304. Next free: 8388608.
- `TGPT_MATERIAL_*`: DOUBLE_SIDED 1, ALPHA_TEST 2, VERTEX_COLOR 4, ALPHA_TILE 8, CAMERA_ONLY 16, HDRP_SHADED 32, GLASS
  64, EMISSIVE_ALBEDO 128, TRANSLUCENT 256, THIN_CLOTH 512, MASK_TILE 1024, STRANDS 4096, SWAYS 8192. Free: 2048 (planned:
  SEE_THROUGH), 16384. Instance masks: sways 0x02, still 0xFD (0x04 planned for see-through glass).
- `TGPT_INSTANCE_*`: ALPHA_TEST 1, HIDDEN 2, STAND_IN 4, CHARACTER 8 (tracer-set), MOVING 16, SWAYS 32 (tracer-set).
  `TGPT_LIGHT_*` above the cookie's 16 bits: HDRP_SHADOWS 0x10000, NO_SPECULAR 0x20000. Frame flags: RESET 1, GUIDES 2.

### 9.13 Performance (RTX 5080, 4K, DLSS Performance: render 1924x1084)
- 2026-09-28 tavern at Bounces 1: GPU 19.3 ms: bounced light 8.3, reflections 5.7, direct 3.7. At the old Bounces
  setting the bounced light alone was 27.8 of ~35 ms. The owner saw 20-25 fps at 4K (some rows left high after tests).
- Physical light reach raised tracer time from ~10 to 18-24 ms (every pixel has lights). A first 3840x2160 frame once
  allocated ~2.7 GB of tracer textures; ~300 MB at a 1080p render plus geometry. SER: within noise in the harness.

## 10. DEAD ENDS - measured not to work; don't re-attack without a new reason, in writing

Path tracing:
- HDRP's own ray tracing: every DXR variant is stripped from the shipped build.
- A GI or hybrid mode: removed at the owner's word; Full only.
- Excluding hair or eyes from tracing as a fix for dark eyes or odd hair shadows (owner, 2026-09-28).
- Raising ReSTIR history caps on harness numbers: the tracer's own ReSTIR at cap 20 was grainy in game (1 was chosen
  there). Since the RTXDI move the caps are RTXDI's defaults (DI 20, GI 8), with RTXDI's decorrelation.
- A denoised harness reference: it scored blur as a win (sun merged into the diffuse signal: 3.0 % -> 0.9 % while every
  sun shadow blurred). The sun and lamps are their own signal.
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
- Full RELAX (direct and specular too): blurred sun shadows, flickering speculars. Bounced light only.
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

- Deploys: the game folder's DLL can be a Vortex symlink to an older staged build ("exactly like before", 2026-09-20).
  `Build.ps1 -Deploy` copies from `artifacts/Native`, which only `cmake --install` refreshes. Hash all three places.
- `d3d11.hlsl` is embedded at CMake configure time (CMAKE_CONFIGURE_DEPENDS now forces a reconfigure), in 8000-char
  chunks (MSVC's 16380-char literal limit). `small` is a Windows macro (rpcndr.h).
- `Package.ps1` packages existing builds only (`Build.ps1 -Package`), and refuses without `build-manifest.json`.
- The flat-walled harness room couldn't show grain: procedural bumps in the G-buffer normal did. A check passed on
  noise (the leaf quad sat in the back wall's shadow). The micromap test passed with every micro-triangle unknown until
  mutation-checked. Total variation is unchanged by blurring a step edge: use squared differences of block averages.
  Whole-image shadow sums grow with bounce count (a cut-out cube traps light): run them cache-off.
- A plugin logging `Loading [X]` and nothing else died in Awake (no Unity log). A mod folder is not an installed mod.
- Windows Python in text mode keeps CRLF; never pass `newline=""` when writing.
- Claude Code: the Agent tool can't set effort; `~/.claude/agents/*.md` load at session start; `claude -p` was blocked.

## 12. Build recipes

- In vcvars64 (AGENTS.md). `build/native`: NMake, Release, `FETCHCONTENT_FULLY_DISCONNECTED=ON` (NRD's deps in
  `build/native/_deps`). Configure from the repo root (`<repo>`: its absolute path, forward slashes):
  ```
  cmake -S . -B build\native -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DTGFG_BUILD_NATIVE=ON -DBUILD_TESTING=ON
    -DSTREAMLINE_ROOT=<repo>/Streamline-2.14.1 -DNGX_INCLUDE_DIR=<repo>/Streamline-2.14.1/external/ngx-sdk/include
    -DFIDELITYFX_ROOT=<repo>/FidelityFX-SDK-2.3.0 -DXESS_ROOT=<repo>/XeSS-SDK-3.0.2
    -DANTILAG2_ROOT=<repo>/AntiLag2-SDK-2.0.0a -DDLSS_ROOT=<repo>/DLSS-SDK-310.9.1 -UTG_DXC
  ```
- One command (join the lines). Then build, install, ctest and `tools\Write-NativeManifest.ps1` as in AGENTS.md;
  `Build-Native.ps1` does it all (`-Generator "NMake Makefiles"` reuses `build/native`); runtimes: `Stage-Runtime.ps1`.
- Ninja (~15 s on 32 cores; NMake is serial): `pip install --target <scratch>/pytools ninja` on PATH (ShaderMake's
  ExternalProject ignores CMAKE_MAKE_PROGRAM), a build folder `%TEMP%\tgb\<name>` (long paths break MSVC PDBs, C1041),
  `-G Ninja -DMINHOOK_ROOT=... -DFETCHCONTENT_SOURCE_DIR_{SHADERMAKE,MATHLIB,DXC}=<repo>/build/native/_deps/*-src`.
- Offline smoke with all mods: `ctest -R tgup_gpu_smoke` (`_with_pt`, `_fg_installed`, `_all_mods`); copy the game's
  `UnityPlayer.dll` beside `build/native/tgup_gpu_smoke.exe`, or Streamline refuses ("Cannot read UnityPlayer.dll").

## 13. Parallel agents (only when the owner asks)

- The tree may hold uncommitted work, so git worktrees don't fit. What worked (2026-09-27): a script copies
  `git ls-files --cached --others --exclude-standard` into `<scratch>/wt/<name>` (plus Build.Local.props), junctions
  (`mklink /J`) the SDK folders (RTXDI, NRD, DXC, Agility, Streamline(-bin), FidelityFX, XeSS, AntiLag2, DLSS), copies
  `artifacts/Native`, and keeps `wt/base` as a pristine snapshot. Build folders in `%TEMP%\tgb\<name>`.
- Assign option, material, instance and view bits to each agent up front (section 9.12). Merge with `git merge-file
  <main file> <base file> <agent file>` per changed file; ABI, FrameConstants and test lists by hand. Read each diff
  before merging (the materials merge made metals black at path vertices until fixed).
- A "session limit" (HTTP 429) loses nothing: edits are files in the copies; messaging each agent by ID resumes it.

## 14. Writing for players

- Nexus pages: BBCode (`[size=5][b]Title[/b][/size]`, `[size=4][b]Section[/b][/size]`, `[list][*]...[/*][/list]`,
  `[quote]`, `[code]`), plain first person, then Installation (Requirements, a directory tree indented with alternating
  non-breaking and plain spaces), Configuration, "Recommended mods". Drafts: `docs/nexus.md` ("Frame Generation"),
  `docs/nexus-upscaling.md` ("FSR and XeSS Upscaling"); no Graphics Bridge page. Tooltips: one short line.
- The owner's other FoA mods on Nexus: Global Illumination 134, All Lights Cast Shadows 133, Contact Shadows 135, Film
  Grain 136, Chromatic Aberration 137, Animation Framerate Unlock 138, Conversation Lighting Overhaul 139, Volumetric
  Fix (ID unknown). BepInEx 5.4 for Mono is Nexus mod 50.
