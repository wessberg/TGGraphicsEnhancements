# Review: the denoiser integrations against NVIDIA's and AMD's guides (GPT-6-astra, xhigh, read-only, 2026-10-11 ~15:20)

Prompt: the coordinator's, after the owner's ruling "Obviously we want to 100% follow NVIDIA's and AMD's implementation guidelines for these technologies" (PT-127). Reviewed tree: the NRD trial build of 15:01 (native 813BC4C511E0). Findings are from source, not measured; test each before accepting it.

No. The new NRD trial fixes important parts of the old integration, but it still has incorrect inputs. RR and AMD Ray Regeneration also have concrete input errors.

This was a read-only source review. I did not build or run the game. The files changed during the review: I rechecked the newly added conversion of NRD history from seconds to frames.

Labels below distinguish **(a) bugs affecting the new NRD trial**, **(b) other departures**, and **(c) recommended or optional features missing**. Consequences are source-based findings, not measured visual results.

Guide references use these abbreviations:

- **NRD README:** `NRD-SDK-4.17.3/README.md`
- **NRD settings:** `NRD-SDK-4.17.3/Include/NRDSettings.h`
- **NRD shader:** `NRD-SDK-4.17.3/Shaders/NRD.hlsli`
- **RR guide:** `%TEMP%\tgb\vendor-docs\dlss-rr-guide.txt`, using its printed page numbers.
- **AMD guide:** `FidelityFX-SDK-2.3.0/Kits/FidelityFX/docs/techniques/denoising.md`

1. **(a) NRD kinds 1/2: the diffuse hit distance is not prepared as NVIDIA requires for ReSTIR.**

   **Guide:** NRD README:495–516 requires a distance representative of the diffuse lobe. It explicitly rejects simply forwarding the distance of the sample ReSTIR selected. It describes probabilistic acceptance/rejection, with an independently valid distance as fallback.

   **Code:** `pt_gi_spatial.hlsl:45–55` takes the selected reservoir’s distance. [pt_nrd_prep.hlsl:201](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_nrd_prep.hlsl:201) forwards it without that filtering. Additional rays can change the RGB result without changing this distance at `pt_gi_spatial.hlsl:81–86`.

   **Effect:** REBLUR receives a misleading guide for contact and spatial filtering. RELAX’s disabled diffuse prepass masks part of this problem; it does not make the input compliant.

   **Fix:** retain a valid cosine-sampled distance, implement the documented ReSTIR acceptance/rejection, and use that retained distance when the selected reservoir distance is rejected.

2. **(b) RR: colored specular reflectance is discarded.**

   **Guide:** RR guide §3.4.2, pp.13–14, requires RGB, view-dependent specular reflectance. Appendix A, p.36, provides an RGB approximation.

   **Code:** [pt_compose.hlsl:52](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_compose.hlsl:52) averages the three specular channels into one number. It stores that number in `g1.a`. `d3d11.hlsl:456` reconstructs three identical channels.

   **Effect:** RR sees grey reflectance for colored metals. That can change how it separates material color from lighting and reconstructs reflections.

   **Fix:** carry all three specular-albedo channels through the tracer’s guide outputs. The final RGB guide texture already exists; the intermediate packing loses the information.

3. **(b) RR: hair normals can describe a different surface from depth, motion and hit distance.**

   **Guide:** RR guide §3.4.1–3.4.3, pp.13–15, requires coherent surface guides when RR computes specular motion. Without application-provided specular motion, normal-guide blending is disallowed.

   **Code:** `pt_primary.hlsli:508–520` writes guides for the first hair card regardless of whether the stochastic camera ray stopped there. [pt_compose.hlsl:41–57](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_compose.hlsl:41) selects a normal by coverage. Depth and motion at lines 75–80 describe the actual sampled surface; specular distance at line 256 belongs to that surface’s path.

   **Effect:** one RR pixel can contain the wall’s depth and reflection distance but the hair’s normal. Reflection reprojection then has no consistent geometry.

   **Fix:** choose one guide surface and make normal, depth, motion and specular distance describe it together. Stable blended albedo alone does not solve this.

