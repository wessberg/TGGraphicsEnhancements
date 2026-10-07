# Round 7 - 2026-10-07 - native DLSS 5: rights and feasibility

The owner asked whether a new DLSS 5 mod could use the same TG Graphics Bridge as Better Upscaling and Frame
Generation, remove the ReShade requirement, and let us control its render stage and game inputs. Scope: understand
whether it is allowed before implementation. This is a separate inquiry; round-06 still carries the ongoing PT work.

## Findings

- [RenoDX's public source license](https://github.com/clshortfuse/renodx/blob/main/LICENSE) is MIT: modification and
  redistribution are permitted with the copyright and permission notices. This applies to covered source; it does
  not by itself establish rights to a separately distributed DLSS 5 add-on or NVIDIA runtime. The public
  [add-ons directory](https://github.com/clshortfuse/renodx/tree/main/src/addons) inspected lists devkit, dlssfix and
  fpslimiter, not the neural-rendering add-on. No authoritative license for the exact DLSS 5 binary was established.
  Earlier project notes call it closed source; this inquiry does not establish that it has no license anywhere.
- [ReShade's license](https://github.com/crosire/reshade/blob/main/LICENSE.md) permits modifications and redistribution
  with notices. Its role as an add-on host is a technical dependency; it does not impose a general requirement that
  every independent DLSS integration use ReShade.
- [NVIDIA's RTX SDK license](https://github.com/NVIDIA/DLSS/blob/main/LICENSE.txt), sections 1-2 and 4, conditionally
  permits SDK use and application distribution in object code. It restricts reverse engineering, circumvention and
  standalone SDK redistribution. The supplement adds NVIDIA-GPU, credit, trademark and commercial-release
  notification conditions. This grants no demonstrated rights to a neural runtime obtained from another game's
  package or a community download. The actual NR package's terms must be checked.
- The [latest public DLSS release](https://github.com/NVIDIA/DLSS/releases/tag/v310.9.1) is still 310.9.1. Both the
  local SDK and [current public feature definitions](https://github.com/NVIDIA/DLSS/blob/main/include/nvsdk_ngx_defs.h)
  leave feature 18 reserved. However, NVIDIA's
  [renderer sample](https://github.com/nvpro-samples/vk_gltf_renderer/blob/master/src/dlss.hpp) has NR calls, explicitly
  requiring a beta NGX SDK. It describes NR after tone mapping, using an LDR image. This corrects the project's
  blanket claim that our own DLSS 5 integration is impossible; authorized feature-specific access is unresolved.

## Recommendation and limits

The owner prefers the direct NVIDIA beta NGX route, removing both RenoDX and ReShade. This is the preferred route
to investigate, conditional on SDK access and release rights. The inspected NVIDIA sample uses Vulkan; it proves
that a direct NGX integration is represented in official sample code, not that our D3D12 integration already works.

Our bridge already shares D3D11 textures with one D3D12 device; our mods capture color, depth and motion. That makes
an independent native integration plausible. Reusing those parts does not prove that the available NR runtime will
work on this device or that the inputs meet its requirements. The exact render stage, color format, motion convention,
history resets, masks and FG/RR ordering require the matching integration documentation and later testing.

Before substantial implementation, establish an authorized NVIDIA NR SDK/runtime source and its mod distribution
terms. If reusing RenoDX's particular implementation, establish the add-on's own license or obtain its author's
permission for a native port. An independent implementation would not need RenoDX code. NVIDIA lists
`nvidia-rtx-license-questions@nvidia.com` in license section 19 for licensing questions; nothing was sent.

No runtime downloaded, binary examined, code changed, build made, package changed or game launched. Only the backlog,
context correction and round notes were changed. Verification: `python tests/source_checks.py` passed all 88 checks.
These checks establish source consistency, not DLSS 5 compatibility or licensing clearance. No tests were added.

## Implementation spec requested

The owner: "investigate and confirm all these things, then write this down as a high-level spec, then I'll get
another agent to do the implementation". Delivered `docs/DLSS5-SPEC.md`: evidence, unresolved access/release questions,
the shared-device architecture, placement, inputs, settings, compatibility, acceptance and an unsent NVIDIA inquiry.

Additional findings: NVIDIA's sample CMake checks a beta-only NR helper header and shows a local internal beta3
package override, but supplies no public download for that package. Its Vulkan adapter wires real depth/motion,
resets and masks. The sample is evidence of a direct interface, not clearance or proof of a D3D12 implementation.
No authoritative mod beta-access or NR redistribution grant was obtained. A public forum access question is not
an NVIDIA answer; the general NGX landing page now redirects to AI for Media, not an NR SDK request flow.

Local integration constraints confirmed from code: the bridge extension ABI has lifecycle/device callbacks, no
common post-effects scheduler. TGFG initializes the shared NGX session first and registers TGBU's DLSS folder only
under the existing TGBU/TGPT condition. A new NR path must be known before that init. Its current HUD-less capture
must be ordered after NR; otherwise changed scene pixels may be mistaken for UI. The current capture is triggered
by FG, so a standalone NR installation needs its own activation. Existing DLL-set and motion-orientation rulings
remain settled. No code, runtime, package, installed file or running agent changed.

The spec distinguishes facts from proposed behavior. SDK eligibility, the actual D3D12 input/lifecycle contract and
release rights require the authorized package or a NVIDIA response. No external message was sent. Source checks
passed 88/88 on the current working tree; this is source consistency, not a licensing or GPU acceptance test.
The owner then requested a commit before finalizing. Initial docs committed as `5b014d3`; pre-existing PT and
release changes stayed outside that documentation commit.

## Follow-up: public SDK and player-supplied runtime

The owner questioned the license and the supposed need to contact NVIDIA. The earlier wording conflated beta
access with redistribution permission. The standard SDK license already grants conditional application distribution;
beta labeling alone is not a blanket redistribution ban. The owner then ruled: "These are all parts of your job,
not the implementers. Your task is to thoroughly research these things. If it ends up being that people need to
brign their own .dll for the neural rendering part of the sdk, so be it".

Finished the public-source investigation in `docs/DLSS5-RESEARCH.md` and revised the spec. The public SDK has generic
D3D12 NGX dispatch and slot 18. Beta helper headers are not inherently required to write an independent adapter.
The owner accepts shipping our integration separately from a supplied `nvngx_dlssnr.dll`. Defined its placement,
discovery, signature/version handling, initial search-path registration and missing-file alert. BYO does not make
the model part of our package, but does not itself grant runtime use or permission for a caller workaround.

Inspected actual source at pinned revisions: UniversalDLSS5 `e47518a74ff539df2a16c71f8aa5c0b7038e8c5a`, UnityDLSSNR
`4781026526ead653e0c594fbbc66c3a210489291`, OptiScaler-Susemi `3def828dadbd79f887996a6733ae261abe0e669f`.
Their working independent paths include caller naming/import measures or fallback to them. A valid NVIDIA signature,
a signed file on disk, or no separate helper DLL is not proof of normal NGX loading without circumvention.
The normal driver-core candidate exists in source, but an older investigation reports failed creation. No normal
create/evaluate success was measured here. Public SDK versus model-export signatures must not be confused; private
parameter-vtable offsets and another game's identity are excluded from the candidate.

Verified official release metadata: DLSS 310.9.1 and Streamline 2.14.1, both published September 8. No public NR
package/access policy established. A read-only search of this PC's NVIDIA DriverStore directories found no
`nvngx_dlssnr.dll`, correcting reliance on community claims that the driver necessarily contains it. No model
download, proprietary binary analysis, GPU probe, source implementation, package change or game run was performed.
Public code was read in memory; no third-party implementation code was copied into tracked files. SDK license
section 4(d) leaves caller workarounds uncleared. No external contact was sent or implied to have answered.

Follow-up verification: `python tests/source_checks.py` passed all 89 checks on the current tree; the earlier run
had 88. This is source consistency, not an NR GPU or licensing verdict.

The implementation handoff now specifies public NGX plus BYO as a concrete candidate, with beta integration optional.
The implementation agent measures discovery/create/evaluate/image correctness on the shared device; it is not
assigned another access/licensing survey. A failed ordinary call must stay a recorded failure, never become an
undisclosed compatibility fallback. Outstanding runtime-use permission is an external evidence limit, not a claim
that the research found a cleared working backend. All sources, decisions and limits are in the research/spec.

## ▶ Starter prompt (R8)

Read BACKLOG and PROJECT-CONTEXT, then `docs/DLSS5-SPEC.md` if working on native DLSS 5. The owner requested the spec
for another agent to implement and accepts a supplied NR DLL. Read `docs/DLSS5-RESEARCH.md`; beta helpers are optional.
Implement the public-NGX candidate with the specified BYO layout; do not repeat access research or silently adopt
an uncleared caller workaround. The current interface is lifecycle, not render scheduling. NR precedes TGFG's
HUD-less capture and registers its path before first NGX initialization. No NR runtime success has been measured.
If returning to PT, read round-06's newest work and starter prompt; this round changed none of its code or deployment.
