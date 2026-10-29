# SPDX-License-Identifier: MIT
"""Does the traced picture carry the sub-pixel shift the upscaler is told? (PT-84, docs/agents/edges.md)

    python tools/harness/edges_shift.py <run folder> <scenario> [--steps 1-game,2-traced-none,3-traced-nrd]
                                        [--box x0,y0,x1,y1] [--sigma 1.0]

Reads the frames a harness run kept of a FROZEN world (artifacts/harness/<run>/<scenario>/<step>/f00.raw ..., with
step.json: each frame's HDRP jitter, the shown and the rendered size) and prints two things for each step:

1. The render grid in the picture. The mean difference between two neighbouring output pixels, by where their border
   falls in the grid of RENDER pixels: across a render pixel's border, and inside one. An upscaler that was handed
   frames sampled at the shifts it was told resolves edges between the render pixels: the two numbers are alike (the
   game's own picture in `forest`, run 20261008-130029: 4.21 and 4.05, a ratio of 1.04, the same down the screen). A
   picture whose samples never moved keeps its render grid: the border number stands out (traced there before PT-84,
   no denoiser: 4.69 and 3.69, 1.27; down the screen 4.55 and 3.24, 1.41; with NRD 1.43 and 1.50).

2. The picture's slide. Each frame's whole-picture movement against the frames' mean (least squares on the mean's
   gradients, after a small blur); then its step from one frame to the next, fitted against the shift HDRP told the
   upscaler in that frame, in the frame before and in the one before that (HDRP's Halton(2, 3) sequence, so the earlier
   shifts are known from this frame's). A picture whose samples sit where the upscaler was told does not slide (the
   game's own moves 0.001 to 0.003 output pixels). One that was sampled without any shift is dragged about by the told
   one (traced, before PT-84, over 150 frame steps of 10 scenes: +0.057 +-0.005 output pixels a render pixel of this
   frame's shift in x, -0.056 in y; in `forest` alone, no denoiser: +0.045 +-0.003 and -0.058 +-0.005, explaining 96 %
   of the steps; with NRD +0.142 and -0.148).

What the numbers should be after PT-84, with Denoising None and with NRD: the grid's ratio near the game's own (about
1.0 to 1.1 where it was 1.3 to 1.5), and no slide (the coefficients within their error of 0). Only numpy is needed.
"""
import argparse
import json
from pathlib import Path

import numpy as np

LUMA = np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)


def load(path, width, height):
    """A kept frame's luminance, the top row first (the files hold the bottom row first)."""
    data = np.fromfile(path, dtype=np.uint8).reshape(height, width, 4)[::-1]
    return data[..., :3].astype(np.float32) @ LUMA


def blurred(image, sigma):
    if sigma <= 0:
        return image
    radius = max(1, int(round(3 * sigma)))
    taps = np.exp(-0.5 * (np.arange(-radius, radius + 1) / sigma) ** 2)
    taps = (taps / taps.sum()).astype(np.float32)
    out = image
    for axis in (0, 1):
        pad = [(0, 0), (0, 0)]
        pad[axis] = (radius, radius)
        padded = np.pad(out, pad, mode="edge")
        acc = np.zeros_like(out)
        for k, weight in enumerate(taps):
            window = [slice(None), slice(None)]
            window[axis] = slice(k, k + out.shape[axis])
            acc += weight * padded[tuple(window)]
        out = acc
    return out


def halton(index, base):
    f, r = 1.0, 0.0
    while index > 0:
        f /= base
        r += f * (index % base)
        index //= base
    return r


def halton_index(jitter_x):
    """HDRP's index (1 to 1024) of the frame whose x shift this is."""
    for i in range(1, 1025):
        if abs(halton(i, 2) - 0.5 - jitter_x) < 1e-6:
            return i
    return None


def grid(image, render_width, render_height):
    """Neighbouring output pixels' mean difference across a render pixel's border and inside one, along x and along y."""
    height, width = image.shape
    out = []
    for axis, size, render in ((1, width, render_width), (0, height, render_height)):
        step = np.abs(np.diff(image, axis=axis)).mean(axis=1 - axis)
        phase = np.modf(np.arange(1, size) * render / size)[0]  # where the border between two output pixels lies in a render pixel
        border = step[(phase < 0.125) | (phase >= 0.875)].mean()
        inside = step[(phase >= 0.375) & (phase < 0.625)].mean()
        out.append((float(border), float(inside)))
    return out


def movements(images):
    """Each picture's whole movement (x, y; output pixels) against the pictures' mean."""
    mean = images.mean(0)
    gy, gx = np.gradient(mean)
    a = np.array([[np.sum(gx * gx), np.sum(gx * gy)], [np.sum(gx * gy), np.sum(gy * gy)]])
    out = []
    for image in images:
        d = image - mean
        s = np.linalg.solve(a, np.array([np.sum(gx * d), np.sum(gy * d)]))  # image(x) = mean(x) + s.grad: the picture moved by -s
        out.append(-s)
    return np.array(out)


