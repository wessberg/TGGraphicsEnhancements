# SPDX-License-Identifier: MIT
"""PT-46: what a perfect mirror shows when traced, against what it should show.

  python tools/harness/mirror.py <run>/<scenario> [--frame]

The `mirror` recipe (tests/ingame/scenarios.json) stands a perfect mirror in the frozen world. Its reference step takes
the mirror away and stands the camera at its mirror image: the game's own picture from there, flipped left to right, is
what a perfect mirror shows, pixel on pixel. Writes mirror.jpg (the mirror's inside: the reference, then each traced
step and the game's own drawing of the mirror, and where the traced one differs) and mirror.md (the numbers):

- brightness: the mirror's mean against the reference's, over the same ratio outside the mirror (the traced picture has
  an exposure of its own, which this takes out);
- shapes: how well the two agree once both are blurred to 1/64 of the mirror's width (1: the same picture);
- detail: the fine part's strength (each minus its own small blur) against the reference's (1: as sharp; well below:
  blurred or low-resolution textures);
- colour: the mean saturation of each.

Where the run has the traced view from the mirror's side too (step 2b-mirror-reference-traced, since 2026-10-07), a second
table holds each traced step against it: what the tracer itself shows of the same surfaces, seen directly. The first table
mixes two things, how the tracer lights the scene and how a mirror shows it; the second is the mirror alone (PT-46: a mirror
shows what the camera there would see). Its brightness takes out the two steps' own exposures (HDRP's and the traced gain,
from step.json), not the scene outside the mirror (a different view).
"""
import json
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

STEPS = (("4-mirror-traced-no-denoiser", "traced, no denoiser"), ("3n-mirror-traced-nrd", "traced, NRD"),
         ("3-mirror-traced-rr", "traced, Ray Reconstruction"),
         ("5-mirror-game", "the game's own drawing of the mirror"))
TRACED_REFERENCE = "2b-mirror-reference-traced"
# The mirror-sharp recipe's switches (PT-46, mirrors not sharp), held against the traced reference too where the run has them:
# reflections read at their textures' top mip (the hidden "Reflections read the top mip"), and Reflection Boost 0.
SWITCHES = (("7-mirror-top-mip-no-denoiser", "traced, top mip, no denoiser"), ("8-mirror-top-mip-rr", "traced, top mip, Ray Reconstruction"),
            ("9-mirror-no-boost-rr", "traced, Reflection Boost 0, Ray Reconstruction"))


def linear(image):
    return (np.asarray(image, dtype=np.float64) / 255.0) ** 2.2


def luma(a):
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


def inner_box(corners, w, h, margin=0.03):
    """The largest upright rectangle inside the mirror's four corners (bottom left, top left, top right, bottom right)."""
    xs, ys = corners[0::2], corners[1::2]
    left, right = max(xs[0], xs[1]), min(xs[2], xs[3])
    top, bottom = max(ys[1], ys[2]), min(ys[0], ys[3])
    mx, my = (right - left) * margin, (bottom - top) * margin
    box = (int((left + mx) * w), int((top + my) * h), int((right - mx) * w), int((bottom - my) * h))
    return box if box[2] - box[0] > 16 and box[3] - box[1] > 16 else None


def fine(image, radius=2):
    blurred = luma(linear(image.filter(ImageFilter.GaussianBlur(radius))))
    return luma(linear(image)) - blurred


