# Unreleased

- Report DLSS-G's presented frames in the pipeline line, and stop printing a zero for FSR. Only the
  XeSS path fed the counter, so the line showed `sdkPresented=0` under FSR, which reads as a failure
  when the FSR API simply exposes no such query; FSR now prints `unreported`. DLSS-G's own
  `numFramesActuallyPresented` feeds the same counter, so all three backends report what their runtime
  states and none of it is inferred from TGFG's own frame counts.

- Query DLSS-G capabilities on the present thread. The startup query ran on whichever thread created
  the bridge, which Streamline warns about because the state carries the inputs-processing completion
  fence. The bridge always starts on the standard swapchain, so the first present fills the
  capabilities instead and there is no longer an off-thread `slDLSSGGetState` call.

- Stop handing DLSS-G options to the state query. `slDLSSGGetState` applies the options it is given,
  and the per-present capability read passed the applied ones, so Streamline received a full options
  set on every frame and re-evaluated its flip metering for no reason; the change check in
  `apply_options` had no effect. The query now passes null, which returns the state of the options
  already in effect. Measured: flip-metering reconfigurations that followed no option change at all,
  half of them in the run before the change, no longer occur.

  This does not remove Streamline's "Repeated slDLSSGSetOptions() call for the frame N" warning,
  which has a different cause. Every remaining one follows a swapchain resize: `resize` disables
  DLSS-G, and the next present turns it back on, with no Present in between to advance Streamline's
  frame counter. That bracketing is correct; Streamline cannot tell it from a race. The game issues
  `ResizeBuffers` at its current 3840x2160 repeatedly, seven times in 88 seconds of menu use in the
  reference run, and two of the three frames over 100 ms in that run land within 600 ms of one.

- Give DLSS Frame Generation and Intel XeSS-FG the same presentation-oriented depth and motion
  vectors FSR already gets. HDRP renders into Y-inverted targets and flips the image in its final
  blit, so the frame all three runtimes interpolate is mirrored against the buffers HDRP produced.
  No SDK can be told a tagged texture is upside down, and the projection matrix DLSS-G and XeSS
  receive does not describe row order, so both were reprojecting from mirrored vectors; DLSS-G's
  optical flow hid most of it, leaving the image warping during movement. The copies now flip for
  every backend and `TGFG_SetCamera` turns the camera's clip-space matrices to match, so the camera
  describes the space the buffers are in. The reporter's own account - FSR looking cleaner than both
  DLSS and XeSS after the FSR-only fix - is what this follows up. Build, eight native tests and the
  Python checks pass. Confirmed in game on Mono, RTX 5080: the reporter finds all three technologies
  equally good afterwards, where DLSS and XeSS were visibly worse than FSR before. The run's log
  shows HUD-less input supplied on every steady gameplay frame for all three, differing from the
  presented frame over 2.4-3.1% of sample points in each case.

- Fix the IL2CPP Options crash by evaluating TGFG's dependent-row visibility
  through a scoped native-getter hook instead of a converted callback. Build and
  regression checks pass; the reporter confirms the upscaling/pause/Options retest works.

- Port the hand-history guard to IL2CPP. Intercept the native compute dispatcher because
  IL2CPP inlines the copy wrapper; preserve one history copy per manager per Unity frame
  while allowing skinning and other kernels. Native-pointer identity handles multiple
  managed wrappers. Build and regression checks pass; IL2CPP visual validation is pending.

- Fix HUD flicker with ReShade/RenoDX by replaying its recognized final-output shader
  and live settings on the HUD-less scene before HDR10 encoding. Observe indexed,
  instanced and indirect draws without assuming three-vertex geometry: the successful
  game log identifies a six-index DrawIndexed pass. Reporter confirms success with
  ReShade and RenoDX DLSS 5, with/without upscaling and the other upscalers tested.
  Logs confirm replay, pixel verification and HUD-less supply; production draw detours
  and color conversion also pass WARP tests.

