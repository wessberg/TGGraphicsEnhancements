# Neural Rendering design

TG Neural Rendering evaluates a DLSS Neural Rendering model on the bridge's shared D3D12 device, independently
of ReShade/RenoDX. Current source version 1.2.4. The signed 310.9.16 runtime is bundled under the existing
release policy; see [THIRD_PARTY.md](../LICENSES/THIRD_PARTY.md). Compatible modified runtimes use a separate
automatic adapter, without a per-file allowlist. Runtime capability checks remain authoritative.

Preserve the selected before/after-upscaling route and explicit handoff to FG. The interface and HUD must stay
outside model processing. Actual model colour, depth, motion, camera, exposure and resets must describe the
same image/frame; PT changes these guides and must publish its own data. One bridge device and NGX lock are
shared with RR. Do not add another presentation owner.

Quality/Performance, strength and advanced appearance/pass controls retain their measured semantics.
Unsupported controls or hardware capabilities must not be invented from version/GPU tables. No extra temporal
filter over NR. Neutral mode must preserve the source image; failures fall back safely and release resources.

[IMPLEMENTATION](DLSS5-IMPLEMENTATION.md), [COMPATIBILITY](DLSS5-COMPATIBILITY.md),
[VALIDATION](DLSS5-VALIDATION.md) and [feedback index](DLSS5-FEEDBACK.md) distinguish implementation from evidence.
The complete earlier design alternatives remain in the local archive.
