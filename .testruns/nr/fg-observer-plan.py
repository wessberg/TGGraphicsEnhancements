import json, sys
from pathlib import Path
sys.path.insert(0,'tools/harness')
from neural_plan import make_plan, setting
plan=make_plan(json.loads(Path('tests/ingame/scenarios.json').read_text(encoding='utf-8-sig')),'game',True)
steps=[]
for name,place,fg,nr in [('after-off','After Upscaling','Off','On'),('after-fg','After Upscaling','DLSS','On'),('before-fg','Before Upscaling','DLSS','On'),('restored','After Upscaling','Off','Off')]:
    steps.append(dict(name=f'{len(steps)+1:02d}-{name}',neural=nr=='On',traced=False,settle=5,settleFrames=160,frames=8,
        set=[setting('Neural Rendering',nr),setting('Model','A'),setting('Placement',place),setting('Strength',1),setting('Local Tone',1),setting('Automatic Mask','Off'),
             setting('Frame generation',fg,'com.wessberg.tgframegeneration'),setting('Frame generation mode','Fixed','com.wessberg.tgframegeneration'),setting('Frame multiplier',2,'com.wessberg.tgframegeneration')]))
plan['recipes']['neural']=steps
Path('build/nr/tuning/fg-observer.json').write_text(json.dumps(plan,indent=2)+'\n')
