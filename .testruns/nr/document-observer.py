from pathlib import Path
import json
root=Path(__file__).resolve().parents[2]
def edit(name, fn):
    p=root/name
    s=p.read_text(encoding='utf-8-sig')
    p.write_text(fn(s),encoding='utf-8',newline='\r\n')

contract='''Depth and motion come from the actual selected DLSS invocation. In 1.2.3 the bridge owns the NGX D3D11
observer formerly private to FG. FG and NR subscribe independently. The packet includes the actual resources,
image regions, vector scale, jitter, reset and depth convention. BU publishes its selected D3D11 RR inputs
at the shared-device handoff and only marks success after writing the upscaled output. NR associates that
packet with the gameplay camera's colour resource; missing, failed or mismatched calls disable NR, without
substituting another camera's guides. Before evaluates before SR; After evaluates after successful SR/RR.
The managed render-graph pass only supplies camera identity, settings and resource lifetime dependencies.

NR-10 / PT-72 remains a renderer migration requirement: when PT changes the visible geometry, the inputs to
DLSS itself must match it. Changing lighting or fog alone does not imply different surface motion. NR must
consume whatever DLSS actually uses, not invent a separate NR-to-PT lookup. PT's published guide API does
not yet export its own geometry depth/motion. Validate hair, glass/water and fog after PT updates that common
handoff. Current scene comparisons use PT Off; the real RR+NR GPU check is synthetic, not PT image acceptance.
'''
for name in ('docs/DLSS5-SPEC.md','docs/DLSS5-IMPLEMENTATION.md','docs/DLSS5-TUNING.md'):
    def replace(s):
        start=s.index('Depth, motion, camera matrices, exposure convention and frame identity must describe that same completed image.')
        end=s.index('The current NR scene comparisons use the game\'s renderer with PT Off; they do not validate this contract.',start)
        end+=len('The current NR scene comparisons use the game\'s renderer with PT Off; they do not validate this contract.')
        return s[:start]+contract.rstrip()+s[end:]
    edit(name,replace)