4. **(b) RR: required reflection information is treated as disposable.**

   **Guide:** RR guide §3.4.8–3.4.9, pp.16–17, provides two alternatives: specular motion vectors, or specular hit distance with the required matrices, normals and depth.

   **Code:** [rr_upscaler.cpp:147](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/upscaling/src/rr_upscaler.cpp:147) requires both hit distance and the optional transparency snapshot before supplying either. Lines 167–172 drop both after an evaluation failure. `upscaler.cpp:332–352` and `StpHost.cs:584–585` have similar all-or-nothing handling. No specular motion vectors replace the lost distance.

   **Effect:** RR can continue without either documented reflection-motion input.

   **Fix:** handle hit distance independently from the optional transparency guide. A failure involving transparency must not silently remove required reflection information.

5. **(b) AMD: motion-vector Z ignores object motion.**

   **Guide:** AMD guide:927–966 and `ffx_denoiser.h:185–190,215–228` require the previous surface’s signed view-depth minus its current signed view-depth.

   **Code:** [pt_rrg_guides.hlsl:31–38](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_rrg_guides.hlsl:31) projects the current world position through the previous camera matrix. That accounts for camera movement, but not the surface moving. Meanwhile, XY uses `motion_at`, whose producer handles previous vertices and transforms in `pt_primary.hlsli:300–327`.

   **Effect:** moving characters, cloth and vegetation get internally inconsistent motion guides. History rejection and reprojection can fail.

   **Fix:** retain the previous position already calculated for XY motion and derive Z motion from that same position.

6. **(b) AMD: radiance units change while its history remains alive.**

   **Guide:** AMD guide:525–552 defines temporal inputs as radiance signals. The dispatch contract provides no exposure/history-rescaling factor.

   **Code:** `tracer.cpp:3224–3242` changes `st.reference` as lighting changes. The signal shaders store radiance divided by that reference. [tracer.cpp:3934](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/src/tracer.cpp:3934) passes those changing units directly to AMD. NRD explicitly removes this changing scale at lines 3955–3969; AMD does not.

   **Effect:** old and new samples can represent different amounts of physical light. The denoiser can interpret a storage-scale change as a lighting change.

   **Fix:** give AMD a stable radiance scale, with matching conversion back afterward. Reset its history whenever that scale changes.

7. **(b) AMD: direct specular lighting is submitted as indirect specular lighting.**

   **Guide:** AMD guide:525–552 and `ffx_denoiser.h:353–435` define separate direct-specular and indirect-specular signals. Only the indirect signal carries an indirect ray distance.

   **Code:** `pt_di_spatial.hlsl:98` seeds the specular texture with direct highlights. `pt_specular.hlsl:206` adds indirect reflections. [ray_regeneration.cpp:95](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/src/ray_regeneration.cpp:95) enables only direct diffuse, indirect diffuse and indirect specular; lines 125–129 submit the combined specular texture as indirect.

   **Effect:** direct highlights are filtered using an unrelated reflection distance.

   **Fix:** preserve separate direct and indirect specular buffers, enable `FFX_DENOISER_SIGNAL_DIRECT_SPECULAR`, and submit its matching dispatch structure. NRD’s documented combined-specular rule does not establish the same contract for AMD.

8. **(a) NRD kinds 1/2: the specular distance producer still contains fabricated distances and a different sampling rule.**

   **Guide:** NRD README:495–515 requires a genuine in-lobe hit distance and recommends its VNDF v3 sampling baseline.

   **Code:** [pt_specular.hlsl:99–101](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_specular.hlsl:99) sets distance to `1.0` when rough reflections come from the cache. Lines 71–76 use the older GGX sampler followed by manually reflecting directions above the geometric surface. `pt_nrd_prep.hlsl:202` forwards the result.

   **Effect:** the cache branch supplies no measured reflection geometry. The manually altered sampling distribution also needs reconciliation with NVIDIA’s in-lobe requirement.

   **Fix:** generate the distance from a real, appropriate lobe ray even when cached radiance supplies its lighting. Replace or correctly account for the direction adjustment. This producer also feeds RR and AMD.

