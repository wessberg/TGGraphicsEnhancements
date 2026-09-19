# Autoload is part of the existing repository

This change is based on the supplied v0.3.0 source snapshot plus the standalone
v0.1.0 autoload source. It is not based on a fetch of a live Git repository or on
unuploaded local edits. Apply the provided Git patch to the existing repo; do not
replace that repo with another source folder. No native file is changed.

## One build and deployment flow

`src/TGFG.Autoload` is beside `src/TGFrameGeneration` in the same repo and solution.
Both import the existing root `Build.Local.props`, with the same fallback game
paths. Both target net472 and net6.0. The gameplay project has a build-only
ProjectReference to autoload (`ReferenceOutputAssembly=false`, `Private=false`).
This ensures the preloader builds alongside the gameplay plugin without becoming
an assembly dependency loaded by gameplay. The preloader has no Unity or gameplay
assembly reference. Do not combine the two entry points into one DLL.

`Build.ps1 -Runtime Mono` builds and stages BOTH managed DLLs. `-Runtime Both`
builds both pairs. `-Deploy` now packages and deploys the complete mod, including
the already-built native backend and staged signed NVIDIA runtimes. `-Package`
creates the full runtime-specific player ZIP without deploying. The separate
`Package.ps1` does the same from existing managed/native artifacts without compiling.

Native artifacts remain at `artifacts/Native`. The existing `Build-Native.ps1`
and `Stage-Runtime.ps1` remain unchanged. Native ABI is still `0x00030000`, and the
native build manifest must identify v0.3.0 and the actual matching DLL hash. An
existing matching native build can be reused; neither launcher existence nor
launcher hash is a dependency of the new package/installed-layout checks.

The managed projects are now v0.3.1. Do not copy a preloader from net6.0 into Mono
or vice versa. Their filenames match, but the artifact roots are separate.
`Build.ps1` limits restore/build to the selected target framework.

Direct `dotnet build ... -p:DeployToGame=true` retains a complete deploy path: the
csproj calls the same root build script with `-StageOnly -Deploy` after both
projects compile. That mode does not recursively invoke dotnet. It assumes the
standard bin/<Configuration>/<TargetFramework> output layout.

## Existing local changes

The patch does not touch `native/`, `CMakeLists.txt`, `Build-Native.ps1`,
`Stage-Runtime.ps1`, `Build.Local.props`, NVIDIA binaries or existing build artifacts.
Your native compilation fixes and local SDK copies remain yours. Git may report a
conflict in a managed project/build script you also changed. Use `git apply --check`
and do not bypass conflicts by overwriting the whole repository.

The default game paths remain the previous v0.3 defaults. Both projects share the
root Build.Local.props. The build writes the evaluated game directory to a small
build-output file so packaging uses MSBuild's actual value, not a second script's
possibly different default or an ad-hoc XML parser.

## Installed layout

```
BepInEx/
  patchers/TGFG.Autoload.dll
  plugins/TGFrameGeneration/TGFrameGeneration.dll
  TGFrameGeneration/
    Install-manifest.json
    Autoload.log
    Native/
      TGFG.Native.dll
      TGFG.Native.log
      Streamline/<matching official NVIDIA runtime files>
```

The package assembler validates hashes, assembly identities, native PE machine
architecture and NVIDIA signatures. It only copies an allowlisted payload. The
installer backs up overwritten TGFG files and attempts rollback on copy failure.
It does not edit a game EXE, another mod, Doorstop configuration, Steam launch
settings, launch arguments, steam_appid.txt or existing TGFG configuration. Backup
folders and disable-autoload.txt are not removed during deployment.

`Build.ps1 -Deploy` installs Install-manifest.json; manual player ZIP extraction
can instead retain TGFG-Package-manifest.json in the game root for Test-Setup.ps1.
Neither manifest is needed for BepInEx to discover the preloader at game startup.
The generated distribution includes no launcher or developer scripts.

`Test-Setup.ps1` now checks the INSTALLED autoload layout. It does not require the
repo's native artifacts or launcher to exist on a player's computer. The old
`Launch-Game.ps1` now only checks installation and prints instructions to use
Steam; it does not start a process. The native launcher source remains available
for deliberate low-level development, but is not deployed by this workflow.

## Validation boundary

This is a repository integration and startup-packaging change, not a new renderer.
The original autoload preloader bootstrap implementation is retained, with its
version updated. It has not been compiled or run on Windows here. Native startup
can fail or occur too late. A successful bootstrap is not proof of a captured
swapchain or generated output. All v0.3 rendering limitations, including missing
game-specific HUD-less extraction and unverified RenoDX ordering, remain.

The latest supplied diagnostic predates this integration and showed v0.3.0 Mono
with no native DLL. It is not evidence of successful autoload timing.

## Primary references checked for this integration

- BepInEx 5 preloader contract; separate patcher/plugin assemblies:
  https://docs.bepinex.dev/articles/dev_guide/preloader_patchers.html
- BepInEx 6 BasePatcher lifecycle:
  https://docs.bepinex.dev/master/articles/dev_guide/preloader_patchers.html
- MSBuild ProjectReference ReferenceOutputAssembly semantics:
  https://learn.microsoft.com/en-us/visualstudio/msbuild/common-msbuild-project-items
