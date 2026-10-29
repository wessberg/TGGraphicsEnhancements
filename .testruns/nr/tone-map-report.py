"""One matched Quality chair comparison of two HDR compression operators."""
import json
from pathlib import Path
import sys
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/harness'))
from analyze import read_frame
from neural_chairs import check_walk
from neural_analysis import counters, matched_motion
from neural_stages import analyze_step

root = Path(sys.argv[1])
request = json.loads((root/'request.json').read_text(encoding='utf-8-sig'))
scene, = request['scenarios']
takes = scene['steps']
assert len(takes) == 5 and [t.get('neural') for t in takes] == [False, True, True, True, False]
assert [t.get('neuralToneMap') for t in takes[1:4]] == [0, 1, 0]
captures, metrics, screens = [], {}, []
indices = [0, 48, 95, 143, 191]
for take in takes:
    directory = root/scene['key']/take['name']
    step = json.loads((directory/'step.json').read_text(encoding='utf-8-sig'))
    assert not step.get('error') and not step.get('menuOpen')
    assert not counters(take, step)
    walk = check_walk(take, step['frames'])
    if captures: assert not matched_motion(captures[0], step)
    values = {x['key']: x['value'] for x in take['set']}
    assert values['Model'] == 'A' and values['Placement'] == 'After Upscaling' and values['Passes'] == '1'
    assert all(float(values[k]) == 1 for k in ('Strength', 'Local Tone', 'Detail', 'Skin Detail'))
    assert values['Automatic Mask'] == 'Off'
    data = analyze_step(directory, take) if take.get('neuralStages') else {}
    metrics[take['name']] = dict(walk=walk, stages=data)
    selected, absolute, channels = {}, 0., np.zeros(3)
    for i,f in enumerate(step['frames']):
        frame=read_frame(directory/f['file'],step['width'],step['height'],step['format'])
        if i in indices: selected[i]=frame
        if captures:
            first=captures[0]
            baseline=read_frame(root/scene['key']/takes[0]['name']/first['frames'][i]['file'],first['width'],first['height'],first['format'])
            delta=frame.astype(np.float32)-baseline.astype(np.float32)
            absolute+=float(np.abs(delta).mean());channels+=delta.mean((0,1))
    screens.append(selected)
    if captures:
        metrics[take['name']]['screen_effect_mean_abs'] = absolute/len(step['frames'])
        metrics[take['name']]['screen_effect_mean_rgb'] = (channels/len(step['frames'])).tolist()
    captures.append(step)
tilew, tileh, label = 480, 270, 22
atlas = Image.new('RGB',(tilew*5,(tileh+label)*len(indices)), '#181818')
draw=ImageDraw.Draw(atlas)
for row, index in enumerate(indices):
    for col, (take, frames) in enumerate(zip(takes,screens)):
        frame=Image.fromarray(np.uint8(np.clip(frames[index],0,255))).resize((tilew,tileh),Image.Resampling.LANCZOS)
        x,y=col*tilew,row*(tileh+label)
        draw.text((x+5,y+4),f"{take['name']} / frame {index}",fill='white')
        atlas.paste(frame,(x,y+label))
atlas.save(root/'nr-tone-map-atlas.jpg',quality=94)
(root/'nr-tone-map-comparison.json').write_text(json.dumps(metrics,indent=2)+'\n')
print(json.dumps({name:dict(speed=m['walk']['metres_per_second'],effect=m.get('screen_effect_mean_abs'),
    held_effect=m['stages'].get('model_effect',{}).get('hold_max_range'),
    tail_effect=m['stages'].get('model_effect',{}).get('tail32_max_range'),
    fallback=m['stages'].get('model_output_range',{}).get('fallback_fraction_max'))
    for name,m in metrics.items()},indent=2))