9. **(a) NRD kinds 1/2: the claimed `[0,250]` bound is a luminance bound, and excess lighting bypasses denoising.**

   **Guide:** NRD README:809 recommends positive RGB in a manageable range, with scaling or other compression for wider ranges.

   **Code:** [pt_nrd_prep.hlsl:191–199](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_nrd_prep.hlsl:191) limits luminance, not the largest RGB component. For example, blue `(0,0,1000)` passes that test. Lines 141–144 and `pt_compose.hlsl:240–246` restore the excluded portion raw.

   **Effect:** the stated RGB bound is not guaranteed. More importantly, the brightest excess samples remain undenoised, including in the “all light” trial. This is not proof of an actual FP16 overflow in every such pixel.

   **Fix:** use a documented, reversible scaling/compression scheme that handles the full signal. If an undenoised residual remains, disclose it as a remaining gap.

10. **(a) NRD kinds 1/2: five custom reflection-filter passes still run over the whole screen.**

    **Guide:** NRD README:129–141 describes executing NRD’s returned passes and consuming their outputs. The redundant work here is an implementation issue, rather than an explicitly forbidden API use.

    **Code:** `tracer.cpp:3071` keeps `denoiseOwn` enabled for every NRD kind. [tracer.cpp:3901](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/src/tracer.cpp:3901) runs reflection temporal filtering, followed by four à-trous passes at lines 3922–3925. `pt_nrd_prep.hlsl:130–144` then replaces their opaque-surface results with NRD’s results.

    **Effect:** opaque pixels pay for two reflection denoisers. The custom result survives only for excluded surfaces such as water and glass.

    **Fix:** restrict the fallback work to the surfaces that actually consume it, or replace that fallback architecture. I did not measure its GPU cost.

11. **(b) NRD kind 0: the original integration remains substantially outside the documented setup.**

    **Guide:** NRD README:445–455 requires shading normals; 495–516 describes hit distances; 566–577 describes material demodulation.

    **Code:** [pt_nrd_prep.hlsl:224–256](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_nrd_prep.hlsl:224) supplies the geometric/smooth guide plane, a constant distance of `1`, and a custom `bump_share` transformation. Direct light and reflections still use the custom filters.

    There is another packing departure: line 254 obtains `sh1` from `RELAX_FrontEnd_PackSh`, then line 256 discards it and substitutes a differently weighted brightness. Compare NRD shader:392–400 and 897–910.

    **Effect:** this is a custom directional-light filtering scheme. Its past image comparisons do not establish correct NRD inputs.

    **Fix:** retire those transformations when the corrected trial replaces kind 0. If retaining SH, use the front-end output and documented resolve consistently.

12. **(b) NRD kinds 1/2: diffuse demodulation does not use the documented material factor.**

    **Guide:** NRD README:566–577 recommends `NRD_MaterialFactors`; NRD shader:727–737 defines both factors.

    **Code:** [pt_nrd_prep.hlsl:67–70](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_nrd_prep.hlsl:67) calculates both factors but returns only the specular one. Diffuse input at line 191 is already divided by ordinary diffuse albedo, and composition multiplies ordinary albedo back.

    **Effect:** the trial does remove diffuse color, but it does not reproduce NVIDIA’s complete factor, including its Fresnel treatment and floor. This is not an unmatched multiplication bug; it is a departure from the requested helper-based integration.

    **Fix:** transform the existing diffuse signal into the helper’s units and apply the identical factor after denoising.

