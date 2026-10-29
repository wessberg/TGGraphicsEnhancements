"""PT-75, on the CPU: what the a-trous pass's weights do to the mean of a signal made of rare bright samples.

A flat surface (no edges: only the luminance weight acts). Each pixel's light arrives as rare bright samples: with
probability p a frame brings 1 / p, else nothing (mean 1). The temporal pass is imitated as pt_temporal does it: an
exponential history of 16 frames with the luminance's two moments, the variance divided by the history's length. Then
the a-trous iterations (5x5 B3 spline, steps 1 and 2, the first fed back into the history, as tracer.cpp runs the direct
light) over many frames, three ways:

  old           each pixel divides by its own weights' sum; the weight by this pixel's noise alone (3x3 smoothed)
  both ways     the same division; the weight by both pixels' noise (the earlier agent's measured variant)
  keeps light   every weight the same seen from either pixel, and a pixel keeps what its neighbours do not take

Printed: the settled picture's mean over the samples' mean, and its noise (a pixel's spread over time).
"""
import numpy as np

rng = np.random.default_rng(3)
K1 = np.array([1 / 16, 1 / 4, 3 / 8, 1 / 4, 1 / 16])


def shifted(a, dy, dx):
    return np.roll(np.roll(a, dy, 0), dx, 1)   # a torus: no borders, every pair is seen from both sides


def gauss3(v):
    out = np.zeros_like(v)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            out += shifted(v, dy, dx) * (0.5 if dy == 0 else 0.25) * (0.5 if dx == 0 else 0.25)
    return out


def atrous(c, dev, step, way):
    lc = c
    var3 = gauss3(dev * dev)
    if way == "keeps light":
        total, taken, var = np.zeros_like(c), np.zeros_like(c), np.zeros_like(c)
    else:
        total, weights, var = c.copy(), np.ones_like(c), dev * dev
    for y in range(-2, 3):
        for x in range(-2, 3):
            if x == 0 and y == 0:
                continue
            cq, dq = shifted(c, y * step, x * step), shifted(dev, y * step, x * step)
            k = K1[y + 2] * K1[x + 2]
            if way == "old":
                sigma = 4 * np.sqrt(np.maximum(var3, 1e-10)) + 1e-4
            elif way == "both ways":
                sigma = 4 * np.sqrt(np.maximum(var3 + dq * dq, 1e-10)) + 1e-4
            else:
                sigma = 4 * np.sqrt(np.maximum(0.5 * (dev * dev + dq * dq), 1e-10)) + 1e-4
            wl = np.exp(-np.abs(cq - lc) / sigma)
            if way == "keeps light":
                w = k * wl
                total += cq * w; taken += w; var += dq * dq * w * w
            else:
                w = k / (K1[2] * K1[2]) * wl
                total += cq * w; weights += w; var += dq * dq * w * w
    if way == "keeps light":
        kept = 1 - taken
        return c * kept + total, np.sqrt(var + dev * dev * kept * kept)
    return total / weights, np.sqrt(var) / weights


def run(p, way, size=96, frames=160, measure=64, length=16):
    hist = np.zeros((size, size)); m1 = np.zeros_like(hist); m2 = np.zeros_like(hist); n = np.zeros_like(hist)
    outs = []; raw_sum = 0.0
    for f in range(frames):
        sample = (rng.uniform(size=(size, size)) < p) / p
        n = np.minimum(n + 1, length)
        a = np.maximum(1 / n, 1 / length)
        hist = hist + (sample - hist) * a
        m1 = m1 + (sample - m1) * a; m2 = m2 + (sample * sample - m2) * a
        var = np.maximum(m2 - m1 * m1, 0) / n
        var = np.where(n < 4, np.maximum(var, sample * sample * (4 - n)), var)
        dev = np.sqrt(var)
        first, d1 = atrous(hist, dev, 1, way)
        second, _ = atrous(first, d1, 2, way)
        hist = first                                   # the feedback
        if f >= frames - measure:
            outs.append(second); raw_sum += sample.mean()
    outs = np.array(outs)
    return outs.mean() / (raw_sum / measure), outs.std(0).mean() / outs.mean()


if __name__ == "__main__":
    for p in (0.5, 0.1, 0.02, 0.005):
        line = f"a bright sample every {1 / p:5.0f} frames:"
        for way in ("old", "both ways", "keeps light"):
            mean, noise = run(p, way)
            line += f"  {way}: mean {mean:.3f}, noise {noise:.3f} |"
        print(line)
