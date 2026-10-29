import json
from pathlib import Path
import sys
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0,'tools/harness')
from analyze import read_frame

root=Path('artifacts/harness/nr-tavern-retry/tavern-room')
def frame(name,index=0):
    folder=root/name
    s=json.loads((folder/'step.json').read_text())
    return Image.fromarray(read_frame(folder/s['frames'][index]['file'],s['width'],s['height'],s['format']))
def tile(picture,label,width=640):
    picture=picture.resize((width,round(picture.height*width/picture.width)),Image.Resampling.LANCZOS)
    out=Image.new('RGB',(width,picture.height+26),(20,20,20))
    out.paste(picture,(0,26)); ImageDraw.Draw(out).text((8,7),label,fill='white')
    return out
names=[('01-off','Off'),('03-after-A','After: A, tone 1'),('05-after-B','After: B, tone 1'),('07-after-C','After: C, tone 1'),
       ('10-before-neutral','Before: neutral'),('11-before-A','Before: A, tone 1'),('12-before-B','Before: B, tone 1'),('13-before-C','Before: C, tone 1'),
       ('14-off-restored','Off restored'),('04-after-A-tone-zero','After: A, tone 0'),('06-after-B-tone-zero','After: B, tone 0'),('08-after-C-tone-zero','After: C, tone 0')]
sheet=Image.new('RGB',(2560,386*3))
for i,(name,label) in enumerate(names): sheet.paste(tile(frame(name),label),(i%4*640,i//4*386))
sheet.save(root/'nr-models.jpg',quality=95)
names=[('15-motion-off','Off'),('16-motion-after-A','After: A, tone 1'),('17-motion-after-A-tone-zero','After: A, tone 0'),('22-motion-before-A','Before: A, tone 1')]
sheet=Image.new('RGB',(2560,386*4))
for row,index in enumerate([0,12,24,36]):
    for col,(name,label) in enumerate(names): sheet.paste(tile(frame(name,index),f'Frame {index:02}: {label}'),(col*640,row*386))
sheet.save(root/'nr-motion-matched.jpg',quality=95)
# Quantify restoration away from the HUD, retaining all original strict report failures.
baseline=np.asarray(frame('01-off'),dtype=np.float32)
results={}
for name in ('02-after-neutral','10-before-neutral','14-off-restored'):
    image=np.asarray(frame(name),dtype=np.float32)
    h,w=image.shape[:2]
    diff=np.abs(image-baseline)[h//10:h-h//10,w//10:w-w//10]
    results[name]={'central_rgb_absolute_difference':float(diff.mean()),'central_rgb_p99':float(np.percentile(diff,99))}
(root/'nr-restoration-centre.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results))
print(root/'nr-models.jpg'); print(root/'nr-motion-matched.jpg')
