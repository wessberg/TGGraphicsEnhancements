# Automatic loading

TG Graphics Bridge starts with the game, and with it the native side of TG Frame Generation and
TG Better Upscaling. There is no launcher, no replaced `dxgi.dll`/`winhttp.dll`/`version.dll` and no Steam
launch option.

The bridge is not a download of its own: each mod bundles all of it under the mod's own folders. A
player installs one mod, or both, and Vortex never sees the two deploy the same file. At run time one
copy starts, as below.

## Assemblies

`src/TGGraphicsBridge.Autoload` is the source of the bridge's BepInEx preloader patcher, built once
per mod: `src/TGFrameGeneration.Autoload` and `src/TGBetterUpscaling.Autoload` import its
`Autoload.props`, add an `Identity.cs` (the mod's folder, title and patcher GUID) and produce
`TGFrameGeneration.Autoload.dll` and `TGBetterUpscaling.Autoload.dll`. The distinct assembly names matter:
BepInEx 5 keys patchers by assembly and type name and BepInEx 6 loads each with `Assembly.LoadFrom`,
so two copies under one name would clash. The preloader loads `TGGraphicsBridge.Native.dll` early
enough to see the game create its swapchain, registers the native DLL of every mod built on the
bridge, and calls the bridge's bootstrap synchronously, before Unity starts the game. It has no Unity
or gameplay reference.

Before any extension loads, the bridge hooks the swapchain methods every DXGI factory shares, on a
factory of its own, rather than waiting for the game to create one. Two reasons:

- A factory created before the hooks, or one another hook forwards past them, is never seen. ReShade
  (installed as `dxgi.dll`) hooks `dxgi.dll`'s exports the moment it loads and forwards the game's
  factory calls past hooks installed after its own; without Frame Generation's NVIDIA runtimes,
  nothing else created a factory the bridge saw, and the game's swapchain went unnoticed.
- Hooks on one function run last-installed first, and ReShade leaves a D3D12 device or swapchain
  unwrapped only while its own swapchain hook is on the stack. The bridge's hook therefore has to be
  installed before ReShade's, which ReShade installs on the first factory created through its hooks.
  So the bridge creates its factory past any hook already on the factory exports: the first bytes of
  `CreateDXGIFactory`, `CreateDXGIFactory1` and `CreateDXGIFactory2` are put back as `dxgi.dll` has
  them on disk for that one call (only a jump written over an export's start, only where no
  relocation falls), then the other hook's bytes are restored. A device ReShade wrapped would also be
  the one it returns to the RenoDX DLSS add-on, which then finds no device of its own.

`tgup_gpu_smoke_unity_order` tests the order in which Unity creates its factory and device. The same
test with ReShade's `dxgi.dll` copied next to `tgup_gpu_smoke.exe` shows the order against ReShade: its
log must show the game's `CreateSwapChainForHwnd` and no `D3D12CreateDevice` (see TESTING.md).

`src/TGGraphicsBridge` is the bridge's gameplay plugin and the library the two mods compile against:
native status, the Video-tab helpers (`VideoTab`, `SwitchRowsOption`), the mod-manager integration,
IL2CPP class injection, the texture pointer cache and the motion-vector fixes. Each mod ships a copy
beside its own plugin (`BepInEx/plugins/<mod>/TGGraphicsBridge.dll`), always under the same GUID
(`com.wessberg.tggraphicsbridge`). BepInEx 5 and 6 load only the highest version of a GUID and log
`Skipping [TG Graphics Bridge ...] because a newer version exists` for the other copy; the two mods
have a hard BepInEx dependency on that GUID, and their references resolve to the copy BepInEx
loaded. None of the plugins loads a native DLL: they bind to the ones the preloader loaded.

All five projects target net472 (BepInEx 5, Mono) and net6.0 (BepInEx 6, IL2CPP). The three plugins
import `src/Game.References.props` (BepInEx, Unity, game and HDRP references, the evaluated game
folder); the preloaders import only `Build.Local.props` through `Autoload.props`. Each mod references
the bridge library with `Private=false` (`Build.ps1` stages the copy it bundles) and has a build-only
reference to its own preloader (`ReferenceOutputAssembly=false`). Never mix a net472 and a net6.0
build: the file names match, the artifact folders don't.

## One bridge per process

A mod takes part by shipping `BepInEx/<its folder>/graphics-bridge-extension.json`, which names its
own native DLL and the copy of the bridge it bundles:

```json
{
  "name": "TG Better Upscaling",
  "native": "Native/TGBetterUpscaling.Native.dll",
  "bridge": "Native/TGGraphicsBridge.Native.dll",
  "bridgeVersion": "1.1.0"
}
```

Every mod's preloader runs (BepInEx calls every patcher's `Initialize`, then every `Finish`). Each
reads all the manifests in alphabetical folder order, refuses a path outside a mod's own folder,
skips a mod whose folder has a `disable-autoload.txt`, and skips a folder whose mod is not installed
any more (no `BepInEx/patchers/<folder>.Autoload.dll` or `BepInEx/plugins/<folder>/<folder>.dll`: a
manual uninstall leaves the folder with its logs and backups). Then:

- the copy bundled with the newest bridge version loads its own `TGGraphicsBridge.Native.dll`,
  passes every mod's DLL to `TGGraphicsBridge_AddExtension` and calls `TGGraphicsBridge_Bootstrap`;
