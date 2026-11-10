# Neural Rendering compatibility

Signed SDK runtimes use normal NVIDIA dispatch. Compatible modified/community runtimes automatically select
the isolated helper; no hash/version allowlist. The helper checks x64, direct exports/API and reported support,
holds the model file against replacement during validation/loading, and shares bridge device/NGX synchronization.

A byte-identical caller-name control reproduced the community runtime requirement. In the earlier Available=0 case, NGX signature validation stopped loading before hardware support could be checked.
Keep loading, model support and successful evaluation as separate diagnostic states.

Official/community standalone rendering and sharing with RR were checked on RTX 5080. Official in-game normal/Mono combinations were checked. Community execution on RTX 20/30/40-series still
needs tests on those cards; the 5080 tests cover only that GPU.

ReShade/RenoDX HDR output was measured separately from the RenoDX DLSS 5 add-on. Unity's HDR flag can stay false
while RenoDX owns an FP16 swapchain, so it is not sufficient to detect that output path.
Do not infer active installation from a leftover mod/config directory; read BepInEx Loading lines.

The signed model is bundled in the current release policy. Reapply a modified model after updating the package.
See [validation](DLSS5-VALIDATION.md), [research](DLSS5-RESEARCH.md) and existing third-party notices.
