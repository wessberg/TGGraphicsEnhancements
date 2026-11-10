# Neural Rendering compatibility

Signed SDK runtimes use normal NVIDIA dispatch. Compatible modified/community runtimes automatically select
the isolated helper; no hash/version allowlist. The helper checks x64, direct exports/API and reported support,
holds the model file against replacement during validation/loading, and shares bridge device/NGX synchronization.

A byte-identical caller-name control reproduced the community runtime requirement. Earlier Available=0
was not proof of hardware refusal or successful DLL loading: NGX signature validation had stopped it earlier.
Keep loading, model support and successful evaluation as separate diagnostic states.

Official/community standalone rendering and sharing with RR were checked on RTX 5080. Official in-game
normal/Mono combinations were checked; this does not establish community execution on RTX 20/30/40-series.
Never advertise that hardware as validated from a5080 result or from absence of a series gate in our code.

ReShade/RenoDX HDR output was measured separately from the RenoDX DLSS 5 add-on. Unity's HDR flag can stay false
while RenoDX owns an FP16 swapchain, so it is not sufficient to detect that output path.
Do not infer active installation from a leftover mod/config directory; read BepInEx Loading lines.

The signed model is bundled in the current release policy. Reapply a modified model after updating the package.
See [validation](DLSS5-VALIDATION.md), [research](DLSS5-RESEARCH.md) and existing third-party notices.
