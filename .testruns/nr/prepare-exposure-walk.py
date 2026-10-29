from pathlib import Path
import copy
import json

root=Path.cwd()
plan=json.loads((root/'build/nr/guide-plan.json').read_text())
template=plan['recipes']['nr-chairs'][1]
steps=[]
for name,scale in [('01-input1',1),('02-input025',.25),('03-input1-repeat',1)]:
    step=copy.deepcopy(template)
    step.update(name=name,neuralInputScale=scale)
    steps.append(step)
plan['recipes']['nr-chairs']=steps
plan['scenarios'][0]['tests']='Quality/full-tone exposure during a 1.2 m/s walk. Input multiplier 1, 0.25, 1. Diagnostic input scaling is undone after NR. Fixed speed means per-frame poses can differ.'
(root/'build/nr/exposure-walk-plan.json').write_text(json.dumps(plan,indent=2))

print('Prepared three Quality takes. No default or game-file change.')
