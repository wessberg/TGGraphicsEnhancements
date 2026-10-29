import json, sys
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, 'tools/harness')
from analyze import read_frame
from neural_analysis import matched_motion

root = Path(sys.argv[1])
steps = {p.name: json.loads((p / 'step.json').read_text()) for p in root.iterdir() if (p / 'step.json').exists()}
def frame(name, index=0):
    s = steps[name]
    return Image.fromarray(read_frame(root/name/s['frames'][index]['file'], s['width'], s['height'], s['format']))
def tile(picture, label):
    picture = picture.resize((640, 360), Image.Resampling.LANCZOS)
    out = Image.new('RGB', (640,386), (20,20,20))
    out.paste(picture,(0,26)); ImageDraw.Draw(out).text((8,7),label,fill='white')
    return out
names = [('01-off','Off'), ('03-after-A','After A / tone 1'), ('05-after-B','After B / tone 1'), ('07-after-C','After C / tone 1'),
         ('02-after-neutral','After neutral'), ('04-after-A-tone-zero','After A / tone 0'), ('06-after-B-tone-zero','After B / tone 0'), ('08-after-C-tone-zero','After C / tone 0'),
         ('10-before-neutral','Before neutral'), ('11-before-A','Before A / tone 1'), ('12-before-B','Before B / tone 1'), ('13-before-C','Before C / tone 1')]
names = [(n,l) for n,l in names if n in steps]
sheet=Image.new('RGB',(2560,386*((len(names)+3)//4)))
for i,(name,label) in enumerate(names): sheet.paste(tile(frame(name),label),(i%4*640,i//4*386))
sheet.save(root/'nr-models.jpg',quality=95)
motion=[]
for suffix,label in [('motion-off','Off'),('motion-after-A','After A / tone 1'),('motion-after-A-tone-zero','After A / tone 0'),('motion-before-A','Before A / tone 1')]:
    name=next((n for n in steps if n.endswith('-'+suffix)),None)
    if name: motion.append((name,label))
if len(motion)==4:
    sheet=Image.new('RGB',(2560,386*4))
    for row,index in enumerate([0,12,24,36]):
        for col,(name,label) in enumerate(motion): sheet.paste(tile(frame(name,index),f'Frame {index:02}: {label}'),(col*640,row*386))
    sheet.save(root/'nr-motion-matched.jpg',quality=95)
baseline=steps['01-off']
poses={n:matched_motion(baseline,s,'static') for n,s in steps.items() if 'motion-' not in n}
(root/'nr-static-poses.json').write_text(json.dumps(poses,indent=2)+'\n')
print(root, {n:v for n,v in poses.items() if v})
