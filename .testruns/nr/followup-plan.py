import copy
import json
from pathlib import Path

root=Path('build/nr/tuning')
plan=json.loads((root/'compare.json').read_text())
steps=plan['recipes']['neural']
static_next=[]
motion_next=[]
for source in steps:
    if source['name'] in ('11-before-A','12-before-B','13-before-C'):
        step=copy.deepcopy(source)
        step['name']=source['name'][3:]+'-tone-zero'
        for pair in step['set']:
            if pair['key']=='Local Tone': pair['value']='0'
        static_next.append(step)
    if source['name']=='22-motion-before-A':
        for model in 'ABC':
            step=copy.deepcopy(source)
            step['name']='motion-before-'+model+'-tone-zero'
            for pair in step['set']:
                if pair['key']=='Local Tone': pair['value']='0'
                if pair['key']=='Model': pair['value']=model
            motion_next.append(step)
new=[]
for step in steps:
    if step['name']=='14-off-restored': new.extend(static_next)
    if step['name']=='23-motion-off-restored': new.extend(motion_next)
    step=copy.deepcopy(step)
    step['name']=step['name'][3:]
    new.append(step)
for i,step in enumerate(new): step['name']=f'{i+1:02d}-'+step['name']
plan['recipes']['neural']=new
(root/'followup.json').write_text(json.dumps(plan,indent=2)+'\n')
print(f'{len(new)} takes per scene; before-upscaling A/B/C at tone zero included.')
