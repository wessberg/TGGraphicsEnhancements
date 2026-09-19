# Automatic loading

TGFG starts with the game. There is no launcher, no replaced `dxgi.dll`/`winhttp.dll`/`version.dll`
and no Steam launch option.

## Two assemblies

`src/TGFG.Autoload` is a BepInEx preloader patcher. It loads `TGFG.Native.dll` early enough to
see the game create its Direct3D 11 device and swapchain, and it has no Unity or gameplay reference.
`src/TGFrameGeneration` is the gameplay plugin: Video-tab rows, camera and frame events,
diagnostics. It only binds to the native DLL the preloader loaded.

Both projects target net472 (BepInEx 5, Mono) and net6.0 (BepInEx 6, IL2CPP) and import the root
`Build.Local.props`. The gameplay project has a build-only reference to the preloader
(`ReferenceOutputAssembly=false`), so both build together without the plugin loading the patcher.
Keep them as two DLLs, and never mix a net472 and a net6.0 preloader: the file names match, the
artifact folders don't.

## Build and deploy

`Build.ps1 -Runtime Mono` builds and stages both managed DLLs; `-Runtime Both` builds both pairs.
`-Package` creates the player ZIP and `-Deploy` also installs it. `Package.ps1` does the same from
existing artifacts without compiling. The native DLL (`Build-Native.ps1`) and the vendor runtimes
(`Stage-Runtime.ps1`) are built and staged separately, under `artifacts/Native`.

`dotnet build ... -p:DeployToGame=true` calls the same script with `-StageOnly -Deploy` after both
projects compile.

## Installed layout

```
BepInEx/
  patchers/TGFG.Autoload.dll
  plugins/TGFrameGeneration/TGFrameGeneration.dll
  config/com.wessberg.tgframegeneration.cfg      (created on first start)
  TGFrameGeneration/
    Install-manifest.json
    Autoload.log
    Native/
      TGFG.Native.dll
      TGFG.Native.log
      Streamline/   NVIDIA Streamline and DLSS Frame Generation (interposer renamed TGFG.Streamline.dll)
      FidelityFX/   AMD FSR Frame Generation, with AMD's license
      XeSS/         Intel XeSS Frame Generation and XeLL, with Intel's license
```

The packager checks hashes, assembly identities, PE architecture and the vendor signatures, and copies
an allowlisted payload only. The installer backs up every TGFG file it replaces under
`BepInEx/TGFrameGeneration/Backups` and rolls back on a failed copy. It does not touch the game EXE,
other mods, Doorstop or Steam settings. `Test-Setup.ps1` checks an installed layout without starting
the game.

An empty `BepInEx/TGFrameGeneration/disable-autoload.txt` stops the preloader from loading the native
DLL.

## References

- BepInEx 5 preloader patchers: https://docs.bepinex.dev/articles/dev_guide/preloader_patchers.html
- BepInEx 6 BasePatcher: https://docs.bepinex.dev/master/articles/dev_guide/preloader_patchers.html
- MSBuild ProjectReference metadata: https://learn.microsoft.com/en-us/visualstudio/msbuild/common-msbuild-project-items
