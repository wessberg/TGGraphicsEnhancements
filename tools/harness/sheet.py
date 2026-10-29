# SPDX-License-Identifier: MIT
"""Every step of one scenario on one sheet (its full.jpg, or its frame.jpg), small: for the `views` recipe, where each
step is one of the tracer's debug views.

  python tools/harness/sheet.py artifacts/harness/<run>/<scenario> [--box l,t,r,b] [--width 960] [--columns 4]

--box cuts the same part out of every step (shares of the screen) and shows it at full size instead of the whole
frame scaled down. Writes views.jpg (or views-<box>.jpg) into the scenario's folder.
"""
import argparse
import sys
from pathlib import Path

from PIL import Image, ImageDraw


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("scenario")
    parser.add_argument("--box", default="")
    parser.add_argument("--width", type=int, default=960)
    parser.add_argument("--columns", type=int, default=4)
    parser.add_argument("--out", default="")
    args = parser.parse_args()
    folder = Path(args.scenario)
    box = [float(v) for v in args.box.split(",")] if args.box else None
    tiles = []
    for step in sorted(d for d in folder.iterdir() if d.is_dir()):
        path = step / "full.jpg" if (step / "full.jpg").exists() else step / "frame.jpg"
        if not path.exists():
            continue
        image = Image.open(path).convert("RGB")
        if box:
            w, h = image.size
            image = image.crop((int(box[0] * w), int(box[1] * h), int(box[2] * w), int(box[3] * h)))
            if image.width > args.width:
                image = image.resize((args.width, round(image.height * args.width / image.width)), Image.LANCZOS)
        else:
            image = image.resize((args.width, round(image.height * args.width / image.width)), Image.LANCZOS)
        draw = ImageDraw.Draw(image)
        area = draw.textbbox((4, 3), step.name)
        draw.rectangle((area[0] - 3, area[1] - 2, area[2] + 3, area[3] + 2), fill=(0, 0, 0))
        draw.text((4, 3), step.name, fill=(255, 255, 255))
        tiles.append(image)
    if not tiles:
        print("no pictures")
        return 1
    columns = min(args.columns, len(tiles))
    rows = (len(tiles) + columns - 1) // columns
    tw, th = max(t.width for t in tiles), max(t.height for t in tiles)
    sheet = Image.new("RGB", (columns * tw + (columns - 1) * 4, rows * th + (rows - 1) * 4), (255, 220, 0))
    for i, tile in enumerate(tiles):
        sheet.paste(tile, ((i % columns) * (tw + 4), (i // columns) * (th + 4)))
    out = Path(args.out) if args.out else folder / ("views.jpg" if not box else "views-" + "-".join(f"{v:.2f}" for v in box) + ".jpg")
    sheet.save(out, quality=92)
    print(f"{out}: {sheet.size[0]}x{sheet.size[1]}, {len(tiles)} steps")
    return 0


if __name__ == "__main__":
    sys.exit(main())
