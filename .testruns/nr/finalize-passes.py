from pathlib import Path
import re

def read(name):
    return Path(name).read_text(encoding='utf-8')

def write(name, text):
    Path(name).write_bytes(text.replace('\r\n','\n').replace('\n','\r\n').encode('utf-8'))

def repair(match):
    s=match.group()
    for _ in range(6):
        try: fixed=s.encode('cp1252').decode('utf-8')
        except (UnicodeEncodeError,UnicodeDecodeError): break
        if fixed==s: break
        s=fixed
    return s

# Earlier PowerShell-fed Python used the Windows default decoder. Repair only malformed
# non-ASCII runs in the documents this NR session wrote; preserve ordinary Unicode.
docs=['BACKLOG.md','docs/PROJECT-CONTEXT.md','ROUNDS.md','CHANGELOG.md','README.md','docs/TESTING.md',
      'docs/DLSS5-IMPLEMENTATION.md','docs/DLSS5-FEEDBACK.md','docs/DLSS5-TUNING.md','docs/rounds/round-10.md']
for name in docs:
    text=read(name); fixed=re.sub(r'[^\x00-\x7f]+',repair,text)
    if fixed!=text: write(name,fixed)

replacements={
 'CHANGELOG.md': [('Strength0.5/1 match independent exact-input calls; three broken chains rejected. Game/deploy pending.',
 'Strength0.5/1 match independent exact-input calls; three broken chains rejected. NR-only deployed11:22;\n  tavern7 takes valid, correct pass counts, Off restored. Owner sees worse1pass temporal colour: shortened\n  sweep1.15s vs6s earlier; native old/new120-frame moving HDR output bit-identical (NR-22). No stability fix claimed.')],
 'docs/TESTING.md': [('menu update deployed10-08 10:31; subsequent Passes1-4 build awaits deployment, real-game UI acceptance pending.',
 'menu update deployed10-08 10:31; Passes1-4 deployed11:22 and tavern7 takes pass, real-game UI acceptance pending.')],
 'docs/DLSS5-IMPLEMENTATION.md': [('Both managed builds clean; focused tavern game check running, UI acceptance pending.',
 'Both managed builds clean; tavern7 valid (5 NR/2 Off), all native pass counts match, Off restored; UI acceptance pending.')],
 'docs/DLSS5-FEEDBACK.md': [
 ('constant; investigate an old/new binary comparison with identical moving inputs before blaming test speed.',
 'constant. Old009321545816 and newE011F0C82733 produce bit-identical output across120 synthetic HDR\nframes (held/moving/held, real model, full A, same bridge/DLL). Actual in-game one-pass sweep1.153s\nvs earlier6.001s, maximum yaw step9.57 vs3.26 degrees. Harsher movement is the supported explanation,\nnot proof against every game-specific regression. NR-14 remains open; do not soften defaults to hide it.\nThe fast recipe was a functional stress check, not comparable temporal-quality evidence. Retain the\nearlier48 moving/96 held frames for quality tests and report speed. Evidence: artifacts/harness/\nnr-passes-20261008 and build/nr/single-regression/comparison.json.'),
 ('relaxation. Deployed11:22 after PT finished; focused tavern check running. Keep defaults at one pass.',
 'relaxation. Deployed11:22 after PT finished; tavern7 takes valid, Off restored,117 saves unchanged.\nModel invocations and captured pass counts match; four passes visibly exaggerate the effect. Keep defaults at1.\nGame closed, configs restored, no queued NR launch. Menu appearance remains unverified in Video/FoA.')],
 'docs/DLSS5-TUNING.md': [('Strength0.5/1; three broken chains rejected. Native ABI0x00010300, deployed11:22; tavern check running.',
 'Strength0.5/1; three broken chains rejected. Native ABI0x00010300, deployed11:22; tavern7 valid.\nRun20261008-112319:5 NR/2 Off takes, pass counts match; four passes visibly stronger, Off restored, saves117\nunchanged. This was a fast functional stress test:16 motion/16 hold frames, not the earlier48/96 quality\nrecipe. Owner reports worse1pass instability (NR-22). Its sweep took1.153s vs6.001s before; exposure constant\nthroughout, no skips/failures/resets. Old/new native binaries give bit-identical120-frame moving HDR results.\nThis points to faster movement exposing NR-14; it is not a stability fix or a universal regression exclusion.')],
}
for name, changes in replacements.items():
    text=read(name)
    for old,new in changes:
        if old in text:
            assert text.count(old)==1,(name,old)
            text=text.replace(old,new)
        else:
            assert text.count(new)==1,(name,old)
    write(name,text)

