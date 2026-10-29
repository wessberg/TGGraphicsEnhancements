import json,sys
from pathlib import Path
from PIL import Image,ImageDraw
sys.path.insert(0,'tools/harness')
from analyze import read_frame
from neural_analysis import counters,matched_motion

root=Path(sys.argv[1])
for scene in ('hair-eyebrows','tavern-room','forest'):
    folder=root/scene
    steps={p.name:json.loads((p/'step.json').read_text()) for p in folder.iterdir() if (p/'step.json').exists()}
    def find(suffix): return next(k for k in steps if k[3:]==suffix)
    def frame(name,index=0):
        s=steps[name]
        return Image.fromarray(read_frame(folder/name/s['frames'][index]['file'],s['width'],s['height'],s['format']))
    def tile(picture,label,width=640,height=360):
        picture=picture.resize((width,height),Image.Resampling.LANCZOS)
        result=Image.new('RGB',(width,height+26),(20,20,20));result.paste(picture,(0,26))
        ImageDraw.Draw(result).text((7,7),label,fill='white');return result
    suffixes=['off','after-A','after-B','after-C','before-neutral','before-A','before-B','before-C',
              'after-neutral','after-A-mask','before-A-mask','off-restored']
    sheet=Image.new('RGB',(2560,386*3))
    for i,suffix in enumerate(suffixes):sheet.paste(tile(frame(find(suffix)),suffix),(i%4*640,i//4*386))
    sheet.save(folder/'nr-sharp-models.jpg',quality=96)
    suffixes=['motion-off','motion-after-A','motion-after-B','motion-after-C']
    sheet=Image.new('RGB',(2560,386*4))
    for row,index in enumerate((12,48,49,79)):
        for col,suffix in enumerate(suffixes):
            sheet.paste(tile(frame(find(suffix),index),f'{suffix} / frame {index}'),(col*640,row*386))
    sheet.save(folder/'nr-sharp-motion.jpg',quality=96)
    errors={}
    for name,s in steps.items():
        expected=not (name.endswith('-off') or name.endswith('-off-restored'))
        error=counters(dict(neural=expected),s)
        base=steps[find('motion-off' if 'motion-' in name else 'off')]
        pose=matched_motion(base,s)
        if error or pose: errors[name]=dict(counters=error,pose=pose)
    (folder/'nr-sharp-evidence-errors.json').write_text(json.dumps(errors,indent=2)+'\n')
    print(scene,errors)
