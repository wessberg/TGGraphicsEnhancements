"""Short NR compatibility runs. They do not retune the approved Quality look."""
import copy
import json
from pathlib import Path
import sys
sys.path.insert(0, 'tools/harness')
from neural_plan import make_plan, setting

out = Path('build/nr/validation-r15')
source = json.loads(Path('tests/ingame/scenarios.json').read_text(encoding='utf-8-sig'))
FG = 'com.wessberg.tgframegeneration'

def take(name, nr, fg=None, advanced='Off', mask='Off', manager=False, perf=0, strength=1):
    controls = [setting('Neural Rendering', 'On' if nr else 'Off'), setting('Model', 'A'),
                setting('Placement', 'After Upscaling'), setting('Passes', 1), setting('Strength', strength),
                setting('Local Tone', 1), setting('Detail', 1), setting('Skin Detail', 1),
                setting('Automatic Mask', mask), setting('Advanced', advanced)]
    if fg is not None:
        controls += [setting('Frame generation', fg, FG), setting('Frame generation mode', 'Fixed', FG),
                     setting('Frame multiplier', 2, FG)]
    return dict(name=name, neural=nr, traced=False, live=fg is not None or bool(perf), settle=3,
                settleFrames=48, frames=16 if fg is not None else 4, set=controls,
                neuralManager=manager, perfSeconds=perf, scale=2)

def save(name, steps):
    plan = make_plan(source, 'game')
    plan['recipes']['neural'] = steps
    (out / (name + '.json')).write_text(json.dumps(plan, indent=2) + '\n')

save('menu', [take('1-off', False, manager=True), take('2-basic', True, manager=True),
              take('3-advanced', True, advanced='On', manager=True),
              take('4-skin', True, advanced='On', mask='On', manager=True),
              take('5-restored', False, manager=True)])
save('fg', [take('1-off', False, 'Off'), take('2-nr', True, 'Off'),
            take('3-dlss-nr', True, 'DLSS'), take('4-dlss-only', False, 'DLSS'),
            take('5-fsr-nr', True, 'FSR'), take('6-xess-nr', True, 'XeSS'),
            take('7-nr-again', True, 'Off'), take('8-off-restored', False, 'Off')])
save('standalone', [take('1-off', False), take('2-neutral', True, strength=0),
                    take('3-full', True), take('4-off-restored', False)])
save('cost', [take('1-off', False, perf=15), take('2-nr', True, perf=15), take('3-off-restored', False, perf=15)])
print('Prepared menu, FG, standalone and cost plans. No game or GPU work was started.')