13. **(b) NRD and AMD: caustic lighting is outside the submitted diffuse signal.**

    **Guide:** NRD README:493,769–786 and AMD guide:525–552 describe the diffuse/specular lighting signals being denoised.

    **Code:** [pt_compose.hlsl:21–24](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_compose.hlsl:21) explicitly keeps caustics after filtering. Line 249 adds them after NRD or AMD has finished. The comment gives the reason: avoiding trails.

    **Effect:** “all diffuse light goes through NRD” is still false. Noisy caustic lighting bypasses both integrations.

    **Fix:** include this lighting in the appropriate diffuse input and provide the motion/confidence needed for moving caustics. Keeping it outside is a documented departure, not a completed all-light integration.

14. **(b) AMD: specular demodulation and remodulation use different factors near zero.**

    **Guide:** AMD guide:1034–1117 describes material-dependent specular reflectance. Any application-side division/multiplication pair must preserve the signal.

    **Code:** [pt_rrg_guides.hlsl:48](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_rrg_guides.hlsl:48) divides by `max(spec, 1e-3)`. `pt_compose.hlsl:257` multiplies by unclamped `spec`.

    **Effect:** whenever a channel’s factor is below `0.001`, even an identity denoiser loses energy. The analytic approximation can also return negative values for sufficiently small reflectance.

    **Fix:** calculate one valid factor and use exactly the same value on both sides. Keep the guide’s material reflectance and the numerical protection explicit.

15. **(b) AMD: switching away and back can reuse stale denoiser history.**

    **Guide:** AMD’s reset flag invalidates accumulated history; see `ffx_denoiser.h:577` and AMD guide’s dispatch-flags section.

    **Code:** `tracer.cpp:2515–2517` retains AMD’s context for 120 unused frames. [tracer.cpp:3931](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/src/tracer.cpp:3931) resets only for the general tracer reset. The gap detector at lines 3001–3002 tracks traced frames, not AMD dispatches.

    **Effect:** briefly selecting None or another denoiser can leave AMD history from an older frame while supplying motion for the immediately preceding frame.

    **Fix:** track the last successful AMD dispatch and reset after any interruption. NRD already has separate previous-use tracking.

16. **(b) NRD: kind 0 still uses frame-count history at the wrong time scale; the trials retain an initial-integration settings departure.**

    **Guide:** NRD settings:24–30,264,369 says to convert accumulation time into frames. NRD README:867 recommends disabling anti-lag during initial integration.

    **Code:** [nrd_denoiser.cpp:223–245](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/src/nrd_denoiser.cpp:223) now converts history only for kinds 1/2. Kind 0 retains approximately 30 main-history frames: about three seconds at 10 FPS. The settings retain default active anti-lag.

    **Fix:** apply time-based history to every retained NRD route. Establish the new integration’s input baseline with anti-lag disabled as requested by the README, then tune it.

    Diffuse prepass radius zero is a supported choice for sufficiently clean signals. RELAX anti-firefly being enabled is non-default but explicitly supported by the README and previously approved by the owner; it is not an automatic integration bug.

17. **(b) RR: color undergoes nonlinear highlight processing before reconstruction.**

    **Guide:** RR guide §3.1, p.12, places reconstruction before post-processing and tone mapping; §3.4.5, p.16, identifies the input as noisy ray-traced color.

    **Code:** [pt_compose.hlsl:262–265](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_compose.hlsl:262) applies brightness-dependent highlight handling before RR. Line 19 also defines a maximum-channel clamp used on composed color.

    **Effect:** RR reconstructs a transformed noisy signal. Nonlinear processing before averaging can change both noise and the resulting mean.

    **Fix:** preserve the owner’s chosen appearance processing, but apply it after RR. Supply RR with the preceding linear lighting signal.

    Separately, `StpHost.cs:197–211` observes the asset’s STP placement without enforcing it. If that placement is after depth of field, the missing DoF guide violates §3.1/§3.4.13. I did not establish the current asset’s runtime placement.

