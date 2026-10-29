from pathlib import Path

def edit(name, transform):
    p = Path(name)
    old = p.read_text(encoding='utf-8')
    new = transform(old)
    assert new != old, name
    p.write_bytes(new.replace('\r\n', '\n').replace('\n', '\r\n').encode('utf-8'))

def replace_once(text, old, new):
    assert text.count(old) == 1, old[:100]
    return text.replace(old, new, 1)

contract = '''### Renderer ownership: fog and geometry guides

Owner, 2026-10-07: "Keep in mind that the PT mod fundamentally renders the scene differently, including its own
rendering of fog". NR consumes the active renderer's completed scene, including its fog and transparents. With
PT enabled these are PT's responsibility; NR must not assume HDRP supplied them, add fog again, or split the
tracer's surface and volume lighting. Placement selects the side of SR/RR, while both choices precede lens
effects. RR still requires After because it denoises and upscales together.

Depth, motion, camera matrices, exposure convention and frame identity must describe that same completed image.
Owner's follow-up: "You should read them from DLSS". The current UpscalePlacement hook already takes the same
depth/motion handles dispatched to DLSS SR or BU's RR host. CustomPassContext's cameraDepthBuffer and
cameraMotionVectorsBuffer names do not imply a separate lookup: CustomPassRecorder binds those supplied handles.
Open integration gap NR-10 / PT-72 is shared with DLSS/RR: PT's published TGPT_Guides / RayReconstruction API
does not export its own depth/motion, and RR still uses the dispatcher inputs. The migration must supply matching
geometry at that common boundary; if RR selects different resources internally, publish that selected packet
for NR to consume. Do not build an independent NR-to-PT lookup or silently substitute stale geometry. Preserve
dimensions, encodings, frame identity and resource lifetime on the shared device. Validate moving hair,
water/glass and fog with PT+RR and PT+NRD after that migration.
The current NR scene comparisons use the game's renderer with PT Off; they do not validate this contract.
'''

def feedback(s):
    rows = {
        '01': 'Owner wants the full lighting/colour effect. Corrected/deployed before lens effects at 20:44. Eight tavern takes pass continuous evaluation and ordering; the movement-stop brightness jump drops from +1.130 to -0.163 display levels at full tone. View-dependent grading remains; owner verdict pending.',
        '03': 'Both placements implemented. Since 20:44 they bracket SR/RR before lens effects and use the HDR adapter. Before evaluates 1924x1084 versus After 3840x2160; about 3.6 versus 11-12 ms model time in the quiet diagnostic, excluding transfer/game cost. RR forces After and restores the preference. No real-frame speed verdict yet.',
        '05': 'A/B/C map to Style 0/1/2 in signed 310.8.0. New 60-take Ron/tavern/forest comparison completed at 20:58 after correcting placement. Eleven report warnings remain; exclude the final three forest movement takes. No winner selected; A and full tone/detail/strength remain the defaults.',
        '06': 'Analysis rejects different static/moving poses and FOV. Eight NR evidence tests detect nine deliberate faults. Latest diagnostic restoration passes; model run still flags small Ron/tavern Off differences and larger forest neutral/Off differences. Thresholds unchanged.',
        '07': 'Implemented/deployed 20:44: both choices precede DoF, motion blur, chromatic aberration and vignette. Full-tone blur/hold diagnostic passes all eight takes, with NR evaluated and recorded before lens effects on every captured NR frame. This substantially reduces the stop jump; it is not proof of universal motion stability.',
        '08': 'Implemented before HDRP lens effects and tone mapping on both sides of SR/RR. Reviewed RenoDX Unity HDR source: its grading/display mapping and final output encoding follow this hook; format upgrades happen earlier. Actual coexistence remains untested. Public NR add-on implementation was not found.',
        '09': 'Owner clarifies PT renders its own fog. Contract: NR sees the completed scene from the active renderer, including that renderer\'s fog; never double-compose HDRP fog over PT. Fog remains visible in the inspected game-rendered captures. PT\'s new volume path still needs separate validation; linked geometry-guide gap NR-10 / PT-72.',
    }
    lines=s.splitlines()
    for i,line in enumerate(lines):
        for key,state in rows.items():
            if line.startswith('| NR-'+key+' |'):
                cells=line.split('|')
                lines[i]='|'+cells[1]+'|'+cells[2]+'| '+state+' |'
    s='\n'.join(lines)+'\n'
    mark='\nThe owner permits reading RenoDX'
    s=replace_once(s,mark,'\n| NR-10 | Discovered: PT colour and NR geometry guides can belong to different renderers. | Open, dependency on PT migration (PT-72). NR reads HDRP depth/motion; PT currently exports material/RR guides but no own depth/motion packet. Publish and consume synchronized PT geometry guides before declaring compatibility. Not established as the cause of the PT-Off movement report. |\n'+mark)
    s=replace_once(s,'the wrapper restored the owner\'s configs. The remaining named face close-up is run 20261007-200519.',
        'the wrapper restored the owner\'s configs. The completed named face close-up is in nr-sharp-models-20261007;\nits restoration warning remains open. No run is active.')
    return s
