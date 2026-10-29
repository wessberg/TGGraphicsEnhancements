from pathlib import Path

def replace(path, old, new):
    p = Path(path)
    text = p.read_text(encoding='utf-8-sig')
    if text.count(old) != 1:
        raise RuntimeError(f'{path}: expected one matching block, found {text.count(old)}')
    p.write_bytes(text.replace(old, new).replace('\r\n', '\n').replace('\n', '\r\n').encode('utf-8'))

replace('README.md',
'''TG Neural Rendering and the shared changes for it are built as 1.2.2 in an isolated folder. Native GPU checks pass;
I’m still testing its appearance in game. The Video tab adds **Neural Rendering** (Off / DLSS) and **Strength**. The row
greys out when DLSS is not selected or another requirement is missing, with the reason in its tooltip. The model is''',
'''TG Neural Rendering 1.2.2 is built for both game versions. I’m still testing its appearance in game. Video and FoA
Mod Manager have **Neural Rendering** (Off / DLSS), **Model** (A/B/C), **Placement** (before/after upscaling),
**Strength**, **Local Tone**, **Detail**, **Automatic Mask** and, with masking on, **Skin Detail**. It starts Off.
After Upscaling is the default; Ray Reconstruction requires that placement. The earlier path is still being tested.
The main row greys out unless DLSS and the other requirements are available, with the reason in its tooltip. The model is''')
replace('CHANGELOG.md', 'TG Neural Rendering 1.2.2 (2026-10-07; isolated builds, no deployment or release package):',
        'TG Neural Rendering 1.2.2 (2026-10-07; Mono deployed for testing, no release package):')
replace('CHANGELOG.md', '- Adds Neural Rendering (Off / DLSS, renamed from Off / On after the owner\'s first test) and Strength in Video/FoA.',
'''- Adds A/B/C, before/after upscaling, Local Tone, Detail, Automatic Mask and Skin Detail to Video/FoA. Earlier
  placement adapts HDR colour for the LDR model and restores HDR; RR requires After Upscaling. Neutral HDR probe
  error is at most 0.144%; live intensity preserves history. These are functional results, not a chosen look.
  The Mono update was deployed/hash-verified at 18:21; the tavern run timed out loading, so tuning remains open.
  Stronger matched motion and a 23-take comparison plan are ready. [Evidence](docs/DLSS5-TUNING.md).
- Adds Neural Rendering (Off / DLSS, renamed from Off / On after the owner's first test) and Strength in Video/FoA.''')
replace('docs/PROJECT-CONTEXT.md',
'''- TG Neural Rendering (R8): public NGX feature 18 evaluates signed archive 310.8.0 on our shared device; offline
  Streamline/RR/NR coexistence passes. Shared AfterPostProcess orders NR before FG; one NGX lock and early NR paths.
  DLSS-only (owner), SDR, starts Off. Owner requested deployment: Mono NR/FG/BU/bridge 1.2.2 installed 10-07 17:02.
  First game feedback/movement evidence and row rename (17:43): `docs/DLSS5-FEEDBACK.md`; tuning and placement open.''',
'''- TG Neural Rendering (R8): signed 310.8.0 works through public NGX feature 18 on the shared device, alongside RR/FG.
  DLSS-only, SDR, starts Off; AfterPostProcess orders NR before FG. Controls/earlier HDR-adapted placement deployed
  10-07 18:21 (NR ABI 0x00010100); RR forces After. A/B/C functional checks pass, no best look selected. Tavern
  load timed out; baseline forest excluded (owner moved). Strong matched motion ready: `docs/DLSS5-TUNING.md`.''')
