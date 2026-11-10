# Neural Rendering references

Current integration uses the public DLSS/NGX API and an isolated optional compatibility adapter.
The full API/loading investigation is preserved in the local archive. Existing release policy bundles the
unmodified signed 310.9.16 model from RTX Remix 1.6.1, with its NVIDIA notice; modified models are supplied separately.

Primary references retained from the integration work:

- [NVIDIA DLSS 310.9.16](https://github.com/NVIDIA/DLSS/releases/tag/v310.9.16): NGX API and SR/RR runtime inputs.
- [RTX Remix 1.6.1](https://github.com/NVIDIAGameWorks/rtx-remix/releases/tag/remix-1.6.1): signed NR model source.
- [DXVK Remix](https://github.com/NVIDIAGameWorks/dxvk-remix): NR parameter/preset usage and upstream distribution information.
- [Sample integration](https://github.com/nvpro-samples/vk_gltf_renderer/blob/master/src/dlss.cpp): guide/tone-map/reset concepts; its helper is not our build dependency.

Model binaries, proprietary headers and game assemblies are not included in public source. Source MIT terms
do not replace vendor terms. [LICENSES/THIRD_PARTY.md](../LICENSES/THIRD_PARTY.md) retains the existing notices,
release-policy reasoning and outstanding licensing correspondence status; this cleanup makes no new clearance claim.

Verify signed/community loading, device support, evaluation and appearance separately on each tested setup.