18. **(b) RR: some diffuse guides contain invented reflectance.**

    **Guide:** RR guide §3.4.1, p.13, defines diffuse albedo as material reflectance.

    **Code:** [d3d11.hlsl:455–458](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/d3d11.hlsl:455) adds `0.05` to dark diffuse guides. Lines 505–510 and 567–568 mix emission-derived color into the fallback material guide. The comments cite another NVIDIA sample as the rationale.

    **Effect:** the guide can describe brighter or differently colored material than the tracer shades.

    **Fix:** use the actual material reflectance for the guide-defined baseline. If retaining a sample-specific stabilization rule, document and validate that departure separately rather than calling it literal compliance with this guide.

19. **(b) RR: render sizes come from the SR query, and preset F is pinned.**

    **Guide:** RR guide §3.2, p.13, and its optimal-settings section, p.32, use the RR optimal-settings query. The preset guidance recommends Default.

    **Code:** `DlssModes.cs:25–26` queries Unity’s SR interface. [StpHost.cs:345](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/src/TGBetterUpscaling/StpHost.cs:345) uses that scale for RR; `QualityModes.cs:91` adds 0.1 percentage point below native resolution. `rr_upscaler.cpp:130–135` pins F to avoid the default changing after updates.

    **Effect:** the selected dimensions are not the RR runtime’s exact answer. Pinning F also deliberately bypasses future default selection.

    **Fix:** query `NGX_DLSSD_GET_OPTIMAL_SETTINGS` for RR and use its returned dimensions. Use Default for strict default-policy compliance. F itself is supported, not an invalid preset.

20. **(b) RR: glass submits accumulated path length as its reflection-distance guide.**

    **Guide:** RR guide §3.4.9, p.17, defines world-space distance from a specular origin on the primary surface to its hit.

    **Code:** `pt_dielectric.hlsli:149–150` accumulates successive segment lengths. [pt_specular.hlsl:154](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_specular.hlsl:154) exports that accumulated glass distance.

    **Effect:** bent, multi-interface paths are represented by a distance that is not the literal primary-origin-to-hit distance described by the guide. Transmission and reflection also share this signal.

    **Fix:** construct coherent reflection guides for the chosen glass surface, or provide the appropriate specular motion. Do not label accumulated optical-path length as the guide’s ordinary world-space reflection distance.

21. **(b) RTXDI: temporal visibility uses the current scene instead of the previous scene.**

    **Guide:** `DI/ReSTIRDIParameters.h:94–98`, `GI/ReSTIRGIParameters.h:83–87`, and `DI/TemporalResampling.hlsli:21–26` prefer previous-frame geometry for unbiased temporal correction. Current geometry is permitted as a biased approximation.

    **Code:** [pt_rtxdi.hlsli:209–211](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_rtxdi.hlsli:209) explicitly uses the current acceleration structure and current DI surface. Lines 228–246 trace GI’s previous surface against current geometry.

    **Effect:** moving occluders can invalidate the visibility assumption behind temporal weights.

    **Fix:** retain the required previous geometry/transforms for temporal visibility. Until then, describe Raytraced correction as using the documented current-scene approximation, not fully unbiased temporal visibility.