- Accept ReShade's wrapped D3D11 device for HUD-less capture by checking its immediate
  context against the bridge's existing identity/tag validation. Previously all captures
  were rejected as device mismatches, bringing HUD flicker back when ReShade/RenoDX was
  enabled. Verified capture and HUD-less supply now work across all three FG backends.

- Mono: preserve Kandra's previous skinned positions across extra camera-rendering contexts
  while DLSS, FSR or XeSS FG is selected. The latest capture shows enabling hand material
  motion passes alone was insufficient. The guard prevents duplicate history copies within
  one Unity frame, with runtime counters. The reporter confirms hands are much better;
  the new input capture confirms hand vectors no longer follow camera-only motion.
  No minimap mod dependency; single-camera history is preserved in regression tests.

- The FSR capture shows first-person hands receiving camera-only motion instead of their own motion.
  Enable existing disabled motion-vector passes on the first-person Kandra rig's opaque materials
  for DLSS, FSR and XeSS frame generation, restoring them when FG is Off. The shipped character skin material disables
  that pass despite having compiled variants. Visual and corrected-input validation are pending.

- FSR: align depth and motion textures with HDRP's Y-flipped presentation color, including vertical
  motion scale and jitter. Prevent submission across a backend/orientation change. HUD separation
  is confirmed fixed in-game; the reporter finds hands improved but still flickering during camera movement.
- Add opt-in FSR debug flag 32 to save three source color/depth/motion frames and metadata once
  gameplay movement begins, for diagnosing the remaining hand flicker. Copies precede readback;
  the capture stops automatically and uses at most 384 MiB of image data.

- Replace the stale end-camera swapchain capture with HDRP's current `AfterPostProcess` color,
  copied on render event 4 before overlay UI composition. Remove the `rendersUIOverlay` override.
  Handle padded RTHandles, final-blit Y orientation, SDR sRGB and HDR PQ conversion. A verified
  copy is supplied to DLSS-G, FSR and XeSS for their UI separation paths. The reporter confirms
  HUD flicker is fixed in SDR; HDR validation remains pending.
- Keep the present-time freshness and HUD-area checks, including skipping still frames. The native
  log reports capture/rejection/supply counts, while the managed log reports HDRP color layout and HDR state.
- The advanced setting "Keep the HUD out of frame generation" (default on) controls the custom
  pass. Screen-space overlay UI is separated; camera/world-space UI and lens effects already in
  scene color remain interpolation inputs.
- FSR resets history when the HUD-less source changes, and recreates its context when input flags
  change (including enabling/disabling FSR's debug checker).
- Add a D3D11 WARP regression test for the actual HUD-less color conversion shader.
- DLSS Frame Generation is told to allocate its user interface recomposition path while a verified
  HUD-less image is supplied. Streamline only interpolates the HUD-less image separately from the
  back buffer when that option is on, so the HUD-less tag had nothing to act on before. XeSS already
  switches its UI composition on with the same condition.
- Low Latency can be Off while FSR frame generation runs. AMD pairs FSR with Anti-Lag 2, which needs
  an AMD GPU; on other GPUs the row previously forced NVIDIA Reflex, which only knows about generated
  frames through Streamline and therefore only for DLSS Frame Generation. DLSS still requires Reflex
  and XeSS still runs with XeLL.
- FSR is told the resolution the game renders at instead of the display resolution. The FSR API
  takes that as maxRenderSize at context creation, allocates its render-resolution buffers for it
  and scales its lookups into them by renderSize/maxRenderSize. With DLSS or another upscaler on,
  the two differ (1896x1068 against 3840x2160 at 4K Performance), so the context is now rebuilt for
  the render resolution of the first frame and whenever it changes.
- The HUD-less check no longer counts a still image against the capture. A menu, a pause or a
  loading screen leaves the frame unchanged, which the check read as a stale capture and which
  reset the streak of good results, so the HUD-less image was never handed to a technology in a
  normal session. Still frames are skipped and counted separately in the log.
- FSR: near and far are passed swapped for inverted depth, as FSR's own integrations do; the inputs
  FSR receives (render and display size, motion vector scale, jitter, near/far, FOV, frame time,
  reset, HUD-less) are logged once and whenever they change. New advanced setting "FSR debug flags"
  turns on FSR's overlays (tear lines, reset indicators, the debug view of motion vectors, depth and
  masks, pacing lines) and FSR's input checker, for troubleshooting from a screenshot and the log.
