# Released mods - project context

Sections 5-8 moved unchanged from PROJECT-CONTEXT on 2026-10-12 (base20i).
They retain their dated reports and validation limits. Read the shared engine/settings/bridge sections and
DEAD ENDS in `docs/PROJECT-CONTEXT.md` too; newest release state is in BACKLOG and the round notes.

## 5. TG Frame Generation

- Inputs: the DLSS SR NGX evaluation when the game's DLSS runs (NVUnityPlugin; feature flags 43 =
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
- Preset C (2026-10-30, DLSS 310.9.16, Streamline 2.14.16; row Frame Generation Quality): one create-time NGX parameter,
  `DLSSG.LiteCandidateBlendingEnabled` (`nvsdk_ngx_defs_dlssg.h`; NVIDIA's RTX Remix sets it the same way). `sl::DLSSGOptions`
  has no field for it, so `control.cpp` hooks `_nvngx.dll`'s D3D12 CreateFeature, only once a preset was asked for, and
  turns DLSS-G off for one frame so the feature is made again. It cannot run with UI recomposition (we keep that off).
  Run 20261030-124931, 4K, DLSS Performance, 2x, game in front, stream on: Quality 73.2 / 73.5 / 73.1, Performance
  76.0 / 76.1 rendered fps, 13.65 against 13.13 ms a frame. The guide `DLSS-FG Programming Guide.pdf` in that SDK says 310.7.0 and
  does not mention it; NVIDIA App's route is the driver profile (`NGX_DLSS_FG_OVERRIDE_RENDER_PRESET_SELECTION_ID`).
- DLSS-G generates nothing while the game's window is behind another (status 0, fgOn counts on, "80 on screen from 80
  rendered"; the No FPS gain alert fires). In front the same build shows 147 from 73. Judge frame generation only in front.
- Backends. Streamline 2.14.16 (the newest release, 2026-10): eDynamic ignores `numFramesToGenerate`;
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
  (10-08 owner comparison uses Upscaled; older notes said Present), `DirectNeuralRenderingRequireDlss=1`. Its NGX EvaluateFeature hook can skip an evaluation
  (0xBAD00000). With ReShade + RenoDX HDR, FG "works perfectly" (2026-09-19; the game then calls ResizeBuffers every
  10-30 s).
- Hybrid laptops (HybridVsync.cs, DLSS-G guide 22.6): DLSS FG + `display.crossAdapter=1` -> the game's VSync off and
  greyed, Target FPS once to "RefreshRate_Unlimited", toast, opt-out; `TGFG_SetNoTearing` while it holds.
- FrameRateCheck.cs: < 15 % gain after 3 s settle + 5 s measure -> toast and a "No FPS gain" row. VRAM full and RAM full
  rows while FG generates. FrameStart.cs: a PlayerLoop entry after `WaitForLastPresentationAndUpdateTime`; frameId is a
  rising counter, not Time.frameCount. (Open items, such as the hitches on fast pans: BACKLOG.)

## 6. TG Better Upscaling

- Unity 6000.3 (the game's 1.31, Steam build 26298687): HDRP's `CustomPass.ExecuteInternal` gained a `ScriptableRenderContext` (7 arguments), so FSR and XeSS fell
  back to STP with a `TargetParameterCountException` and NR recorded nothing until `Shared/CustomPassRecorder.cs` passed an
  empty one (it is only handed on to the pass; ours use `ctx.cmd`). NGX's optimal-settings query answers 33.3 % for DLAA:
  `DlssModes.cs` holds DLAA at 100 %. FSR SDK 2.4.0's upscaler 4.1.2 offers FSR 4 on RX 6000 and newer and some Ryzen chips, not
  on the Ryzen 9950X3D's built-in Radeon (device 13C0, driver 2026-04: offered 3.1.6 and 2.3.4 only; 3.1.6 ran there, 10.7 ms for 720p to
  1080p). Our inputs stay as they are: the SDK's input list did not change.
- HDRP's DLSS feature is created at the first frame's size, grows but never shrinks (`DLSSPass.UpdateViewState`), and a too-large one costs 2 to 3 ms a frame at 4K. `TGUP_GetDlssSize` and `StpHost.TickDlssSize` rebuild it once by a one-frame change of quality level. Render size alone hides this: both states render 1924x1084.
- The game's update of 2026-10-06 (Steam build 25629220) ships Unity's FSR 2: `AMDUnityPlugin.dll`, a fourth row entry
  `UpScalingType_FSR` at index 3 (this mod's index and ID), `UpScalingController.EnableFSR` (the camera's
  `allowFidelityFX2SuperResolution`, scale from the game's slider). HDRP's FSR2Pass, a stub before, takes the same
  color, depth and motion vectors as `DoStpPasses` plus a bias mask (`UpscalerColorMaskPass`); preExposure fixed at 1,
  reset only on a camera's first frame, no extra mip bias. HDRP runs one upscaler per camera: the first in its asset's
  `advancedUpscalersByPriority` that the camera allows (the mod's load line logs the list). No FSR 3, no frame
  generation. 1.2.0 failed on the row's 4 entries (CHANGELOG).
- FSR and XeSS take STP's place: entries 3/4 of the game's Upscaling row (`EnumArrowsOption._options`; the mod's FSR
  entry replaces the game's where AMD's runtime reports FSR, else the game's FSR 2 keeps it; re-point the row's
  `Option` when replacing its current entry, or `OptionInt` is -1; PrefMemory key "UpScalingType" restores them); a
  Harmony prefix on `UpScalingController.OnSettingChanged` sets the
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

- R17/NR-42: all three tested normal-version1.2.3 zips now packaged in artifacts/Nexus. FG/BU payloads
  match the NR-39 smoke deployment byte-for-byte, including shared managed/native bridge copies.
  Each zip was extracted and checked for hashes, managed version, native architecture, vendor signatures,
  required/allowed paths and Vortex override. NR-43: owner switched back; all3 Mono1.2.3 packages rebuilt
  and deployed13:49 with official NR model. BepInEx5.4.23.5. Final gameplay smoke13:54 passes two real
  pause/options/apply/resume cycles, DLSS SR + FG2x/Reflex + NR Quality/Performance, no errors/crash.
  Source105 passes, configs restored,117 saves unchanged, probe removed, game closed. All6 zips ready.
- R17/NR-37: keep a managed Il2CppReferenceArray wrapper for the entire lifetime of an injected option
  that owns it. SwitchRowsOption previously passed temporary arrays to its inherited OtherOptions field,
  both in its constructor and on selector changes. The real menu probe read recycled JSON objects as
  PrefOptions; the owner crash was in Attribute.IsDefined while obtaining the row view. The nativeRows
  field now retains the native array/GC handle explicitly. Built/deployed13:07;20 main-menu cycles and
  20 actual gameplay pause/options/apply/resume cycles with active NR and forced GC pass (13:27).
  Owner says it works great. Avoid a global GC or reflection patch. Original crash occurred during play
  with no background deploy or GPU tests. Scratch probe cleanup must not write AllSettingsUI.LastSettingsTab:
  be.755's generated static string setter passes string data instead of the address of the reference.
- R17/NR-36: never Harmony-patch HDRP RenderPostProcess on be.755. Its `in PrepassOutput` is a non-blittable
  struct exposed as a wrapper class; the generated native trampoline dereferences its data as an object
  pointer and crashes before our prefix, including while NR is Off. NR's Il2CppPostProcessHook checks the
  exact signature and forwards that argument as an opaque IntPtr through the existing DetourProvider.
  GenerateTrampoline prepares the original before Apply (OriginalTrampoline alone can be unprepared).
  Camera-scoped BeginPost/EndPost and finally restoration stay; other hooks and Mono's Harmony path stay.
  Deployed12:44 with official model; first frame and4800 NR evaluations observed in IL2CPP, no crash at117s.
- R17 test-harness pitfall: the generated AllSettingsUI.LastSettingsTab static string setter crashes
  in il2cpp_field_static_set_value. The scratch Options probe no longer writes that process-local field.
  This was the later diagnostic cleanup crash (PID45272), not the owner's Options crash (PID45724).
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
  (3) Never step an IL2CPP enumerator from managed code (`GetEnumerator()`, `MoveNext()` on a set behind an
  IEnumerable): `Model.Views` threw "Collection was modified" at the first step, every session, and the rows' alert
  icons never showed (found 2026-10-06). Ask Unity (`Object.FindObjectsByType(Il2CppType.Of<T>(), ...)`) or index.
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

- Built 2026-09-24 for Win10/RX 9060 XT no FG, 4060 laptop DLSS-G and 9060 XT FG halving fps (CHANGELOG,
  TESTING). One `BepInEx/TG Troubleshooting Report <date>.txt` (~25 KB), Nexus-ready; button under Performance
  Overlay. Tests never write player config; pauses restore player settings until play resumes.
- Notices: Label <=16 chars, Summary tooltip <=240, Detail report-only. Install "correct" requires the plugin
  loaded from the right path; `InstallCheck.Tests` has read-only `check <exe> <loaded>` for a real installation.
- First report (2026-09-26, 4060 laptop/1080p144/Mono): all FG 2x, DLSS-G cheapest. 09-27 fixes: report Pipeline
  per-step, autosave camera blip, only divided driver VSync alerts (not normal VSync/Max Frame Rate), 45 fps row.
- R18, 10-09: antivirus error 225 became misleading "Missing files". Released FG 83947a2a is Wacatac.H!ml;
  October 7 FG 55843c48 scans clean, differs ONLY in six build-timestamp bytes. All code identical; no new
  community-helper code in that difference. Both released zips affected; exact evidence/limits in round-18.
- 10-12 report (DDDepresso; normal version, FG + BU 1.2.3): a Steam Deck on SteamOS (his own words on Nexus),
  the first report under Proton. The report itself says "Windows 10 Pro 21H1, build 19045" (real Windows 19045 is
  22H2) and has no Wine check; its tells: `vkd3d-proton.cache` in the game folder, GPU "AMD VANGOGH" (Mesa's
  name), driver 35.0.65535.65535. His first crash at start was an old BepInEx. He asked for "DLSS without
  OptiScaler": in OptiScaler "DLSS" is the game's DLSS INPUTS run through FSR or XeSS (no DLSS runs on AMD), which
  is what TGBU feeds FSR and XeSS directly. He also uses Lossless Scaling (on SteamOS a Vulkan layer, lsfg-vk):
  the report cannot see it, nor the Deck's own frame limiter. The
  mods run there (D3D11-to-D3D12 sharing, FSR and XeSS FG, 0 native errors). FG 2x halves rendering: 26.5 -> 13.6
  (27 on screen); an earlier run 29.7 -> 15.0 at a steady 66.6 ms. Presents are held to 30 a second, the wait inside
  the present call (22 ms with FG off, where the game's own limit waits outside it). Suspect, unproven: the Deck's
  own Framerate Limit. Gaps: "Problems found" lists notices only and never judges the test run's steps
  (`Troubleshooting.Summary`); `FrameRateCheck` sees no limit outside the game on AMD.

