# SPDX-License-Identifier: MIT
"""The whole frame, the game's own beside the traced one: `game-vs-traced.jpg` in each scenario's folder.

The picture to look at first. The region's crops (`side-by-side.jpg`, the atlases) show one part at full size and miss
what changed elsewhere on screen: far hills lit through the fog, grass gone dark, a place's colour.

analyze.py writes it for every scenario; for a run analysed before it existed:

  python tools/harness/pairs.py artifacts/harness/<run> [--step 2-traced-rr] [--width 1600]
"""
import argparse
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw


def label(picture, text):
    draw = ImageDraw.Draw(picture)
    box = draw.textbbox((6, 4), text)
    draw.rectangle((box[0] - 4, box[1] - 2, box[2] + 4, box[3] + 2), fill=(0, 0, 0))
    draw.text((6, 4), text, fill=(255, 255, 255))
    return picture


def write_pair(folder, roi=None, step="", width=1600):
    """folder: a scenario's folder with its steps' full.jpg (the frames' mean at full size). Returns the path written."""
    folder = Path(folder)
    game = folder / "1-game" / "full.jpg"
    traced = folder / step / "full.jpg" if step else None
    if traced is None or not traced.exists():
        # The first traced step that kept its picture, in the steps' order (their folders are numbered).
        traced = next((d / "full.jpg" for d in sorted(folder.iterdir()) if d.is_dir() and d.name != "1-game" and (d / "full.jpg").exists()), None)
    if not game.exists() or traced is None:
        return None
    tiles = []
    for path, text in ((game, "the game's own"), (traced, traced.parent.name)):
        image = Image.open(path).convert("RGB")
        scale = width / image.width
        image = image.resize((width, round(image.height * scale)), Image.LANCZOS)
        if roi:
            ImageDraw.Draw(image).rectangle((roi[0] * image.width, roi[1] * image.height, roi[2] * image.width, roi[3] * image.height), outline=(255, 220, 0))
        tiles.append(label(image, text))
    gap = 6
    sheet = Image.new("RGB", (tiles[0].width + gap + tiles[1].width, max(t.height for t in tiles)), (255, 220, 0))
    sheet.paste(tiles[0], (0, 0))
    sheet.paste(tiles[1], (tiles[0].width + gap, 0))
    out = folder / "game-vs-traced.jpg"
    sheet.save(out, quality=92)
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("run")
    parser.add_argument("--plan", default="")
    parser.add_argument("--step", default="")
    parser.add_argument("--width", type=int, default=1600)
    args = parser.parse_args()
    run = Path(args.run)
    plan_path = Path(args.plan) if args.plan else Path(__file__).resolve().parents[2] / "tests" / "ingame" / "scenarios.json"
    try:
        rois = {s["key"]: s.get("roi") for s in json.loads(plan_path.read_text(encoding="utf-8-sig")).get("scenarios", [])}
    except (OSError, ValueError):
        rois = {}
    written = 0
    for folder in sorted(d for d in run.iterdir() if d.is_dir()):
        out = write_pair(folder, rois.get(folder.name), args.step, args.width)
        if out:
            written += 1
            print(out)
    print(f"{written} pictures written.")
    return 0 if written else 1


if __name__ == "__main__":
    sys.exit(main())