replace('docs/DLSS5-IMPLEMENTATION.md',
        '`build/nr/label-deployment.json`. The original deployment report remains the historical record of the first copy.',
'''`build/nr/label-deployment.json`. The original deployment report remains the historical record of the first copy.
At 18:21 the NR native/plugin pair was updated for A/B/C, tone/detail/masking and before/after placement. Both
versions compile; the native probe passes. The tavern scene timed out loading and no new look was selected.
Current evidence, hashes, input-contract limits and next run: [DLSS5-TUNING.md](DLSS5-TUNING.md).''')
replace('docs/DLSS5-IMPLEMENTATION.md',
'''Video and FoA expose `DLSS Neural Rendering` (Off / On) and its `Strength` slider. It starts Off. Per the owner's
latest instruction, the row greys out unless **DLSS is the selected upscaler**. Missing/rejected runtime, unsupported
hardware, HDR and PT debug/raw views also explain their unavailable state. The saved On preference survives a
temporary unavailable state. FoA receives an Off-only choice list and the same reason while unavailable, using the
existing manager API. Strength is hidden while Off. No unverified style, tone, skin or mask controls are exposed.
Strength 0, 0.5 and 1 have been measured; the feature is recreated when strength changes for this pinned profile.''',
'''Video and FoA expose Neural Rendering (Off / DLSS), Model (A/B/C), Placement, Strength, Local Tone, Detail,
Automatic Mask and Skin Detail (masking On only). Starts Off; child controls hide while Off. Missing requirements
grey out the main row, with a reason; DLSS is mandatory. Saved preferences survive temporary unavailability.
Earlier placement uses render-size HDR-adapted colour; RR forces After and explains why. Strength changes keep
history; style/tone/detail/masking changes reset it. New controls compile for both versions but their UI behaviour
and appearance still need game acceptance. See the tuning report for the limits of the earlier colour adapter.''')
replace('docs/DLSS5-IMPLEMENTATION.md',
'''Style/model and finer controls need runtime-profile and
scene evidence before exposure. Final strength/defaults require the owner's appearance verdict. HDR stays disabled.''',
'''The finer controls now have a pinned adapter and A/B/C dispatch checks;
scene evidence and the owner's appearance verdict still determine final defaults. HDR display stays disabled.''')
replace('docs/DLSS5-IMPLEMENTATION.md',
'''the installed game and existing release artifacts remain their previous builds.''',
'''the later deployment records above identify the installed builds. Existing release zips were not rebuilt.''')
replace('docs/TESTING.md',
        'TG Neural Rendering: [isolated build evidence and pending game checks](DLSS5-IMPLEMENTATION.md). Its GPU probe',
'''TG Neural Rendering: [build evidence and pending game checks](DLSS5-IMPLEMENTATION.md), plus the
[A/B/C and placement tuning run](DLSS5-TUNING.md). Its GPU probe''')
replace('docs/PT-HARNESS.md',
        'TG Neural Rendering uses these same saved views through `tools/harness/neural_plan.py`; see',
'''TG Neural Rendering uses these same saved views through `tools/harness/neural_plan.py`. `--compare` adds A/B/C,
placement and strong matched motion; [current evidence](DLSS5-TUNING.md). `-AllowGpuContention` is the owner's
explicit NR-run override: other GPU jobs may overlap, but FoA and Starfield must both be closed. The default
five-second GPU-idle gate is unchanged. See''')

p = Path('ROUNDS.md')
text = p.read_text(encoding='utf-8')
row = next(line for line in text.splitlines() if line.startswith('## [Round 8]'))
replace(str(p), row, '## [Round 8](docs/rounds/round-08.md) - 2026-10-07 - TG Neural Rendering 1.2.2: public NGX and shared-device RR/FG coexistence pass. Mono deployed 17:02, row renamed 17:43, A/B/C/tone/detail/masking and before/after placement deployed 18:21. Both versions compile; HDR-neutral and A/B/C GPU checks pass. Baseline forest excluded after owner camera movement. Tavern timed out loading; no model/look selected. Strong matched motion and 23-take plan ready, docs/DLSS5-TUNING.md. Source checks 89/89. No release zip or final visual verdict. Parallel PT remains round-06.')

p = Path('BACKLOG.md')
text = p.read_text(encoding='utf-8')
start = text.index('- NR follow-up:')
end = text.index('- 2026-10-07 (round-07):', start)
replace(str(p), text[start:end], '''- NR follow-up: brightness/colour changes during movement. Owner wants tavern first, stronger camera motion,
  A/B/C, before/after upscaling and Off/On. Controls and real earlier placement (HDR adapter; RR forces After)
  built both versions; NR native/plugin deployed/hash-verified 18:21, ABI 0x00010100. A/B/C and HDR-neutral
  GPU checks pass. Model A remains an ungraded default. All requests: `docs/DLSS5-FEEDBACK.md`.
  Baseline forest excluded (owner moved camera); tavern load timed out after 420 s with heavy GPU memory
  pressure, no comparison frames. 117 saves unchanged. Strong matched motion and 23-take plan ready.
  Explicit NR override allows other GPU work only while FoA and Starfield are closed; respects the shared
  harness lock. Starfield and another coordinator's queued run prevented an immediate retry. Evidence and
  next run: `docs/DLSS5-TUNING.md`. No release zip, no chosen best look or performance verdict.
  Earlier Mono NR/FG/BU/bridge 1.2.2 deployment at 17:02 remains installed (PT preserved); row renamed
  Neural Rendering: Off / DLSS at 17:43. Source checks 89/89. Standalone/ReShade/FG and PT scene combinations,
  FoA interaction, motion stability and before-upscale appearance remain open.
''')
print('Updated README, CHANGELOG, PROJECT-CONTEXT, implementation/testing/harness docs, ROUNDS and BACKLOG.')
