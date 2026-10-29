#!/usr/bin/env python3
"""Quality-only NR reproduction: turn from the seated men and walk towards the lady's chairs."""
import argparse
import copy
import json
from pathlib import Path
import numpy as np
from neural_plan import setting


def plan(source):
    result = copy.deepcopy(source)
    scene = copy.deepcopy(next(s for s in source['scenarios'] if s['key'] == 'eyebrows-flickering'))
    scene.update(key='nr-chair-walk', pitch=0, yawOffset=180, freeze=True, recipe='nr-chairs',
                 roi=[.1, .2, .9, .9], where='Turn 180 degrees from the two seated men, then walk toward the lady and chairs.',
                 tests='NR colour and shadow changes during a forward walk and held endpoint; Quality Off / neutral / full / Off.')
    scene.pop('cameraSaved', None)
    scene.pop('steps', None)
    steps = []
    for name, enabled, strength in [('01-off', False, 1), ('02-quality-neutral', True, 0),
                                    ('03-quality-full', True, 1), ('04-off-restored', False, 1)]:
        steps.append(dict(name=name, neural=enabled, traced=False,
            set=[setting('Passes', 1), setting('Placement', 'After Upscaling'), setting('Model', 'A'),
                 setting('Strength', strength), setting('Local Tone', 1), setting('Detail', 1),
                 setting('Skin Detail', 1), setting('Automatic Mask', 'Off'),
                 setting('Neural Rendering', 'On' if enabled else 'Off')],
            settle=2, settleFrames=90, frames=192, motionFrames=96, motionHoldFrames=96,
            walkForward=2, scale=4, neuralStages=enabled))
    result.update(defaults={'recipe': 'nr-chairs'}, recipes={'nr-chairs': steps}, scenarios=[scene])
    return result


def check_walk(request, frames):
    """Validate captured poses independently of CameraSlide: straight forward travel, then a held endpoint."""
    if len(frames) != request['frames'] or request['motionFrames'] < 2:
        raise ValueError('Incomplete forward-walk capture')
    positions = np.array([[f[k] for k in ('px', 'py', 'pz')] for f in frames], dtype=float)
    angles = np.array([[f[k] for k in ('rx', 'ry', 'rz', 'fov')] for f in frames], dtype=float)
    times = np.array([f['realtime'] for f in frames], dtype=float)
    if not all(np.isfinite(a).all() for a in (positions, angles, times)) or np.any(np.diff(times) <= 0):
        raise ValueError('Invalid walk metadata')
    if np.max(np.abs((angles-angles[0]+180) % 360-180)) > .02:
        raise ValueError('Walk rotated or changed FOV')
    yaw = np.deg2rad(angles[0, 1])
    direction = np.array([np.sin(yaw), 0, np.cos(yaw)])
    displacement = positions-positions[0]
    distance = displacement @ direction
    if np.max(np.linalg.norm(displacement-distance[:, None]*direction, axis=1)) > .002:
        raise ValueError('Camera did not walk straight forward')
    if np.min(np.diff(distance)) < -.002:
        raise ValueError('Camera walked backward or looped')
    if request.get('walkSpeed', 0) > 0:
        motion_time = np.array([f['cameraMotionTime'] for f in frames], dtype=float)
        moving = np.flatnonzero(distance > .002)
        if not len(moving) or moving[0] > 2:
            raise ValueError('Real-time walk did not start on the next rendered frame')
        # The first screenshot can be the frame already rendering when BeginCycle is called.
        start = max(0, moving[0]-1)
        expected = np.clip((motion_time-motion_time[start])*request['walkSpeed'], 0, request['walkForward'])
        if not np.isfinite(motion_time).all() or np.any(np.diff(motion_time) <= 0):
            raise ValueError('Invalid camera motion timestamps')
        if np.max(np.abs(distance-expected)) > .003:
            raise ValueError('Walk does not follow requested real-time speed')
        held = np.flatnonzero(expected >= request['walkForward'])
        if len(held) < 2 or np.ptp(distance[held]) > .002:
            raise ValueError('Real-time walk endpoint was not held')
        return dict(distance_metres=float(distance[-1]), seconds=float(motion_time[held[0]]-motion_time[start]),
                    metres_per_second=float(request['walkSpeed']), max_step_metres=float(np.max(np.diff(distance))), realtime=True)
    # One capture-frame offset is allowed, never a missing/shortened walk or a teleport.
    increment = request['walkForward']/(request['motionFrames']-1)
    if abs(distance[-1]-request['walkForward']) > increment*1.1+.002:
        raise ValueError('Walk distance differs from request')
    if np.max(np.diff(distance)) > increment*1.1+.002:
        raise ValueError('Camera jumped during the walk')
    held = distance[request['motionFrames']+1:]
    if len(held) < 2 or np.ptp(held) > .002:
        raise ValueError('Walk endpoint was not held')
    seconds = times[request['motionFrames']-1]-times[0]
    return dict(distance_metres=float(distance[-1]), moving_seconds=float(seconds),
                metres_per_second=float(distance[-1]/seconds), max_step_metres=float(np.max(np.diff(distance))))


