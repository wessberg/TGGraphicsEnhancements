"""Generate a focused A/B/C repeated-input comparison using the checked shared NR recipe."""
import copy
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path('tools/harness').resolve()))
from neural_plan import make_plan, stage_recipe

def recipe(face):
    base, result = stage_recipe(face), []
    for suffix in ('motion-off', 'motion-after-A', 'motion-before-A', 'motion-off-restored'):
        source = next(s for s in base if s['name'][3:] == suffix)
        for model in ('ABC' if source['neural'] else 'A'):
            take = copy.deepcopy(source)
            name = suffix.replace('-A', '-'+model)
            take.update(name=f'{len(result)+1:02d}-{name}-retained' if source['neural'] else f'{len(result)+1:02d}-{name}',
                        neuralReplay=source['neural'], neuralReplayReset=source['neural'],
                        motionHoldFrames=192, frames=240, scale=4)
            for s in take['set']:
                if s['key'] == 'Model': s['value'] = model
            result.append(take)
    return result

plan=make_plan(json.loads(Path('tests/ingame/scenarios.json').read_text(encoding='utf-8-sig')), 'game')
plan['recipes']={'nr-models-face':recipe(True),'nr-models-room':recipe(False)}
plan['defaults']={'recipe':'nr-models-room'}
plan['scenarios']=[s for s in plan['scenarios'] if s['key'] in ('hair-eyebrows','tavern-room')]
assert len(plan['scenarios'])==2
for s in plan['scenarios']:
    s['recipe']='nr-models-face' if s['key']=='hair-eyebrows' else 'nr-models-room'
    if s['key']=='hair-eyebrows': s['roi']=[.2,.15,.8,.85]
Path('build/nr/tuning/model-history.json').write_text(json.dumps(plan,indent=2)+'\n')
print('16 takes; A/B/C at full strength/tone, both placements, 48 moving and 192 held frames with retained NR inputs.')
