# Build dependencies

Pinned baseline: 2026-10-30, version 1.2.4. Current build scripts/configuration take precedence over historical
reference links in SOURCES.md. SDK downloads remain local and ignored; none belongs in public source.

| Component | Local input | Used by |
|---|---|---|
| Streamline headers | Streamline-2.14.16 | FG and NGX declarations |
| Streamline production runtime | Streamline-2.14.16-bin | Signed runtime staging |
| DLSS | DLSS-SDK-310.9.16 | NGX headers/static library, SR/RR runtime staging |
| FidelityFX | FidelityFX-SDK-2.4.0 | FSR FG/SR/RRG headers and signed runtimes |
| XeSS | XeSS-SDK-3.0.2 | XeSS SR/FG and XeLL |
| Anti-Lag2 | AntiLag2-SDK-2.0.0a | FG header |
| DXC | DXC-1.9.2607 | PT SM6.9 shader compilation |
| Agility | Agility-SDK-1.619.6 | PT DXR1.2 runtime |
| RTXDI | RTXDI-Library-3.1 | PT native/shader integration |
| NRD | NRD-SDK-4.17.3 | PT embedded denoiser |
| NVAPI | NVAPI-R615 | PT Mega Geometry |
| Neural model | DLSS-NR-Runtime | Signed 310.9.16 staging input, not source SDK |

CMake fetches MinHook v1.3.4; Mega Geometry also fetches meshoptimizer v1.3. NRD has its own MathLib,
ShaderMake and compiler dependencies. The game/BepInEx/Unity assemblies are taken from local installations.
Read [README.md](../README.md), [SOURCES.md](SOURCES.md) and [third-party notices](../LICENSES/THIRD_PARTY.md).

## Superseded local downloads

DLSS 310.9.1, Streamline 2.14.1 source/runtime and FidelityFX 2.3.0 were moved out of the source root into
the ignored `.development/retired/2026-11-10/` archive. Two obsolete build trees (`build/nr`, `build/native-nrd`)
and five extracted SDK ZIP downloads were moved with them. The current SDKs and `build/native` remain in place.

Generated caches are disposable output, not dependency requirements. Their references do not justify keeping
an obsolete SDK. Three NR mutation scripts still named the old DLSS SDK; they now use 310.9.16. The inventory
reports cache references as information only. No rendering build or GPU mutation run was performed.

RTXDI/NRD/NVAPI/DXC/Agility are not obsolete merely because managed PT is tabled: the native CMake graph
still builds PT targets/shaders. Build.ps1 -Skip TGPathTracing skips managed build work; it does not split
the native dependency graph.

Run the read-only inventory:

```powershell
python tools/maintenance/audit_sdks.py
python tools/maintenance/audit_sdks.py --json
```

The retirement move preserves the old files privately, without changing vendor code, rebuilding shaders or
touching installed game files. Its `move-manifest.json` records their original and archived locations.
