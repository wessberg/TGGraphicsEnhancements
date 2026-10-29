#!/usr/bin/env python3
"""Inspect exact DLSS source/NR guide pixels and compare motion with camera geometry and image tracking."""
import argparse
import csv
import json
import struct
from pathlib import Path
import cv2
import numpy as np
from PIL import Image, ImageDraw


def load(path):
    data = path.read_bytes()
    h = struct.unpack('<12I', data[:48])
    magic, version, frame, camera, w, height, fmt, stride, flip, flags, x, y = h
    if magic != 0x3147524e or version != 1 or len(data) != 48+w*height*stride:
        raise ValueError(f'Invalid guide capture: {path}')
    if fmt in (19, 20):
        pixels = np.frombuffer(data, '<f4', offset=48).reshape(height, w, 2)[..., :1]
    elif fmt in (1, 2, 15, 16, 39, 40, 41):
        pixels = np.frombuffer(data, '<f4', offset=48).reshape(height, w, stride//4)
    elif fmt in (9, 10, 33, 34):
        pixels = np.frombuffer(data, '<f2', offset=48).reshape(height, w, stride//2).astype(np.float32)
    elif fmt == 26:
        packed = np.frombuffer(data, '<u4', offset=48).reshape(height, w)
        channels = []
        for shift, mantissa in ((0, 6), (11, 6), (22, 5)):
            v = (packed >> shift) & ((1 << (mantissa+5))-1)
            m, e = v & ((1 << mantissa)-1), (v >> mantissa).astype(np.int32)
            value = np.where(e == 0, m*np.exp2(-14.-mantissa), (1+m/(1 << mantissa))*np.exp2(e-15.))
            channels.append(np.where(e == 31, np.nan, value))
        pixels = np.stack(channels, axis=-1).astype(np.float32)
    elif fmt in (27, 28, 29, 87, 91):
        pixels = np.frombuffer(data, np.uint8, offset=48).reshape(height, w, 4).astype(np.float32)/255
        if fmt in (87, 91): pixels = pixels[..., [2, 1, 0, 3]]
    elif fmt in (44, 45):
        packed = np.frombuffer(data, '<u4', offset=48).reshape(height, w)
        pixels = ((packed & 0xffffff).astype(np.float32)/0xffffff)[..., None]
    else:
        raise ValueError(f'Unsupported DXGI format {fmt}: {path}')
    if flip: pixels = pixels[::-1]
    if not np.isfinite(pixels).all(): raise ValueError(f'Nonfinite guide: {path}')
    return h, pixels.copy()


def display(rgb):
    rgb = np.maximum(rgb[..., :3], 0)
    rgb = rgb/(1+rgb.max(axis=-1, keepdims=True))
    rgb = np.where(rgb <= .0031308, rgb*12.92, 1.055*rgb**(1/2.4)-.055)
    return np.uint8(np.clip(rgb*255, 0, 255))


def percentiles(values):
    values = np.asarray(values)
    return dict(count=int(values.size), median=float(np.median(values)), p95=float(np.percentile(values, 95)),
                p99=float(np.percentile(values, 99))) if values.size else dict(count=0)


def compare_copies(folder, frame, unity=True):
    _, raw_depth = load(folder/f'guide-{frame}-raw-depth.bin')
    header, depth = load(folder/f'guide-{frame}-depth.bin')
    _, raw_motion = load(folder/f'guide-{frame}-raw-motion.bin')
    _, motion = load(folder/f'guide-{frame}-motion.bin')
    expected = raw_motion.copy()
    markers = expected[..., 0] > 1 if unity else np.zeros(expected.shape[:2], bool)
    expected[markers] = 0
    expected = expected.astype(np.float16).astype(np.float32)
    if not np.array_equal(raw_depth[..., :1], depth): raise ValueError('NR depth differs from vertically corrected DLSS depth')
    if not np.array_equal(expected, motion): raise ValueError('NR motion differs from corrected DLSS vectors')
    return header, depth[..., 0], motion, markers


def track(previous, current):
    a, b = [cv2.cvtColor(display(p), cv2.COLOR_RGB2GRAY) for p in (previous, current)]
    points = cv2.goodFeaturesToTrack(b, maxCorners=3500, qualityLevel=.015, minDistance=7, blockSize=5)
    if points is None: raise ValueError('No image features')
    options = dict(winSize=(21, 21), maxLevel=4,
                   criteria=(cv2.TERM_CRITERIA_EPS | cv2.TERM_CRITERIA_COUNT, 35, .005))
    old, valid, err = cv2.calcOpticalFlowPyrLK(b, a, points, None, **options)
    back, valid2, _ = cv2.calcOpticalFlowPyrLK(a, b, old, None, **options)
    p, q = points[:, 0], old[:, 0]
    keep = (valid[:, 0] != 0) & (valid2[:, 0] != 0) & (err[:, 0] < 18)
    keep &= np.linalg.norm(back[:, 0]-p, axis=1) < .35
    height, width = a.shape
    for pos in (p, q):
        keep &= (pos[:, 0] >= 12) & (pos[:, 0] < width-12) & (pos[:, 1] >= 12) & (pos[:, 1] < height-12)
    return p[keep], (q-p)[keep]


def report(root, selected=None):
    cv2.setNumThreads(1)
    request = json.loads((root/'request.json').read_text(encoding='utf-8-sig'))
    results = []
    for scene in request['scenarios']:
        for take in scene['steps']:
            if not take.get('neuralStages'): continue
            if selected and take['name'] != selected: continue
            directory = root/scene['key']/take['name']
            step = json.loads((directory/'step.json').read_text(encoding='utf-8-sig'))
            if step.get('error'): raise ValueError(step['error'])
            from neural_analysis import counters
            from neural_chairs import check_walk
            if take.get('traced'): raise ValueError('This guide audit covers vanilla DLSS SR, with path tracing off')
            problem = counters(take, step)
            if problem or step.get('menuOpen'): raise ValueError(problem or 'Gameplay left during capture')
            route = check_walk(take, step['frames'])
            folder = directory/'nr-stages'
            if (folder/'error.txt').exists(): raise ValueError((folder/'error.txt').read_text())
            with (folder/'frames.csv').open() as stream:
                metadata = {int(r['frame']): r for r in csv.DictReader(stream)}
            poses = {f['frame']: f for f in step['frames']}
            frames = sorted(int(p.name.split('-')[1]) for p in folder.glob('guide-*-color.bin'))
            if len(frames) != 6: raise ValueError(f'Expected six exact guide frames, got {len(frames)} in {folder}')
            for previous, current in zip(frames[::2], frames[1::2]):
                if current != previous+1: raise ValueError('Guide pair is not consecutive')
                hp, color_p = load(folder/f'guide-{previous}-color.bin')
                hc, color_c = load(folder/f'guide-{current}-color.bin')
                compare_copies(folder, previous)
                header, depth, motion, markers = compare_copies(folder, current)
                row, oldrow = metadata[current], metadata[previous]
                height, width = depth.shape
                if color_c.shape[:2] != depth.shape or motion.shape[:2] != depth.shape:
                    raise ValueError('This audit requires equal render-resolution colour, depth and motion')
                native = motion*np.array([float(row['motionScaleX'])*width/float(row['width']),
                                          float(row['motionScaleY'])*height/float(row['height'])])
                points, flow = track(color_p, color_c)
                px, py = np.rint(points).astype(int).T
                sampled = native[py, px]
                hypotheses = {name: percentiles(np.linalg.norm(sampled*scale-flow, axis=1)) for name, scale in
                              [('as_supplied', [1, 1]), ('flip_x', [-1, 1]), ('flip_y', [1, -1]),
                               ('flip_both', [-1, -1]), ('half_scale', [.5, .5]), ('double_scale', [2, 2])]}
                # Colour before SR is jittered. The native motion excludes that projection offset.
                # Record the uniform residual separately, do not silently fold it into supplied motion.
                residual = np.median(flow-sampled, axis=0)
                jitter_delta = [float(oldrow[k])-float(row[k]) for k in ('jitterX', 'jitterY')]
                tracked = percentiles(np.linalg.norm(sampled+residual-flow, axis=1))
                tracked_with_jitter = percentiles(np.linalg.norm(sampled+np.array([jitter_delta[0], -jitter_delta[1]])-flow, axis=1))
                p, c = poses[previous], poses[current]
                if max(abs((c[k]-p[k]+180)%360-180) for k in ('rx', 'ry', 'rz')) > .02:
                    raise ValueError('Geometry audit expects fixed camera rotation')
                delta = np.array([c[k]-p[k] for k in ('px', 'py', 'pz')])
                pitch, yaw, roll = np.deg2rad([c[k] for k in ('rx', 'ry', 'rz')])
                rx = np.array([[1, 0, 0], [0, np.cos(pitch), -np.sin(pitch)], [0, np.sin(pitch), np.cos(pitch)]])
                ry = np.array([[np.cos(yaw), 0, np.sin(yaw)], [0, 1, 0], [-np.sin(yaw), 0, np.cos(yaw)]])
                rz = np.array([[np.cos(roll), -np.sin(roll), 0], [np.sin(roll), np.cos(roll), 0], [0, 0, 1]])
                # Unity's Quaternion.Euler applies Z, X, Y. Include the frozen camera's small tilt.
                dx, dy, dz = (ry @ rx @ rz).T @ delta
                near, far = float(row['nearPlane']), float(row['farPlane'])
                inverted = bool(header[9] & 1)
                d = depth if inverted else 1-depth
                z = near*far/(near+(far-near)*d)
                yy, xx = np.indices(depth.shape, dtype=np.float32)
                cx, cy = width/2-.5, height/2-.5
                fy = height/(2*np.tan(np.deg2rad(c['fov'])/2))
                expected = np.stack(((xx-cx)*z/(z+dz)+dx*fy/(z+dz)-(xx-cx),
                                     (yy-cy)*z/(z+dz)-dy*fy/(z+dz)-(yy-cy)), axis=-1)
                error = np.linalg.norm(native-expected, axis=-1)
                valid = ~markers & (depth > 0) & (depth < 1) & np.isfinite(error)
                logz = np.log(np.maximum(z, .0001))
                edge = cv2.dilate(logz, np.ones((3, 3), np.uint8))-cv2.erode(logz, np.ones((3, 3), np.uint8)) > .03
                geom = {name: percentiles(np.linalg.norm(native*scale-expected, axis=-1)[valid]) for name, scale in
                        [('as_supplied', [1, 1]), ('flip_x', [-1, 1]), ('flip_y', [1, -1]), ('half_scale', [.5, .5]), ('double_scale', [2, 2])]}
                region_errors = {}
                for label, rect in {'stairs_right': [.87, .23, 1, .82], 'wall_shields_left': [.02, .31, .40, .65],
                                    'far_stairs': [.70, .35, .82, .73]}.items():
                    x0, y0, x1, y1 = [int(v*s) for v,s in zip(rect, [width, height, width, height])]
                    select = np.zeros(depth.shape, bool); select[y0:y1, x0:x1] = True
                    region_errors[label] = dict(rect=rect, all=percentiles(error[valid & select]), edges=percentiles(error[valid & select & edge]))
                result = dict(take=take['name'], previous=previous, frame=current, guide_size=[width, height],
                              route=route,
                              depth_range=[float(depth.min()), float(depth.max())], inverted=inverted, near=near, far=far,
                              marker_fraction=float(markers.mean()), source_copies_exact=True,
                              tracked_hypotheses=hypotheses, tracked_uniform_residual=residual.tolist(),
                              observed_jitter_previous_minus_current=jitter_delta, tracked_after_uniform_residual=tracked,
                              tracked_with_recorded_jitter=tracked_with_jitter, regions=region_errors,
                              geometry_hypotheses=geom, geometry_at_depth_edges=percentiles(error[valid & edge]),
                              geometry_all_motion=percentiles(np.linalg.norm(native, axis=-1)[valid]),
                              camera_delta=[float(dx), float(dy), float(dz)])
                results.append(result)
                shown = display(color_c)
                overlay = shown.copy(); overlay[edge] = [255, 60, 60]
                depth_image = cv2.applyColorMap(np.uint8(np.clip((logz-np.percentile(logz, 1))/max(.001, np.percentile(logz, 99)-np.percentile(logz, 1))*255, 0, 255)), cv2.COLORMAP_VIRIDIS)[..., ::-1]
                error_image = cv2.applyColorMap(np.uint8(np.clip(error/2*255, 0, 255)), cv2.COLORMAP_TURBO)[..., ::-1]
                tiles = [(shown, 'Actual DLSS colour, displayed with simple compression'), (overlay, 'Depth boundaries over colour'),
                         (depth_image, 'Depth in metres, logarithmic display only'), (error_image, 'Motion vs camera/depth error: 0 to 2 guide pixels')]
                atlas = Image.new('RGB', (1200, 730), (22, 22, 22)); draw = ImageDraw.Draw(atlas)
                for index, (im, label) in enumerate(tiles):
                    x0, y0 = index%2*600, index//2*365
                    draw.text((x0+5, y0+5), label, fill='white')
                    atlas.paste(Image.fromarray(im).resize((600, 338)), (x0, y0+27))
                atlas.save(directory/f'guides-{current}.jpg', quality=94)
    if not results: raise ValueError('No guide captures')
    (root/'nr-guide-audit.json').write_text(json.dumps(results, indent=2)+'\n', encoding='utf-8')
    print(json.dumps([dict(take=r['take'], frame=r['frame'], copies=r['source_copies_exact'],
                           geometry=r['geometry_hypotheses']['as_supplied'], edge=r['geometry_at_depth_edges'],
                           tracking=r['tracked_after_uniform_residual'], jitter_residual=r['tracked_uniform_residual']) for r in results], indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--take', help='Explicitly inspect one completed take from a partial run')
    args = parser.parse_args()
    report(args.root, args.take)
