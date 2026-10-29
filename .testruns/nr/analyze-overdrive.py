from pathlib import Path
import json
import numpy as np
root = Path('build/nr/overdrive-check')
rows = []
for size in (256, 320):
    for strength in (0.5, 1.):
        name = f'{size}-strength-{strength:.6f}.f32'
        baseline = np.fromfile(root / 'baseline' / name, dtype=np.float32).reshape(-1, 4)[:, :3]
        for case in ('repeat', 'onehalf', 'double'):
            value = np.fromfile(root / case / name, dtype=np.float32).reshape(-1, 4)[:, :3]
            assert np.isfinite(value).all()
            row = dict(size=size, strength=strength, case=case, identical=bool(np.array_equal(value, baseline)),
                       mean_difference=float(np.abs(value-baseline).mean()), max_difference=float(np.abs(value-baseline).max()))
            rows.append(row)
            print(row)
(root / 'comparison.json').write_text(json.dumps(rows, indent=2)+'\n')
