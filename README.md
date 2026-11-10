# TGGraphicsEnhancements

Graphics mods for Tainted Grail: The Fall of Avalon. Each mod adds its settings to the game's Video menu and
works on its own. They share TG Graphics Bridge, which comes bundled with each download.

| Mod | What it adds |
|---|---|
| TG Frame Generation | NVIDIA DLSS, AMD FSR and Intel XeSS frame generation, with low-latency controls. |
| TG Better Upscaling | FSR and XeSS in the game's Upscaling setting, vendor quality modes and a newer DLSS runtime. |
| TG Neural Rendering | DLSS 5 Neural Rendering, with controls for its lighting, colour and detail changes. |
| TG Path Tracing | A Mono-only prototype that replaces the game's lighting with traced light. Development is paused. |

## Status

The source is at version 1.2.4. Frame Generation, Better Upscaling, Neural Rendering and the bridge were tested
on both game versions with Unity 6000.3 on 30 October 2026. See the [changelog](CHANGELOG.md) and
[backlog](BACKLOG.md) for release work.

Path Tracing is paused and needs fixes to compile against Unity 6000.3. Its [status page](docs/PT-STATE.md)
records the implemented renderer, unfinished work and pending tests.

I have tested the mods on an RTX 5080. FSR 4 on supported AMD hardware, AMD Anti-Lag 2, Intel-specific features
and the community Neural Rendering runtime on older RTX cards have not yet been confirmed in game.
The [test notes](docs/TESTING.md) record what was checked and what remains unverified.

## Installation

Choose the download for your game: the Mono version uses BepInEx 5; the normal version uses BepInEx 6.
Install BepInEx, then extract the mod into the game folder. Vortex users should install it as an Engine Injector.
Keep matching versions when using several TG mods together.

See [installation and updates](docs/INSTALLING.md) for the folder layout and requirements.

## Settings and troubleshooting

Open Options > Video. FoA Mod Manager shows the same rows. Unavailable settings are greyed out; their tooltips
explain why. The [settings guide](docs/SETTINGS.md) covers quality modes, Neural Rendering controls and the
performance overlay.

For a bug report, use Create Troubleshooting Report under Performance Overlay and include the generated text
file from your BepInEx folder. The [diagnostics guide](docs/INSTALLING.md#troubleshooting) lists the log paths.

## Building and contributing

Build on Windows x64 with Visual Studio C++ tools, CMake and .NET. The [build guide](docs/BUILDING.md) lists the
steps; [dependencies](docs/BUILD-DEPENDENCIES.md) lists the separate SDK downloads.

Read [CONTRIBUTING.md](CONTRIBUTING.md) before changing rendering behaviour. [ARCHITECTURE.md](docs/ARCHITECTURE.md)
describes the shared bridge and rendering paths. [Development notes](docs/DEVELOPMENT.md) explain how the current
docs and private historical archive fit together.

## License

The source is [MIT-licensed](LICENSE). Vendor runtimes retain their own licenses and notices;
see [third-party components](LICENSES/THIRD_PARTY.md).

This software contains source code provided by NVIDIA Corporation.
