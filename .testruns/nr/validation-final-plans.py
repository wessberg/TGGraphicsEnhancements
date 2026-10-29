import copy
import json
from pathlib import Path

root = Path('build/nr/validation-r15')
source = json.loads((root / 'fg.json').read_text())
nr = 'com.wessberg.tgneuralrendering'
bu = 'com.wessberg.tgbetterupscaling'

def set_(plugin, key, value):
    return dict(plugin=plugin, section='Video', key=key, value=str(value))

def take(name, active=True, upscaler='DLSS', quality='Performance', fg='Off', perf=0, manager=False):
    step = copy.deepcopy(source['recipes']['neural'][1])
    step.update(name=name, neural=active, live=True, perfSeconds=perf, neuralManager=manager, frames=16)
    # Preserve NR's On preference through unsupported upscalers, testing its automatic return.
    step['set'] = [x for x in step['set'] if not (x['plugin']==nr and x['key']=='Neural Rendering')]
    for pair in step['set']:
        if pair['key']=='Frame generation': pair['value']=fg
    step['set'] += [set_(bu, 'Upscaler', upscaler), set_(bu, 'DLSS quality mode', quality)]
    return step

steps = [take('1-dlss'), take('2-fsr-gate', False, 'FSR', manager=True), take('3-return'),
         take('4-xess-gate', False, 'XeSS', manager=True), take('5-return'),
         take('6-dlaa', quality='DLAA'), take('7-dlss-fg-nr', fg='DLSS', perf=15),
         take('8-dlss-fg-only', active=False, fg='DLSS', perf=15),
         take('9-fsr-fg-nr', fg='FSR', perf=15), take('10-xess-fg-nr', fg='XeSS', perf=15),
         take('11-off', active=False)]
steps[0]['set'].append(set_(nr, 'Neural Rendering', 'On'))
steps[7]['set'].append(set_(nr, 'Neural Rendering', 'Off'))
steps[8]['set'].append(set_(nr, 'Neural Rendering', 'On'))
steps[10]['set'].append(set_(nr, 'Neural Rendering', 'Off'))
source['recipes']['neural'] = steps
source['scenarios'] = [next(s for s in source['scenarios'] if s['key']=='tavern-room')]
(root/'final-combinations.json').write_text(json.dumps(source, indent=2)+'\n')
print('Prepared remaining upscaler gates, DLAA and FG real-frame cost checks.')
