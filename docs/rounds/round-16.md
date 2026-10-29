# Round 16 - 2026-10-08 - NR HDR output check

The owner enabled ReShade, RenoDX and Windows HDR and clarified that RenoDX's DLSS5 add-on is not installed.
The goal was to verify what reaches the display after RenoDX, because Unity's screenshot path stops before
ReShade and cannot prove HDR correctness.

I cloned the matching ReShade 6.8 headers and built a temporary, read-only add-on. The add-on captured the
presented back buffer after ReShade and wrote the raw FP16 pixels plus colour-space metadata. The game ran
through `Run-PtHarness.ps1 -AllowReShade` for one tavern scene with six frames each of Off, neutral, full and
restored Off. ReShade.log confirms RenoDX loaded and changed the swapchain to `R16G16B16A16_FLOAT`; it also
reported an extended-sRGB-linear runtime. The probe reported scRGB and the display's 10-bit PQ space with a
1037.51-nit peak. All 24 captures were valid, with no NaNs, readback errors or fallback pixels. The contact
sheet is in `artifacts/harness/nr-validation-hdr-r15/tavern-hdr-contact.png`.

The image looked sane. Off and neutral are close, while full strength gives the expected Neural Rendering
look. Full-strength stationary frames still vary more than Off: mean adjacent-frame HDR difference 0.00163
versus 0.00042, and 2.58% versus 0.19% of channels moved more than 0.01. This is a successful HDR wiring
check, not a stability fix. NR-14 remains a release blocker. Unity's HDR flag was false because RenoDX owns
the FP16 scRGB swapchain, so that flag cannot be the only HDR test for the RenoDX path.

The temporary probe was removed. ReShade.ini, saves and the owner's HDR setup were preserved. The GPU report
recorded another process using up to 37.8%, so no performance conclusion is drawn.

The owner then asked about the patched RTX 2000/3000/4000 `nvngx_dlssnr.dll` shared by the RenoDX community. The
RenoDX Discord link was added to the Nexus draft with a warning that it is a community source. TG Neural
Rendering rejects that patched file by design: the shared loader verifies the NVIDIA Authenticode signer and
the NR adapter accepts only the validated 310.8.0.0 profile. RenoDX accepting its own patched file does not
establish that the ordinary NGX route is compatible. No signature check was weakened.

The owner then supplied the exact patched file for an isolated check. It is x64 and reports 310.8.0.0, but
`Get-AuthenticodeSignature` returns `HashMismatch` even though the certificate names NVIDIA. Its SHA256 is
`E67DEE209320CDAFE0E93E45675D7AA34323A53ACC57A72B2E40A181581C989A`. Swapping only that DLL into a scratch
probe made `tgnr_gpu_smoke` report `TGNR_REJECTED` before NGX initialization with zero evaluations. The game folder
was not touched, confirming that the current result is a signature-policy rejection rather than a version mismatch.

To separate policy rejection from interface compatibility, a scratch-only TGNR build bypassed the signature checks.

R17 correction: the following interpretation was wrong. Verbose driver logs and a null module handle show NGX
rejected the signature before loading the model. It was not a hardware-support result. R17's separate direct
adapter initializes and renders this exact DLL on5080. The owner approved it, then required automatic selection
without per-file allowlisting. See `round-17.md`; the old starter below is superseded on this point.

The patched DLL loaded and reached NGX capability querying on the RTX 5080, then reported `DLSSNR.Available=0`.
No frame evaluated. This is loadability evidence only. It does not establish support on the RTX 2000/3000/4000
cards the patch targets, so no production bypass was added.

## ▶ Starter prompt (R17)

Keep the approved NR defaults and no extra temporal filter. The HDR path has real final-output evidence now,
but NR-14 walking instability is still open. NR-30/31/32 records that the supplied patched RTX 2000/3000/4000 file
is rejected by Authenticode in production, while a scratch bypass can load it but reports unsupported on the RTX 5080.
Any compatibility path needs target hardware evidence and an explicit policy decision. Do not repeat the HDR run
unless code changes the colour contract. Continue the next focused integration check or wait for the owner's next
visual test. Preserve the PT agent's shared files and lock rules.