- New optional exports TGFG_SetHudlessCapture, TGFG_SetHudless and TGFG_SetDebugFlags; the ABI
  (0x00050000) and existing structures are unchanged. The new HUD-less packet is 32 bytes.

# v0.5.0 - AMD FSR and Intel XeSS frame generation, one Low Latency selector

- The plugin is now "TG Frame Generation" with the ID com.wessberg.tgframegeneration. Settings
  live in BepInEx/config/com.wessberg.tgframegeneration.cfg; the old
  tgfoa.experimental.dlss-frame-generation.cfg and BepInEx/TGFrameGeneration/VideoSettings.cfg
  are migrated on first start and renamed to *.migrated.
- FoA Mod Manager lists exactly the Video tab's six rows, with the same names, choices, labels,
  order and visibility rules. Choices follow what this GPU reports, and a change made there
  shows up in the Video tab once the Options screen is closed. The other options sit in a hidden
  [Advanced] section.
- Avalon Mod Manager's MODS tab no longer lists TGFG: TGFG adds itself to that manager's
  blacklist for the session, without saving its config. Its Apply writes back the values staged
  when the Options screen opened, which undid Video-tab changes.
- Low Latency can be Off only while frame generation is Off; FSR now runs with Reflex or Anti-Lag
  2. The row is greyed out when only one choice remains (XeSS with XeLL).
- Frame generation no longer needs DLSS upscaling. Without DLSS, a custom pass before
  post-processing hands the native side HDRP's own depth and motion vectors (TGFG_SetHdrpInputs,
  render event 3), with HDRP's TAA jitter and the motion vector scale HDRP gives DLSS. DLSS's
  inputs take precedence while it runs. FSR and XeSS frame generation therefore work on AMD and
  Intel GPUs. If the custom pass can't be installed, the rows are greyed out unless DLSS is the
  selected upscaler.
- The standard version (IL2CPP, BepInEx 6) has the same Video rows and HDRP input path as the Mono
  build, from the same sources. TGFG's subclasses of the game's Setting and DependentOption and of
  HDRP's CustomPass are registered with Il2CppInterop, which routes the base class's virtual calls
  to them. Il2CppInterop allocates those objects without running the IL2CPP base constructor, so
  TGFG runs it (Il2CppTypes.Construct).
- Packages include NVIDIA's license notices next to the Streamline runtime
  (LICENSE-Streamline.txt, LICENSE-NVIDIA-RTX-SDKs.txt, LICENSE-NVIDIA-Reflex.txt), staged from
  the SDK release by Stage-Runtime.ps1.
- Packaging writes zip entries with forward slashes (Compress-Archive in Windows PowerShell used
  backslashes) and copies each package to artifacts/Nexus as "TGFrameGeneration - mono.zip" or
  "TGFrameGeneration.zip". Build paths are mapped, so the DLLs no longer name local folders.
  A package holds the BepInEx folder and TGFG-Package-manifest.json; TGFG-PLAYER-INSTALL.txt is
  no longer included.
- Removed leftovers of the 0.3 patch workflow: APPLY.md, integrate-autoload.patch,
  source-manifest.json, the validation logs, docs/VALIDATION.md, the retired Launch-Game.ps1 and
  the 0.3 example config.
- Video tab: Frame Generation [Off | DLSS | FSR | XeSS]. A technology is listed only when its
  runtime is installed and reports support on the GPU. FSR runs through AMD's FSR API (FSR SDK
  2.3.0: FSR 4 ML frame generation on Radeon RX 9000, FSR 3.1 analytic frame generation elsewhere)
  and AMD's frame generation swapchain. XeSS runs through Intel's XeSS-FG swapchain (XeSS SDK
  3.0.2). All three use the same inputs (see the HDRP input path above).
