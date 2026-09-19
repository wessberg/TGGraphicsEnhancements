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
  Intel GPUs. Where the HDRP path is missing (IL2CPP), the rows are greyed out unless DLSS is the
  selected upscaler.
- Packages include NVIDIA's license notices next to the Streamline runtime
  (LICENSE-Streamline.txt, LICENSE-NVIDIA-RTX-SDKs.txt, LICENSE-NVIDIA-Reflex.txt), staged from
  the SDK release by Stage-Runtime.ps1.
- Packaging writes zip entries with forward slashes (Compress-Archive in Windows PowerShell used
  backslashes) and copies each package to artifacts/Nexus as "TGFrameGeneration - mono.zip" or
  "TGFrameGeneration.zip". Build paths are mapped, so the DLLs no longer name local folders.
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
