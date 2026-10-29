from pathlib import Path

def edit(name, before, after):
    p = Path(name)
    text = p.read_text(encoding='utf-8')
    assert text.count(before) == 1, (name, before[:70], text.count(before))
    p.write_bytes(text.replace(before, after).replace('\n', '\r\n').encode('utf-8'))

edit('README.md', 'DLSS 5 Neural Rendering after tone mapping and before the interface and frame generation capture.',
     'DLSS 5 Neural Rendering before the camera effects, interface and frame generation capture, with a choice of before or after upscaling.')
edit('CHANGELOG.md', '  scene pass records NR before FG capture, after tone mapping and before overlay UI. FG and RR share its NGX lock',
     '  scene pass originally recorded NR after tone mapping; the placement correction below moves it before lens effects. FG and RR share its NGX lock')
edit('CHANGELOG.md', '- Static NR restoration checks reject different camera poses,',
     '- Corrects both NR placements to run immediately before/after HDRP upscaling, ahead of DoF, motion blur,\n'
     '  chromatic aberration, vignette and tone mapping. Both use the reversible HDR adapter. The native FG capture\n'
     '  remains downstream. BU and NR share their CustomPass recorder, including the IL2CPP unboxing fix. NR now\n'
     '  requests the bridge motion fixes itself. Both game versions compile; Mono deployed/hash-verified 20:44.\n'
     '  Eight tavern move/hold takes pass: NR evaluates every frame before lens effects; the stop jump falls from\n'
     '  1.130 to -0.163 display levels with Local Tone still 1. Off restoration passes in that run; this is not a\n'
     '  general appearance or HDR-compatibility verdict. Fog stays in the incoming scene. See tuning evidence.\n'
     '- Static NR restoration checks reject different camera poses,')
edit('docs/DLSS5-IMPLEMENTATION.md',
     '`BeforePostProcess` copies real depth and object/camera motion. The bridge\'s single `AfterPostProcess` pass records\n'
     'NR first, then TGFG\'s existing HUD-less capture. NR therefore processes tone-mapped gameplay colour before the\n'
     'game\'s screen-space overlay UI and before FG sees that scene. No Present hook belongs to NR. No FG, BU, PT or\n'
     'ReShade installation is required. ReShade compatibility still needs an actual game run.',
     'At 20:44, the default after-post-processing hook was replaced: `UpscalePlacement` records NR on the chosen\n'
     'side of `DoUpscalingAndAntiAliasing`, before lens effects. It scopes the upscaler schedule to BeforePost\n'
     'for this gameplay camera and restores it even on error. Both routes capture matching depth/motion and adapt\n'
     'scene-linear HDR into the model\'s LDR range, then restore HDR for downstream effects and tone mapping.\n'
     'The shared recorder source is also used by BU; the bridge ABI/binary is unchanged. FG\'s existing\n'
     'AfterPostProcess capture naturally follows NR. Fog and transparents remain in NR\'s input. NR requests shared\n'
     'motion corrections independently. No FG, BU, PT or ReShade installation is required. RenoDX HDR ordering\n'
     'was inspected in source; compatibility still needs an actual game run. Current evidence: DLSS5-TUNING.md.')

p = Path('BACKLOG.md'); t=p.read_text(encoding='utf-8')
start=t.index('- NR follow-up:'); end=t.index('- 2026-10-07 (round-07):', start)
t=t[:start]+'''- NR follow-up (NR-01 through NR-09, `docs/DLSS5-FEEDBACK.md`): owner wants full lighting/colour, not a tone-zero
  workaround. Both placements now sit either side of SR/RR before motion blur, DoF, chromatic aberration,
  vignette and tone mapping; HDR adapter on both. Fog stays in the incoming scene (owner asks to investigate).
  Mono deployed/hash-verified 20:44; both versions compile. Shared recorder source with BU; no bridge ABI change.
  Eight tavern motion/hold takes pass, with continuous NR and correct order on every frame: stop brightness jump
  1.130 -> -0.163 levels at full tone. Off restoration passes there. A/B/C rerun 20261007-204924: 60 captures across
  Ron, tavern and forest complete, analysis pending (`nr-sharp-models-20261007`). Game closed; 117 saves untouched.
  Source checks 90/90, eight evidence tests with nine deliberate faults. Evidence: `docs/DLSS5-TUNING.md`.
  RenoDX HDR source inspected; live coexistence, standalone/FG/FoA, normal-version and PT combinations remain open.
  Our own HDR mod is future work. No chosen best model or owner appearance verdict yet. Explicit NR override
  permits background GPU tests only with FoA/Starfield closed, through the shared harness lock. No release zip.
''' + t[end:]
p.write_bytes(t.replace('\n','\r\n').encode('utf-8'))

p=Path('docs/PROJECT-CONTEXT.md'); t=p.read_text(encoding='utf-8')
start=t.index('- TG Neural Rendering (R8):'); end=t.index('\n\n', start)
t=t[:start]+'''- TG Neural Rendering (R8): signed 310.8 public NGX feature 18, shared device with RR/FG. DLSS-only, SDR, starts Off.
  Both placements now before lens effects, either side of SR/RR, with HDR adapter; RR forces After. Mono 20:44.
  Full tone stays 1: blur caused the stop jump (1.130 -> -0.163 levels after correction); continuous NR, no resets.
  Fog stays in input; RenoDX source ordering checked, live HDR untested. Runs/status: `docs/DLSS5-TUNING.md`.''' +t[end:]
p.write_bytes(t.replace('\n','\r\n').encode('utf-8'))
