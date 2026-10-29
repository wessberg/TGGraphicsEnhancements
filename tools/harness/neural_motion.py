#!/usr/bin/env python3
"""Compare signed NR vectors with independently tracked screen motion in neutral, sharp captures."""
import argparse
import csv
import json
from pathlib import Path
import cv2
import numpy as np
from analyze import read_frame as read_screen
from neural_stages import analyze_step, read_frame
from neural_analysis import counters


def track(current, previous):
    current = cv2.cvtColor(current, cv2.COLOR_RGB2GRAY)
    previous = cv2.cvtColor(previous, cv2.COLOR_RGB2GRAY)
    points = cv2.goodFeaturesToTrack(current, maxCorners=1200, qualityLevel=.02, minDistance=8, blockSize=7)
    if points is None:
        raise ValueError('No image features to track')
    options = dict(winSize=(31, 31), maxLevel=4,
                   criteria=(cv2.TERM_CRITERIA_EPS | cv2.TERM_CRITERIA_COUNT, 40, .01))
    old, ok, error = cv2.calcOpticalFlowPyrLK(current, previous, points, None, **options)
    back, reverse_ok, _ = cv2.calcOpticalFlowPyrLK(previous, current, old, None, **options)
    points, old, back = points[:, 0], old[:, 0], back[:, 0]
    h, w = current.shape
    keep = (ok[:, 0] != 0) & (reverse_ok[:, 0] != 0) & (error[:, 0] < 18)
    keep &= np.linalg.norm(back-points, axis=1) < .75
    for p in (points, old):
        keep &= (p[:, 0] >= 24) & (p[:, 0] < w-24) & (p[:, 1] >= 24) & (p[:, 1] < h-24)
    flow = old-points  # Current screen pixel to its previous location, top-left origin.
    keep &= np.linalg.norm(flow, axis=1) > 1
    return points[keep], flow[keep]


def scores(native, measured):
    if len(native) < 100 or not np.isfinite(native).all() or not np.isfinite(measured).all():
        raise ValueError('Insufficient finite motion correspondences')
    hypotheses = {'as_supplied': [1, 1], 'flip_x': [-1, 1], 'flip_y': [1, -1],
                  'flip_both': [-1, -1], 'half_scale': [.5, .5], 'double_scale': [2, 2]}
    return {name: dict(median_error_pixels=float(np.median(np.linalg.norm(native*scale-measured, axis=1))),
                       median_relative_error=float(np.median(np.linalg.norm(native*scale-measured, axis=1)
                                                            / np.maximum(np.linalg.norm(measured, axis=1), 1))))
            for name, scale in hypotheses.items()}


def report(root):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    cv2.setNumThreads(1)
    request = json.loads((root / 'request.json').read_text(encoding='utf-8-sig'))
    results = []
    for scene in request['scenarios']:
        for take in scene['steps']:
            if not take.get('neuralStages'):
                continue
            controls = {p['key']: p['value'] for p in take.get('set', []) if p.get('plugin') == 'com.wessberg.tgneuralrendering'}
            if controls.get('Strength') != '0' or not take.get('disableMotionBlur') or take.get('neuralReplay'):
                raise ValueError('Motion verification needs neutral NR, suppressed motion blur and live inputs')
            folder = root / scene['key'] / take['name']
            measurement = analyze_step(folder, take)
            step = json.loads((folder / 'step.json').read_text(encoding='utf-8-sig'))
            if counters(take, step):
                raise ValueError('Invalid NR evaluation/order')
            with (folder / 'nr-stages/frames.csv').open() as stream:
                metadata = {int(row['frame']): row for row in csv.DictReader(stream)}
            native, observed, frame_rows = [], [], []
            for i in range(3, take['motionFrames'], 3):
                a, b = step['frames'][i-1:i+1]
                if b['frame'] != a['frame']+1:
                    raise ValueError('Screen frames are not consecutive')
                previous = read_screen(folder / a['file'], step['width'], step['height'], step['format'])
                current = read_screen(folder / b['file'], step['width'], step['height'], step['format'])
                points, flow = track(current, previous)
                if len(points) < 30:
                    continue
                _, grids, _ = read_frame(folder / 'nr-stages' / f"nr-{b['frame']}.bin")
                if len(grids) != 7 or not np.array_equal(grids[0], grids[3]):
                    raise ValueError('Signed motion missing or neutral scene changed')
                row = metadata[b['frame']]
                # A cell stores the mean over its pixel footprint; interpolate at cell centres.
                sample = cv2.remap(grids[6, ..., :2],
                                   (points[:, 0]*64/step['width']-.5).astype(np.float32).reshape(-1, 1),
                                   (points[:, 1]*36/step['height']-.5).astype(np.float32).reshape(-1, 1),
                                   cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)[:, 0]
                sample *= [step['width']/float(row['width']), step['height']/float(row['height'])]
                native.append(sample); observed.append(flow)
                frame_rows.append(dict(index=i, features=len(points),
                                       native=np.median(sample, axis=0).tolist(), measured=np.median(flow, axis=0).tolist()))
            if len(native) < 12:
                raise ValueError('Insufficient moving frames')
            native, observed = np.concatenate(native), np.concatenate(observed)
            result = dict(scene=scene['key'], take=take['name'], placement=controls['Placement'],
                          moving_frames=len(frame_rows), features=len(native), hypotheses=scores(native, observed),
                          frames=frame_rows, held_motion=measurement['motion_hold_max_mean_pixels'])
            results.append(result)
            fig, axes = plt.subplots(1, 2, figsize=(11, 4), constrained_layout=True)
            for axis, label in enumerate(('X', 'Y')):
                axes[axis].scatter(observed[:, axis], native[:, axis], s=1, alpha=.15)
                lo, hi = np.percentile(observed[:, axis], [1, 99])
                axes[axis].plot([lo, hi], [lo, hi], color='red')
                axes[axis].set_xlabel(f'Tracked image {label} motion (capture pixels)')
                axes[axis].set_ylabel(f'NR {label} motion (capture pixels)')
                axes[axis].grid(alpha=.2)
            fig.suptitle(f"{scene['key']} / {controls['Placement']}: current to previous")
            fig.savefig(folder / 'nr-motion-direction.png', dpi=120); plt.close(fig)
            print(f"{scene['key']}/{take['name']}: {json.dumps(result['hypotheses'])}")
    if {r['placement'] for r in results} != {'After Upscaling', 'Before Upscaling'}:
        raise ValueError('Both placements must be measured')
    (root / 'nr-motion-direction.json').write_text(json.dumps(results, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    report(parser.parse_args().run)
