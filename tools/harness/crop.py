# SPDX-License-Identifier: MIT
"""One part of the screen at full size, from several steps or runs side by side: for a head, a cup, a flame.

  python tools/harness/crop.py --box 0.00,0.52,0.09,0.78 --out head.jpg  <run>/<scenario>/1-game  <run>/<scenario>/2-traced-rr  <other run>/<scenario>/2-traced-rr

Each argument is a step's folder (its full.jpg, the frames' mean at full size). --box is left, top, right, bottom as
shares of the screen; --zoom enlarges small parts (nearest neighbour, so pixels stay pixels).
"""
import argparse
import sys
from pathlib import Path

from PIL import Image, ImageDraw


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("steps", nargs="+")
    parser.add_argument("--box", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--zoom", type=int, default=1)
    args = parser.parse_args()
    box = [float(v) for v in args.box.split(",")]
    tiles = []
    for step in args.steps:
        path = Path(step) / "full.jpg"
        if not path.exists():
            print(f"no full.jpg in {step}")
            continue
        image = Image.open(path).convert("RGB")
        w, h = image.size
        tile = image.crop((int(box[0] * w), int(box[1] * h), int(box[2] * w), int(box[3] * h)))
        if args.zoom > 1:
            tile = tile.resize((tile.width * args.zoom, tile.height * args.zoom), Image.NEAREST)
        text = f"{Path(step).parent.parent.name[9:]} {Path(step).name}"
        draw = ImageDraw.Draw(tile)
        area = draw.textbbox((4, 3), text)
        draw.rectangle((area[0] - 3, area[1] - 2, area[2] + 3, area[3] + 2), fill=(0, 0, 0))
        draw.text((4, 3), text, fill=(255, 255, 255))
        tiles.append(tile)
    if not tiles:
        return 1
    sheet = Image.new("RGB", (sum(t.width for t in tiles) + 4 * (len(tiles) - 1), max(t.height for t in tiles)), (255, 220, 0))
    x = 0
    for tile in tiles:
        sheet.paste(tile, (x, 0))
        x += tile.width + 4
    sheet.save(args.out, quality=94)
    print(f"{args.out}: {sheet.size[0]}x{sheet.size[1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
