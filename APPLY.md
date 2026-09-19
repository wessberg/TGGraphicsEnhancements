# Integrate autoload into the existing TGFrameGeneration repository

This is an in-place source patch, not another checkout or standalone mod project.
It is based on the supplied TGFrameGeneration v0.3.0 DX11 source archive. The
user's current local working tree was not fetched. Existing local edits in a
changed hunk may need manual merging; the preflight command stops on conflicts.

## Apply

Extract `integrate-autoload.patch` beside your existing `Build.ps1`, then run from
that repository root:

```powershell
git apply --check --ignore-space-change .\integrate-autoload.patch
if ($LASTEXITCODE -ne 0) { throw 'Patch conflicts with local changes. No files were modified; inspect and merge the conflicting hunks.' }
git apply --ignore-space-change .\integrate-autoload.patch
if ($LASTEXITCODE -ne 0) { throw 'Patch was not applied. Inspect the Git error before continuing.' }
```

Do not use a full-folder overwrite or `git reset --hard` to resolve conflicts.
Review the result with `git diff`. Added files appear as untracked files until you
stage them. A new file you already created (for example TGFrameGeneration.sln or
src/TGFG.Autoload) can also cause a safe conflict. This patch does not modify
native sources, SDK staging scripts, native build artifacts or Build.Local.props.

## Build and deploy using the existing command

Close FoA. Your existing native v0.3.0 build and staged NVIDIA runtime under
`artifacts/Native` are reused; no native rebuild is required for this integration.

```powershell
.\Build.ps1 -Runtime Mono `
  -GameDirMono 'C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA' `
  -Deploy
```

This builds the gameplay plugin AND preloader, packages the existing native
backend/runtime, backs up TGFG files it replaces, and deploys the complete mod.
Afterward, press Play in Steam normally. No TGFG launcher or launch arguments
are part of the normal workflow. Unity still needs to run on DX11 for this backend;
autoload does not change the renderer setting or force a renderer switch.

For both runtimes, configure the existing Build.Local.props and use:

```powershell
.\Build.ps1 -Runtime Both -Package
```

The resulting separate player ZIPs are in `artifacts/Packages`. Review NVIDIA's
runtime distribution terms before publishing a package with their DLLs.

## Files/build graph

```
TGFrameGeneration.sln
src/TGFrameGeneration/TGFrameGeneration.csproj
  -> build-only ProjectReference to ../TGFG.Autoload/TGFG.Autoload.csproj
src/TGFG.Autoload/
  TGFG.Autoload.csproj
  Patcher.cs
  Bootstrap.cs
Build.ps1                  builds/stages both managed DLLs
Package.ps1                stages a complete ZIP and optionally deploys
Test-Setup.ps1             validates the installed automatic-loading layout
```

Both projects use root Build.Local.props and matching net472/net6.0 targets. They
remain separate DLLs because preloader and gameplay initialization are different
lifecycles. There is no reference from the preloader to Unity or the gameplay
plugin. Build-Autoload.ps1 and a separate ProjectRoot are no longer needed.

The managed version becomes 0.3.1; native stays 0.3.0 with ABI 0x00030000. The
existing settings UI and rendering source are retained. Diagnostics now include
an autoload log tail and no longer instruct players to use the old launcher.

Installed paths:

```
BepInEx/patchers/TGFG.Autoload.dll
BepInEx/plugins/TGFrameGeneration/TGFrameGeneration.dll
BepInEx/TGFrameGeneration/Native/TGFG.Native.dll
BepInEx/TGFrameGeneration/Native/Streamline/...
```

The existing EXE, Doorstop, ReShade/RenoDX proxies, Steam settings, app-ID files
and mod configuration are not modified. Autoload's disable marker is preserved.
A separate log records startup in the actual Steam game process:

```
BepInEx/TGFrameGeneration/Autoload.log
BepInEx/TGFrameGeneration/Native/TGFG.Native.log
```

## Verification performed here

- 26 Python source/build-graph consistency checks passed (14 existing + 12 new).
- Both existing portable C++ suites passed (80 and 61 checks).
- Git dry-run/application reproduces the integrated tree on the supplied baseline.
- Synthetic local native, camera and Build.Local.props edits survive application.
- Conflicting build edits are rejected without changing files.
- No native source, CMake project or native SDK build/staging script changed.

No .NET/Windows compilation, PowerShell execution, Steam startup or GPU test was
performed here. This archive contains source changes, not compiled DLLs. The
preloader timing and rendering limitations from the previous prototypes remain.
The default build runs a PowerShell syntax check locally. Optional filesystem
checks, with binary/signature validations explicitly mocked, are included at
`tests/package_tests.ps1`; they are not GPU or actual-signature validation.

Start Off for the first normal Steam run. Successful DLL loading alone does not
establish that the presentation bridge is operating or that FG works.
