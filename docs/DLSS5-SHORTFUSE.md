# ShortFuse DLSS Tool controls inspected, 2026-10-07

The owner requested this comparison. Inspected UI strings in the public
[26.1003.2350 package](https://github.com/RankFTW/rhi-repo/releases/tag/renodx-dlss-SF-26.1003.2350),
downloaded under ignored `build/nr/renodx-review`. Nothing installed or executed. This establishes exposed labels
and tooltips, not its implementation or NVIDIA recommendations. Public upstream branches did not expose the
current NR implementation; no code was copied.

The model selector is A/B/C under `DirectNeuralRenderingStyle`; this inspection found no second independent
style selector. Other controls include:

| Tool control | Relevance to TG NR |
|---|---|
| Intensity, local tone, structure, character mask and skin strength | Corresponding controls already exposed; face mask/skin comparisons now captured. |
| Processing scale and pass count | Potential performance and stronger-effect studies. Neither is currently implemented in TG NR. |
| Input/output/Present placement and DLSS guide reuse | TG uses actual DLSS inputs and both sides of SR before lens effects; no Present or dummy-guide route. |
| Signal encoding, reference white and exposure | Worth investigating for the remaining colour instability. UI strings alone do not establish the correct conversion. |
| Scaling filters and residual reconstruction | Candidates only if a separate NR resolution control is justified by measurements. |

The tool describes native DLSS colour as linear and offers an HDR-to-SDR conversion before NR. This reinforces
the need to validate our colour adapter independently; it does not validate our present conversion or prove the
cause of motion-dependent grading. NVIDIA's sample, our signed runtime profile and matched scene evidence
remain the basis for selecting defaults. Do not copy community style names onto A/B/C without runtime evidence.

Evidence: `shortfuse.zip`, `shortfuse-ui-strings.txt`, `branches.json`, `releases.json` in the review directory.
The RHI installer source was inspected too; its installation controls do not document NR's model behaviour.