name='docs/rounds/round-10.md'; text=read(name);pos=text.rfind('\n## ')
assert 'Starter prompt' in text[pos:]
text=text[:pos]+'''
## Deployment, tavern check and one-pass feedback (11:22-11:30)

Owner: "Try now". PT completed its report at11:22 and cleared the lock. Both games closed. Rebuilt Mono
and deployed only NR managed/native11:22:57; manifest and build/install/game hashes verified.
ManagedE3868ED67C0B0EC33E17693833FFCA83F07D3131E105859B69E98BC2B6249B34;
nativeE011F0C82733921F840F05695835D081950E3A8ED811790C42A12EDC94F87A7C. Backup:
BepInEx/TGNeuralRendering/Backups/20261008-092257-779-tuning. Metadata tuning-deployment.json and
multipass-deployment.json; previous10:31 archived advanced-menu-deployment.json. Normal build clean too;
multipass-build.json. PT/BU/FG/bridge preserved. Vortex should retain the changed NR files.

Shared harness run20261008-112319, artifacts/harness/nr-passes-20261008: one tavern/seven takes. GPU gate
passed, no other3D programs during the run. Loaded28s, all seven completed,0 failed. Five NR takes each
record65 evaluations; all native pass counts/tuning/placement match. No captured skips/failures/extra
resets; before-lens flag true. Off restored: matched brightness shift0.001, range0.026 display levels.
Exposure0.000101725258 /13 constant throughout1pass, equal to the earlier run. Stage5 valid,0 rejected.
Read overview and Quality1 atlas:4pass changes are much stronger; floor/table/clothes brighter and cooler.
This is functional evidence, not owner's quality approval. Synchronous readbacks forbid performance claims.
Game quit11:25:13;117 saves unchanged, configs restored, lock cleared, no queued NR launch. Video/FoA menu
appearance awaits direct viewing; this harness changes config and validates actual rendering.

Owner asks why1pass looked far less stable. Recorded NR-22. Short functional recipe changed motion48 to16
frames, settling60 to32, held96 to16. Actual Quality1 sweep1.1528324s vs6.000732s in
nr-live-models-retry2-20261008; yaw changes9.5674 vs3.26326 degrees per frame. This is much harsher motion;
I should have kept timing consistent for a quality comparison. Evaluation counts do not prove stability.
Current held model-effect range7.8653 Quality1,13.6524 Performance1; different hold lengths/input motion
forbid direct quality ranking against the old run.

An isolated dual-ABI probe runs120 full-A HDR frames: held / translated / held, matching depth/motion,
one initial reset, no skips/failures. Old009321545816 is the actual backed-up installed native binary,
newE011F0C82733 the deployed binary; same signed runtime and installed bridge. All120 output frames are
bit-identical (max/mean error0). Evidence: build/nr/single-regression/comparison.json; preparation script
.testruns/nr/prepare-single-regression.py. No game swap/deploy for that comparison. Faster movement exposing
existing NR colour instability is supported; every game-specific interaction is not ruled out. NR-14 stays
open, defaults settled. No more game runs started, no broad model sweep, no zip/git commit/push.

The owner now wants to play personally and judge stability. No further NR GPU tests, deployment or game
launch while they do so. All NR harness work is stopped; wait for their feedback.

## \u25b6 Starter prompt (R11)

Owner play-testing: leave the game/GPU alone and await feedback. NR-21 deployed11:22 and passes tavern7
plus exact GPU chain checks. No NR game/waiter. Both placements
settled: A/full/maskOff, default Quality, Passes1. Strength/tone/detail apply per pass, not final opacity.
Native ABI0x00010300. Video/FoA UI acceptance and normal-game compatibility remain separate from compile/
render checks. NR-22: owner saw worse1pass motion in the shortened test; native old/new120-frame synthetic
motion output identical, game sweep1.15s vs6s earlier, constant exposure/no resets/skips. Keep quality tests
at48 moving/96 held frames and report speed. No stability fix claimed. NR-14, FG/BU marker audit NR-19,
PT-72 remain open. Preserve PT edits/lock. No zip/git commit/push. Vortex must retain updated NR files.
'''
write(name,text)
print('Updated handoff; backlog bytes',Path('BACKLOG.md').stat().st_size)
