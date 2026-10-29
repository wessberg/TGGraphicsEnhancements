#!/usr/bin/env python3
"""Measure Quality NR colour alignment after SR, using exact native captures.

All errors are in render-resolution pixels. Image tracking is independent of NGX
motion. Its uniform residual is reported, never silently fitted away. Same-frame
pre-SR colour is jittered, whereas the SR output is expected to be unjittered.
"""
import argparse
import csv
import json
from pathlib import Path
import cv2
import numpy as np
from PIL import Image, ImageDraw
from neural_guides import load, display, track, percentiles, compare_copies
from neural_chairs import check_walk
from neural_analysis import counters


def sample(field, points):
    result = cv2.remap(field, points[:, 0].astype(np.float32).reshape(-1, 1),
                       points[:, 1].astype(np.float32).reshape(-1, 1), cv2.INTER_NEAREST)
    return result[:, 0]


def jitter(row):
    return np.array([float(row['jitterX']), -float(row['jitterY'])])


def report(root):
    cv2.setNumThreads(1)
    request = json.loads((root/'request.json').read_text(encoding='utf-8-sig'))
    results = []
    for scene in request['scenarios']:
        for take in scene['steps']:
            directory = root/scene['key']/take['name']
            step = json.loads((directory/'step.json').read_text(encoding='utf-8-sig'))
            if step.get('error') or step.get('menuOpen') or counters(take, step):
                raise ValueError(f'Invalid take: {directory}')
            route = check_walk(take, step['frames'])
            folder = directory/'nr-stages'
            if (folder/'error.txt').exists():
                raise ValueError((folder/'error.txt').read_text())
            with (folder/'frames.csv').open() as stream:
                rows = {int(r['frame']): r for r in csv.DictReader(stream)}
            frames = sorted(int(p.name.split('-')[1]) for p in folder.glob('guide-*-nr-source.bin'))
            if len(frames) != 6:
                raise ValueError(f'Expected six NR-boundary captures, got {len(frames)}')
            for previous, current in zip(frames[::2], frames[1::2]):
                if current != previous+1:
                    raise ValueError('Boundary captures are not consecutive game frames')
                row, oldrow = rows[current], rows[previous]
                if int(row['flags']) & 16:
                    raise ValueError('This audit is for Quality after SR')
                _, depth, motion, markers = compare_copies(folder, current)
                _, _, old_motion, _ = compare_copies(folder, previous)
                height, width = depth.shape
                scale = np.array([float(row['motionScaleX'])*width/float(row['width']),
                                  float(row['motionScaleY'])*height/float(row['height'])])
                native = motion*scale
                old_native = old_motion*np.array([float(oldrow['motionScaleX'])*width/float(oldrow['width']),
                                                  float(oldrow['motionScaleY'])*height/float(oldrow['height'])])
                def boundary(frame, suffix):
                    header, pixels = load(folder/f'guide-{frame}-{suffix}.bin')
                    if header[2] != frame or header[3] != int(rows[frame]['camera']):
                        raise ValueError('Capture frame/camera mismatch')
                    if (header[4], header[5]) != (int(row['width']), int(row['height'])):
                        raise ValueError('NR colour capture is not the full output extent')
                    return cv2.resize(pixels[..., :3], (width, height), interpolation=cv2.INTER_AREA)
                source_p, source_c = [boundary(f, 'nr-source') for f in (previous, current)]
                result_p, result_c = [boundary(f, 'nr-result') for f in (previous, current)]
                _, raw_p = load(folder/f'guide-{previous}-color.bin')
                _, raw_c = load(folder/f'guide-{current}-color.bin')
                if raw_c.shape[:2] != depth.shape:
                    raise ValueError('Pre-SR colour and guide extents differ')
                near, far = float(row['nearPlane']), float(row['farPlane'])
                z = near*far/(near+(far-near)*(depth if int(row['flags']) & 1 else 1-depth))
                logz = np.log(np.maximum(z, .0001))
                edge = (cv2.dilate(logz, np.ones((5, 5), np.uint8))-
                        cv2.erode(logz, np.ones((5, 5), np.uint8))) > .03
                points, flow = track(source_p, source_c)
                supplied = sample(native, points)
                clean = (~sample(edge.astype(np.uint8), points).astype(bool) &
                         ~sample(markers.astype(np.uint8), points).astype(bool))
                error = np.linalg.norm(flow-supplied, axis=1)
                # Actual previous-frame motion is a deliberately different timing hypothesis.
                # Its field is sampled at current coordinates, so this is a diagnostic alternative,
                # not a geometric proof of what a delayed renderer would produce at every edge.
                old_error = np.linalg.norm(flow-sample(old_native, points), axis=1)
                hypotheses = {name: percentiles(np.linalg.norm(flow-supplied*factor, axis=1)[clean])
                              for name, factor in [('as_supplied', [1, 1]), ('flip_x', [-1, 1]),
                                                   ('flip_y', [1, -1]), ('half_scale', [.5, .5]),
                                                   ('double_scale', [2, 2])]}
                regions = {}
                for name, rect in {'stairs_right': [.87, .23, 1, .82],
                                   'wall_shields_left': [.02, .31, .40, .65],
                                   'far_stairs': [.70, .35, .82, .73]}.items():
                    lo, hi = np.array(rect[:2])*[width, height], np.array(rect[2:])*[width, height]
                    inside = np.all((points >= lo) & (points < hi), axis=1)
                    regions[name] = dict(rect=rect, all=percentiles(error[inside]),
                                         interior=percentiles(error[inside & clean]))
                worst = np.argsort(np.where(clean, error, -1))[-6:][::-1]
                outliers = [dict(x=float(points[i, 0]), y=float(points[i, 1]),
                                 error=float(error[i]), tracked=flow[i].tolist(), supplied=supplied[i].tolist())
                            for i in worst if clean[i]]
                same_points, same_flow = track(raw_c, source_c)
                _, stale_flow = track(raw_p, source_c)
                same_clean = ~sample(edge.astype(np.uint8), same_points).astype(bool)
                output_points, output_flow = track(result_p, result_c)
                output_error = np.linalg.norm(output_flow-sample(native, output_points), axis=1)
                output_clean = ~sample(edge.astype(np.uint8), output_points).astype(bool)
                record = dict(take=take['name'], previous=previous, frame=current, route=route,
                              units='render-resolution pixels', analysis_extent=[width, height],
                              source_extent=[int(row['width']), int(row['height'])],
                              source_motion_error=percentiles(error), source_interior_error=percentiles(error[clean]),
                              source_uniform_residual=np.median(flow-supplied, axis=0).tolist(),
                              source_motion_hypotheses=hypotheses, regions=regions, largest_interior_errors=outliers,
                              previous_motion_hypothesis=percentiles(old_error[clean]),
                              source_tracked_motion=percentiles(np.linalg.norm(flow, axis=1)),
                              same_frame_colour_error=percentiles(np.linalg.norm(same_flow-jitter(row), axis=1)),
                              same_frame_colour_interior_error=percentiles(np.linalg.norm(same_flow-jitter(row), axis=1)[same_clean]),
                              same_frame_uniform_offset=np.median(same_flow, axis=0).tolist(),
                              expected_jitter=jitter(row).tolist(),
                              previous_frame_colour_without_motion=percentiles(np.linalg.norm(stale_flow-jitter(oldrow), axis=1)),
                              nr_output_motion_error=percentiles(output_error),
                              nr_output_interior_error=percentiles(output_error[output_clean]))
                results.append(record)
                overlay = display(source_c).copy()
                overlay[edge] = [255, 60, 60]
                for point, residual in zip(points[::5], (flow-supplied)[::5]):
                    a = tuple(np.rint(point).astype(int))
                    b = tuple(np.rint(point+residual*8).astype(int))
                    cv2.arrowedLine(overlay, a, b, (30, 255, 100), 1, tipLength=.25)
                tiles = [(display(raw_c), 'Actual pre-SR input'), (display(source_c), 'Actual post-SR input to NR'),
                         (overlay, 'Depth edges / tracking error arrows (8x)'), (display(result_c), 'NR result before game grading')]
                atlas = Image.new('RGB', (1200, 730), (22, 22, 22))
                draw = ImageDraw.Draw(atlas)
                for index, (pixels, label) in enumerate(tiles):
                    x, y = index % 2*600, index//2*365
                    draw.text((x+5, y+5), label, fill='white')
                    atlas.paste(Image.fromarray(pixels).resize((600, 338)), (x, y+27))
                atlas.save(directory/f'boundary-{current}.jpg', quality=94)
                # Visual audit of tracking outliers. The markers use the same tracked points as
                # the numbers above. A multi-depth tracking window can disagree with its centre.
                crops = Image.new('RGB', (960, 6*150), (22, 22, 22))
                draw = ImageDraw.Draw(crops)
                shown_p, shown_c = display(source_p), display(source_c)
                for n, i in enumerate(worst):
                    if not clean[i]: continue
                    coords = [points[i], points[i]+flow[i], points[i]+supplied[i]]
                    for column, (pixels, point, label) in enumerate(zip(
                            [shown_c, shown_p, shown_p], coords, ['Current', 'Previous: tracked', 'Previous: supplied'])):
                        x, y = np.rint(point).astype(int)
                        patch = Image.fromarray(pixels).crop((x-48, y-48, x+48, y+48)).resize((128, 128))
                        left, top = column*320, n*150
                        crops.paste(patch, (left, top+20))
                        draw.text((left+3, top+3), f'{label}, error {error[i]:.2f}px', fill='white')
                        draw.line((left+59, top+84, left+69, top+84), fill='red')
                        draw.line((left+64, top+79, left+64, top+89), fill='red')
                crops.save(directory/f'boundary-outliers-{current}.jpg', quality=95)
    if not results:
        raise ValueError('No boundary captures analysed')
    (root/'nr-boundary-audit.json').write_text(json.dumps(results, indent=2)+'\n')
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    report(parser.parse_args().root)
