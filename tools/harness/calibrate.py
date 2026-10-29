# SPDX-License-Identifier: MIT
"""Finds the view each scenario's save was made with.

A save keeps where the hero stands and which way they face, not the look up or down: every load comes back looking
level. The save's picture in the load menu (a 320x180 JPEG the game stores with it) shows the view the owner framed.
A calibration run (Run-PtHarness.ps1 -Calibrate) loads each save and takes small frames with the camera swept up and
down and a little to either side; this picks the frame that matches the save's picture and writes the view into
tests/ingame/camera.json, with the save's ID and time, so the numbers are used for that very save only.

  python tools/harness/calibrate.py artifacts/harness/<run> [--camera tests/ingame/camera.json]
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

SIZE = (160, 90)


def gray(image):
    """A picture as a small, slightly blurred gray array: exposure and fine detail differ between the two sources."""
    small = image.convert("L").resize(SIZE, Image.LANCZOS).filter(ImageFilter.GaussianBlur(1.0))
    return np.asarray(small, dtype=np.float32)


def correlation(a, b):
    a, b = a - a.mean(), b - b.mean()
    denominator = float(np.sqrt((a * a).sum() * (b * b).sum()))
    return float((a * b).sum() / denominator) if denominator > 1e-6 else 0.0


def edges(a):
    gy, gx = np.gradient(a)
    return np.sqrt(gx * gx + gy * gy)


def match(a, b):
    """How alike two views are, 1 at best: their brightness pattern and their edges, whatever the exposure."""
    return 0.5 * correlation(a, b) + 0.5 * correlation(edges(a), edges(b))


def read_shot(path, width, height, fmt):
    data = np.fromfile(path, dtype=np.uint8)
    channels = data.size // (width * height)
    image = data.reshape(height, width, channels)[::-1]
    if channels == 4:
        image = image[:, :, 1:4] if fmt == "ARGB32" else image[:, :, 2::-1] if fmt == "BGRA32" else image[:, :, :3]
    return Image.fromarray(np.ascontiguousarray(image))


def peak(values, index, step):
    """The top of a parabola through a best value and its two neighbours, as an offset in the sweep's units."""
    if index <= 0 or index >= len(values) - 1:
        return 0.0
    left, mid, right = values[index - 1], values[index], values[index + 1]
    curve = left - 2 * mid + right
    return 0.0 if curve >= -1e-9 else float(np.clip(0.5 * (left - right) / curve, -0.5, 0.5)) * step


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("run")
    parser.add_argument("--camera", default="")
    args = parser.parse_args()
    run = Path(args.run)
    camera_path = Path(args.camera) if args.camera else Path(__file__).resolve().parents[2] / "tests" / "ingame" / "camera.json"
    try:
        camera = json.loads(camera_path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        camera = {}
    camera.setdefault("about", "The view each scenario's save was made with, found by tools/harness/calibrate.py from the save's own picture "
                               "(a save does not keep the look up or down). Written by a machine: run Run-PtHarness.ps1 -Calibrate to renew it.")
    views = camera.setdefault("views", {})
    request = json.loads((run / "request.json").read_text(encoding="utf-8-sig"))
    done = 0
    for scenario in request.get("scenarios", []):
        key = scenario["key"]
        folder = run / "".join(c if (c.isalnum() or c in "-_.") else "-" for c in key)
        try:
            info = json.loads((folder / "scenario.json").read_text(encoding="utf-8-sig"))
        except (OSError, ValueError):
            print(f"{key}: not run")
            continue
        if info.get("error"):
            print(f"{key}: {info['error']}")
            continue
        shots = info.get("sweep") or []
        picture = run / "saves" / (info.get("slotFile", "") + ".jpg")
        if not shots or not picture.exists():
            print(f"{key}: no sweep or no picture of the save ({picture.name})")
            continue
        target_image = Image.open(picture).convert("RGB")
        target = gray(target_image)
        scored = []
        for shot in shots:
            path = folder / "sweep" / shot["file"]
            if not path.exists():
                continue
            image = read_shot(path, shot["width"], shot["height"], shot.get("format", "RGBA32"))
            scored.append((match(gray(image), target), shot, image))
        if not scored:
            print(f"{key}: no sweep frames left")
            continue
        best_score, best, best_image = max(scored, key=lambda s: s[0])
        level = next((s for s in scored if s[1]["pitch"] == 0 and s[1]["yawOffset"] == 0), None)
        # Between the sweep's steps: a parabola through the best and its neighbours, along the pitch and along the yaw.
        pitches = sorted({s[1]["pitch"] for s in scored})
        yaws = sorted({s[1]["yawOffset"] for s in scored})
        table = {(s[1]["yawOffset"], s[1]["pitch"]): s[0] for s in scored}
        row = [table.get((best["yawOffset"], p), -1.0) for p in pitches]
        column = [table.get((y, best["pitch"]), -1.0) for y in yaws]
        pitch = best["pitch"] + peak(row, pitches.index(best["pitch"]), pitches[1] - pitches[0] if len(pitches) > 1 else 0)
        yaw = best["yawOffset"] + peak(column, yaws.index(best["yawOffset"]), yaws[1] - yaws[0] if len(yaws) > 1 else 0)
        # Only a clear match moves the camera: where the level view is about as good a match as any (an old save whose
        # picture shows another place, a scene of open water), the view stays as the save loads.
        level_score = level[0] if level else -1.0
        clear = best_score >= 0.25 and best_score - level_score >= 0.1
        views[key] = {"pitch": round(pitch) if clear else 0, "yawOffset": round(yaw) if clear else 0, "match": round(best_score, 3),
                      "matchLevel": round(level_score, 3) if level else None, "kept": "the matched view" if clear else "level, as loaded",
                      "slot": info.get("slot", ""), "saved": info.get("slotSaved", ""), "run": request.get("run", run.name)}
        # The save's picture beside the best frame of the sweep and the level one: to see that the match is the view.
        tiles = [("the save's picture", target_image.resize((480, 270), Image.LANCZOS)),
                 (f"best: pitch {best['pitch']:+.0f}, yaw {best['yawOffset']:+.0f} (match {best_score:.2f})", best_image.resize((480, 270), Image.LANCZOS))]
        if level:
            tiles.append((f"as loaded: level (match {level[0]:.2f})", level[2].resize((480, 270), Image.LANCZOS)))
        sheet = Image.new("RGB", (480 * len(tiles), 270), (20, 20, 20))
        for i, (text, tile) in enumerate(tiles):
            draw = ImageDraw.Draw(tile)
            box = draw.textbbox((6, 4), text)
            draw.rectangle((box[0] - 4, box[1] - 2, box[2] + 4, box[3] + 2), fill=(0, 0, 0))
            draw.text((6, 4), text, fill=(255, 255, 255))
            sheet.paste(tile, (i * 480, 0))
        sheet.save(folder / "calibration.jpg", quality=92)
        for shot in shots:
            try:
                (folder / "sweep" / shot["file"]).unlink()
            except OSError:
                pass
        done += 1
        print(f"{key}: pitch {views[key]['pitch']:+d}, yaw {views[key]['yawOffset']:+d} degrees, {views[key]['kept']} (match {best_score:.2f}; as loaded {level_score:.2f}) "
              f"for {info.get('slot', '?')} of {info.get('slotSaved', '?')}")
    camera_path.write_text(json.dumps(camera, indent=1) + "\n", encoding="utf-8")
    print(f"{done} views written to {camera_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
