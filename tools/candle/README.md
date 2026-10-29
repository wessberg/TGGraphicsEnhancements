# Candle checks

Run from the worker copy. No script here launches or deploys the game.

- `cmd /c tools\candle\build.cmd`: native DLL and GPU test only, NRD off, SDKs read from main, outputs under `%TEMP%\tgb\candle-build`.
- `powershell -NoProfile -File tools\candle\gpu.ps1`: stage into the worker's own folder, hash, wait for main harness/game/GPU gate, run the new checks only.
- `powershell -NoProfile -File tools\candle\gpu.ps1 -Full`: same gate, entire GPU test. Run once at the end.
- `python tests/candle_transport_test.py`: independent half-space walks and numerical transport check of the shader inverse.
- `python tools/candle/albedo.py`: regenerate the inverse-fit coefficients. Prints them; no source is overwritten.
- `python tests/source_checks.py`: repository source checks.
- `powershell -NoProfile -File tools/candle/check-effects.ps1`: compile and run the effect state/restoration checks with the existing compiler and reference cache.

Managed build uses the existing net472 package cache and an empty local feed (no download); normal command is the brief's `dotnet build src/TGPathTracing/TGPathTracing.csproj -c Release -f net472 -p:RestoreIgnoreFailedSources=true`. Keep DeployToGame false.

The state/restoration test compiles `tests/candle_effects_test.cs` together with the real `src/TGPathTracing/EffectPasses.cs` against the cached .NET Framework 4.7.2 reference assemblies, then runs the resulting console exe. It uses a Unity API stand-in; the MRT/quad test in the GPU suite checks actual D3D11 output. Neither proves the game's shader variant selection: coordinator's candle/torch Off-On-Off run does that.

The first table-based native attempt hit pipeline startup timeout; `gpu-checks.log` is that failed attempt, not final evidence. `gpu-checks-final.log` uses the compact inverse. `gpu-full.log` will hold the full suite. See `docs/agents/candle.md` for the findings and unresolved gaps.

Do not merge build caches or log binaries as production files. `dotnet/`, `nuget-http/`, `empty-feed/` and `effects-test.exe` are disposable local validation files. The obsolete edit scripts have been removed.