def report(root):
    """Validate the actual route and place the model's own input beside the displayed result."""
    from PIL import Image, ImageDraw
    from analyze import read_frame as read_screen
    from neural_analysis import counters, matched_motion
    from neural_stages import analyze_step, read_frame as read_stage
    request = json.loads((root/'request.json').read_text(encoding='utf-8-sig'))
    scene, = request['scenarios']
    directory = root/scene['key']
    takes = scene['steps']
    if len(takes) != 4 or [t.get('neural') for t in takes] != [False, True, True, False]:
        raise ValueError('Chair report requires Off, neutral, full, Off in that order')
    captures, measurements = [], {}
    for take in takes:
        step = json.loads((directory/take['name']/'step.json').read_text(encoding='utf-8-sig'))
        error = step.get('error') or counters(take, step)
        if error or step.get('menuOpen'):
            raise ValueError(f"{take['name']}: invalid capture: {error}")
        values = {s['key']: s['value'] for s in take['set']}
        if values.get('Placement') != 'After Upscaling' or values.get('Passes') != '1' or values.get('Model') != 'A':
            raise ValueError('Chair report requires Quality, A and one pass')
        if float(values['Strength']) != (0 if len(captures) == 1 else 1):
            raise ValueError('Chair report Strength controls differ')
        if any(float(values[key]) != 1 for key in ('Local Tone', 'Detail', 'Skin Detail')) or values['Automatic Mask'] != 'Off':
            raise ValueError('Chair report requires full controls and masking Off')
        route = check_walk(take, step['frames'])
        if captures and matched_motion(captures[0], step):
            raise ValueError('Chair routes do not match')
        captures.append(step)
        measurements[take['name']] = dict(route=route)
        if take.get('neuralStages'):
            measurements[take['name']]['stages'] = analyze_step(directory/take['name'], take)
            if len(captures) == 2:
                for frame in step['frames']:
                    _, stages, _ = read_stage(directory/take['name']/'nr-stages'/f"nr-{frame['frame']}.bin")
                    if not np.array_equal(stages[0], stages[3]):
                        raise ValueError('Zero Strength changed scene colour')
                measurements[take['name']]['neutral_scene_exact'] = True
    full = captures[2]
    moving_last = takes[2]['motionFrames']-1
    selected = list(dict.fromkeys([round(moving_last*p) for p in (0, .25, .5, .75, 1)] + [takes[2]['frames']-1]))
    tile_w, tile_h, heading = 480, 270, 42
    sheet = Image.new('RGB', (tile_w*4, (tile_h+heading)*len(selected)), (20, 20, 20))
    draw = ImageDraw.Draw(sheet)
    ranges = []
    for row, index in enumerate(selected):
        stage_frame = full['frames'][index]['frame']
        _, stages, _ = read_stage(directory/takes[2]['name']/'nr-stages'/f'nr-{stage_frame}.bin')
        off = read_screen(directory/takes[0]['name']/captures[0]['frames'][index]['file'],
                          captures[0]['width'], captures[0]['height'], captures[0]['format'])
        shown = read_screen(directory/takes[2]['name']/full['frames'][index]['file'],
                            full['width'], full['height'], full['format'])
        tiles = [off, np.uint8(np.clip(stages[1, ..., :3]*255, 0, 255)),
                 np.uint8(np.clip(stages[2, ..., :3]*255, 0, 255)), shown]
        for column, (pixels, label) in enumerate(zip(tiles, ['NR Off / screen', 'NR input / 64x36 averages',
                                                           'NR output / 64x36 averages', 'Quality / screen'])):
            x, y = column*tile_w, row*(tile_h+heading)
            draw.text((x+6, y+5), f'Frame {index}: {label}', fill='white')
            sheet.paste(Image.fromarray(pixels.astype(np.uint8)).resize((tile_w, tile_h), Image.Resampling.NEAREST if column in (1, 2) else Image.Resampling.LANCZOS), (x, y+heading))
        ranges.append(dict(index=index, model_input_cell_percentiles=np.percentile(stages[1, ..., :3], [0, 1, 50, 99, 100]).tolist(),
                           model_output_cell_percentiles=np.percentile(stages[2, ..., :3], [0, 1, 50, 99, 100]).tolist()))
    sheet.save(root/'nr-chair-stages.jpg', quality=94)
    (root/'nr-chair-validation.json').write_text(json.dumps(dict(takes=measurements, sampled_ranges=ranges), indent=2)+'\n', encoding='utf-8')
    lines = ['# Quality chair comparison', '',
             'All four captured camera routes match. Colour stage cells are averages, not full-resolution pixels; these ranges cannot establish absence of pixel clipping.',
             'Readback changes frame times; speed is reported only to describe the test. No performance verdict.', '',
             '| Take | Walk (m) | Moving duration (s) | Speed (m/s) |', '|---|---:|---:|---:|']
    for name, values in measurements.items():
        walk = values['route']
        lines.append(f"| {name} | {walk['distance_metres']:.3f} | {walk['moving_seconds']:.3f} | {walk['metres_per_second']:.3f} |")
    lines += ['', '![Matched screen and model stages](nr-chair-stages.jpg)', '']
    (root/'nr-chair-report.md').write_text('\n'.join(lines), encoding='utf-8')
    print(json.dumps({name: values['route'] for name, values in measurements.items()}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, help='Validate and illustrate an existing four-take capture')
    parser.add_argument('--source', type=Path, default=Path('tests/ingame/scenarios.json'))
    parser.add_argument('--output', type=Path, default=Path('build/nr/chairs-plan.json'))
    args = parser.parse_args()
    if args.report:
        report(args.report)
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(plan(json.loads(args.source.read_text(encoding='utf-8-sig'))), indent=2)+'\n', encoding='utf-8')
        print(args.output)