- a copy whose bridge is older stays idle; in its `Finish` it starts its own bridge only if no newer
  copy did (a newer mod whose preloader is missing or failed);
- the copy that loads the bridge records itself in a process-wide marker
  (`AppDomain.SetData("TGGraphicsBridge.Autoload.StartedBy")`), and every later copy sees it and does
  nothing.

Copies of the same version are meant to be the same files; when they are not, the copy that runs
says so in the log (and `Package.ps1 -Mod` refuses to deploy one mod beside another build of the same
version). An extension DLL that cannot be read is left out and the others start; every file is read
before the marker is set, so a bridge copy that cannot be read leaves the next copy free to start.
With TG Frame Generation 1.0.x installed (its `BepInEx/patchers/TGFG.Autoload.dll` and no manifest in
`BepInEx/TGFrameGeneration`), which starts a bridge of its own, no copy starts this one. BepInEx
picks the bridge's gameplay plugin by the same rule (highest version), so the managed and native
bridge match.
For that the bridge's version must go up whenever the bridge changes, its managed API only grows
(an older mod calls the newer library), and the extension ABI stays compatible or moves in lockstep.

The bridge loads each extension DLL, calls its `TGGraphicsBridge_CreateExtension(Host*, abi)` and
asks what it needs: the shared D3D12 device (both), and for TG Frame Generation the game's swapchain
as well (the first extension that asks for it presents). Without any extension installed nothing
native is loaded. Its log goes to `BepInEx/TGGraphicsBridge`, whichever mod's copy runs; the log
names that mod.

## Build and deploy

`Build.ps1 -Runtime Mono` builds the five managed DLLs and stages them under `artifacts/Mono`, the
bridge's library once per mod; `-Runtime Both` builds both runtimes. `-Package` creates one ZIP per
mod and `-Deploy` also installs them; `-Mod <name>` limits that to one mod. `Package.ps1` does the
same from existing artifacts without compiling. The native DLLs (`Build-Native.ps1`) and the vendor
runtimes (`Stage-Runtime.ps1`) are built and staged separately under `artifacts/Native`; the bridge's
native DLL is packaged into each mod.

`dotnet build ... -p:DeployToGame=true` calls the same script with `-StageOnly -Deploy` after the
projects compile.

## Installed layout

```
BepInEx/
  patchers/
    TGFrameGeneration.Autoload.dll      frame generation's copy of the bridge's preloader
    TGBetterUpscaling.Autoload.dll      upscaling's copy
  plugins/
    TGFrameGeneration/TGFrameGeneration.dll, TGGraphicsBridge.dll
    TGBetterUpscaling/TGBetterUpscaling.dll, TGGraphicsBridge.dll
  config/com.wessberg.tgframegeneration.cfg, com.wessberg.tgbetterupscaling.cfg   (created on first start)
  TGGraphicsBridge/                     created at run time: Autoload.log, TGGraphicsBridge.Native.log
  TGFrameGeneration/
    Install-manifest.json, graphics-bridge-extension.json
    Native/
      TGFG.Native.dll, TGGraphicsBridge.Native.dll, TGFG.Native.log
      Streamline/   NVIDIA Streamline and DLSS Frame Generation (interposer renamed TGFG.Streamline.dll)
      FidelityFX/   AMD FSR Frame Generation, with AMD's license
      XeSS/         Intel XeSS Frame Generation and XeLL, with Intel's license
  TGBetterUpscaling/
    Install-manifest.json, graphics-bridge-extension.json
    Native/
      TGBetterUpscaling.Native.dll, TGGraphicsBridge.Native.dll, TGBetterUpscaling.Native.log
      FidelityFX/   AMD FSR upscaler, with AMD's license
      XeSS/         Intel XeSS Super Resolution, with Intel's license
      DLSS/         NVIDIA DLSS Super Resolution for the game's own DLSS, with NVIDIA's license
```

No path appears in both mods' packages (`tests/package_tests.ps1` checks it). The packager checks
hashes, assembly identities, PE architecture and the vendor signatures, and copies an allowlisted
payload only; each mod may write only its own paths. The installer backs up every file it replaces
under `BepInEx/<mod>/Backups`, replaces a file instead of writing through it (Vortex deploys links
into its staging folder), removes what Frame Generation 1.0.x installed and 1.1.0 no longer uses
(`BepInEx/patchers/TGFG.Autoload.dll`, the interposer under NVIDIA's name), and rolls back on a failed
copy. It does not touch the game EXE, other mods, Doorstop or Steam settings. `Test-Setup.ps1`
checks an installed layout without starting the game.

An empty `disable-autoload.txt` in `BepInEx/TGGraphicsBridge` stops every copy of the preloader from
loading anything; in a mod's own folder it keeps only that mod's native DLL from loading.

## References

- BepInEx 5 preloader patchers: https://docs.bepinex.dev/articles/dev_guide/preloader_patchers.html
- BepInEx 6 BasePatcher: https://docs.bepinex.dev/master/articles/dev_guide/preloader_patchers.html
- BepInEx's duplicate-GUID rule and assembly resolution: `Chainloader` (BepInEx 5) and
  `BaseChainloader.ModifyLoadOrder` (BepInEx 6) keep the highest version per GUID; `LocalResolve`
  answers a reference with an already-loaded assembly of that name. Read from BepInEx 5.4.23 and
  6.0.0-be.755 on 2026-09-23.
- MSBuild ProjectReference metadata: https://learn.microsoft.com/en-us/visualstudio/msbuild/common-msbuild-project-items