def main():
    folder = Path(sys.argv[1])
    info = json.loads((folder / "scenario.json").read_text(encoding="utf-8-sig"))
    corners = info.get("mirrorCorners") or []
    if len(corners) != 8:
        print(f"{folder}: no mirror was put up ({info.get('mirrorText') or info.get('error') or 'no mirror step'})")
        return 1
    # Full size where every step has it (analyze.py since 2026-10-06 evening), else every step's 1920-wide frame.jpg.
    names = ["1-game", "2-mirror-reference", TRACED_REFERENCE] + [s for s, _ in STEPS + SWITCHES]
    # --frame: the 1920-wide frame.jpg even where full.jpg exists, to compare with a run that has only frame.jpg (the
    # detail number depends on the size the mirror is measured at).
    full = "--frame" not in sys.argv and all((folder / n / "full.jpg").exists() for n in names if (folder / n).is_dir())
    picture = "full.jpg" if full else "frame.jpg"
    reference_path = folder / "2-mirror-reference" / picture
    game_path = folder / "1-game" / picture
    if not reference_path.exists():
        print(f"{folder}: no reference picture (run analyze.py first)")
        return 1
    reference_full = Image.open(reference_path).convert("RGB").transpose(Image.Transpose.FLIP_LEFT_RIGHT)
    w, h = reference_full.size
    box = inner_box(corners, w, h)
    if box is None:
        print(f"{folder}: the mirror is too small or not on screen: {corners}")
        return 1
    outside = np.ones((h, w), bool)
    xs, ys = corners[0::2], corners[1::2]
    outside[int(min(ys) * h):int(max(ys) * h), int(min(xs) * w):int(max(xs) * w)] = False
    reference = reference_full.crop(box)
    ref_lin = linear(reference)
    ref_small = np.log(luma(linear(reference.resize((64, max(1, round(64 * reference.height / reference.width))), Image.Resampling.BOX))) + 1e-3)
    ref_fine = fine(reference).std()
    game_scene = luma(linear(Image.open(game_path).convert("RGB")))[outside].mean() if game_path.exists() else None
    lines = [f"# The mirror in {folder.parent.name}/{folder.name}", "",
             f"{info.get('mirrorText', '')}. Mirror inside {box[2] - box[0]}x{box[3] - box[1]} pixels.", "",
             "| step | brightness against the reference (scene's exposure taken out) | shapes (1 = same) | detail (1 = as sharp) | saturation (reference " +
             f"{saturation(ref_lin):.2f}) |", "|---|---|---|---|---|"]
    tiles = [labelled(reference, "should show: the game's own view from the mirror's side, flipped")]
    for step, text in STEPS:
        path = folder / step / picture
        if not path.exists():
            if (folder / step).is_dir():
                lines.append(f"| {step} | no picture | | | |")
            continue
        full = Image.open(path).convert("RGB")
        crop = full.crop(box)
        lin = linear(crop)
        bright = luma(lin).mean() / max(luma(ref_lin).mean(), 1e-6)
        scene = luma(linear(full))[outside].mean()
        exposure = scene / game_scene if game_scene else 1.0
        small = np.log(luma(linear(crop.resize((ref_small.shape[1], ref_small.shape[0]), Image.Resampling.BOX))) + 1e-3)
        shapes = float(np.corrcoef(small.ravel(), ref_small.ravel())[0, 1])
        detail = fine(crop).std() / max(ref_fine, 1e-6)
        lines.append(f"| {text} | {bright / max(exposure, 1e-6):.2f} (raw {bright:.2f}, scene {exposure:.2f}) | {shapes:.2f} | {detail:.2f} | {saturation(lin):.2f} |")
        tiles.append(labelled(crop, text))
        if step == STEPS[0][0]:
            diff = np.abs(np.log(luma(lin) + 1e-3) - np.log(luma(ref_lin) + 1e-3) - np.log(max(exposure, 1e-6)))
            heat = (np.clip(diff / 1.5, 0, 1) * 255).astype(np.uint8)
            tiles.append(labelled(Image.fromarray(np.stack([heat, heat // 3, 255 - heat], -1)), "where traced differs (red: by 4x or more)"))
    traced_reference_path = folder / TRACED_REFERENCE / picture
    if traced_reference_path.exists():
        traced_ref = Image.open(traced_reference_path).convert("RGB").transpose(Image.Transpose.FLIP_LEFT_RIGHT).crop(box)
        tref_lin = linear(traced_ref)
        tref_small = np.log(luma(linear(traced_ref.resize((ref_small.shape[1], ref_small.shape[0]), Image.Resampling.BOX))) + 1e-3)
        tref_fine = fine(traced_ref).std()
        tref_exposure = step_exposure(folder / TRACED_REFERENCE)
        lines += ["", "Against the tracer's own view from the mirror's side (" + TRACED_REFERENCE + ", flipped): the mirror alone, "
                  "not the tracer's light against the game's.", "",
                  "| step | brightness against it (both steps' exposures taken out) | shapes (1 = same) | detail (1 = as sharp) | saturation (it " +
                  f"{saturation(tref_lin):.2f}) |", "|---|---|---|---|---|"]
        tiles.insert(1, labelled(traced_ref, "the tracer's own view from the mirror's side, flipped"))
        for step, text in STEPS[:2] + SWITCHES:
            path = folder / step / picture
            if not path.exists():
                continue
            if (step, text) in SWITCHES:
                tiles.append(labelled(Image.open(path).convert("RGB").crop(box), text))
            lin = linear(Image.open(path).convert("RGB").crop(box))
            bright = luma(lin).mean() / max(luma(tref_lin).mean(), 1e-6)
            exposure = step_exposure(folder / step) / tref_exposure if tref_exposure else 1.0
            small = np.log(luma(linear(Image.open(path).convert("RGB").crop(box).resize((ref_small.shape[1], ref_small.shape[0]), Image.Resampling.BOX))) + 1e-3)
            shapes = float(np.corrcoef(small.ravel(), tref_small.ravel())[0, 1])
            detail = fine(Image.open(path).convert("RGB").crop(box)).std() / max(tref_fine, 1e-6)
            lines.append(f"| {text} | {bright / max(exposure, 1e-6):.2f} (raw {bright:.2f}, exposures {exposure:.2f}) | {shapes:.2f} | {detail:.2f} | {saturation(lin):.2f} |")
    sheet = Image.new("RGB", (sum(t.width for t in tiles) + 8 * (len(tiles) - 1), max(t.height for t in tiles)), (20, 20, 20))
    x = 0
    for t in tiles:
        sheet.paste(t, (x, 0))
        x += t.width + 8
    if sheet.width > 3000:
        sheet = sheet.resize((3000, round(sheet.height * 3000 / sheet.width)), Image.Resampling.LANCZOS)
    sheet.save(folder / "mirror.jpg", quality=92)
    textures = folder / "2-mirror-reference" / "textures.txt"
    if textures.exists():
        lines += ["", "What copies of the textures would take (PT-45), from `2-mirror-reference/textures.txt`:", "", "```"]
        lines += textures.read_text(encoding="utf-8").splitlines()[3:20]
        lines.append("```")
    streaming = folder / "5-mirror-game" / "mipstreaming.txt"
    if streaming.exists():
        lines += ["", "Whether mip streaming makes a new D3D11 texture (PT-45), from `5-mirror-game/mipstreaming.txt`:", "", "```"]
        lines += streaming.read_text(encoding="utf-8").splitlines()[:1]
        lines.append("```")
    (folder / "mirror.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines[:12]))
    print(f"wrote {folder / 'mirror.jpg'} and mirror.md")
    return 0


def step_exposure(step_dir):
    """A step's picture's exposure as a factor: HDRP's (step.json's "exposure log2 X now") times the traced gain; None if unread."""
    try:
        step = json.loads((step_dir / "step.json").read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return None
    found = re.search(r"exposure log2 (-?[0-9.]+) now", str(step.get("exposure", "")))
    if not found:
        return None
    gain = step.get("tracedGain") or 1.0
    return 2.0 ** float(found.group(1)) * float(gain)


def saturation(lin):
    top, low = lin.max(axis=-1), lin.min(axis=-1)
    return float(((top - low) / np.maximum(top, 1e-4)).mean())


def labelled(image, text):
    image = image.copy()
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, 8 + 6 * len(text), 14), fill=(0, 0, 0))
    draw.text((4, 1), text, fill=(255, 255, 255))
    return image


if __name__ == "__main__":
    sys.exit(main())
