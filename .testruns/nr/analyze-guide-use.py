from pathlib import Path
import json
import numpy as np
from PIL import Image, ImageDraw

p=Path('build/nr/guide-use-check')
shape=(40,512,512,4)
names=['guided','no-motion','no-depth','reset-each','guided-repeat','tone-guided','tone-no-motion','tone-reset-each']
images={n:np.memmap(p/(n+'.f32'),dtype='<f4',mode='r',shape=shape) for n in names}
report={}
for a,b in [('guided',n) for n in names[1:5]]+[('tone-guided','tone-no-motion'),('tone-guided','tone-reset-each')]:
    d=np.abs(images[a][...,:3]-images[b][...,:3])
    report[a+' vs '+b]=dict(equal=bool(np.array_equal(images[a],images[b])),mean=float(d.mean()),maximum=float(d.max()),frame_mean=d.mean(axis=(1,2,3)).tolist())

def encode(v):
    return np.where(v<=.0031308,12.92*v,1.055*np.maximum(v,0)**(1/2.4)-.055)

source=np.fromfile(p/'color.f32',dtype='<f4').reshape(512,640,4)[...,:3]
offsets=[0 if f<8 else 2*min(f-7,24) for f in range(40)]
# Compare the same recorded surface pixels after exact integer translation.
# Exclude newly revealed pixels and 32-pixel image-edge margins throughout.
reference=source[:,80:480]
shoulder=1+reference.max(axis=-1,keepdims=True)
input_encoded=encode(reference/shoulder)*255
movement={}
encoded={}
for name in names:
    aligned=np.stack([images[name][f,:,80-o:480-o,:3] for f,o in enumerate(offsets)])
    # Undo only the adapter's fixed input shoulder to inspect model-space colour.
    values=encode(aligned/shoulder)*255
    encoded[name]=values
    blocks=values[:,32:480].reshape(40,28,16,25,16,3).mean(axis=(2,4))
    step=np.abs(np.diff(blocks[8:32],axis=0)).mean(axis=-1)
    span=np.ptp(blocks[8:32],axis=0).mean(axis=-1)
    movement[name]=dict(block_step_p95=float(np.percentile(step,95)),block_range_p95=float(np.percentile(span,95)),block_range_max=float(span.max()),mean_effect=float(np.abs(values[8:32]-input_encoded).mean()))
report['tracked_model_colour']=movement
(p/'comparison.json').write_text(json.dumps(report,indent=2))
canvas=Image.new('RGB',(400*4,512*3+26*3),(20,20,20))
draw=ImageDraw.Draw(canvas)
for row,name in enumerate(['guided','tone-guided','tone-reset-each']):
    for col,f in enumerate([7,15,23,31]):
        y=row*538
        draw.text((col*400+5,y+5),f'{name}, frame {f}',fill='white')
        image=np.uint8(np.clip(encoded[name][f][::-1],0,255))
        canvas.paste(Image.fromarray(image),(col*400,y+26))
canvas.save(p/'same-surface.jpg',quality=94)
print(json.dumps({k:({i:v for i,v in val.items() if i!='frame_mean'} if k!='tracked_model_colour' else val) for k,val in report.items()},indent=2))