22. **(b) RTXDI: several parameter choices depart from NVIDIA’s defaults, with recorded reasons.**

    **Guide:** parameter meanings are in `DI/ReSTIRDIParameters.h:90–166` and `GI/ReSTIRGIParameters.h:63–128`; defaults are in `Source/ReSTIRDI.cpp:53–85` and `Source/ReSTIRGI.cpp:29–57`.

    **Code:** [tracer.cpp:3692–3737](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/src/tracer.cpp:3692), plus `HdrpPasses.cs:1232–1233`:

    | Choice | Current behavior and recorded reason | Consequence / return to baseline |
    |---|---|---|
    | DI history | Managed default is **4**, despite native fallback/comment saying 20. Twenty reportedly boiled on thin geometry. | Less temporal reuse; restore 20 only when deliberately testing NVIDIA’s default. |
    | DI naive-sample discount | Disabled; comments report dark disocclusions with Basic normalization. | Fresh samples spread immediately. Default is enabled; investigate the normalization interaction before restoring it. |
    | GI fallback sampling | Disabled because it borrows unrelated surfaces and darkens disocclusions. | More noise, less documented fallback bias. Default is enabled; this is a valid deliberate choice. |
    | GI permutation | Enabled for denoiser-friendly variation; default is disabled. | This follows the parameter’s stated purpose despite differing from the default. |
    | GI reservoir age | With visibility enabled, set to 256 while stored age saturates at 255. | Age rejection is effectively disabled. Default is 30; comments cite brightness bias from age rejection. |
    | Spatial samples | Player default 4; SDK defaults DI 1 and GI 2. | More reuse work. This is supported tuning, not malformed input. |

    Basic correction is itself NVIDIA’s default. The headers identify its visibility assumption as biased. Boiling strength `0.2`, DI/GI similarity thresholds, DI permutation, and disabled visibility shortcut match the defaults. `targetHistoryLength=0` disables the advertised disocclusion boost, but also matches the SDK default.

23. **(a) NRD kind 1: the new history conversion uses REBLUR’s maximum for RELAX.**

    **Guide:** NRD settings:200 sets REBLUR’s maximum to 63; line 348 sets RELAX’s to 255.

    **Code:** [nrd_denoiser.cpp:225](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/src/nrd_denoiser.cpp:225) clamps both kinds to `REBLUR_MAX_HISTORY_FRAME_NUM`.

    **Effect:** above roughly 126 real FPS, kind 1 cannot maintain its intended half-second history. This does not explain today’s roughly 10-FPS behavior.

    **Fix:** use each selected denoiser’s own maximum.

24. **(c) NRD: lighting-history confidence is entirely missing.**

    **Guide:** NRD README:464–466 and 715–750 strongly recommend confidence derived from lighting gradients, and warn against relying only on internal anti-lag.

    **Code:** `nrd_denoiser.cpp:197–207` leaves confidence disabled. Its resource mapping at lines 258–271 provides neither diffuse nor specular confidence.

    **Effect:** NRD lacks the recommended external indication that lighting changed, especially important with reused RTXDI samples.

    **Fix:** implement the documented gradient procedure: re-evaluate comparable lighting using the previous random seed, filter the gradient, convert it to confidence, and bind the corresponding inputs. A generic motion/disocclusion mask is not the same measurement.

25. **(c) NRD: better motion and material separation are available but unused.**

    **Guide:** NRD README:813 recommends 2.5D motion for moving objects; 836–839 describes material separation and strand-specific treatment.

    **Code:** [nrd_denoiser.cpp:200–202](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/src/nrd_denoiser.cpp:200) deliberately enables only 2D motion. `pt_nrd_prep.hlsl:184` gives every material ID zero. Material comparisons and `strandMaterialID` remain disabled by default.

    **Effect:** moving geometry has less reliable history. Hair, metals and other materials receive no explicit separation or strand treatment.

    **Fix:** provide true previous-depth motion, assign useful material classes, enable their comparison, and identify hair. The trial’s hair normal is not simply a tangent: `pt_primary.hlsli:368–371` already constructs a view-facing normal perpendicular to the strand, consistent with NVIDIA’s suggested construction.

26. **(c) NRD: the sample’s mirror/glass strategy and optional direct-distance refinement are absent.**

    **Guide:** NRD README:601–639 describes primary-surface replacement, where guides describe the reflected virtual surface. Lines 9–11 describe the sample’s separate glass treatment. Lines 777–786 suggest mixing direct and indirect diffuse distances for sharper shadows.

    **Code:** `pt_nrd_prep.hlsl:65` excludes glass, water and underwater pixels. They retain custom filtering. The trial uses only indirect diffuse distance and has no direct-distance mixing.

    **Effect:** the integration has not adopted the documented approaches for difficult reflective/transmissive surfaces or the suggested shadow-distance refinement.

    **Fix:** implement coherent virtual-surface guides where appropriate, or the sample-style glass strategy. Add direct diffuse distance separately if adopting that refinement. Excluding transparency from NRD is not itself an API violation: NRD explicitly does not natively denoise general transparency or volumes.