edit('docs/DLSS5-FEEDBACK.md',feedback)

def tuning(s):
    start=s.index('After Upscaling remains the default:')
    end=s.index('Invalid or saturated model output',start)
    s=s[:start]+'''Both choices now run before lens effects, on either side of SR/RR inside HDRP's post-process graph. The
20:44 deployment scopes the upscaler schedule to BeforePost for the gameplay camera, then restores it. NR
therefore precedes DoF, motion blur, bloom, chromatic aberration, vignette and game tone mapping. After remains
the default. Both receive scene-linear HDR: our adapter compresses positive RGB as `rgb / (1 + max(rgb))`,
converts it to sRGB for NR, then reverses those conversions.
'''+s[end:]
    s=replace_once(s,'At 18:21 local time, the Mono plugin and native NR DLL were replaced together, backed up and hash-verified.',
        'At 20:44 local time, the Mono plugin and native NR DLL were installed, backed up and hash-verified.\nThe native binary is unchanged; the 18:21 deployment record is preserved as build/nr/controls-1821-deployment.json.')
    s=replace_once(s,'7F6068B38C10FE49FEF25E4BE8218A7B79EB19473A53007A6A10B80346D6C46B','74D7735B87DCFD8E85DA88130DEE77E49AC9F1E365ABD85C65E26AE13EBA730F')
    s=replace_once(s,'The six NR analysis checks (including\nfive deliberate faults)', 'The eight NR analysis checks (including\nnine deliberate faults)')
    a=s.index('## Motion bug investigation (active)')
    b=s.index('## Earlier queued state',a)
    s=s[:a]+'''## Motion-blur diagnosis and corrected placement

The old AfterPostProcess route received blurred colour. Run 20261007-202332, eight takes in
`artifacts/harness/nr-motion-diagnosis-20261007`, completed at 20:28. Each cycle moves 48 frames and holds
home for 32, with one warmup cycle. Each blur condition has its own Off reference. Every captured NR frame
evaluated without skips, failures or resets within takes: wholesale bypass or repeated resets did not explain
the effect. Old After/A's NR-minus-Off brightness residual jumped +1.130 display levels between frames 48
(last blurred frame at home) and 49 (first sharp frame). Suppressing blur reduced that jump to +0.095.
Frame 0 is sharp after the preceding hold; it is not a moving-frame reference. GPU contention was recorded.

`UpscalePlacement` now records NR before/after DoUpscalingAndAntiAliasing, with the schedule scoped to BeforePost.
NR and BU compile the same CustomPassRecorder source, preserving IL2CPP's unboxed RenderTargets call. The
bridge managed API/binary did not change. NR now independently requests the shared first-person/Kandra motion
corrections. Existing reversible HDR conversion is enabled on both placements. This differs from NVIDIA's
post-game-tone-map sample and still needs highlight/appearance acceptance; full Local Tone remains 1.

The deployed correction was tested in run 20261007-204410, eight tavern takes, 123.7 seconds, no competing GPU
work: `artifacts/harness/nr-sharp-placement-20261007`. All eight capture steps and the report pass, including
Off restoration. Every NR frame evaluates for the current camera frame and records before the earliest lens
pass (DoF). After/A's stop jump is now -0.163; without blur +0.089. Before/A is -0.204, without blur +0.094.
This strongly reduces the abrupt return of grading when movement stops, without weakening Local Tone.
View-dependent brightness still varies (After/A range 1.507; Before/A 3.204); do not claim all instability fixed.
Model-only timings were roughly 11.3-12.4 ms After versus 3.58-3.60 ms Before, transfer textures 269.04 versus
79.56 MiB. These exclude transfers and the rest of the frame, so are not an FPS claim.

Read the overview, motion-stop-comparison.jpg, motion-brightness.png and motion-diagnosis.json in each folder.
All were inspected. Both runs closed FoA, restored configs and left all 117 save files unchanged.

## A/B/C after the placement correction

Run 20261007-204924 completed at 20:58:46: 20 takes each on Ron (hair-eyebrows), tavern-room and rainy forest,
60 captures in 9 minutes with no competing GPU work. Output: `artifacts/harness/nr-sharp-models-20261007`.
All capture steps completed, but the report has **11 warnings**, so this is not a passed acceptance run.
Model sheets, motion sheets, overview/atlases and restoration differences were inspected. NR remains visible
in blurred moving frames; A increases fine-detail contrast, B lifts Ron but strongly darkens the forest,
C is softer on face/stone details. Haze remains visible; preservation of fog density is not established.

Matched-motion brightness-shift range, display levels relative to Off at each pose:

| Placement/model | Ron | Tavern | Forest |
|---|---:|---:|---:|
| After A | 1.766 | 1.501 | 2.385 |
| After B | 2.401 | 2.462 | 5.997 |
| After C | 1.493 | 0.943 | 3.346 |
| Before A | 3.728 | 3.318 | 5.278 |
| Before B | 3.080 | 3.465 | excluded |
| Before C | 2.455 | 1.849 | excluded |
| Off again | 0.024 | 0.076 | excluded |

Ron/tavern neutral checks pass. Static Off-restoration worst blocks are 2.304/1.954 (means 0.089/0.154): the
pictures show small scene/light differences, whose cause is not established. Forest neutral/Off worst blocks
are 4.231/4.215/4.248. Its last three moving takes have noninteractive-state and FOV mismatches: exclude Before B,
Before C and final Off motion; do not blame owner input without evidence. Thresholds remain unchanged.
Valid captures pass the evaluation/order checks. Defaults stay A/After and full strength/tone/detail; no winner
or owner look verdict. FoA closed, configs restored, all 117 save files unchanged. No active run remains.

## RenoDX and PT integration

Unity's [post-process source](https://github.com/Unity-Technologies/Graphics/blob/6000.0/staging/Packages/com.unity.render-pipelines.high-definition/Runtime/RenderPipeline/HDRenderPipeline.PostProcess.cs)
places BeforePost upscaling before DoF, motion blur and Uber (tone mapping/CA/vignette).
The [render graph](https://github.com/Unity-Technologies/Graphics/blob/6000.0/staging/Packages/com.unity.render-pipelines.high-definition/Runtime/RenderPipeline/HDRenderPipeline.RenderGraph.cs)
composes opaque fog and transparents before post processing; fog is not a single lens pass to relocate.

RenoDX Unity HDR source was inspected at revision `b16c5d6157b66bf49ee951227a7d99c150208913` in
[NotVoosh/renodx-unity](https://github.com/NotVoosh/renodx-unity/tree/b16c5d6157b66bf49ee951227a7d99c150208913).
Its Uber shader calls GradeAndDisplayMap after scene effects; its final shader encodes output as scRGB/PQ.
Those stages follow our new NR hook. Its texture-format upgrades happen earlier at resource creation.
This source ordering is not a live coexistence test. Public NR add-on source was not found; the upstream DLSS
hook framework alone is not its implementation. The current runs have no RenoDX HDR add-on. Own HDR-mod work
is deferred by the owner; HDR display output remains gated pending validation.

'''+contract+'\n'+s[b:]
    a=s.index('The comparison recipe has 23 takes:')
    b=s.index('The owner\'s explicit NR override',a)
    s=s[:a]+'''The current comparison recipe has 20 takes: Off, After neutral/A/B/C/masking, Before neutral/A/B/C/masking,
Off restored, then matched Off/After A/B/C/Before A/B/C/Off-restored movement. Full tone throughout. Motion moves
65 cm and turns 25 degrees each way over 48 rendered frames, then holds home for 32. One complete cycle warms
history, one is captured. Each frame records pose/FOV, native counters and ordering before lens effects;
analysis rejects mismatched routes and compares each pose with Off. The frozen world can still contain image
differences; do not waive restoration checks.

```powershell
python tools/harness/neural_plan.py build/nr/tuning/compare.json --compare
./tools/harness/Run-PtHarness.ps1 -Plan build/nr/tuning/compare.json -Scenario tavern-room -Out artifacts/harness/nr-tavern-next -AllowGpuContention -KeepFrames -TimeoutMinutes 20
```

'''+s[b:]
    s=s.replace('Finish the close-up, then check PT/RR and PT/NRD\ncombinations once the parallel tracer migration is ready.', 'Resolve the excluded forest takes and restoration warnings. Check PT/RR and PT/NRD\ncombinations once the parallel tracer migration publishes its matching guides (NR-10 / PT-72).')
    return s
