# Native DLSS 5 research - public NGX and a player-supplied runtime

Reviewed 2026-10-07. This continues round 7 after the owner rejected handing SDK/runtime research to the implementer
and explicitly accepted a player-supplied `nvngx_dlssnr.dll`. The implementation design is [DLSS5-SPEC.md](DLSS5-SPEC.md).

## Conclusions

- An independent integration can be written against public NGX. A beta helper header is not inherently necessary
  for calling its generic feature API. This corrects the earlier spec's mandatory-beta assumption.
- NR still needs NVIDIA's proprietary model/runtime. The public packages examined do not provide it. Deliver our
  integration separately and support a player-supplied runtime; do not bundle or automatically retrieve a community copy.
- ReShade and RenoDX are not intrinsic requirements. Public projects demonstrate standalone calls, but the working
  examples examined also use caller compatibility measures. Their existence is evidence of technical feasibility,
  not evidence of an unrestricted, supported NVIDIA integration.
- The best candidate for this repo is the normal NGX driver-core route on the existing shared D3D12 device. Its
  success with a genuine NR DLL remains unmeasured here. Do not promise that removing the beta requirement resolves
  initialization, ABI compatibility or runtime rights.
- Player supply removes our redistribution of the model. It does not itself authorize the player's source/use or
  resolve a restriction on circumventing the runtime's caller check. No blanket legal clearance was established.

## Public SDK and download findings

