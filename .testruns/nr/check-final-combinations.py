import json
from pathlib import Path
root = Path('artifacts/harness/nr-validation-combinations-r15')
steps = sorted((root/'tavern-room').glob('*/step.json'), key=lambda p: int(p.parent.name.split('-')[0]))
assert len(steps)==11
report=[]
for file in steps:
    s=json.loads(file.read_text())
    i=int(s['name'].split('-')[0])
    assert not s['error'] and not s['menuOpen'] and len(s['frames'])==16
    assert s['neuralEvaluations']==(0 if i in (2,4,8,11) else 16)
    assert all(f['neuralFailed']==0 and f['neuralSkipped']==0 for f in s['frames'])
    if i in (2,4):
        first,last=[f['upscaling'] for f in (s['frames'][0],s['frames'][-1])]
        assert first['ActiveBackend']==last['ActiveBackend']==(1 if i==2 else 2)
        assert last['FramesUpscaled']>first['FramesUpscaled']
        assert first['FramesFailed']==last['FramesFailed']==0
        assert first['FramesFallback']==last['FramesFallback']==0
        menu=json.loads((file.parent/'foa-menu.json').read_text())
        assert len(menu)==1 and menu[0]['choices']==['Off'] and menu[0]['value']=='Off'
        assert 'Select DLSS' in menu[0]['description']
    if i==6:
        assert 'DLSS quality mode: Performance -> DLAA' in s['applied']
        # Actual DLSS packet depth area contributes 8 bytes/pixel to the NR transfer counter.
        area=s['neuralWidth']*s['neuralHeight']
        assert (s['neuralTransferBytes']-area*32)//8==area
    if s['perf']['measured']:
        assert s['perf']['seconds']>=15 and s['perf']['vSyncCount']==0 and s['perf']['targetFrameRate']==-1
    report.append(dict(name=s['name'],nr=s['neuralEvaluations'],fps=s['perf']['fps'],nrGpuMs=s['neuralGpuMs']))
assert not json.loads((root/'gpu.json').read_text(encoding='utf-8-sig'))['during']
(root/'combination-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: 11 valid takes, FSR/XeSS real upscaler dispatches and NR/FoA gate, automatic DLSS return, full-resolution DLAA guides, four uncapped performance intervals.')