- Frame multipliers come from the runtimes: DLSS from Streamline's numFramesToGenerateMax, XeSS
  from xefgSwapChainGetProperties (up to 4x on Intel Arc, 2x elsewhere). The FSR API has no
  multi-frame control, so FSR is 2x. Frame Multiplier offers up to the highest reported maximum,
  blocks values above the selected technology's maximum and clamps the value when switching
  technology; the native side clamps the request as well. Neither FSR nor XeSS has dynamic frame
  generation, so Dynamic stays DLSS-only. The 6x cap is gone (the ABI bound is 16).
- Switching technology recreates only the private presentation swapchain, between two frames: the
  Streamline swapchain for Off/DLSS, the SDK's own swapchain for FSR/XeSS. The game's D3D11
  buffers are unchanged. An SDK that fails is disabled for the session and presentation falls
  back to the standard swapchain.
- Low Latency [Off | NVIDIA Reflex | NVIDIA Reflex + Boost | Intel XeLL | AMD Anti-Lag 2] replaces
  the NVIDIA Reflex row and lists what the GPU supports. DLSS Frame Generation requires Reflex and
  XeSS Frame Generation runs with XeLL only (Intel's requirement), so the row forbids the other
  choices for those. XeLL on its own needs an Intel GPU; Anti-Lag 2 needs an AMD GPU. The 0.4.0
  "NVIDIA Reflex" value migrates to "Low latency"; the advanced base FPS limit feeds whichever
  technology is active.
- The bridge no longer requires DLSS-G: Streamline binds when DLSS-G or Reflex is supported (RTX
  20/30 get FSR/XeSS with Reflex), other GPUs get a plain D3D12 device, and a Streamline start
  failure no longer stops the other technologies.
- A game waiting on the swapchain's frame-latency object gets a bridge-owned semaphore that
  survives swapchain recreation; the private swapchain no longer carries that flag. HDR metadata
  is re-applied to a recreated swapchain.
- Native ABI v5 (0x00050000): the request gains backend and latencyMode (was reflexMode) and
  baseFpsLimit (was reflexBaseFpsLimit); the status reports available backends, per-backend
  maxima, latency technologies, the active output and latency technology, the GPU vendor and
  backend failures; the camera packet carries the world-to-view matrix XeSS needs.
- Packages add Native/FidelityFX (AMD-signed loader and frame generation DLLs, license notice)
  and Native/XeSS (Intel-signed libxess_fg.dll and libxell.dll, license notice).
  Stage-Runtime.ps1 takes -FidelityFXRoot and -XeSSRoot; Build-Native.ps1 takes the FSR, XeSS and
  Anti-Lag 2 SDK roots, defaulting to folders next to the script.
- Managed and native versions are 0.5.0.

# v0.4.0 - native Video settings rows, Reflex modes, startup crash fix

- Video tab: Frame Generation [Off | DLSS], Frame Generation Mode [Fixed | Dynamic] (shown only
  when the runtime reports Multi Frame Generation and Dynamic support), Frame Multiplier
  [2x .. max reported] (Fixed), Max Frame Multiplier [Up to 2x .. max] and Target Framerate
  [Auto | 60 .. display maximum] (Dynamic; the max is sent as numFramesToGenerate, which the
  Streamline 2.14.1 guide documents as ignored in Dynamic mode, so it is best-effort) and
  NVIDIA Reflex [Off | On | On + Boost] are real game Setting/PrefOption rows registered through
  SettingsMaster.AddGraphicSetting, placed after Dynamic Resolution, with the game's Apply
  Changes / Undo / Restore Defaults. Reflex Off is forbidden while Frame Generation is on.
- Video settings persist in BepInEx/TGFrameGeneration/VideoSettings.cfg, outside the plugin's
  main config, so generic settings managers no longer re-apply stale copies over them. The
  old UGUI adapter and its Mode/Fixed multiplier/target entries in the main config are gone.
- Native ABI v4 (0x00040000): TGFG_Request gains reflexMode (Off/On/On + Boost). Reflex
  follows the user setting and is forced to at least On while FG is requested.
- Managed and native versions are 0.4.0.
- DLSS 5 (RenoDX DLSS addon) compatibility: NVIDIA's sl.interposer.dll is staged and installed,
  byte-identical and still NVIDIA-signed, as `Native/Streamline/TGFG.Streamline.dll`. The addon
  looks Streamline up by module name, hooks the interposer's exports and DXGI factories, and
  releases (E_UNEXPECTED) every swapchain ReShade did not observe, including TGFG's private
  output. Deployment backs up and removes the old `Native/Streamline/sl.interposer.dll`. The
  native log reports another mod's Streamline if one is loaded later.
- ReShade compatibility: NVIDIA's Unity plugin evaluates DLSS on ReShade's proxy context, not the
  real immediate context. The bridge tags the real context with private data (which ReShade
  forwards) and matches on it, so depth/motion capture works under ReShade + RenoDX.
- Video tab: Frame Multiplier is shown only in Fixed mode; Dynamic mode shows Max Frame Multiplier
  and Target Framerate instead. The pipeline log line includes Reflex's own latency reports.
- Fixes found in the first in-game FG runs: the end-of-camera marker never executed under HDRP
  (endCameraRendering fires after the last Submit); FoA's camera rect is not bit-exact full screen;
  the R32G32_FLOAT motion copy is not NT-handle shareable on NVIDIA D3D11 (now R16G16_FLOAT).
  A "Pipeline 5s:" line in TGFG.Native.log shows where frames drop out.

- Native: removed 64 KB stack buffers (GetModuleFileNameW/GetSystemDirectoryW) from the
  GetProcAddress hook and bootstrap paths. The hook runs on every thread in the process,
  including NVIDIA driver worker threads with small stacks, and the 64 KB frame probe caused
  a STATUS_STACK_OVERFLOW at the first Streamline Present. Paths are now heap-grown.
- Build-Native.ps1: passes -DCMAKE_BUILD_TYPE=Release for single-config generators; the
  NMake build previously linked the debug CRT.

# v0.3.1 - repository-integrated Steam autoload

- Adds src/TGFG.Autoload in the existing solution and gameplay build graph.
- Same root Build.Local.props, two runtime-specific managed pairs.
- Build.ps1 -Deploy stages/installs complete plugin/preloader/native/runtime package.
- Package.ps1 produces a launcher-free player ZIP from this repo's artifacts.
- Test-Setup.ps1 checks the installed Steam-autoload layout, not developer launcher files.
- Replaces stale launcher advice and adds an autoload log tail to gameplay diagnostics.
- Retains all native v0.3.0 source, SDK build scripts and ABI unchanged.
- No Windows compilation, PowerShell execution or in-game validation claimed.

# Changelog

## 0.3.0 — DX11 rendering / private DX12 Streamline presentation

Replaced the D3D12-source backend with a direct D3D11-to-D3D12 bridge. Added a
D3D11-facing swapchain, shared textures/fences, typed D3D11 NGX capture, protected
input conversion, HDR output conversion, expanded diagnostics and local setup
checks. Kept the Mono/IL2CPP managed targets and existing in-game menu adapter.

Changed the early launcher to require `-force-d3d11` and rendezvous after Windows
loader initialization at the restored PE entrypoint. ABI changed from v2 to v3.
Added Windows HLSL compilation tests and portable interop policy checks. No
OptiScaler/FidelityFX rendering backend is included. Windows build/game testing
has not been performed in the preparation environment.

The HUD-less capture export is implemented, but no verified FoA/RenoDX HUD-less
stage is hooked. Indexed source buffers, nonblocking presentation, full device
recovery and zero-overhead Off are not claimed.

## 0.2.0

Earlier direct Streamline prototype, requiring actual D3D12 game rendering.
Not compatible with this version's native ABI.

## 0.1.0

Earlier architecture was superseded. It is not a dependency of this package.