edit('docs/DLSS5-TUNING.md',tuning)

def backlog(s):
    s=replace_once(s,'- PT-69 Review findings tested, both real:', '- PT-72 NR/RR need the tracer\'s own synchronized depth/motion, not HDRP geometry beside traced colour (PT-71).\n- PT-69 Review findings tested, both real:')
    s=s.replace('NR-01 through NR-09','NR-01 through NR-10')
    s=s.replace('Fog stays in the incoming scene (owner asks to investigate).', 'Input includes the active renderer\'s fog; PT guide contract is PT-72.')
    s=replace_once(s,'Ron, tavern and forest complete, analysis pending (`nr-sharp-models-20261007`). Game closed; 117 saves untouched.', 'Ron, tavern and forest complete: 11 report warnings; final three forest motion takes excluded. Restoration\n  differences remain; no model winner. Details in `nr-sharp-models-20261007`. Game closed; 117 saves untouched.')
    return s
edit('BACKLOG.md',backlog)

def pt(s):
    mark='### PT-71 The migration to our own renderer:'
    entry='''### PT-72 Publish the tracer's geometry guides for NR and Ray Reconstruction
- Found during NR integration, 2026-10-07. Owner: "Keep in mind that the PT mod fundamentally renders the scene
  differently, including its own rendering of fog". NR must consume the completed traced scene, with PT's fog,
  before lens effects. Do not assume HDRP owns the fog or composite it twice.
- Evidence: src/TGNeuralRendering/SceneCapture.cs reads cameraDepthBuffer/cameraMotionVectorsBuffer. The current
  TGPT_Guides and RayReconstruction.TryGetGuides/TryGetExtraGuides export material/RR guides but no own depth or
  motion. BU's RR host also reads HDRP geometry. PT's camera rays can hit a different surface (PT-59/70/71).
  This is a renderer/guide mismatch, not evidence that it caused the NR movement defect in PT-Off captures.
- Open, part of PT-71 item 10. Publish the matching traced image's depth and motion, frame/camera identity,
  dimensions, encodings and resource lifetime through the shared device contract; wire NR and BU/RR consumers.
  Coordinate with migration's removal of delayed/HDRP reprojection. Do not silently substitute stale or unrelated
  guides. Then test PT+NRD and PT+RR with NR, moving hair, water/glass, fog, camera cuts and resolution changes.
  NR tracks this as NR-10 in docs/DLSS5-FEEDBACK.md. No PT implementation was changed by the NR investigation.

'''
    return replace_once(s,mark,entry+mark)
