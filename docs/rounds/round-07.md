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
The owner then requested a commit before finalizing. Stage this spec, this round and only this inquiry's changes
to BACKLOG, PROJECT-CONTEXT and ROUNDS; the pre-existing PT and release changes stay outside that documentation commit.

## ▶ Starter prompt (R8)

Read BACKLOG and PROJECT-CONTEXT, then `docs/DLSS5-SPEC.md` if working on native DLSS 5. The owner requested the spec
for another agent to implement. Resolve authorized NVIDIA NR beta SDK/runtime access, D3D12 contracts and distribution
terms first; the spec contains an unsent inquiry. The current interface is a lifecycle API, not a render-stage
scheduler. NR must run before TGFG's HUD-less capture, and its search paths must exist at the first NGX initialization.
If returning to PT, read round-06's newest work and starter prompt; this round changed none of its code or deployment.
