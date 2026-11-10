# SPDX-License-Identifier: MIT
"""Measure hair stability across captured camera motion. Inspect the native-resolution frames alongside the numeric result; a scalar score alone cannot establish appearance."""
import json
import sys
from pathlib import Path

import numpy as np

LUMA = np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)


def read(path, w, h, fmt):
    data = np.fromfile(path, dtype=np.uint8)
    ch = data.size // (w * h)
    img = data.reshape(h, w, ch)[::-1]
    if ch == 4 and fmt == "ARGB32":
        img = img[:, :, 1:4]
    elif ch == 4 and fmt == "BGRA32":
        img = img[:, :, 2::-1]
    else:
        img = img[:, :, :3]
    return img.astype(np.float32) @ LUMA


def box(img, b):
    h, w = img.shape
    return img[int(b[1] * h):int(b[3] * h), int(b[0] * w):int(b[2] * w)]


def shift_of(a, b):
    """The (dy, dx) that moves b onto a, to a pixel, by phase correlation, and the peak's strength."""
    fa = np.fft.fft2(a - a.mean()); fb = np.fft.fft2(b - b.mean())
    r = fa * np.conj(fb); r /= np.maximum(np.abs(r), 1e-9)
    c = np.fft.ifft2(r).real
    i = np.unravel_index(np.argmax(c), c.shape)
    dy, dx = [v if v <= n // 2 else v - n for v, n in zip(i, c.shape)]
    return int(dy), int(dx), float(c.max())


def main():
    args = sys.argv[1:]
    opts = {"--head": None, "--hair": None, "--steps": None}
    folders = []
    while args:
        a = args.pop(0)
        if a in opts:
            opts[a] = args.pop(0)
        else:
            folders.append(Path(a))
    head = [float(v) for v in opts["--head"].split(",")]
    hair = [float(v) for v in opts["--hair"].split(",")]
    for folder in folders:
        steps = opts["--steps"].split(",") if opts["--steps"] else sorted(p.name for p in folder.iterdir() if (p / "step.json").exists())
        print(f"{folder.parent.name}/{folder.name}: head box {head}, hair box {hair}")
        for name in steps:
            sdir = folder / name
            try:
                step = json.loads((sdir / "step.json").read_text(encoding="utf-8-sig"))
            except OSError:
                continue
            frames = [f for f in step.get("frames", []) if (sdir / f["file"]).exists()]
            if len(frames) < 2:
                continue
            w, h, fmt = step["width"], step["height"], step.get("format", "RGBA32")
            imgs = [read(sdir / f["file"], w, h, fmt) for f in frames]
            ref_head = box(imgs[0], head)
            aligned = []
            offsets, hair_off = [], []
            for img in imgs:
                dy, dx, _ = shift_of(ref_head, box(img, head))
                offsets.append((dy, dx))
                moved = np.roll(np.roll(img, dy, axis=0), dx, axis=1)
                aligned.append(box(moved, hair))
            ref_hair = aligned[0]
            for a in aligned[1:]:
                hy, hx, _ = shift_of(ref_hair, a)
                hair_off.append(abs(hy) + abs(hx))
            change = np.mean([np.abs(aligned[i + 1] - aligned[i]).mean() for i in range(len(aligned) - 1)])
            travel = max(abs(o[1]) for o in offsets)
            consecutive = all(frames[i + 1]["frame"] - frames[i]["frame"] == 1 for i in range(len(frames) - 1))
            # Frame to frame (2026-10-07, agent hair-6): the head's step between two frames in a row, then the hair box of the
            # later one moved by that step against the earlier one's. Lined up on the first frame (above), a head that
            # travels more than half its box's width wraps around in the phase correlation, and everything after reads
            # tens of pixels off: a traced run, whose frames come slower and so move farther each, crossed that within 16
            # frames where the game's did not (run 20261007-144742: 16.9 and 31.9 px so, 2.1 and 4.7 px frame to frame).
            step_px, step_off, step_change, walked = [], [], [], [0]
            for i in range(1, len(imgs)):
                dy, dx, _ = shift_of(box(imgs[i - 1], head), box(imgs[i], head))
                moved = np.roll(np.roll(imgs[i], dy, axis=0), dx, axis=1)
                a, b = box(imgs[i - 1], hair), box(moved, hair)
                hy, hx, _ = shift_of(a, b)
                step_px.append(abs(dx) + abs(dy)); step_off.append(abs(hy) + abs(hx)); step_change.append(np.abs(a - b).mean())
                walked.append(walked[-1] + dx)
            # (By the steps added up: against the first frame a wrapped shift reads small again.)
            wrapped = max(abs(w) for w in walked) >= box(imgs[0], head).shape[1] // 2 - 2
            print(f"  {name:28s} traced={str(step.get('traced')):5s} frames={len(frames):2d}{' consecutive' if consecutive else ''}: "
                  f"head travels {travel:3d} px; hair after lining up on the head: changes {change:5.2f} levels a frame, "
                  f"its pattern off the head's by {np.mean(hair_off) if hair_off else 0:4.1f} px on average, at most {max(hair_off) if hair_off else 0} px"
                  f"{' (THE HEAD WRAPPED: read the next line)' if wrapped else ''}")
            print(f"  {'':28s} frame to frame: the head steps {np.mean(step_px):5.1f} px; the hair against it changes {np.mean(step_change):5.2f} levels, "
                  f"its pattern off the head's by {np.mean(step_off):4.1f} px a frame, at most {max(step_off)} px")


if __name__ == "__main__":
    main()
