# SPDX-License-Identifier: MIT
"""Count pale, low-colour pixels on the shared water of the water-day camera.

This is an appearance measure, not a material ID mask: smooth sky reflections can
also pass it. Report both thresholds and compare to the foam-off step. The fixed
polygon excludes dry beach and sky in save Slot_86; do not use it for other views.
Reads captures only; prints JSON, never writes into the supplied run.

  python tools/harness/water_coverage.py <run>/water-day
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

POLYGON = ((.13,.31), (1,.31), (1,1), (.45,1), (.28,.68), (.29,.49), (.13,.34))


def measure(step):
    info = json.loads((step / 'step.json').read_text(encoding='utf-8-sig'))
    width, height = info['width'], info['height']
    if info['format'] != 'RGBA32':
        raise ValueError(f'{step}: expected raw RGBA32')
    mask_image = Image.new('L', (width, height))
    ImageDraw.Draw(mask_image).polygon([(int(x*width), int(y*height)) for x,y in POLYGON], fill=1)
    mask = np.asarray(mask_image).astype(bool)
    shares = []
    for frame in info['frames']:
        path = step / frame['file']
        pixels = np.fromfile(path, np.uint8).reshape(height, width, 4)[::-1, :, :3][mask].astype(np.int16)
        low, high = pixels.min(1), pixels.max(1)
        shares.append([float(np.mean((low >= threshold) & (high-low <= 30))*100) for threshold in (80,90)])
    if not shares:
        return None
    values = np.asarray(shares)
    return dict(step=step.name, frames=len(shares), water_pixels=int(mask.sum()),
                threshold_min_rgb=[80,90], max_channel_spread=30,
                mean_percent=values.mean(0).tolist(), range_percent=[values.min(0).tolist(),values.max(0).tolist()],
                gpu_ms=info.get('gpuMs'), traced_gain=info.get('tracedGain'), exposure=info.get('exposure'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('water_day', type=Path)
    args = parser.parse_args()
    for step in sorted(args.water_day.iterdir()):
        if (step / 'step.json').exists() and list(step.glob('f*.raw')):
            result = measure(step)
            if result:
                print(json.dumps(result), flush=True)
