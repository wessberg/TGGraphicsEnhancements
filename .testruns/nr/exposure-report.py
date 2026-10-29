import csv
import json
import sys
from pathlib import Path
import cv2
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, str(Path('tools/harness').resolve()))
from analyze import read_frame
from neural_stages import read_frame as read_stage, analyze_step
from neural_analysis import counters
from neural_chairs import check_walk

root = Path(sys.argv[1] if len(sys.argv)>1 else 'artifacts/harness/nr-exposure-walk-retry-20261008')
request = json.loads((root/'request.json').read_text(encoding='utf-8-sig'))
scene, = request['scenarios']
take_data, metrics = [], []
sheet = Image.new('RGB', (1440, 1800), (20, 20, 20))
draw = ImageDraw.Draw(sheet)
for col, take in enumerate(scene['steps']):
    folder = root/scene['key']/take['name']
    step = json.loads((folder/'step.json').read_text(encoding='utf-8-sig'))
    assert not step['error'] and not counters(take, step) and not step.get('menuOpen')
    route = check_walk(take, step['frames'])
    analyze_step(folder, take)
    positions = np.array([[f[k] for k in ('px','py','pz')] for f in step['frames']])
    distance = np.linalg.norm(positions-positions[0], axis=1)
    screens = np.array([read_frame(folder/f['file'], step['width'], step['height'], step['format'])[..., :3] for f in step['frames']])
    effects, restored_effects, in_color, output, motion = [], [], [], [], []
    def common_display(rgb):
        rgb = np.maximum(rgb, 0)
        rgb = rgb/(1+rgb.max(axis=-1, keepdims=True))
        return np.where(rgb <= .0031308, rgb*12.92, 1.055*rgb**(1/2.4)-.055)*255
    with (folder/'nr-stages/frames.csv').open() as stream:
        metadata = {int(row['frame']): row for row in csv.DictReader(stream)}
    for f in step['frames']:
        _, grids, _ = read_stage(folder/'nr-stages'/f"nr-{f['frame']}.bin")
        in_color.append(grids[1, ..., :3]*255)
        output.append(grids[2, ..., :3]*255)
        effects.append((grids[2, ..., :3]-grids[1, ..., :3])*255)
        # Use a common scale AFTER the exposure compensation. Different model input exposures
        # otherwise make their encoded model-space differences an unfair stability comparison.
        restored_effects.append(common_display(grids[3, ..., :3])-common_display(grids[0, ..., :3]))
        row = metadata[f['frame']]
        motion.append(grids[6, ..., :2]*[64/float(row['width']), 36/float(row['height'])])
    effects, restored_effects, in_color, output = map(np.asarray, (effects, restored_effects, in_color, output))
    yy, xx = np.indices((36, 64), np.float32)
    errors, restored_errors = [], []
    for i in range(3, len(effects)):
        if distance[i]-distance[i-1] < .002: continue
        mapping = np.stack([xx, yy], axis=-1)+motion[i]
        warped = cv2.remap(effects[i-1], mapping[..., 0].astype(np.float32), mapping[..., 1].astype(np.float32), cv2.INTER_LINEAR)
        valid = (mapping[..., 0] >= 1) & (mapping[..., 0] < 62) & (mapping[..., 1] >= 1) & (mapping[..., 1] < 34)
        errors.append(np.abs(effects[i]-warped)[valid].max(axis=-1))
        restored_warped = cv2.remap(restored_effects[i-1], mapping[..., 0].astype(np.float32), mapping[..., 1].astype(np.float32), cv2.INTER_LINEAR)
        restored_errors.append(np.abs(restored_effects[i]-restored_warped)[valid].max(axis=-1))
    errors = np.concatenate(errors)
    restored_errors = np.concatenate(restored_errors)
    blocks = screens[:, :528, :960].astype(np.float32).reshape(len(screens), 33, 16, 60, 16, 3).mean(axis=(2, 4))
    spread = np.ptp(blocks[-32:], axis=0).max(axis=-1)
    metrics.append(dict(take=take['name'], route=route, model_effect_mean_abs=float(np.abs(effects).mean()),
                        restored_effect_common_scale_mean_abs=float(np.abs(restored_effects).mean()),
                        moving_restored_effect_common_scale_p95=float(np.percentile(restored_errors,95)),
                        moving_warped_effect_median=float(np.median(errors)), moving_warped_effect_p95=float(np.percentile(errors, 95)),
                        held_screen_block_range_p95=float(np.percentile(spread, 95)), held_screen_block_range_max=float(spread.max()),
                        final32_mean_screen_rgb=screens[-32:].mean(axis=(0, 1, 2)).tolist()))
    selected = [int(np.argmin(abs(distance-d))) for d in [0, 1, 2, 3, 4]]+[len(screens)-1]
    for r, index in enumerate(selected):
        x, y = col*480, r*300
        draw.text((x+5,y+5), f"{take['name']}  {distance[index]:.2f}m  frame{index}", fill='white')
        sheet.paste(Image.fromarray(screens[index].astype(np.uint8)).resize((480, 270)), (x, y+27))
    take_data.append(screens)
sheet.save(root/'exposure-comparison.jpg', quality=96)
(root/'exposure-comparison.json').write_text(json.dumps(metrics, indent=2)+'\n')
print(json.dumps(metrics, indent=2))