The public [NGX header](https://github.com/NVIDIA/DLSS/blob/v310.9.1/include/nvsdk_ngx.h) supplies project-ID initialization,
feature requirements, parameter allocation, scratch queries, creation, evaluation and release. It distinguishes
SDK-to-driver and driver-to-model function signatures. Use the SDK side; similarly named model exports are not
interchangeable. Its thread-safety warning applies across consumers sharing a session.

The [feature definitions](https://github.com/NVIDIA/DLSS/blob/v310.9.1/include/nvsdk_ngx_defs.h) reserve identifier 18.
Community code uses that slot for NR. This is a version-specific interface observation, not an official NR support
declaration. Keep the mapping in one backend adapter. Do not reinterpret any other reserved feature as NR.

GitHub release metadata independently checked during this follow-up:

| Official release | Published | Listed assets | NR finding |
| --- | --- | --- | --- |
| [DLSS 310.9.1](https://github.com/NVIDIA/DLSS/releases/tag/v310.9.1) | 2026-09-08 | Windows and Linux sample archives, plus GitHub source archives | No NR package advertised. Targeted inspection of installed public SDK files found no NR helper/model. |
| [Streamline 2.14.1](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.14.1) | 2026-09-08 | x64, aarch64 and arm64ec SDK archives, plus source archives | A feature enum alone does not supply an NR plugin. No matching plugin/helper/model found locally. |

NVIDIA's [developer download page](https://developer.nvidia.com/rtx/dlss) and
[Streamline access page](https://developer.nvidia.com/rtx/streamline/get-started) did not establish a public NR beta
download or an independent-mod beta admission policy. This is a search result, not proof that nobody can obtain it.
NVIDIA's [sample build](https://github.com/nvpro-samples/vk_gltf_renderer/blob/master/CMakeLists.txt) expects a separate
beta helper and illustrates a private package override. That sample's build requirement is not a universal
requirement for an independently written generic-NGX adapter.

Some community READMEs say the model comes with NVIDIA drivers. A read-only search for `nvngx_dlssnr.dll` under this
PC's NVIDIA DriverStore directories returned no files. Do not instruct players that installing the current driver
necessarily supplies NR. No driver installer was downloaded or unpacked, so this does not establish the contents
of every driver distribution.

## What the public implementations actually do

Inspected source, not just project descriptions. The revisions below were obtained from GitHub tree metadata.
No implementation code was copied into tracked source and none of these projects was built or run here.

| Project/revision | Observed path | Consequence for this mod |
| --- | --- | --- |
| [UniversalDLSS5](https://github.com/MotionflowOffical/UniversalDLSS5/tree/e47518a74ff539df2a16c71f8aa5c0b7038e8c5a), `e47518a74ff539df2a16c71f8aa5c0b7038e8c5a` | Public SDK headers; [NR backend](https://github.com/MotionflowOffical/UniversalDLSS5/blob/e47518a74ff539df2a16c71f8aa5c0b7038e8c5a/src/neural/ngx_nr.cpp) binds model exports and prefers a caller forwarder. It uses a fixed snippet application ID. | Demonstrates an independent adapter, but cannot be cited as successful ordinary project-ID NGX dispatch. Its extra device/presentation architecture is unsuitable for our shared bridge. |
| [UnityDLSSNR runtime](https://github.com/Kuan-Mi/UnityDLSSNR/blob/4781026526ead653e0c594fbbc66c3a210489291/RenderingPlugin/Source/DlssNrRuntime.cpp), `4781026526ead653e0c594fbbc66c3a210489291` | Loads model exports, uses a fixed ID/API version, and intercepts a Windows module-path query to satisfy the caller check. | A Unity integration exists, but it is not proof of a permitted ordinary NGX route. No applicable license for this project's own integration source was found in the inspected tree; copy nothing. |
| [OptiScaler-Susemi proxy](https://github.com/grim-susemi/OptiScaler-Susemi/blob/3def828dadbd79f887996a6733ae261abe0e669f/OptiScaler/dlssnr/DlssNr_Proxy.cpp), `3def828dadbd79f887996a6733ae261abe0e669f` | Tries driver-core create/evaluate first. On creation failure it can select a [compatibility runtime](https://github.com/grim-susemi/OptiScaler-Susemi/blob/3def828dadbd79f887996a6733ae261abe0e669f/OptiScaler/dlssnr/DlssNr_CompatibilityRuntime.cpp) that intercepts caller-path imports. | A successful session may be the fallback. Logs must identify which path ran; absence of a helper DLL is not absence of a bypass. The source is [GPLv3](https://github.com/grim-susemi/OptiScaler-Susemi/blob/3def828dadbd79f887996a6733ae261abe0e669f/LICENSE), not a permissive drop-in. |
| [OptiScaler direct-core investigation](https://github.com/Dagherbou/OptiScaler_DLSSNR/blob/dlss-neural-rendering/OptiScaler/dlssnr/FORWARDER_INVESTIGATION.md) | Reports the core recognizing NR but failing creation with `0xBAD0000B`; repeated warm-up attempts did not resolve it. | Recognition is not successful creation. An older report is not proof that all driver/runtime combinations fail. |
| [Video player](https://github.com/Zonnery/dlss5-nr-player) | Its README requires a caller-validation shim and externally supplied binaries. | Evidence for direct model calls, not ordinary NGX-loader success. Its broad claim that NVIDIA binaries cannot be redistributed is not our licensing authority. |

[UniversalDLSS5's license](https://github.com/MotionflowOffical/UniversalDLSS5/blob/e47518a74ff539df2a16c71f8aa5c0b7038e8c5a/LICENSE)
is Apache-2.0. That grants rights to covered project code, not NVIDIA's model or exemptions from its conditions.
[RenoDX's MIT license](https://github.com/clshortfuse/renodx/blob/main/LICENSE) similarly does not establish coverage
of a separately distributed NR add-on. This design independently implements the adapter and color handling.

## Licensing findings and limits

Under the [RTX SDK license](https://github.com/NVIDIA/DLSS/blob/v310.9.1/LICENSE.txt), section 1(c) grants compiled
application redistribution, conditional on section 2. Section 4 prohibits standalone SDK distribution, reverse
engineering and circumvention of technical restrictions. Section 6 cautions about prerelease use; it is not an
express blanket redistribution ban. Separate update/package terms can govern.

The supplement restricts NGX/DLSS to NVIDIA-GPU systems, requires applicable credits/marks, and addresses release
notification, including plugins to commercial applications. Branding approval and notification are distinct from
a special redistribution exception. Free distribution is not an automatic exemption from every condition.

Therefore standard SDK integration does not automatically require a new permission grant. Verify the particular
runtime's terms before bundling it. BYO delivery omits that runtime from our package but leaves use restrictions
intact. Our reading is that caller spoofing or import changes to defeat the check conflict with section 4(d);
this is an inference, not an NVIDIA ruling. The reviewed materials supply no exemption. Section 19 provides the contact
for clarification if that route is pursued. No inquiry was sent.

A file copied from an owned game is not automatically SDK-licensed for unrelated use. For example,
[Take-Two's terms](https://www.take2games.com/legal/en-US/) limit copying and modification, subject to applicable
exceptions. No reviewed game notice supplies a transferable NR integration grant. A community mirror, a file hash,
an intact NVIDIA signature or somebody else distributing a DLL does not establish such a grant.

## Candidate public-NGX contract

The public SDK provides the dispatch mechanism; the NR-specific mapping is currently unofficial. The
[community proxy](https://github.com/grim-susemi/OptiScaler-Susemi/blob/3def828dadbd79f887996a6733ae261abe0e669f/OptiScaler/dlssnr/DlssNr_Proxy.cpp)
binds color, output, depth, motion, reset, depth direction, individual subrects and motion scale. It supplies
settings at creation and retires handles after GPU completion. Treat this as an observed version-specific mapping,
not a guarantee that every genuine runtime accepts it. Never transplant private parameter-vtable offsets.

NVIDIA's [Vulkan adapter](https://github.com/nvpro-samples/vk_gltf_renderer/blob/master/src/dlss.cpp) independently
corroborates these input concepts and adds a control mask and global tone strength. It uses display-resolution
color/output with potentially lower-resolution guides, declares the depth convention, and scales motion for its
own guide encoding. These values cannot be copied blindly into Unity. In particular, format conversion alone does
not establish the motion convention. Its controls being evaluated per frame does not settle what the older
community runtime latches at creation; recreate on tuning changes where that build requires it.

First candidate: SDK-to-driver calls with our project identity, the supplied model folder in the initial path list,
owned capability/parameter maps and appropriate scratch/resources. Serialize shared calls. Query feature
requirements if that driver supports the query; record unsupported-query separately from create/evaluate failure.
Submit creation work and honor GPU completion before evaluation. Do not fall back to a spoofed caller or game ID.

Evidence must distinguish four stages: model discovery, feature creation, successful GPU evaluation and a valid
changed image. Capture source/output with neutral and nonzero strength and moving guides. A success code, a loaded
module or a source-wiring test alone does not demonstrate the desired result. No supplied NR DLL was available
for that measurement in this research.

## Delivery choice and remaining uncertainty

The owner's accepted fallback is **our native integration plus a player-supplied NR DLL**. This is now a specified
delivery mode, not an implementer's licensing research assignment. Prefer genuine NVIDIA-signed runtime builds;
the existing bridge rejects unsigned vendor files. Community architecture-patched DLLs cannot pass that loader
unchanged, and no weakening of that policy was authorized by accepting BYO delivery.

Discover a supplied DLL in the defined mod folder or an existing game installation, validate it and register its
folder before shared NGX initialization. No ReShade setup, model rehosting, automatic community download or renamed
game executable. A missing/incompatible file affects only NR. Give one exact placement instruction on its row.

Unresolved externally: a publicly obtainable NR package/use grant and any permission for caller compatibility.
Unmeasured implementation behavior: whether the genuine runtime works through normal NGX on our shared device,
its exact accepted formats/settings and resulting picture/performance. These are explicit limits of the evidence.
The implementation agent should implement and measure the stated candidate, not repeat this access/license survey.
If ordinary loading fails, retain that result; do not silently substitute the community bypass and call it cleared.
