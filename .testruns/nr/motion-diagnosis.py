import json,sys
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
sys.path.insert(0,'tools/harness')
from analyze import read_frame
from neural_analysis import matched_motion,colour_motion
root=Path(sys.argv[1]); steps={}; report={}
for folder in sorted(root.iterdir()):
    if not (folder/'step.json').exists(): continue
    s=json.loads((folder/'step.json').read_text())
    rgb=[]; selected={}
    for i,info in enumerate(s['frames']):
        a=read_frame(folder/info['file'],s['width'],s['height'],s['format'])
        h,w=a.shape[:2]; rgb.append(a[h//10:h-h//10,w//10:w-w//10].mean(axis=(0,1)))
        if i in (0,48,79): selected[i]=Image.fromarray(a)
    steps[folder.name]=(s,np.array(rgb),selected)
baselines={}
for name,(s,rgb,_) in steps.items():
    group='no-blur' if '-no-blur-' in name else 'game-blur'
    if group not in baselines:
        assert s['neuralEvaluations']==0
        baselines[group]=(s,rgb)
    base=baselines[group]
    mismatch=matched_motion(base[0],s)
    record={'pose_error':mismatch,'menuOpen':s['menuOpen'],'step_error':s['error'],
            'blur_states':sorted(set(str(f.get('motionBlurActive')) for f in s['frames']))}
    f=s['frames']
    record['frame_lag']=sorted(set(x['frame']-x['neuralFrame'] for x in f))
    for key in ('neuralEvaluated','neuralSkipped','neuralFailed','neuralResets'):
        record[key]=[f[0][key],f[-1][key]]
    if not mismatch:
        delta=rgb-base[1]; luma=delta@np.array([.2126,.7152,.0722])
        record.update(colour_motion(base[1],rgb))
        record['luma_by_frame']=luma.tolist()
        record['rgb_by_frame']=delta.tolist()
        # Frame 0 follows a hold and is sharp. Frame 48 is the first home-pose frame,
        # still blurred by the previous moving pose; 49 is the next stationary frame.
        record['first_home_luma']=float(luma[48])
        record['next_home_luma']=float(luma[49])
        record['stop_jump']=float(luma[49]-luma[48])
        record['same_pose_stopped_luma']=float(luma[72:].mean())
    report[name]=record
(root/'motion-diagnosis.json').write_text(json.dumps(report,indent=2)+'\n')
for name,v in report.items(): print(name,{k:x for k,x in v.items() if k not in ('luma_by_frame','rgb_by_frame')})
try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig,axes=plt.subplots(2,1,figsize=(11,7),sharex=True)
    for ax,group in zip(axes,('game-blur','no-blur')):
        for name,v in report.items():
            if '-'+group+'-' not in name or name.endswith('motion-off'): continue
            if 'luma_by_frame' in v: ax.plot(v['luma_by_frame'],label=name[3+len(group)+1:])
        ax.axvspan(48,79,color='gray',alpha=.15,label='camera held still')
        ax.set(title=group,ylabel='Brightness change vs matching Off (0-255)'); ax.legend(); ax.grid(alpha=.2)
    axes[-1].set_xlabel('Captured frame'); fig.tight_layout(); fig.savefig(root/'motion-brightness.png',dpi=150)
except ImportError: pass
names=[n for n in steps if n.endswith('motion-off') or n.endswith('motion-after-A')]
sheet=Image.new('RGB',(640*len(names),386*3))
for col,name in enumerate(names):
    for row,index in enumerate((0,48,79)):
        tile=Image.new('RGB',(640,386),(20,20,20))
        tile.paste(steps[name][2][index].resize((640,360),Image.Resampling.LANCZOS),(0,26))
        ImageDraw.Draw(tile).text((5,7),f'{name} / frame {index}',fill='white')
        sheet.paste(tile,(640*col,386*row))
sheet.save(root/'motion-stop-comparison.jpg',quality=95)