result=json.loads((root/'build/nr/observer-deploy/deployment-result.json').read_text(encoding='utf-8-sig'))
hashes='\n'.join(f"- `{f['path']}`: `{f['sha256']}`" for f in result['files'] if f['path'].endswith('.dll') and ('TGNeuralRendering.dll' in f['path'] or f['path'].endswith('.Native.dll')))
metrics=json.loads((root/'artifacts/harness/nr-dlss-inputs-20261007/tavern-room/motion-diagnosis.json').read_text())
after=metrics['02-game-blur-motion-after-A']; before=metrics['03-game-blur-motion-before-A']
section=f'''
## Actual DLSS inputs, 1.2.3 (21:46 local deployment)

Owner corrected the integration: read guides from DLSS, as FG does. Earlier claims that the common HDRP handles
were equivalent omitted actual NGX metadata. The observer now lives in the bridge; details in the renderer
contract above. NR ABI 0x00010200, frame 100/status 72 bytes; events 4/5 bracket actual DLSS. Bridge Host/managed
APIs stay compatible; additive native observer exports. FG and BU were updated together. All source versions
are 1.2.3. No PT implementation/binaries replaced (version metadata only in source).

Both game versions of NR, FG and BU compile. Synthetic RR/NR/FG coexistence passes: 32 RR, 30 NR, no fallback.
A wrong-camera mutation fails the intended NR-evaluation assertion; the later RR image checks also fail because
the test exits its frame loop early. The legacy NR GPU image probe passes neutral SDR/HDR, model switching,
intensity, alpha, Off and resize. Source checks 90/90; recorder unboxing mutation detected; eight NR evidence
checks and six shared analysis checks pass (with their deliberate bad-evidence cases).

Mono deployment: 42 payload entries plus the signed model, backed up/replaced/hash-verified against build and
scratch install. Evidence: `build/nr/observer-deploy/deployment-result.json`, UTC `{result['utc']}`.
PT hashes checked unchanged. No zip, commit or push. Vortex must not revert these replaced files.

{hashes}

Game run `20261007-214804`, `artifacts/harness/nr-dlss-inputs-20261007`: eight tavern motion/hold checks plus
one Ron framing take complete in 2.4 minutes, zero failed captures. One report warning: PT GPU test contention
up to 20.7%, so no performance conclusion. All 117 saves untouched; configs restored and game closed.
The bridge log confirms actual DLSS SR guides: 1924x1084 motion, scale -1924/-1084. NR evaluates every captured
On frame before lens effects, with no skips/failures or history reset during capture. Off restoration passes.
At full tone, the stop jump is {after['stop_jump']:.3f} After / {before['stop_jump']:.3f} Before, and view-dependent brightness
ranges are {after['luma_shift_range']:.3f} / {before['luma_shift_range']:.3f}. This retains the pre-lens fix; observing NGX is not claimed
to eliminate model colour variation. Face framing verified from overview.jpg: camera 1.8 m closer, FOV 40.

NR-11: Video and FoA now label placement Quality (After Upscaling) / Performance (Before Upscaling), preserving
saved config values. Both remain available subject to RR's denoise requirement. Tune their results separately;
no independently optimized defaults are established yet. Full strength/tone/detail remain 1.
NR-13: `neural_plan.py --face` runs 30 Ron close-up takes, Off/neutral/A/B/C at both placements, automatic mask,
skin detail 0/1, then matching moving/holding comparisons and Off restoration. Close-up movement is 20 cm and
10 degrees; the 65 cm/25-degree tavern route takes the close face out of view. The camera/FOV restore each take.
Run `20261007-215715` is in progress; do not claim its result until report and atlases are inspected.
'''
edit('docs/DLSS5-TUNING.md',lambda s:s+section)
edit('docs/DLSS5-IMPLEMENTATION.md',lambda s:s.replace('## Runtime and adapter','''Latest update: 1.2.3 installed at 21:46, NR/FG/BU/bridge together; actual NGX observation is now shared.
Both game versions compile. Synthetic coexistence and eight real tavern motion captures pass; GPU contention
precludes timing claims. Close-up face comparison in progress. See the final section of DLSS5-TUNING.

## Runtime and adapter''',1))
edit('docs/PROJECT-CONTEXT.md',lambda s:s.replace('  NR shares DLSS input depth/motion; PT must update that common input (PT-72/NR-10) and own fog. HDR live untested. See `docs/DLSS5-TUNING.md`.','  1.2.3 at 21:46 shares FG/NR actual NGX observation in bridge (regions/scales/jitter/depth/reset); BU publishes RR inputs.\n  Eight real tavern motion takes pass. PT must update DLSS guides if geometry differs (PT-72/NR-10). HDR live untested; `docs/DLSS5-TUNING.md`.'))
edit('CHANGELOG.md',lambda s:s.replace('# Unreleased\n','# Unreleased\n\nTG Neural Rendering / shared bridge 1.2.3 (2026-10-07; Mono deployed for testing):\n\n- Moves FG\'s actual NGX input observation into the shared bridge so NR works independently of FG and reads the\n  selected DLSS resources, regions, scale, jitter, reset and depth convention. BU publishes its RR handoff too.\n  No substitute HDRP input lookup. RR+NR GPU coexistence passes; wrong-camera mutation is rejected. Eight real\n  tavern motion takes pass; contention prevents timing claims. Both game versions compile.\n- Labels NR placement Quality (After Upscaling) / Performance (Before Upscaling) in Video and FoA, retaining\n  saved values. Adds a real close-up face harness with A/B/C, mask, skin detail and matched camera motion.\n\n',1))
edit('docs/PT-REQUESTS.md',lambda s:s.replace("### PT-72 Publish the tracer's geometry guides for NR and Ray Reconstruction", "### PT-72 Publish the tracer's geometry guides to DLSS, shared with NR").replace('  NR tracks this as NR-10 in docs/DLSS5-FEEDBACK.md. No PT implementation was changed by the NR investigation.', '  NR tracks this as NR-10 in docs/DLSS5-FEEDBACK.md. Since 1.2.3 NR and FG read the actual selected DLSS\n  packet via the bridge; BU publishes its RR inputs. PT must update DLSS inputs when traced geometry differs;\n  lighting/fog changes alone do not change surface motion. No separate NR-to-PT lookup. No PT implementation\n  was changed by the NR investigation.'))
edit('docs/rounds/round-08.md',lambda s:s.replace('## ▶ Starter prompt (R9)', '''## Actual DLSS input follow-up, 21:46 deployment

Owner corrected NR to consume DLSS's actual inputs, requested a performance placement option, ShortFuse Tool
research and close-up faces. Implemented shared NGX observation in bridge, FG subscriber and BU RR publisher;
NR arms camera colour identity, uses actual depth/motion/regions/scales/jitter/depth/reset, then evaluates at
the selected side of upscaling. No separate HDRP guide lookup. Native observer exports additive, NR ABI10200.
All source versions 1.2.3; PT only version metadata. Both versions compile; synthetic coexistence/image tests
pass, wrong-camera mutation fails. Source90, recorder mutation, eight NR and six shared analysis checks pass.
Installed/hash-checked 42 entries/model at 21:46; preserved PT. Eight real tavern motion checks pass with actual
NGX inputs; 20.7% GPU contention warning. 117 saves untouched. Close-up framing verified at 1.8 m/FOV40.
Thirty face comparisons are running (20261007-215715). Finish and inspect them before choosing defaults.
ShortFuse public release controls inspected read-only; no add-on installed. Details in DLSS5-TUNING.

## ▶ Starter prompt (R9)''',1))
edit('ROUNDS.md',lambda s:s.replace('TG Neural Rendering 1.2.2 implemented/deployed.', 'TG Neural Rendering 1.2.3 deployed at 21:46: actual DLSS inputs shared with FG through bridge; eight tavern motion checks pass; face close-up run pending.',1))
edit('BACKLOG.md',lambda s:s.replace('Shared actual NGX inputs now built in isolated 1.2.3; game remains 20:44 1.2.2 pending validation.', 'Shared actual NGX inputs deployed in 1.2.3 at 21:46; eight real tavern motion takes pass. Face run in progress.'))
print('Updated implementation, tuning, renderer contract and shared handoff notes')
