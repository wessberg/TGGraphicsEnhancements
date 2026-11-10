# SPDX-License-Identifier: MIT
"""Measure temporal variation in raw and NRD captures. Use matching frozen scenes and sufficient frames, and distinguish spatial detail loss from reduced temporal noise. See docs/PT-HARNESS.md."""
import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze import load_json, read_frame  # noqa: E402


def frames_of(step, limit=None):
    info = load_json(step / "step.json", {}) or {}
    width, height = int(info.get("width") or 0), int(info.get("height") or 0)
    fmt = info.get("format") or ""
    files = sorted(step.glob("f*.raw"))
    if limit:
        files = files[:limit]
    if not files:
        raise SystemExit(f"{step}: no f*.raw frames (run the harness with -KeepFrames)")
    if not width or not height:
        scenario = load_json(step.parent / "scenario.json", {}) or {}
        for s in scenario.get("steps", []):
            if s.get("name") == step.name:
                width, height, fmt = int(s.get("width") or 0), int(s.get("height") or 0), s.get("format") or fmt
    if not width or not height:
        size = files[0].stat().st_size
        for w, h in ((3840, 2160), (2560, 1440), (1920, 1080)):
            if size in (w * h * 4, w * h * 3):
                width, height = w, h
    return [read_frame(f, width, height, fmt).astype(np.float32) for f in files]


def luma(image):
    return image[..., 0] * 0.2126 + image[..., 1] * 0.7152 + image[..., 2] * 0.0722


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("step")
    ap.add_argument("--game")
    ap.add_argument("--out")
    ap.add_argument("--spots", type=int, default=6)
    ap.add_argument("--box", type=int, default=224)
    ap.add_argument("--frames", type=int, default=4)
    ap.add_argument("--gain", type=float, default=1.0)
    ap.add_argument("--boil", action="store_true")
    a = ap.parse_args()
    step = Path(a.step)
    frames = frames_of(step)
    if len(frames) < 2:
        raise SystemExit("needs at least two frames")
    lum = [luma(f) for f in frames]
    change = np.mean([np.abs(lum[i + 1] - lum[i]) for i in range(len(lum) - 1)], axis=0)
    h, w = change.shape
    print(f"{step}: {len(frames)} frames of {w}x{h}; mean change a frame {change.mean():.2f} levels, "
          f"95th percentile {np.percentile(change, 95):.2f}, 99.9th {np.percentile(change, 99.9):.2f}; "
          f"pixels changing more than 8 levels a frame {100.0 * (change > 8).mean():.2f} %")
    game = None
    game_frames = None
    if a.game:
        game_frames = frames_of(Path(a.game), 16)
        game = np.mean(game_frames[:4], axis=0)
    box = a.box
    white = 16.0
    shown_index = list(range(min(a.frames, len(frames))))
    if a.boil:
        b = 16

        def wander(ls):
            stack = np.stack([x[: h // b * b, : w // b * b].reshape(h // b, b, w // b, b).mean(axis=(1, 3)) for x in ls])
            return stack.std(axis=0)

        small = wander(lum)
        change = np.zeros((h, w), dtype=np.float32)
        up = np.kron(small, np.ones((b, b), dtype=np.float32))
        change[: up.shape[0], : up.shape[1]] = up
        white = 4.0
        shown_index = sorted(set(int(round(i)) for i in np.linspace(0, len(frames) - 1, min(a.frames, len(frames)))))
        text = (f"  boiling: 16-pixel blocks wander by {small.mean():.3f} levels on average over {len(frames)} frames, "
                f"95th percentile {np.percentile(small, 95):.2f}, 99.9th {np.percentile(small, 99.9):.2f}; "
                f"blocks wandering more than 1 level {100.0 * (small > 1).mean():.2f} %")
        if game_frames is not None and len(game_frames) > 1:
            g = wander([luma(f) for f in game_frames])
            text += f"; the game's own: {g.mean():.3f} on average, 95th percentile {np.percentile(g, 95):.2f} ({len(game_frames)} frames)"
        print(text)
    cells = change[: h // box * box, : w // box * box].reshape(h // box, box, w // box, box).mean(axis=(1, 3))
    order = np.dstack(np.unravel_index(np.argsort(-cells, axis=None), cells.shape))[0]
    spots = []
    for cy, cx in order:
        if all(abs(cy - y) > 1 or abs(cx - x) > 1 for y, x in spots):
            spots.append((int(cy), int(cx)))
        if len(spots) >= a.spots:
            break
    shown = [frames[i] for i in shown_index]
    columns = (1 if game is not None else 0) + len(shown) + 1
    pad = 6
    sheet = Image.new("RGB", (columns * (box + pad) + pad, len(spots) * (box + pad + 14) + pad), (18, 18, 18))
    draw = ImageDraw.Draw(sheet)
    for row, (cy, cx) in enumerate(spots):
        y0, x0 = cy * box, cx * box
        y = pad + row * (box + pad + 14)
        tiles = []
        if game is not None:
            tiles.append(("game", np.clip(game[y0:y0 + box, x0:x0 + box] * a.gain, 0, 255)))
        for i, f in enumerate(shown):
            tiles.append((f"frame {shown_index[i]}", np.clip(f[y0:y0 + box, x0:x0 + box] * a.gain, 0, 255)))
        c = np.clip(change[y0:y0 + box, x0:x0 + box] / white, 0, 1) * 255
        tiles.append(("wander" if a.boil else "change", np.dstack([c, c, c])))
        for col, (label, tile) in enumerate(tiles):
            x = pad + col * (box + pad)
            sheet.paste(Image.fromarray(tile.astype(np.uint8)), (x, y + 12))
            draw.text((x, y), label, fill=(210, 210, 210))
        print(f"  spot {row + 1}: at {(x0 + box / 2) / w:.2f}, {(y0 + box / 2) / h:.2f} of the screen (x, y from the top left): "
              f"{'blocks wander' if a.boil else 'change a frame'} {cells[cy, cx]:.2f} levels, its brightest {lum[0][y0:y0 + box, x0:x0 + box].max():.0f}, "
              f"mean {lum[0][y0:y0 + box, x0:x0 + box].mean():.0f}")
    out = Path(a.out) if a.out else step.parent / f"{'boil' if a.boil else 'flicker'}-{step.name}.jpg"
    sheet.save(out, quality=92)
    heat = Image.fromarray((np.clip(change / white, 0, 1) * 255).astype(np.uint8)).resize((w // 4, h // 4))
    heat.save(out.with_name(out.stem + "-map.jpg"), quality=88)
    print(f"  written: {out} (one row a spot: {'the game, ' if game is not None else ''}{len(shown)} consecutive frames, the change) "
          f"and {out.with_name(out.stem + '-map.jpg').name} (the whole screen, white: {white:.0f} levels)")


if __name__ == "__main__":
    main()
