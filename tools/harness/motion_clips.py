# SPDX-License-Identifier: MIT
"""Two steps of a harness run side by side IN MOTION, for a choice the owner makes from pictures (AGENTS.md "Choices go
on a page with pictures": frames grabbed during motion, the game's motion blur off).

  python tools/harness/motion_clips.py <run folder> <out folder> [--a STEP] [--b STEP] [--none STEP]
         [--label-a TEXT] [--label-b TEXT] [--by-index] <scene>:<x>,<y>,<w>,<h> [...]

The run must have kept its raw frames (`Queue-PtRun.ps1 -KeepFrames`) and its moving steps must carry
`"disableMotionBlur": true` (a step without it is smeared by the game's own motion blur: this tool stops when a
frame says the blur was on or does not say). Defaults are the recipe `nrd-motion`'s steps (RELAX beside REBLUR).
For each scene it writes into the out folder:
  <scene>-crop.webp   a moving picture of the box at the game's own pixels (not scaled), A beside B
  <scene>-whole.webp  the whole frame at a quarter of its pixels, the box drawn in
  <scene>-still.jpg   the pair from the middle of the sweep
  <scene>-none.jpg    the same moment of the --none step (no denoiser), when given
The two steps' frames are lined up by where the camera looks (the nearest yaw within one sweep from left to right),
or frame for frame with --by-index (a recipe that poses the camera by the frame's number). The picture plays there
and back, at the pace the frames were taken (a run that keeps raw 4K frames takes about ten a second).
"""
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw


def step_of(run, scene, name):
    d = json.load(open(os.path.join(run, scene, 'scenario.json'), encoding='utf-8-sig'))
    found = [x for x in d['steps'] if x['name'] == name]
    if not found:
        sys.exit('%s has no step "%s" (its steps: %s)' % (scene, name, ', '.join(x['name'] for x in d['steps'])))
    return found[0]


