from pathlib import Path
import json, shutil, sys

root=Path(__file__).resolve().parents[2]
deploy=(root/'.testruns/nr/deploy.ps1').read_text(encoding='utf-8-sig')
deploy=deploy.replace("'build/nr/deploy'", "'build/nr/observer-deploy'").replace('1.2.2', '1.2.3')
deploy=deploy.replace("if ($Install) { Assert-TgGameClosed }", """if ($Install) {
    Assert-TgGameClosed
    if (Get-Process -Name 'Starfield' -ErrorAction SilentlyContinue) { throw 'Starfield is running; nothing deployed.' }
    if (Test-Path -LiteralPath (Join-Path $repo 'artifacts/harness/RUNNING')) { throw 'Another coordinator owns the harness; nothing deployed.' }
}""")
(root/'.testruns/nr/deploy-observer.ps1').write_text(deploy,encoding='utf-8',newline='\r\n')
plan=json.loads((root/'build/nr/tuning/motion-diagnosis.json').read_text(encoding='utf-8-sig'))
sys.path.insert(0,str(root/'tools/harness'))
from neural_plan import comparison_recipe
face=[s for s in json.loads((root/'tests/ingame/scenarios.json').read_text(encoding='utf-8-sig'))['scenarios'] if s['key']=='hair-eyebrows'][0]
face['recipe']='face-check'
face['roi']=[.2,.15,.8,.85]
steps=comparison_recipe()[:1]
steps[0].update(cameraForward=1.8,cameraFov=40,frames=2,settle=4,settleFrames=120)
plan['recipes']['face-check']=steps
plan['scenarios']=[s for s in plan['scenarios'] if s['key']=='tavern-room']+[face]
(root/'build/nr/tuning/observer-diagnostic.json').write_text(json.dumps(plan,indent=2)+'\n')
# Update the legacy image probe to the same freshly installed binaries.
target=root/'build/nr/probe/BepInEx/TGNeuralRendering/Native'
for name in ('TGNeuralRendering.Native.dll','TGGraphicsBridge.Native.dll'):
    shutil.copyfile(root/'build/nr'/name,target/name)
print('Prepared observer deployment and tavern/face diagnostic plan')
