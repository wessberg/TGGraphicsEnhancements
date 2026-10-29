#!/usr/bin/env python3
"""Summarize retained-input NR model response, including local changes hidden by global averages."""
import argparse
import json
from pathlib import Path

import numpy as np
from neural_stages import analyze_step, read_frame
from neural_analysis import counters, matched_motion


def report(root):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt

    request = json.loads((root / 'request.json').read_text(encoding='utf-8-sig'))
    rows = []
    for scene in request['scenarios']:
        reference = json.loads((root / scene['key'] / scene['steps'][0]['name'] / 'step.json').read_text(encoding='utf-8-sig'))
        takes = [take for take in scene['steps'] if take.get('neuralReplay')]
        if not takes:
            continue
        fig, axes = plt.subplots(len(takes), 2, figsize=(12, 2.7 * len(takes)), squeeze=False, constrained_layout=True)
        for i, take in enumerate(takes):
            folder = root / scene['key'] / take['name']
            measurement = analyze_step(folder, take)
            step = json.loads((folder / 'step.json').read_text(encoding='utf-8-sig'))
            problem = counters(take, step) or matched_motion(reference, step)
            if problem:
                raise ValueError(f"{scene['key']}/{take['name']}: {problem}")
            values = np.asarray([read_frame(folder / 'nr-stages' / f"nr-{f['frame']}.bin")[1][1:3, ..., :3]
                                 for f in step['frames']], dtype=np.float64) * 255
            effect = values[:, 1] - values[:, 0]
            hold = measurement['hold_start']
            tail = effect[-32:]
            spread = np.ptp(tail, axis=0).max(axis=-1)
            y, x = np.unravel_index(np.argmax(spread), spread.shape)
            controls = {s['key']: s['value'] for s in take['set'] if s.get('plugin') == 'com.wessberg.tgneuralrendering'}
            row = dict(scene=scene['key'], take=take['name'], model=controls['Model'], placement=controls['Placement'],
                       automatic_mask=controls['Automatic Mask'], retained_frames=len(effect)-hold,
                       input_range=measurement['model_in']['hold_max_range'],
                       full_hold_max=measurement['model_effect']['hold_max_range'],
                       tail32_max=float(spread.max()), tail32_p95=float(np.percentile(spread, 95)),
                       tail32_area_over2_percent=float(np.mean(spread > 2) * 100),
                       tail32_mean_effect=float(np.abs(tail).mean()), worst_cell=[int(x), int(y)],
                       late32_windows=[float(np.ptp(effect[start:start+32], axis=0).max())
                                       for start in range(hold+64, len(effect)-31, 32)])
            rows.append(row)
            axes[i, 0].imshow(spread, vmin=0, vmax=15, cmap='inferno', origin='upper', aspect='auto')
            axes[i, 0].plot(x, y, '+', color='cyan')
            axes[i, 0].set_title(f"{take['name']}\nLast 32 range: max {row['tail32_max']:.2f}; p95 {row['tail32_p95']:.2f}")
            for c, color in enumerate(('red', 'green', 'blue')):
                axes[i, 1].plot(effect[:, y, x, c], color=color, label='RGB'[c])
            axes[i, 1].axvline(hold, color='gray', linestyle='--')
            axes[i, 1].set_title(f'Output minus input at cell ({x}, {y})')
            axes[i, 1].set_xlabel('Captured frame'); axes[i, 1].set_ylabel('Encoded RGB levels')
            axes[i, 1].grid(alpha=.2)
        fig.suptitle(f"{scene['key']}: fixed NR inputs after dashed line; maps use range 0 to 15 RGB levels")
        fig.savefig(root / scene['key'] / 'nr-history-local.png', dpi=110)
        plt.close(fig)
    if not rows:
        raise ValueError('No retained-input NR takes')
    (root / 'nr-history.json').write_text(json.dumps(rows, indent=2)+'\n', encoding='utf-8')
    lines = ['# NR model history comparison', '',
             'Inputs are constant within each held take, not guaranteed identical between models or runs.',
             'These are 64x36 cell averages in encoded RGB levels, not perceptual pass thresholds or internal mask measurements.',
             'The final-32 area uses a descriptive threshold of 2 levels; lower effect strength can also reduce variation.', '',
             '| Scene | Placement | Model | Mask | Hold max | Last32 max | Last32 p95 | Area >2 (%) | Mean absolute effect |',
             '|---|---|---|---|---:|---:|---:|---:|---:|']
    for r in rows:
        lines.append(f"| {r['scene']} | {r['placement']} | {r['model']} | {r['automatic_mask']} | {r['full_hold_max']:.3f} | "
                     f"{r['tail32_max']:.3f} | {r['tail32_p95']:.3f} | {r['tail32_area_over2_percent']:.2f} | {r['tail32_mean_effect']:.3f} |")
    (root / 'nr-history.md').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    print('\n'.join(lines))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    report(parser.parse_args().run)