def fit(steps, shifts):
    """steps = sum of c[i] * shifts[:, i] + a constant: the coefficients, their standard errors and the share explained."""
    x = np.concatenate([shifts, np.ones((len(steps), 1))], 1)
    coefficients = np.linalg.lstsq(x, steps, rcond=None)[0]
    left = steps - x @ coefficients
    spread = (left @ left) / max(len(steps) - x.shape[1], 1)
    errors = np.sqrt(np.maximum(np.diag(np.linalg.pinv(x.T @ x)) * spread, 0.0))
    centred = steps - steps.mean()
    explained = 1.0 - (left @ left) / max(centred @ centred, 1e-30)
    return coefficients, errors, explained


def report(folder, scenario, step, box, sigma):
    meta = json.load(open(folder / "step.json", encoding="utf-8-sig"))
    frames = meta["frames"]
    width, height = int(meta["width"]), int(meta["height"])
    render_width, render_height = int(meta.get("renderWidth") or width), int(meta.get("renderHeight") or height)
    print(f"{scenario}/{step}: {len(frames)} frames, {width}x{height} shown, {render_width}x{render_height} rendered" + (f", box {box}" if box else ""))
    images, borders = [], []
    for frame in frames:
        image = load(folder / frame["file"], width, height)
        borders.append(grid(image, render_width, render_height))
        if box:
            image = image[box[1]:box[3], box[0]:box[2]]
        images.append(blurred(image, sigma))
    borders = np.array(borders).mean(0)  # [axis][border, inside]
    print("  the render grid in the picture (neighbouring pixels' difference across a render pixel's border, and inside one): "
          f"x {borders[0][0]:.2f} and {borders[0][1]:.2f} (x{borders[0][0] / max(borders[0][1], 1e-9):.2f}); "
          f"y {borders[1][0]:.2f} and {borders[1][1]:.2f} (x{borders[1][0] / max(borders[1][1], 1e-9):.2f})")
    if len(frames) < 2:
        return
    moved = movements(np.stack(images))
    print(f"  the picture's movement against the frames' mean: {np.sqrt(np.mean(moved[:, 0] ** 2)):.4f} output pixels (x), {np.sqrt(np.mean(moved[:, 1] ** 2)):.4f} (y), root mean square")
    told_x = np.array([f["jitterX"] for f in frames], dtype=np.float64)
    told_y = np.array([f["jitterY"] for f in frames], dtype=np.float64)
    if not (np.any(told_x != told_x[0]) or np.any(told_y != told_y[0])):
        print("  HDRP told no shift in these frames")
        return
    indices = [halton_index(x) for x in told_x]
    if any(i is None for i in indices) or any(abs(halton(i, 3) - 0.5 - y) > 1e-4 for i, y in zip(indices, told_y)):
        print("  the frames' shifts are not HDRP's Halton(2, 3): the frame before's shift is not known, no fit")
        return
    # The steps between frames that follow each other in the game (their Halton indices do).
    pairs = [k for k in range(1, len(frames)) if indices[k] == indices[k - 1] % 1024 + 1]
    if len(pairs) < 8:
        print(f"  {len(pairs)} steps between frames that follow each other: too few to fit the slide (8 at least; the traced steps keep 16 frames)")
        return
    def earlier(index, lag):
        return (index - 1 - lag) % 1024 + 1
    for name, axis, base in (("x", 0, 2), ("y", 1, 3)):
        steps = np.array([moved[k, axis] - moved[k - 1, axis] for k in pairs])
        shifts = np.array([[halton(earlier(indices[k], lag), base) - 0.5 for lag in range(3)] for k in pairs])
        (a, b, c, _), (ea, eb, ec, _), explained = fit(steps, shifts)
        print(f"  its slide along {name}, output pixels a render pixel of shift, over {len(pairs)} steps: this frame's told shift {a:+.3f} +-{ea:.3f}, "
              f"the frame before's {b:+.3f} +-{eb:.3f}, the one before that {c:+.3f} +-{ec:.3f} (they explain {100 * max(explained, 0.0):.0f} % of the steps)")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("run", help="a harness run's folder (artifacts/harness/<time>)")
    parser.add_argument("scenario")
    parser.add_argument("--steps", default="1-game,2-traced-none,3-traced-nrd")
    parser.add_argument("--box", default="", help="x0,y0,x1,y1 in output pixels: the slide over this part only")
    parser.add_argument("--sigma", type=float, default=1.0, help="the blur before the slide is measured, output pixels")
    args = parser.parse_args()
    box = [int(v) for v in args.box.split(",")] if args.box else None
    for step in args.steps.split(","):
        folder = Path(args.run) / args.scenario / step
        if not (folder / "step.json").exists():
            print(f"{args.scenario}/{step}: no step.json in {folder}")
            continue
        report(folder, args.scenario, step, box, args.sigma)


if __name__ == "__main__":
    main()
