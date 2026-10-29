# SPDX-License-Identifier: MIT
"""A contact sheet of the game's saves from an inventory run (Run-PtHarness.ps1 -Inventory): each save's own picture
with its ID, its name and where it was made, newest first, so a save can be picked by what it shows.

  python tools/harness/saves_sheet.py artifacts/harness/<run> [--columns 8] [--only Slot_8]
"""
import argparse
import json
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("run")
    parser.add_argument("--columns", type=int, default=6)
    parser.add_argument("--only", default="", help="only saves whose ID or name contains this")
    parser.add_argument("--page", type=int, default=48, help="saves per sheet")
    args = parser.parse_args()
    run = Path(args.run)
    saves = json.loads((run / "saves.json").read_text(encoding="utf-8-sig")).get("saves", [])
    if args.only:
        saves = [s for s in saves if args.only.lower() in (s["id"] + " " + s["displayName"]).lower()]
    tile_w, tile_h, text_h = 320, 180, 44
    sheets = []
    for start in range(0, len(saves), args.page):
        page = saves[start:start + args.page]
        rows = math.ceil(len(page) / args.columns)
        sheet = Image.new("RGB", (args.columns * tile_w, rows * (tile_h + text_h)), (16, 16, 16))
        draw = ImageDraw.Draw(sheet)
        for i, save in enumerate(page):
            x, y = (i % args.columns) * tile_w, (i // args.columns) * (tile_h + text_h)
            picture = run / "saves" / (save["file"] + ".jpg")
            if picture.exists():
                try:
                    sheet.paste(Image.open(picture).convert("RGB").resize((tile_w, tile_h)), (x, y))
                except OSError:
                    pass
            draw.text((x + 4, y + tile_h + 2), f"{save['id']}  \"{save['displayName']}\"  {save['saved']}", fill=(255, 255, 255))
            draw.text((x + 4, y + tile_h + 16), f"{save['location']} ({save['scene']}{', ' + save['additiveScene'] if save['additiveScene'] else ''})", fill=(200, 200, 140))
            draw.text((x + 4, y + tile_h + 30), f"{save['hero']} level {save['heroLevel']}, played {save['playTime']}", fill=(150, 150, 150))
        path = run / f"saves-sheet-{len(sheets) + 1}.jpg"
        sheet.save(path, quality=88)
        sheets.append(path)
    print(f"{len(saves)} saves on {len(sheets)} sheets: " + ", ".join(str(p) for p in sheets))
    return 0


if __name__ == "__main__":
    sys.exit(main())