edit('docs/PT-REQUESTS.md',pt)

def context(s):
    return replace_once(s,'  Fog stays in input; RenoDX source ordering checked, live HDR untested. Runs/status: `docs/DLSS5-TUNING.md`.',
        '  NR shares DLSS input depth/motion; PT must update that common input (PT-72/NR-10) and own fog. HDR live untested. See `docs/DLSS5-TUNING.md`.')
edit('docs/PROJECT-CONTEXT.md',context)

def spec(s):
    mark='## Goal\n'
    note='''## Implementation and pipeline update, 2026-10-07 evening

The original research requirements below are retained. Ordinary public NGX has since created/evaluated NR on
our shared device, including offline RR coexistence. Both game versions compile; Mono is deployed. Current
evidence and remaining acceptance work: [DLSS5-TUNING.md](DLSS5-TUNING.md).

The owner's later instruction supersedes the original post-tone-map placement: both Before and After Upscaling
must precede motion blur, DoF, chromatic aberration and vignette. The implementation brackets SR/RR before
lens effects, using our reversible HDR-to-LDR adapter on both routes; game tone mapping follows NR. That adapter
is a measured integration choice, not NVIDIA's demonstrated tone-mapped input, and appearance/HDR validation
remains necessary. FG capture and UI follow NR. RR forces After; no noisy undenoised PT input is accepted.

'''+contract+'\n'
    return replace_once(s,mark,note+mark)
edit('docs/DLSS5-SPEC.md',spec)

def implementation(s):
    s=replace_once(s,'Current evidence, hashes, input-contract limits and next run:',
        'At 20:44 the NR pair was deployed again for placement before lens effects. Eight motion-diagnostic takes pass;\n60 model-comparison takes completed with 11 analysis warnings. PT geometry-guide integration remains open (PT-72).\nCurrent evidence, hashes, input-contract limits and next run:')
    s=s.replace('Both routes capture matching depth/motion and adapt', 'Both routes capture HDRP depth/motion and adapt')
    s=replace_once(s,'Frame and camera IDs, both viewports, jitter and depth convention must match.',contract+'\nFrame and camera IDs, both viewports, jitter and depth convention must match.')
    return s
