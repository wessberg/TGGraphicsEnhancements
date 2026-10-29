"""Inspect full-effect conditioning captures; use only completed, matching, validated takes."""
from pathlib import Path
import json
import sys
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, str(Path('tools/harness').resolve()))
from neural_stages import read_frame, analyze_step
from neural_analysis import matched_motion
from analyze import read_frame as read_screen

root = Path(sys.argv[1])
request = json.loads((root / 'request.json').read_text(encoding='utf-8-sig'))
rows = []
for scene in request['scenarios']:
    if len(sys.argv) > 2 and scene['key'] != sys.argv[2]: continue
    directory = root / scene['key']
    reference = json.loads((directory / scene['steps'][0]['name'] / 'step.json').read_text(encoding='utf-8-sig'))
    tiles = []
    for take in scene['steps']:
        folder = directory / take['name']
        step = json.loads((folder / 'step.json').read_text(encoding='utf-8-sig'))
        assert not step.get('error') and not step.get('menuOpen'), take['name']
        assert not matched_motion(reference, step), take['name']
        rgb = read_screen(folder / step['frames'][-1]['file'], step['width'], step['height'], step['format'])
        tile = Image.fromarray(rgb).resize((640, 360))
        titled = Image.new('RGB', (640, 389), '#161616'); titled.paste(tile, (0, 29))
        ImageDraw.Draw(titled).text((8, 8), take['name'], fill='white'); tiles.append(titled)
        if not take.get('neuralStages'): continue
        controls = {s['key']: s['value'] for s in take['set']}
        assert all(float(controls[k]) == 1 for k in ('Strength', 'Local Tone', 'Detail', 'Skin Detail'))
        measurement = analyze_step(folder, take)
        stages = np.asarray([read_frame(folder / 'nr-stages' / f"nr-{f['frame']}.bin")[1][:4, ..., :3] for f in step['frames']], dtype=np.float64)
        last = stages[-32:]
        row = dict(scene=scene['key'], take=take['name'], placement=controls['Placement'], scale=measurement['input_scale'],
                   input_mean=float(last[:, 1].mean()*255), model_effect_mean_absolute=float(np.abs(last[:, 2]-last[:, 1]).mean()*255),
                   model_effect_hold_range=measurement['model_effect']['hold_max_range'],
                   model_effect_tail_range=measurement['model_effect']['tail32_max_range'],
                   scene_effect_mean_absolute=float(np.abs(last[:, 3]-last[:, 0]).mean()),
                   exposure_range=measurement['hold_exposure_range'], motion_hold=measurement['motion_hold_max_mean_pixels'])
        off_tail=[]; on_tail=[]
        for a,b in zip(reference['frames'][-32:],step['frames'][-32:]):
            off_tail.append(read_screen(directory/scene['steps'][0]['name']/a['file'],reference['width'],reference['height'],reference['format']).astype(float))
            on_tail.append(read_screen(folder/b['file'],step['width'],step['height'],step['format']).astype(float))
        row['display_effect_mean_absolute']=float(np.abs(np.asarray(on_tail)-np.asarray(off_tail)).mean())
        rows.append(row)
    atlas=Image.new('RGB',(1280,389*((len(tiles)+1)//2)))
    for i,tile in enumerate(tiles): atlas.paste(tile,((i%2)*640,(i//2)*389))
    atlas.save(directory/'signal-final-frames.jpg',quality=95)
(root/('signal-summary-'+sys.argv[2]+'.json' if len(sys.argv)>2 else 'signal-summary.json')).write_text(json.dumps(rows,indent=2)+'\n')
print('scene / placement / input scale / input mean / effect mean / held range / final32 range / display effect')
for r in rows:
    print(f"{r['scene']:14} {r['placement']:16} {r['scale']:4g} {r['input_mean']:7.2f} {r['model_effect_mean_absolute']:7.2f} {r['model_effect_hold_range']:7.2f} {r['model_effect_tail_range']:7.2f} {r['display_effect_mean_absolute']:7.2f}")
