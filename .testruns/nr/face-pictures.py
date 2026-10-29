import json, sys
from pathlib import Path
from PIL import Image, ImageDraw
sys.path.insert(0,'tools/harness')
from analyze import read_frame
from neural_analysis import matched_motion, colour_motion
import numpy as np
root=Path('artifacts/harness/nr-face-20261007/hair-eyebrows')
request=json.loads((root.parent/'request.json').read_text(encoding='utf-8-sig'))
requested=next(s for s in request['scenarios'] if s['key']==root.name)['steps']
steps={s['name'][3:]:(root/s['name'],json.loads((root/s['name']/'step.json').read_text())) for s in requested}
def frame(name,index=0):
    p,s=steps[name]
    return Image.fromarray(read_frame(p/s['frames'][index]['file'],s['width'],s['height'],s['format']))
def sheet(names,columns,path,width=640,indices=None):
    height=width*9//16; rows=(len(names)+columns-1)//columns
    result=Image.new('RGB',(columns*width,rows*(height+24)),(20,20,20))
    draw=ImageDraw.Draw(result)
    for i,name in enumerate(names):
        index=indices[i] if indices else 0
        x,y=i%columns*width,i//columns*(height+24)
        result.paste(frame(name,index).resize((width,height),Image.Resampling.LANCZOS),(x,y+24))
        draw.text((x+6,y+5),name+(f' / frame {index}' if indices else ''),fill='white')
    result.save(root/path,quality=96)
sheet(['off','after-A','after-B','after-C','off','before-A','before-B','before-C'],4,'face-models.jpg')
names=[]
for where in ('after','before'):
    for mask in ('off','skin1','skin0'):
        for model in 'ABC':
            name=f'{where}-{model}'
            if mask!='off': name+= '-mask' if model=='A' and mask=='skin1' else f'-mask-{mask}'
            names.append(name)
sheet(names,3,'face-masks.jpg',width=640)
names=[]; indices=[]
for index in (12,48,49,79):
    for name in ('motion-off','motion-after-A','motion-after-B','motion-after-C'):
        names.append(name); indices.append(index)
sheet(names,4,'face-motion.jpg',indices=indices)
print('Wrote face-models.jpg, face-masks.jpg and face-motion.jpg')