def raw(run, scene, step, frame):
    w, h = step['width'], step['height']
    a = np.fromfile(os.path.join(run, scene, step['name'], frame['file']), dtype=np.uint8)
    return a.reshape(h, w, a.size // (w * h))[::-1, :, :3]


def yaw(frame):
    # (A camera that faces north swings across 0 / 360.)
    return frame['ry'] - 360.0 if frame['ry'] > 180.0 else frame['ry']


def sweep(frames):
    """The longest run of frames in which the camera turns one way: one pass from its leftmost to its rightmost look."""
    y = [yaw(f) for f in frames]
    best = (0, 0)
    i = 0
    while i < len(y) - 1:
        j = i
        while j < len(y) - 1 and y[j + 1] > y[j]:
            j += 1
        if j - i > best[1] - best[0]:
            best = (i, j)
        i = max(j, i + 1)
    return list(range(best[0], best[1] + 1))


def blur_off(step, scene):
    states = set(f.get('motionBlurActive') for f in step['frames'])
    if states != {False}:
        sys.exit('%s, step %s: the game\'s motion blur was not off in every frame (%s): give the step '
                 '"disableMotionBlur": true and run again' % (scene, step['name'], sorted(str(s) for s in states)))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('run'); p.add_argument('out'); p.add_argument('boxes', nargs='+')
    p.add_argument('--a', default='3-relax-moving'); p.add_argument('--b', default='4-reblur-moving')
    p.add_argument('--none', default='2-none-moving')
    p.add_argument('--label-a', default="RELAX (NVIDIA's for RTXDI)"); p.add_argument('--label-b', default='REBLUR')
    p.add_argument('--by-index', action='store_true')
    p.add_argument('--allow-blur', action='store_true', help='for looking at a run that had the blur on; never for the owner\'s page')
    o = p.parse_args()
    os.makedirs(o.out, exist_ok=True)
    for spec in o.boxes:
        scene, box = spec.split(':')
        x, y, w, h = (int(v) for v in box.split(','))
        sa, sb = step_of(o.run, scene, o.a), step_of(o.run, scene, o.b)
        sn = step_of(o.run, scene, o.none) if o.none else None
        if not o.allow_blur:
            for s in (sa, sb) + ((sn,) if sn else ()):
                blur_off(s, scene)
        fa, fb = sa['frames'], sb['frames']
        if o.by_index:
            ia = list(range(min(len(fa), len(fb)))); pick = lambda k, at: k
        else:
            ia, ib = sweep(fa), sweep(fb); pick = lambda k, at: min(ib, key=lambda q: abs(yaw(fb[q]) - at))
        crops, wholes, times = [], [], []
        for n, k in enumerate(ia):
            at = yaw(fa[k]); kb = pick(k, at)
            A, B = raw(o.run, scene, sa, fa[k]), raw(o.run, scene, sb, fb[kb])
            pair = Image.new('RGB', (2 * w + 6, h + 22), (0, 0, 0))
            pair.paste(Image.fromarray(A[y:y + h, x:x + w]), (0, 22)); pair.paste(Image.fromarray(B[y:y + h, x:x + w]), (w + 6, 22))
            d = ImageDraw.Draw(pair); d.text((6, 5), o.label_a, fill=(255, 255, 255)); d.text((w + 12, 5), o.label_b, fill=(255, 255, 255))
            crops.append(pair)
            W4, H4 = A.shape[1] // 4, A.shape[0] // 4
            half = Image.new('RGB', (2 * W4 + 6, H4 + 22), (0, 0, 0))
            half.paste(Image.fromarray(A).resize((W4, H4), Image.BILINEAR), (0, 22)); half.paste(Image.fromarray(B).resize((W4, H4), Image.BILINEAR), (W4 + 6, 22))
            d = ImageDraw.Draw(half); d.text((6, 5), o.label_a, fill=(255, 255, 255)); d.text((W4 + 12, 5), o.label_b, fill=(255, 255, 255))
            for left in (0, W4 + 6):
                d.rectangle((left + x // 4, 22 + y // 4, left + (x + w) // 4, 22 + (y + h) // 4), outline=(255, 220, 0))
            wholes.append(half)
            times.append(int(round(1000 * (fa[k + 1]['realtime'] - fa[k]['realtime']))) if k + 1 < len(fa) else 110)
            if n == len(ia) // 2:
                pair.save(os.path.join(o.out, '%s-still.jpg' % scene), quality=93)
                if sn:
                    fn = sn['frames']; kn = k if o.by_index and k < len(fn) else min(sweep(fn), key=lambda q: abs(yaw(fn[q]) - at))
                    N = Image.new('RGB', (w, h + 22), (0, 0, 0)); N.paste(Image.fromarray(raw(o.run, scene, sn, fn[kn])[y:y + h, x:x + w]), (0, 22))
                    ImageDraw.Draw(N).text((6, 5), 'no denoiser, the same moment', fill=(255, 255, 255)); N.save(os.path.join(o.out, '%s-none.jpg' % scene), quality=90)
        there_and_back = lambda L: L + L[-2:0:-1]
        crops[0].save(os.path.join(o.out, '%s-crop.webp' % scene), save_all=True, append_images=there_and_back(crops)[1:], duration=there_and_back(times), loop=0, quality=82, method=4)
        wholes[0].save(os.path.join(o.out, '%s-whole.webp' % scene), save_all=True, append_images=there_and_back(wholes)[1:], duration=there_and_back(times), loop=0, quality=70, method=4)
        print('%s: %d pairs; the camera from %.1f to %.1f degrees; a frame every %d ms' % (scene, len(ia), yaw(fa[ia[0]]), yaw(fa[ia[-1]]), sum(times) // max(len(times), 1)))
        for n in ('crop.webp', 'whole.webp', 'still.jpg', 'none.jpg'):
            f = os.path.join(o.out, '%s-%s' % (scene, n))
            if os.path.exists(f):
                print('   %s %.1f MB' % (os.path.basename(f), os.path.getsize(f) / 1e6))


if __name__ == '__main__':
    main()
