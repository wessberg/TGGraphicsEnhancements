#!/usr/bin/env python3
"""Measure NR colour changes on tracked visible features, with matching Off takes as controls."""
import argparse
import json
from pathlib import Path
import cv2
import numpy as np
from analyze import read_frame
from neural_analysis import counters, matched_motion
from neural_motion import track
from neural_stages import analyze_step


def colour_change(current, previous, points, flow):
    """RGB effect change, current minus previous, following current-to-previous feature motion."""
    if current.shape != previous.shape or current.ndim != 3 or current.shape[2] != 3:
        raise ValueError('Effect image dimensions differ')
    if points.shape != flow.shape or points.ndim != 2 or points.shape[1] != 2 or not len(points):
        raise ValueError('Missing motion correspondences')
    if not all(np.isfinite(v).all() for v in (current, previous, points, flow)):
        raise ValueError('Nonfinite colour or motion')
    h, w = current.shape[:2]
    for xy in (points, points + flow):
        if np.any(xy < 8) or np.any(xy[:, 0] >= w-8) or np.any(xy[:, 1] >= h-8):
            raise ValueError('Feature lies outside the reliable image area')
    # Suppress pixel resampling noise for this colour measurement only. Display images are untouched.
    current = cv2.GaussianBlur(current.astype(np.float32), (0, 0), 2)
    previous = cv2.GaussianBlur(previous.astype(np.float32), (0, 0), 2)
    def sample(image, xy):
        return cv2.remap(image, xy[:, 0].astype(np.float32).reshape(-1, 1),
                         xy[:, 1].astype(np.float32).reshape(-1, 1), cv2.INTER_LINEAR)[:, 0]
    return sample(current, points) - sample(previous, points + flow)


def capture(folder):
    step = json.loads((folder / 'step.json').read_text(encoding='utf-8-sig'))
    if step.get('error') or step.get('menuOpen') or not step.get('frames'):
        raise ValueError(f'Invalid capture: {folder}')
    return step


def screen(folder, step, index):
    return read_frame(folder / step['frames'][index]['file'], step['width'], step['height'], step['format'])


def report(root, selected=None):
    cv2.setNumThreads(1)
    request = json.loads((root / 'request.json').read_text(encoding='utf-8-sig'))
    results = []
    for scene in request['scenarios']:
        if selected and scene['key'] != selected:
            continue
        takes = [s for s in scene['steps'] if s.get('motionFrames', 0) > 0]
        if not takes or takes[0].get('neural') is not False:
            raise ValueError('Matching moving Off reference required')
        if not any(t.get('neural') is False for t in takes[1:]):
            raise ValueError('Each scene needs a repeated Off control')
        directory = root / scene['key']
        reference = capture(directory / takes[0]['name'])
        if counters(takes[0], reference):
            raise ValueError('Invalid Off reference')
        pairs = []
        for i in range(3, takes[0]['motionFrames'], 3):
            a, b = reference['frames'][i-1:i+1]
            if b['frame'] != a['frame']+1:
                raise ValueError('Reference frames are not consecutive')
            previous = screen(directory / takes[0]['name'], reference, i-1)
            current = screen(directory / takes[0]['name'], reference, i)
            points, flow = track(current, previous)
            if len(points) >= 30:
                pairs.append((i, previous, current, points, flow))
        if len(pairs) < 8:
            raise ValueError('Too few reliable moving feature pairs')
        for take in takes[1:]:
            if take.get('neuralReplay'):
                raise ValueError('This measurement needs normal changing NR inputs')
            folder = directory / take['name']
            step = capture(folder)
            if matched_motion(reference, step) or counters(take, step):
                raise ValueError('Capture route or actual NR evaluation differs from request')
            if (step['width'], step['height']) != (reference['width'], reference['height']):
                raise ValueError('Capture resolutions differ')
            if take.get('neuralStages'):
                analyze_step(folder, take)
            changes, per_frame = [], []
            for i, off_previous, off_current, points, flow in pairs:
                if step['frames'][i]['frame'] != step['frames'][i-1]['frame']+1:
                    raise ValueError('Comparison frames are not consecutive')
                previous = screen(folder, step, i-1).astype(np.float32) - off_previous
                current = screen(folder, step, i).astype(np.float32) - off_current
                delta = colour_change(current, previous, points, flow)
                magnitude = np.abs(delta).max(axis=1)
                changes.append(magnitude)
                per_frame.append(dict(index=i, features=len(points), median=float(np.median(magnitude)),
                                      p95=float(np.percentile(magnitude, 95)), mean_rgb_change=delta.mean(0).tolist()))
            all_changes = np.concatenate(changes)
            row = dict(scene=scene['key'], take=take['name'], neural=bool(take.get('neural')),
                       pairs=len(pairs), features=len(all_changes), median=float(np.median(all_changes)),
                       p95=float(np.percentile(all_changes, 95)), frames=per_frame)
            results.append(row)
            print(f"{row['scene']}/{row['take']}: median {row['median']:.3f}, p95 {row['p95']:.3f} RGB levels")
    if not results or not any(not r['neural'] for r in results):
        raise ValueError('A repeated Off control is required')
    stem = 'nr-tracked-colour' + ('-'+selected if selected else '')
    (root / (stem+'.json')).write_text(json.dumps(results, indent=2)+'\n', encoding='utf-8')
    lines = ['# Colour changes on tracked visible features', '',
             'Actual screen output minus Off at matching camera poses. Current-to-previous tracking uses only Off images.',
             'Reports the largest RGB-channel change per tracked feature in 0-255 display levels after a 2-capture-pixel Gaussian blur.',
             'Tracks pass forward/backward consistency and exclude borders. These are local frame pairs, not persistent object identities.',
             'View-dependent lighting/detail changes can be intentional. This measurement alone cannot label them a defect.',
             'Compare with the repeated Off control; moving images may contain motion blur. No performance verdict.', '',
             '| Scene/take | Feature pairs | Median change | p95 change |', '|---|---:|---:|---:|']
    for r in results:
        lines.append(f"| {r['scene']}/{r['take']} | {r['features']} | {r['median']:.3f} | {r['p95']:.3f} |")
    (root / (stem+'.md')).write_text('\n'.join(lines)+'\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('--scene')
    args = parser.parse_args()
    report(args.run, args.scene)