27. **(c) RR: optional guides are mostly absent; they are not all mandatory defects.**

    **Guide:** RR guide §§3.4.10–3.4.14, pp.18–20, and `nvsdk_ngx_helpers_dlssd_d3d.h:454–524` distinguish optional guides.

    **Code:** `rr_upscaler.cpp:142–165` supplies the basic guides, hit distance when available, and before/after-transparency color. It does not supply specular motion, reflected albedo, a separate transparency layer, disocclusion, SSS, DoF or responsivity guides.

    **Assessment:** specular motion is optional **only while valid hit distance is supplied**. The screen-space SSS guide is not required for the tracer’s physical scattering walk. Responsivity is optional and normally should remain absent/zero. Header fields alone do not make every guide mandatory.

    The remaining precision concern is real: `pt_primary.hlsli:508–520` packs normals through two 8-bit channels before the final float texture. RR recommends testing reduced precision against a genuine 16-bit guide. Restore precision before claiming that comparison is complete.

28. **(c) AMD: optional shadow and diagnostic paths are not implemented.**

    **Guide:** AMD guide:299–306 allows selected subsets of signals; 537 describes dominant-light visibility; 786–837 describes diagnostic views. AMD’s [sample](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/docs/samples/denoiser.md) exposes these independently.

    **Code:** `ray_regeneration.cpp:91–99` enables three signals with no validation/debug flags. There is no dominant-light visibility input or corresponding direction/angular-radius data.

    **Effect:** the specialized dominant-light shadow path is unavailable, and guide errors cannot be inspected through AMD’s intended overlays.

    **Fix:** add those inputs if adopting that optional shadow path; enable validation and diagnostic views in integration builds. Their absence does not invalidate a correctly supplied radiance-only integration. Context creation at `tracer.cpp:3009–3017` also happens on the render path, contrary to AMD guide:200–203’s recommendation to avoid critical-path creation.

29. **What already matches—and what I did not find broken.**

    - **NRD trials:** direct and indirect diffuse are combined; direct highlights and indirect reflections are combined separately. Using the indirect reflection distance for combined specular agrees with NRD README:773–775. Shading normals replace the old guide plane. REBLUR uses its packing/normalization helpers with matching default parameters. Specular demodulation/remodulation uses the same factor. The ambient-occlusion factor is applied consistently before filtering and when restoring excess diffuse light.
    - **NRD API:** the named resource mappings, SRV/UAV transitions, per-dispatch UAV barriers, restoration of external states, and restoration of the tracer’s descriptor heaps/root signature appear correct. I found no concrete wrong-resource-slot or missing-barrier bug. Unjittered matrices, frame index, positive view depth, and initial-clear versus ordinary-reset handling match the documented conventions.
    - **RR:** packed normal/roughness mode, world-space normals, hardware/reversed depth flags, render-pixel motion scaling, jitter conversion, matrix memory order, and feature recreation/reset handling are correctly wired. Color and the transparency snapshot are both handed to NGX as RGBA16F. RR’s guide explicitly says exposure/auto-exposure/sharpening inputs are unsupported; the missing exposure texture is therefore not a defect.
    - **RR color contents:** it receives the STP input after the PT composite. That includes traced glass/water and traced fog already composed there, plus any effects HDRP renders before STP. Its “before transparency” snapshot already includes those traced components. It is not a snapshot of opaque-only, untouched path-traced lighting.
    - **AMD:** signed depth, XY motion direction, motion scales, camera-position delta, matrix storage convention, octahedral normals, roughness placement, square-root albedo encoding, matching context/dispatch signal flags, and in-place input/output aliasing match the documented contracts. Runtime availability is queried. These are source checks; AMD execution was not tested on the RTX 5080.