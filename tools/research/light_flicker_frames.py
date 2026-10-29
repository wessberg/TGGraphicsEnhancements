"""Read frozen captures; write only the requested local report. No image filtering fixes."""
import json
import sys
from pathlib import Path
import numpy as np

run = Path(sys.argv[1])
rows = {}
for scene, step_name, crop in [('glass-jars', '2-traced-none', (448, 448)), ('forest', '2-traced-none', (2464, 1568)), ('forest', '3-traced-raw', (2464, 1568)), ('vista', '2-traced-none', (1568, 1568))]:
    step = run / scene / step_name
    if not step.is_dir():
        continue
    info = json.loads((step / 'step.json').read_text())
    w, h = info['width'], info['height']
    frames, means, gradients = [], [], []
    for file in sorted(step.glob('f*.raw')):
        f = np.fromfile(file, np.uint8).reshape(h, w, 4)[::-1, :, :3].astype(np.float32)
        y = f @ np.array([.2126, .7152, .0722], np.float32)
        x0, y0 = crop
        c = y[y0:y0+224, x0:x0+224]
        means.append([float(y.mean()), float(c.mean())])
        gradients.append(float((np.diff(c, axis=0)**2).mean() + (np.diff(c, axis=1)**2).mean()))
        frames.append(c)
    f = np.array(frames)
    rows[scene + ('-raw' if step_name == '3-traced-raw' else '')] = dict(frame_ids=[i['frame'] for i in info['frames']], means=means, squared_gradient=gradients,
                       lag_mae={i: float(np.abs(f[i:]-f[:-i]).mean()) for i in range(1, 9)},
                       consecutive_mae=[float(np.abs(f[i+1]-f[i]).mean()) for i in range(len(f)-1)], crop=crop,
                       exposure=info['exposure'])
out = Path(sys.argv[2])
out.write_text(json.dumps(rows, indent=2))
print('Wrote', out, 'for', ', '.join(rows))
