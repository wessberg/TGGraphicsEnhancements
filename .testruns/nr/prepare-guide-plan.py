import copy
import json
import sys
from pathlib import Path
sys.path.insert(0, str(Path('tools/harness').resolve()))
from neural_chairs import plan

p = plan(json.loads(Path('tests/ingame/scenarios.json').read_text(encoding='utf-8-sig')))
original = p['recipes']['nr-chairs']
steps = [original[1], original[2]]
names = ['01-quality-neutral', '02-quality-full']
for i, (step, name) in enumerate(zip(steps, names)):
    step.update(name=name, frames=128, motionFrames=96, motionHoldFrames=32, walkForward=4, walkSpeed=1.2,
                disableLensEffects=True, disableMotionBlur=True)
    if step['neural']:
        step['neuralToneMap'] = 0
p['recipes']['nr-chairs'] = steps
p['scenarios'][0]['tests'] = 'Quality input audit during a 1.2 m/s walk. Raw DLSS and NR guide pixels, neutral/full colour. No per-frame colour comparison across takes.'
Path('build/nr/guide-plan.json').write_text(json.dumps(p, indent=2)+'\n', encoding='utf-8')
