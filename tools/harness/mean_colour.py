"""Does a denoiser change what the picture converges to?

In a frozen world the mean of a step's undenoised frames is what the picture converges to (AGENTS.md: "Judge the
picture without a denoiser first"). A denoiser may take noise away; it may not move that mean. This compares the mean
of one step's kept frames (f00.raw, ...: a run made with -KeepFrames) with another's, block by block, in linear light:

    python tools/harness/mean_colour.py <scenario>/2-traced-none <scenario>/3-traced-nrd [--game <scenario>/1-game]
        [--block 64] [--region name=x0,y0,x1,y1 ...] [--out map.jpg]

It prints, for the whole frame and each region, each step's mean colour and the second step's light as a share of the
first's a channel; then how many blocks differ by more than 10 % in a channel, and the worst of them. --out writes a
map: grey where the two agree, red, green or blue where the second step has more of that channel, the opposite colour
where it has less.
"""
import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from flicker import frames_of  # noqa: E402


def linear_mean(step):
    frames = frames_of(Path(step))
    total = np.zeros_like(frames[0][..., :3], dtype=np.float64)
    for f in frames:
        total += (np.clip(f[..., :3], 0, 255) / 255.0) ** 2.2
    return total / len(frames), len(frames)


def blocks(image, size):
    h, w = image.shape[0] // size * size, image.shape[1] // size * size
    return image[:h, :w].reshape(h // size, size, w // size, size, 3).mean(axis=(1, 3))


def shown(c):
    return " ".join(f"{v:6.1f}" for v in (np.clip(c, 0, 1) ** (1 / 2.2)) * 255.0)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("first")
    p.add_argument("second")
    p.add_argument("--game")
    p.add_argument("--block", type=int, default=64)
    p.add_argument("--region", action="append", default=[])
    p.add_argument("--out")
    a = p.parse_args()
    first, n1 = linear_mean(a.first)
    second, n2 = linear_mean(a.second)
    game = linear_mean(a.game)[0] if a.game else None
    h, w = first.shape[:2]
    regions = [("whole frame", (0.0, 0.0, 1.0, 1.0))]
    for r in a.region:
        name, box = r.split("=")
        regions.append((name, tuple(float(v) for v in box.split(","))))
    print(f"{Path(a.first).name}: mean of {n1} frames; {Path(a.second).name}: mean of {n2} frames; {w}x{h}")
    for name, (x0, y0, x1, y1) in regions:
        s = (slice(int(y0 * h), max(int(y1 * h), int(y0 * h) + 1)), slice(int(x0 * w), max(int(x1 * w), int(x0 * w) + 1)))
        m1, m2 = first[s].reshape(-1, 3).mean(axis=0), second[s].reshape(-1, 3).mean(axis=0)
        line = f"  {name}: first R G B {shown(m1)} | second {shown(m2)} | second's light / first's: R {m2[0] / max(m1[0], 1e-9):.3f} G {m2[1] / max(m1[1], 1e-9):.3f} B {m2[2] / max(m1[2], 1e-9):.3f}"
        if game is not None:
            line += f" | the game's {shown(game[s].reshape(-1, 3).mean(axis=0))}"
        print(line)
    b1, b2 = blocks(first, a.block), blocks(second, a.block)
    lit = b1.max(axis=2) > (8 / 255.0) ** 2.2  # A block too dark to tell is not counted.
    ratio = (b2 + 1e-6) / (b1 + 1e-6)
    off = (np.abs(ratio - 1.0).max(axis=2) > 0.10) & lit
    print(f"  blocks of {a.block} px: {off.sum()} of {lit.sum()} lit blocks ({100.0 * off.sum() / max(lit.sum(), 1):.1f} %) differ by more than 10 % in a channel;"
          f" median second/first R {np.median(ratio[lit][:, 0]):.3f} G {np.median(ratio[lit][:, 1]):.3f} B {np.median(ratio[lit][:, 2]):.3f}")
    worst = np.argsort(-(np.abs(ratio - 1.0).max(axis=2) * lit).ravel())[:5]
    for i in worst:
        y, x = divmod(int(i), ratio.shape[1])
        if not lit[y, x]:
            continue
        print(f"    at {(x + 0.5) * a.block / w:.2f}, {(y + 0.5) * a.block / h:.2f}: first {shown(b1[y, x])} second {shown(b2[y, x])}"
              f" (R {ratio[y, x, 0]:.2f} G {ratio[y, x, 1]:.2f} B {ratio[y, x, 2]:.2f})")
    if a.out:
        shift = np.clip(0.5 + (np.log2(ratio) * lit[..., None]) * 0.5, 0, 1)
        picture = Image.fromarray((shift * 255).astype(np.uint8), "RGB").resize((w // 4, h // 4), Image.NEAREST)
        picture.save(a.out, quality=90)
        print(f"  map: {a.out}")


if __name__ == "__main__":
    main()
