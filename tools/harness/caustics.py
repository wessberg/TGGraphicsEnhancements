# SPDX-License-Identifier: MIT
"""PT-14: what the Caustics row adds to a scene (traced with it on, then off).

  python tools/harness/caustics.py <run>/<scenario> [...]
  python tools/harness/caustics.py <run>/lantern-lake --on 6-shade-traced --off 5-shade-no-caustics --out caustics-shade.jpg
  python tools/harness/caustics.py <run>/lantern-lake --on 3-lantern-traced --off 4-lantern-no-caustics --game 2-lantern-game

--game, --on and --off name the steps (the `caustics` recipe's by default), --out the picture, --gain how many times the
difference is brightened (8), --box left,top,right,bottom the part of the screen to give numbers for (as shares).

Writes the picture into each scenario's folder: the game's own picture, traced with caustics, traced without, and the
light the row adds (traced with minus without, in linear light, brightened), and prints how much light it adds over the
screen and where most of it is, and the exposure each of the two steps ran at (a different exposure is a difference too).
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw


def linear(path):
    return (np.asarray(Image.open(path).convert("RGB"), dtype=np.float64) / 255.0) ** 2.2


def exposure(step_dir):
    try:
        step = json.loads((step_dir / "step.json").read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return "?"
    return f"gain {step.get('tracedGain', 0):.3f}, {step.get('exposure', '?')}"


def main():
    names = {"--game": "1-game", "--on": "2-traced-rr", "--off": "3-traced-no-caustics", "--out": "caustics.jpg", "--gain": "8", "--box": ""}
    folders, args = [], sys.argv[1:]
    while args:
        arg = args.pop(0)
        if arg in names:
            names[arg] = args.pop(0)
        else:
            folders.append(Path(arg))
    gain = float(names["--gain"])
    for folder in folders:
        steps = [folder / names[n] / "full.jpg" for n in ("--game", "--on", "--off")]
        if not all(p.exists() for p in steps):
            print(f"{folder}: a step's full.jpg is missing ({', '.join(str(p.parent.name) for p in steps if not p.exists())})")
            continue
        game, on, off = (linear(p) for p in steps)
        added = np.clip(on - off, 0.0, None)
        luma = lambda a: 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
        share = luma(added).sum() / max(luma(off).sum(), 1e-9)
        h, w = added.shape[:2]
        cells = luma(added).reshape(8, h // 8, 8, w // 8).sum(axis=(1, 3)) if h % 8 == 0 and w % 8 == 0 else None
        where = ""
        if cells is not None and cells.sum() > 0:
            r, c = np.unravel_index(np.argmax(cells), cells.shape)
            where = f"; most of it in the screen's eighth at {c / 8:.2f}-{(c + 1) / 8:.2f} across, {r / 8:.2f}-{(r + 1) / 8:.2f} down"
        print(f"{folder.parent.name}/{folder.name} ({names['--on']} against {names['--off']}): caustics add {100 * share:.2f} % of the traced light{where}")
        print(f"  with: {exposure(folder / names['--on'])}")
        print(f"  without: {exposure(folder / names['--off'])}")
        if names["--box"]:
            x0, y0, x1, y1 = (float(v) for v in names["--box"].split(","))
            box = (slice(int(y0 * h), int(y1 * h)), slice(int(x0 * w), int(x1 * w)))
            with_, without = luma(on[box]), luma(off[box])
            ratio = with_ / np.maximum(without, 1e-6)
            lit = without > np.percentile(without, 20)
            print(f"  in the box {names['--box']}: light with {with_.mean():.5f}, without {without.mean():.5f} (x{with_.mean() / max(without.mean(), 1e-9):.2f}); "
                  f"per pixel the ratio's 5th, 50th and 95th percentile {np.percentile(ratio[lit], 5):.2f}, {np.percentile(ratio[lit], 50):.2f}, {np.percentile(ratio[lit], 95):.2f}")
        tiles = []
        for a, text in ((game, f"the game's own ({names['--game']})"), (on, f"traced, caustics on ({names['--on']})"), (off, f"traced, caustics off ({names['--off']})"),
                        (np.clip(added * gain, 0, 1), f"what caustics add, x{gain:g}")):
            image = Image.fromarray((np.clip(a, 0, 1) ** (1 / 2.2) * 255).astype(np.uint8)).resize((960, round(960 * h / w)), Image.Resampling.LANCZOS)
            draw = ImageDraw.Draw(image)
            draw.rectangle((0, 0, 8 + 6 * len(text), 14), fill=(0, 0, 0))
            draw.text((4, 1), text, fill=(255, 255, 255))
            tiles.append(image)
        sheet = Image.new("RGB", (2 * 960 + 8, 2 * tiles[0].height + 8), (20, 20, 20))
        for i, t in enumerate(tiles):
            sheet.paste(t, ((i % 2) * 968, (i // 2) * (t.height + 8)))
        sheet.save(folder / names["--out"], quality=90)


if __name__ == "__main__":
    main()
