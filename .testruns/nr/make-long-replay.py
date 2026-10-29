import copy
import json
from pathlib import Path

source = Path('build/nr/tuning/replay.json')
plan = json.loads(source.read_text())
plan['scenarios'] = [s for s in plan['scenarios'] if s['key'] == 'tavern-room']
steps = []
for step in plan['recipes']['nr-stages-room']:
    if step['neural'] and not step['neuralReplayReset']:
        continue
    step = copy.deepcopy(step)
    step['name'] = f'{len(steps)+1:02d}-' + step['name'][3:]
    step.update(motionHoldFrames=600, frames=648, scale=8)
    steps.append(step)
plan['recipes'] = {'nr-stages-room': steps}
Path('build/nr/tuning/replay-long.json').write_text(json.dumps(plan, indent=2)+'\n')
