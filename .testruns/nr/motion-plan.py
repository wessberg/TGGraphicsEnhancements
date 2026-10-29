import copy,json
from pathlib import Path
source=json.loads(Path('build/nr/tuning/compare.json').read_text())
recipe=source['recipes']['neural']
steps=[]
for suppress in (False,True):
    for suffix in ('motion-off','motion-after-A','motion-before-A','motion-off-restored'):
        base=next(s for s in recipe if s['name'].endswith('-'+suffix))
        step=copy.deepcopy(base)
        step.update(name=f'{len(steps)+1:02}-'+('no-blur-' if suppress else 'game-blur-')+suffix,
                    disableMotionBlur=suppress,motionFrames=48,motionHoldFrames=32,frames=80,
                    settle=1,settleFrames=16,scale=2)
        steps.append(step)
source['recipes']['neural']=steps
Path('build/nr/tuning/motion-diagnosis.json').write_text(json.dumps(source,indent=2)+'\n')
print(len(steps),'matched motion/hold steps, full tone and structure, two blur conditions')
