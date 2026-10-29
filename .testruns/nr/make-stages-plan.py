import copy
import json
import shutil
import sys
from pathlib import Path
sys.path.insert(0,str(Path('tools/harness').resolve()))
from neural_plan import comparison_recipe

plan=json.loads(Path('tests/ingame/scenarios.json').read_text(encoding='utf-8-sig'))
steps=[]
source=comparison_recipe()
for suffix in ('motion-off','after-neutral','motion-after-A','motion-after-B',
               'before-neutral','motion-before-A','motion-before-B','motion-off-restored'):
    step=copy.deepcopy(next(s for s in source if s['name'][3:]==suffix))
    name=suffix if suffix.startswith('motion-') else 'motion-'+suffix
    step.update(name=f'{len(steps)+1:02d}-{name}',neuralStages=step['neural'],
                motionFrames=48,motionHoldFrames=96,frames=144,scale=4,slide=.65,turn=25,
                settle=2,settleFrames=60)
    steps.append(step)
face=copy.deepcopy(steps)
for step in face: step.update(slide=.2,turn=10,cameraForward=1.8,cameraFov=40)
plan['recipes']={'nr-stages-room':steps,'nr-stages-face':face}
plan['defaults']={'recipe':'nr-stages-room'}
plan['scenarios']=[s for s in plan['scenarios'] if s['key'] in ('tavern-room','hair-eyebrows')]
for scene in plan['scenarios']:
    scene['recipe']='nr-stages-face' if scene['key']=='hair-eyebrows' else 'nr-stages-room'
    scene.pop('steps',None)
    if scene['key']=='hair-eyebrows': scene['roi']=[.2,.15,.8,.85]
out=Path('build/nr/tuning/stages.json')
out.write_text(json.dumps(plan,indent=2)+'\n',encoding='utf-8')
shutil.copyfile('tests/ingame/camera.json',out.parent/'camera.json')
print(out)