edit('docs/DLSS5-IMPLEMENTATION.md',implementation)

def round_doc(s):
    start=s.index('Run 20261007-200519 is active')
    s=s[:start]+'''Run 20261007-200519 later timed out with noninteractive-state warnings; it is partial evidence, not a clean
character comparison. No run is active now.

## Before lens effects, deployed 20:44

The owner asked for NR before blur, CA and vignette on both sides of upscaling; RenoDX HDR should follow it.
The old hook received motion blur. Eight diagnostic takes isolated a movement-stop brightness jump of +1.130
display levels, versus +0.095 when blur was suppressed. Evaluations were continuous, without resets/skips.
NR now brackets HDRP's upscaler before lens effects, scoping/restoring the upscaler schedule per camera. Both
routes use the existing HDR adapter. NR and BU share CustomPassRecorder source with the IL2CPP unboxed target
call preserved; no bridge API/binary change. NR independently requests shared motion corrections.

Both NR and BU build for both game versions; only NR was deployed. Native NR unchanged. Managed SHA256
74D7735B87DCFD8E85DA88130DEE77E49AC9F1E365ABD85C65E26AE13EBA730F; build/nr/tuning-deployment.json.
Eight corrected diagnostic takes pass at full tone, before lens effects and evaluated every frame. The stop
jump becomes -0.163. Overall view-dependent grading remains; no universal stability claim. Source checks90/90;
eight NR evidence tests detect nine faults. Revised recorder source check is also mutation-checked separately.

Run 20261007-204924 completed 60 model takes across Ron, tavern and forest, 9 minutes without competing GPU
work. Eleven report warnings: small Ron/tavern Off differences; forest neutral/Off differences; the last three
forest moving takes excluded for UI/FOV state. The motion and model sheets were inspected. No model winner or
owner look approval. Full tone/detail/strength and A/After remain defaults. Both completed runs restored configs,
closed the game and left 117 saves unchanged. Exact results and evidence links: docs/DLSS5-TUNING.md.

RenoDX Unity HDR source confirms downstream grading/output encoding; texture-format upgrades happen earlier.
The actual public NR add-on source was not found. Live HDR coexistence remains untested; own HDR mod is future
work. Owner reminds us PT renders its own fog. Recorded completed-scene contract: use the active renderer's
fog and geometry guides. Found PT does not yet publish own depth/motion for NR/RR (PT-72 / NR-10); coordinate
with PT-71 migration. No PT source changed. No release zip, commit or push.

## ▶ Starter prompt (R9)

Continue NR from BACKLOG, DLSS5-TUNING and NR-01 through NR-10. The Mono 20:44 correction is installed; check
current game processes and shared harness lock before any launch. No run was left active. Native public NGX
works; do not repeat SDK/caller-workaround research. Keep full Local Tone 1: owner rejected weakening the effect
to hide motion instability. Review the new sharp-placement/model evidence; resolve restoration warnings and
excluded forest motion, then obtain the owner's look verdict. Do not weaken thresholds or claim a best model.
Coordinate matching PT colour/fog/depth/motion with PT-72 and ongoing PT-71 migration before grading PT+NR.
Standalone, FoA interaction, normal-version, FG and ReShade/RenoDX combinations remain open. HDR display stays
gated. Preserve concurrent PT edits. Deploy authorization persists while FoA and Starfield are closed; launch
only through Run-PtHarness, respecting its lock. No commit/push/release is requested.
'''
    return s
edit('docs/rounds/round-08.md',round_doc)

def rounds(s):
    a=s.index('## [Round 8]');b=s.index('\n\n## [Round 7]',a)
    return s[:a]+'''## [Round 8](docs/rounds/round-08.md) - 2026-10-07 - TG Neural Rendering 1.2.2 implemented/deployed. At 20:44 both placements moved before lens effects; eight full-tone motion takes pass and stop brightness jump drops +1.130 to -0.163. New A/B/C run: 60 captures across Ron/tavern/forest, 11 report warnings; final three forest motion takes excluded. No model winner or owner look approval. PT owns its fog and geometry guides; missing depth/motion handoff tracked as PT-72/NR-10. Source checks 90/90. Game closed; 117 saves unchanged. docs/DLSS5-TUNING.md. No release zip. Parallel PT remains round-06.'''+s[b:]
edit('ROUNDS.md',rounds)
print('Updated nine NR/shared documents plus PT request record; CRLF preserved.')
