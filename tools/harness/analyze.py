# SPDX-License-Identifier: MIT
"""Turns a harness run's folder into pictures and numbers.

The runner plugin (src/TGPathTracing.Harness) wrote, for each scenario and step, the frames as raw bytes, the state
of HDRP as text and a step.json. This reads them and writes, into the same folder:

  <scenario>/overview.jpg        every step side by side: the frame, the scenario's region at 1:1, what changes
                                 from frame to frame
  <scenario>/<step>/atlas.jpg    the region in every frame, 1:1, with the mean, the change map and two differences
  <scenario>/restore-<step>.jpg  the game before tracing, the game after it was turned off, and their difference
  report.md, summary.json        the numbers, the checks against tests/ingame/targets.json, and what went wrong

Numbers are in 8-bit levels of the picture as shown (0 to 255, luma), so they mean what the eye sees:
  flicker   how much a single pixel changes from frame to frame (the standard deviation over the frames, averaged)
  boil      the same for the mean of 8x8-pixel blocks: single-pixel noise averages out, patches that boil remain
The game's own picture (path tracing off) is the reference for both: its upscaler shimmers a little too.

  python tools/harness/analyze.py artifacts/harness/<run> [--plan ...] [--targets ...] [--keep-frames]
"""
import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pairs  # noqa: E402  (beside this file: the whole-frame pair, also a tool of its own)
import re  # noqa: E402


def per_second_tables(perf):
    """PT-109 (megageo-5): a step measured a second at a time (Step.perSecond, every walk): its stutter (the frames over 50 ms
    and the worst), the tracer's GPU time, the scene stage and the plants' structures in it (TGPT_GetStageTimes over that
    second), what the traced world still waits for, its acceleration structures' memory and where the camera was."""
    lines = []
    for p in perf:
        rows = p.get("perSecond") or []
        if not rows:
            continue
        lines += ["", f"{p['name']}: {p.get('over50', 0)} frames over 50 ms and {p.get('over100', 0)} over 100 ms of {p.get('frames', 0)}, the worst {p.get('worstMs') or 0:.0f} ms.",
                  "", "| second | frames | mean ms | worst ms | over 50 ms | tracer GPU ms | scene ms | plants' structures ms (built, refitted a frame) | top level ms | still to come (instances, plants, meshes, micromaps, tiles) | acceleration MB | camera x, z |",
                  "|---|---|---|---|---|---|---|---|---|---|---|---|"]
        for r in rows:
            stages = r.get("stages") or ""
            scene = re.search(r"\bscene ([\d.]+) ms", stages)
            plants = re.search(r"plants ([\d.]+) ms \(([\d.]+) built, ([\d.]+) refitted", stages)
            top = re.search(r"top level ([\d.]+) ms", stages)
            waiting = f"{r.get('instancesWaiting', 0)}, {r.get('plantsWaiting', 0)}, {r.get('meshesQueued', 0)}, {r.get('micromapsWaiting', 0)}, {r.get('tilesToBake', 0)}"
            lines.append(f"| {r.get('at', 0):.0f} | {r.get('frames', 0)} | {r.get('frameMs', 0):.1f} | {r.get('worstMs', 0):.0f} | {r.get('over50', 0)} | {r.get('tracerGpuMs', 0):.1f} | "
                         f"{scene.group(1) if scene else '-'} | {(plants.group(1) + ' (' + plants.group(2) + ', ' + plants.group(3) + ')') if plants else '-'} | "
                         f"{top.group(1) if top else '-'} | {waiting} | {r.get('accelerationMb', 0):.0f} | {r.get('x', 0):.0f}, {r.get('z', 0):.0f} |")
    return lines
import neural_analysis  # noqa: E402

LUMA = np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
HEAT = np.array([[0, 0, 0], [40, 10, 90], [130, 30, 110], [215, 70, 50], [250, 160, 20], [255, 240, 150], [255, 255, 255]], dtype=np.float32)


def load_json(path, default=None):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return default


def read_frame(path, width, height, fmt):
    """One frame as uint8 RGB, top row first."""
    data = np.fromfile(path, dtype=np.uint8)
    channels = data.size // (width * height) if width * height else 0
    if channels not in (3, 4) or data.size != width * height * channels:
        raise ValueError(f"{path.name}: {data.size} bytes is not {width}x{height} of 3 or 4 channels")
    image = data.reshape(height, width, channels)[::-1]
    if channels == 3:
        return image
    if fmt == "ARGB32":
        return image[:, :, 1:4]
    if fmt == "BGRA32":
        return image[:, :, 2::-1]
    return image[:, :, :3]


def block_means(luma, size):
    h, w = luma.shape
    h2, w2 = h - h % size, w - w % size
    return luma[:h2, :w2].reshape(h2 // size, size, w2 // size, size).mean(axis=(1, 3))


def heat(values, full):
    """A map of values (0..full) as colors, black to white through purple and orange."""
    t = np.clip(values / max(full, 1e-6), 0, 1) * (len(HEAT) - 1)
    low = np.floor(t).astype(np.int32)
    high = np.minimum(low + 1, len(HEAT) - 1)
    f = (t - low)[..., None]
    return (HEAT[low] * (1 - f) + HEAT[high] * f).astype(np.uint8)


def fit(image, width):
    """An array as a PIL image at most `width` wide."""
    picture = Image.fromarray(np.ascontiguousarray(image))
    if picture.width > width:
        picture = picture.resize((width, max(1, round(picture.height * width / picture.width))), Image.LANCZOS)
    return picture


def label(picture, text):
    draw = ImageDraw.Draw(picture)
    box = draw.textbbox((6, 4), text)
    draw.rectangle((box[0] - 4, box[1] - 2, box[2] + 4, box[3] + 2), fill=(0, 0, 0))
    draw.text((6, 4), text, fill=(255, 255, 255))
    return picture


def region_pixels(roi, width, height):
    left, top, right, bottom = roi
    x0, x1 = int(round(left * width)), int(round(right * width))
    y0, y1 = int(round(top * height)), int(round(bottom * height))
    return max(0, x0), max(0, y0), min(width, max(x1, x0 + 16)), min(height, max(y1, y0 + 16))


def centre_crop(box, limit):
    """At most limit x limit pixels around the middle of a box."""
    x0, y0, x1, y1 = box
    cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
    w, h = min(x1 - x0, limit), min(y1 - y0, limit)
    return cx - w // 2, cy - h // 2, cx - w // 2 + w, cy - h // 2 + h


class StepData:
    """A step's frames, reduced: the mean picture, how each pixel and block changes, and the region's crops."""

    def __init__(self, folder, step, roi):
        self.folder, self.step, self.name = folder, step, step.get("name", folder.name)
        self.frames = [f for f in step.get("frames", []) if (folder / f["file"]).exists()]
        self.ok = len(self.frames) > 0
        self.metrics = {}
        if not self.ok:
            return
        w, h, fmt = step["width"], step["height"], step.get("format", "RGBA32")
        self.width, self.height = w, h
        self.box = region_pixels(roi, w, h)
        self.crop_box = centre_crop(self.box, 448)
        total = np.zeros((h, w, 3), dtype=np.float32)
        s1 = np.zeros((h, w), dtype=np.float64)
        s2 = np.zeros((h, w), dtype=np.float64)
        blocks8, blocks32, crops, halves = [], [], [], []
        self.first = None
        self.frame_rgb = []
        self.frame_blocks_rgb = []
        for index, frame in enumerate(self.frames):
            rgb = read_frame(folder / frame["file"], w, h, fmt)
            if self.first is None:
                self.first = rgb.copy()
            self.frame_rgb.append(rgb[h // 10:h - h // 10, w // 10:w - w // 10].mean(axis=(0, 1)).tolist())
            if step.get("neuralEvaluations") is not None:
                self.frame_blocks_rgb.append(neural_analysis.rgb_blocks(rgb))
            luma = rgb.astype(np.float32) @ LUMA
            total += rgb
            s1 += luma
            s2 += luma.astype(np.float64) ** 2
            blocks8.append(block_means(luma, 8))
            blocks32.append(block_means(luma, 32))
            cx0, cy0, cx1, cy1 = self.crop_box
            crops.append(rgb[cy0:cy1, cx0:cx1].copy())
            halves.append(luma)
            if len(halves) > 0 and index == len(self.frames) // 2 - 1:
                self.first_half = np.mean(halves, axis=0)
                halves = []
        n = len(self.frames)
        self.second_half = np.mean(halves, axis=0) if halves and n > 1 else None
        if n < 2:
            self.first_half = None
        self.mean = total / n
        self.mean_luma = (s1 / n).astype(np.float32)
        self.std = np.sqrt(np.maximum(s2 / n - (s1 / n) ** 2, 0)).astype(np.float32)
        b8, b32 = np.stack(blocks8), np.stack(blocks32)
        self.block8_std = b8.std(axis=0)
        self.block32_mean = b32.mean(axis=0)
        self.block8_step = np.abs(np.diff(b8, axis=0)).mean(axis=0) if n > 1 else np.zeros_like(b8[0])
        self.crops = crops
        x0, y0, x1, y1 = self.box
        # The middle of the screen: the edges hold what is left of a HUD, and vignette.
        mx0, mx1, my0, my1 = w // 10, w - w // 10, h // 10, h - h // 10
        m = self.metrics
        m["frames"] = n
        m["consecutive"] = bool(n > 1 and all(self.frames[i + 1]["frame"] - self.frames[i]["frame"] == 1 for i in range(n - 1)))
        m["luma"] = float(self.mean_luma[my0:my1, mx0:mx1].mean())
        # The darkest and brightest twentieths of the picture: haze lifts the dark end, blown highlights raise the bright.
        middle = self.mean_luma[my0:my1, mx0:mx1]
        m["dark"], m["bright"] = float(np.percentile(middle, 5)), float(np.percentile(middle, 95))
        m["near_white"] = float((middle > 235).mean() * 100)
        m["luma_roi"] = float(self.mean_luma[y0:y1, x0:x1].mean())
        m["rgb_roi"] = [float(v) for v in self.mean[y0:y1, x0:x1].reshape(-1, 3).mean(axis=0)]
        m["flicker"] = float(self.std[my0:my1, mx0:mx1].mean())
        m["flicker_roi"] = float(self.std[y0:y1, x0:x1].mean())
        inner8 = self.block8_std[my0 // 8:my1 // 8, mx0 // 8:mx1 // 8]
        roi8 = self.block8_std[y0 // 8:max(y1 // 8, y0 // 8 + 1), x0 // 8:max(x1 // 8, x0 // 8 + 1)]
        m["boil"] = float(inner8.mean())
        m["boil_p95"] = float(np.percentile(inner8, 95))
        m["boil_roi"] = float(roi8.mean())
        m["boil_roi_p95"] = float(np.percentile(roi8, 95))
        m["step_change"] = float(self.block8_step[my0 // 8:my1 // 8, mx0 // 8:mx1 // 8].mean())
        # Fine detail in the region: the mean picture's Laplacian (the noise is averaged out of it).
        region = self.mean_luma[y0:y1, x0:x1]
        if region.shape[0] > 4 and region.shape[1] > 4:
            lap = 4 * region[1:-1, 1:-1] - region[:-2, 1:-1] - region[2:, 1:-1] - region[1:-1, :-2] - region[1:-1, 2:]
            m["detail_roi"] = float(np.abs(lap).mean())
        else:
            m["detail_roi"] = 0.0

    def frame_picture(self, width):
        picture = fit(self.mean.astype(np.uint8), width)
        scale = picture.width / self.width
        x0, y0, x1, y1 = self.box
        ImageDraw.Draw(picture).rectangle((x0 * scale, y0 * scale, x1 * scale, y1 * scale), outline=(255, 220, 0), width=2)
        return picture

    def atlas(self, path):
        tiles = list(self.crops)
        th, tw = tiles[0].shape[:2]
        cx0, cy0, cx1, cy1 = self.crop_box
        extra = [("mean", self.mean[cy0:cy1, cx0:cx1].astype(np.uint8)),
                 ("change per pixel (white: 8 levels)", heat(self.std[cy0:cy1, cx0:cx1], 8.0))]
        if len(tiles) > 1:
            extra.append(("frame 1 - frame 0, x8", np.clip(np.abs(tiles[1].astype(np.int16) - tiles[0]) * 8, 0, 255).astype(np.uint8)))
            extra.append((f"frame {len(tiles) - 1} - frame 0, x8", np.clip(np.abs(tiles[-1].astype(np.int16) - tiles[0]) * 8, 0, 255).astype(np.uint8)))
        columns = 4
        named = [(f"frame {i}", t) for i, t in enumerate(tiles)] + extra
        rows = math.ceil(len(named) / columns)
        sheet = Image.new("RGB", (columns * tw, rows * th), (20, 20, 20))
        for i, (text, tile) in enumerate(named):
            sheet.paste(label(Image.fromarray(np.ascontiguousarray(tile)), text), ((i % columns) * tw, (i // columns) * th))
        sheet.save(path, quality=92)


def side_by_side(steps, path):
    """The scenario's region at full size, the game's own picture beside each traced one (the frames' mean): the
    picture to look at for "does hair still look like the game's"."""
    picked = []
    for s in steps:
        if s.ok and s.step.get("frozen") and (not picked or s.step.get("traced")):
            picked.append(s)
    if len(picked) < 2:
        return
    tiles = []
    for s in picked[:5]:
        x0, y0, x1, y1 = centre_crop(s.box, 768)
        denoiser = "Ray Reconstruction" if s.step.get("rayReconstruction") else "NRD" if s.step.get("nrd") else "traced"
        tiles.append(label(Image.fromarray(np.ascontiguousarray(s.mean[y0:y1, x0:x1].astype(np.uint8))), f"{s.name}: " + (denoiser if s.step.get("traced") else "the game's own")))
    sheet = Image.new("RGB", (sum(t.width for t in tiles), max(t.height for t in tiles)), (20, 20, 20))
    x = 0
    for tile in tiles:
        sheet.paste(tile, (x, 0))
        x += tile.width
    sheet.save(path, quality=93)


def overview(steps, path, title):
    """Every step of a scenario side by side: the frame, the region at 1:1, the change from frame to frame."""
    steps = [s for s in steps if s.ok]
    if not steps:
        return
    width = 640
    columns = []
    for s in steps:
        m = s.metrics
        frame = label(s.frame_picture(width), f"{s.name}  luma {m['luma']:.1f}")
        cx0, cy0, cx1, cy1 = s.crop_box
        region = label(fit(s.first[cy0:cy1, cx0:cx1], width), f"region, 1:1  flicker {m['flicker_roi']:.2f}  boil {m['boil_roi']:.2f}")
        change = label(fit(heat(s.std, 8.0), width), f"change per pixel (white: 8 levels)  flicker {m['flicker']:.2f}  boil {m['boil']:.2f}")
        column = Image.new("RGB", (width, frame.height + region.height + change.height), (20, 20, 20))
        column.paste(frame, (0, 0))
        column.paste(region, ((width - region.width) // 2, frame.height))
        column.paste(change, (0, frame.height + region.height))
        columns.append(column)
    # At most five steps a row: a ten-step recipe in one row is too small to read.
    per_row = 5
    rows = math.ceil(len(columns) / per_row)
    height = max(c.height for c in columns)
    sheet = Image.new("RGB", (width * min(len(columns), per_row), rows * height + 22), (20, 20, 20))
    ImageDraw.Draw(sheet).text((6, 4), title, fill=(255, 255, 255))
    for i, column in enumerate(columns):
        sheet.paste(column, ((i % per_row) * width, 22 + (i // per_row) * height))
    sheet.save(path, quality=90)


def restore_check(before, after, limits, path, again=None, self_pairs=()):
    """The game's picture after tracing was turned off against the game's picture before it was ever on."""
    # Judged against the take right before tracing (the second one, where the recipe has two): the recipe puts it as
    # long after the first take as the take after tracing comes after it. What the game's picture drifts by itself in
    # that time (the control recipe showed it does: a place settling after its load, light that moves on sunlit ground)
    # is then in the floor, between the two takes before tracing, and not in the verdict.
    reference = again if again is not None else before
    result = {"step": after.name, "against": reference.name}
    difference = np.abs(after.mean_luma - reference.mean_luma)
    blocks = np.abs(after.block32_mean - reference.block32_mean)
    # What the game's own picture changes by itself, block by block: the floor each block is judged against. Between
    # two halves of the first take's frames (a twentieth of a second); between the two takes before anything was
    # traced, seconds apart (a lantern flickers even in a frozen world); and between two takes after tracing (a
    # leftover of the tracer is the same in both: what differs between them moves by itself, a flame, foam on water).
    # A block's own floor, not the picture's worst: one flame must not excuse the rest of the picture.
    floor_map = np.zeros_like(blocks)
    floor_mean = 0.0
    if before.first_half is not None and before.second_half is not None:
        floor_map = np.maximum(floor_map, np.abs(block_means(before.first_half, 32) - block_means(before.second_half, 32)))
        floor_mean = float(np.abs(before.first_half - before.second_half).mean())
    pairs = ([(before, again)] if again is not None else []) + list(self_pairs)
    for a, b in pairs:
        floor_map = np.maximum(floor_map, np.abs(a.block32_mean - b.block32_mean))
        floor_mean = max(floor_mean, float(np.abs(a.mean_luma - b.mean_luma).mean()))
    # What moves spills into the blocks beside it.
    padded = np.pad(floor_map, 1, mode="edge")
    spread = np.zeros_like(floor_map)
    for dy in range(3):
        for dx in range(3):
            spread = np.maximum(spread, padded[dy:dy + floor_map.shape[0], dx:dx + floor_map.shape[1]])
    allowed_map = np.maximum(limits.get("block_levels", 1.0), limits.get("floor_factor", 3.0) * spread)
    over = blocks - allowed_map
    worst = np.unravel_index(np.argmax(over), over.shape)
    allowed_mean = max(limits.get("mean_levels", 0.25), limits.get("floor_factor", 3.0) * floor_mean)
    result["difference"] = float(difference.mean())
    result["block_max"] = float(blocks[worst])
    result["block_p99"] = float(np.percentile(blocks, 99))
    result["floor"] = floor_mean
    result["floor_block_max"] = float(floor_map.max())
    result["blocks_over"] = int((over > 0).sum())
    result["allowed_block"], result["allowed_mean"] = float(allowed_map[worst]), allowed_mean
    result["same_picture"] = bool(result["blocks_over"] == 0 and result["difference"] <= allowed_mean)
    result["worst_at"] = [float((worst[1] + 0.5) * 32 / before.width), float((worst[0] + 0.5) * 32 / before.height)]
    width = 640
    parts = [label(fit(reference.mean.astype(np.uint8), width), reference.name),
             label(fit(after.mean.astype(np.uint8), width), after.name),
             label(fit(heat(difference, 8.0), width), f"difference (white: 8 levels)  mean {result['difference']:.3f}  worst block {result['block_max']:.2f}")]
    sheet = Image.new("RGB", (width * 3, max(p.height for p in parts)), (20, 20, 20))
    for i, part in enumerate(parts):
        sheet.paste(part, (i * width, 0))
    sheet.save(path, quality=90)
    return result


def read_audit(path):
    values = {}
    try:
        for line in Path(path).read_text(encoding="utf-8-sig").splitlines():
            key, tab, value = line.partition("\t")
            if tab:
                values[key] = value
    except OSError:
        return None
    return values


def same_value(a, b):
    if a == b:
        return True
    try:
        x, y = float(a), float(b)
    except ValueError:
        return False
    return abs(x - y) <= 1e-5 * max(abs(x), abs(y), 1e-6)


def audit_check(before_dir, after_dir):
    """HDRP's state after tracing was turned off against its state before: lights, volumes, camera, materials."""
    a1, a2 = read_audit(before_dir / "audit-a.txt"), read_audit(before_dir / "audit-b.txt")
    b1, b2 = read_audit(after_dir / "audit-a.txt"), read_audit(after_dir / "audit-b.txt")
    if a1 is None or b1 is None:
        return None
    # What differs between two audits a moment apart changes by itself (a flame's flicker), not by the tracer.
    volatile = set()
    for first, second in ((a1, a2), (b1, b2)):
        if second is not None:
            volatile.update(k for k in first if k in second and not same_value(first[k], second[k]))
    changed, added, removed = [], [], []
    compared = 0
    for key, value in a1.items():
        if key.startswith("info|"):
            continue
        if key not in b1:
            removed.append(key)
            continue
        compared += 1
        if key in volatile:
            continue
        if not same_value(value, b1[key]):
            changed.append({"key": key, "before": value, "after": b1[key]})
    for key in b1:
        if not key.startswith("info|") and key not in a1:
            added.append(key)
    # Lights, volumes and custom passes that appear or vanish count; materials come and go as the game makes them.
    structural = lambda keys: [k for k in keys if not k.startswith("material|")]
    return {"compared": compared, "changed": changed, "volatile": len(volatile),
            "added": len(added), "removed": len(removed),
            "added_other": structural(added)[:40], "removed_other": structural(removed)[:40],
            "same_state": len(changed) == 0}


def changed_parts(entry):
    """For a material's line, only the parts that differ."""
    before, after = entry["before"], entry["after"]
    if "; " not in before:
        return f"{before} -> {after}"
    b = dict(p.partition("=")[::2] for p in before.replace("; ", ";").split(";") if "=" in p)
    a = dict(p.partition("=")[::2] for p in after.replace("; ", ";").split(";") if "=" in p)
    out = []
    for name in sorted(set(a) | set(b)):
        if a.get(name) != b.get(name):
            out.append(f"{name}: {b.get(name, '(none)')[:160]} -> {a.get(name, '(none)')[:160]}")
    return "; ".join(out) or f"{before[:160]} -> {after[:160]}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("run")
    parser.add_argument("--plan", default="")
    parser.add_argument("--targets", default="")
    parser.add_argument("--keep-frames", action="store_true")
    args = parser.parse_args()
    run = Path(args.run)
    plan = load_json(args.plan, {}) if args.plan else {}
    targets = load_json(args.targets, {}) if args.targets else {}
    request = load_json(run / "request.json", {})
    environment = load_json(run / "environment.json", {})
    done = load_json(run / "done.json")
    saves_check = load_json(run / "saves-check.json", {})
    planned = {s["key"]: s for s in plan.get("scenarios", [])}
    limits = targets.get("restore", {})
    stability = targets.get("stability", {})
    performance = targets.get("performance", {})
    problems, to_make, summary = [], [], {"run": request.get("run", run.name), "scenarios": []}
    lines = [f"# Harness run {request.get('run', run.name)}", ""]
    if done is None:
        problems.append("The run did not finish (no done.json): the game closed, froze or ran out of time. See progress.log.")
    elif done.get("error"):
        problems.append("The run stopped: " + done["error"])
    lines.append(f"- GPU: {environment.get('gpu', '?')} ({environment.get('gpuDriver', '?')}), screen {environment.get('screenWidth', '?')}x{environment.get('screenHeight', '?')}"
                 f", HDR output {'on' if environment.get('hdrOutput') else 'off'}")
    lines.append(f"- Game {environment.get('game', '?')}, Unity {environment.get('unity', '?')}, {len(environment.get('plugins', []))} BepInEx plugins: "
                 + ", ".join(f"{p['name']} {p['version']}" for p in environment.get("plugins", [])))
    if saves_check:
        lines.append(f"- Save files: {saves_check.get('files', '?')}, " + ("untouched by the run" if saves_check.get("untouched") else "CHANGED during the run"))
        if not saves_check.get("untouched", True):
            problems.append("Save files changed during the run: " + "; ".join(saves_check.get("changes", [])[:6]))
    if done:
        lines.append(f"- {done.get('scenarios', 0)} scenarios in {done.get('seconds', 0) / 60:.1f} minutes, {done.get('failed', 0)} failed, {done.get('savesBlocked', 0)} saves stopped")
    build = load_json(run / "build.json", {})
    if build:
        lines.append("- The build tested: " + ", ".join(f"{name} {info.get('sha256', '')[:12]} ({info.get('written', '?')})" for name, info in build.items()))
    gpu = load_json(run / "gpu.json", {})
    if gpu:
        during = gpu.get("during") or {}
        # The owner's stream (Sunshine) is no "other program" for the run's speed since 2026-10-12 ("I would prefer if I
        # can stay on the SUnshine/moonlight stream"): the owner plays over it, so speed is measured with it. It is named
        # below; what spoils a run's speed is any OTHER program on the card.
        others = {name: percent for name, percent in during.items() if not name.lower().startswith("sunshine")}
        lines.append(f"- The GPU: waited {gpu.get('waitedSeconds', 0)} s for it to be free; other programs during the run: "
                     + (", ".join(f"{name} up to {percent} %" for name, percent in during.items()) if during else "none"))
        if others:
            problems.append("Other programs used the GPU during the run (" + ", ".join(f"{name} up to {percent} %" for name, percent in others.items())
                            + "): the frame rates are not to be trusted, and the run slowed them down.")
        # The game in front (the owner, 2026-10-10: "only respect our frametimes when we know it was"): a run that never
        # saw the game's window, or saw it behind another or minimized, says so; its stretches name the steps to doubt.
        behind = gpu.get("notInFront") or []
        if isinstance(behind, str):
            behind = [behind]
        if gpu.get("keptMinimized"):
            lines.append("- The game's window: KEPT MINIMIZED ON PURPOSE for this run (-KeepMinimized), to measure what that costs.")
            problems.append("The game was kept minimized on purpose (-KeepMinimized): its frame rates are not the player's.")
        elif "windowChecks" in gpu:
            lines.append(f"- The game's window: checked {gpu.get('windowChecks', 0)} times; not in front: " + ("; ".join(behind) if behind else "never"))
            if not gpu.get("windowChecks"):
                problems.append("The game's window was never seen: whether it was in front is not known: the frame rates are not to be trusted.")
            elif behind:
                problems.append("The game was not in front (" + "; ".join(behind) + "): frame rates of steps in those stretches are not to be trusted.")
        else:
            lines.append("- The game's window: not checked (a run from before 2026-10-10 16:00): whether it was in front is not known.")
        video = gpu.get("video") or {}
        if video:
            # Not a problem by the owner's word (2026-10-06): the screen's own streaming. It still costs a little.
            lines.append("- Video encoding during the run (it holds no run back, and costs a little frame rate): "
                         + ", ".join(f"{name} up to {percent} % of the encoder" for name, percent in video.items()))
            # A stream being encoded by a program (Sunshine) is not "a little" for the tracer's two reordering passes
            # (2026-10-12: the forest's bounced light 17.4 to 19.9 ms and its reflections 24.0 to 27.0 with Sunshine at
            # 20 % of the encoder and under the guard's 4 % of the 3D engines; round 19 saw the same). The owner plays
            # over the stream and keeps it on (2026-10-12), so this is the speed that counts: it is held against other
            # runs WITH a stream alone, never against a quiet card's.
            streams = {name: percent for name, percent in video.items() if not name.startswith("System")}
            if streams:
                lines.append("- THE OWNER'S STREAM WAS ON during the run (" + ", ".join(f"{name} up to {percent} % of the encoder" for name, percent in streams.items())
                             + "): its frame rates and stage times are the speed WITH a stream (the bounced light and the reflections "
                             "read 2 to 13 ms slower than on a quiet card): hold them against runs with a stream alone.")
    lines.append("")

    for scenario in request.get("scenarios", []):
        key = scenario["key"]
        folder = run / "".join(c if (c.isalnum() or c in "-_.") else "-" for c in key)
        info = load_json(folder / "scenario.json", {})
        about = planned.get(key, {})
        roi = about.get("roi", [0.375, 0.3, 0.625, 0.7])
        entry = {"key": key, "save": scenario.get("save", ""), "error": info.get("error", ""), "steps": [], "restore": [], "perf": []}
        summary["scenarios"].append(entry)
        lines.append(f"## {key}")
        if about.get("tests"):
            lines.append(f"What it is for: {about['tests']}")
        lines.append(f"Save \"{scenario.get('save', '')}\" = {info.get('slot', '?')} \"{info.get('slotName', '')}\", {info.get('location', '?')}; scenes {info.get('scenes', '?')}; "
                     f"camera ({info.get('px', 0):.1f}, {info.get('py', 0):.1f}, {info.get('pz', 0):.1f}) facing {info.get('ry', 0):.0f} deg, pitch {info.get('rx', 0):.0f}; "
                     f"game time {info.get('weatherTime', '?')}; loaded in {info.get('loadSeconds', 0):.0f} s")
        if info.get("standIn"):
            lines.append(f"(A stand-in: the save \"{scenario.get('save', '')}\" does not exist yet. Where to make it: {about.get('where', '?')})")
            to_make.append(f"{scenario.get('save', '')}: {about.get('where', '')} (now run on {info.get('slot', '?')})")
        if info.get("error"):
            if "was not found" in info["error"]:
                # Not a failure of the build under test: the owner has not made this scenario's save yet.
                to_make.append(f"{scenario.get('save', '')}: {about.get('where', '')} (not run: no stand-in)")
                lines += ["", f"Not run: the save \"{scenario.get('save', '')}\" does not exist yet. Where to make it: {about.get('where', '?')}", ""]
            else:
                problems.append(f"{key}: {info['error']}")
                lines += ["", f"**Failed: {info['error']}**", ""]
            continue
        steps = []
        for step_request in scenario.get("steps", []):
            name = step_request["name"]
            step_dir = folder / "".join(c if (c.isalnum() or c in "-_.") else "-" for c in name)
            step = load_json(step_dir / "step.json")
            if step is None:
                problems.append(f"{key}/{name}: no step.json (the run stopped before or in this step)")
                continue
            data = None
            if step.get("frames"):
                try:
                    data = StepData(step_dir, step, roi)
                    if data.ok:
                        data.atlas(step_dir / "atlas.jpg")
                        # The whole frame (the mean of the step's frames) at a size to look around in.
                        data.frame_picture(1920).save(step_dir / "frame.jpg", quality=92)
                        # And at full size where pictures get compared pixel for pixel: the game's first take and
                        # each traced one in the frozen world (hair, grass, a cup's rim: details a smaller copy loses), and
                        # the mirror recipe's untraced takes (tools/harness/mirror.py compares them pixel for pixel).
                        if step.get("frozen") and (step.get("traced") or step_request.get("neural") is not None or name.startswith("1-") or "mirror" in name or "lantern" in name):
                            Image.fromarray(np.ascontiguousarray(data.mean.astype(np.uint8))).save(step_dir / "full.jpg", quality=95)
                        # A step named for it ("anim", the world running): its frames in a row as a moving picture, since no
                        # still shows light rippling (PT-14's lantern over water). anim.gif opens anywhere; anim.webp keeps
                        # the dark tones a GIF's 256 colours band.
                        if ("anim" in name or step_request.get("neural") is not None and step_request.get("live")) and len(data.frames) > 1:
                            shots = [fit(read_frame(step_dir / f["file"], step["width"], step["height"], step.get("format", "RGBA32")), 1280) for f in data.frames]
                            spans = [b["realtime"] - a["realtime"] for a, b in zip(data.frames, data.frames[1:])]
                            each = int(min(max(1000.0 * sum(spans) / len(spans), 40.0), 500.0))
                            shots[0].save(step_dir / "anim.gif", save_all=True, append_images=shots[1:], duration=each, loop=0)
                            try:
                                shots[0].save(step_dir / "anim.webp", save_all=True, append_images=shots[1:], duration=each, loop=0, quality=90)
                            except (OSError, KeyError, ValueError):
                                pass
                except (ValueError, OSError, MemoryError) as ex:
                    problems.append(f"{key}/{name}: the frames could not be read: {ex}")
                    data = None
            steps.append((step_request, step, step_dir, data))
            denoiser = "Ray Reconstruction" if step.get("rayReconstruction") else "Ray Regeneration" if step.get("rayRegeneration") else "NRD" if step.get("nrd") else "-"
            wanted = next((p["value"] for p in step_request.get("set", []) if p["key"].lower() == "denoising"), "")
            record = {"name": name, "traced": step.get("traced"), "expected_traced": step.get("expectedTraced"), "denoiser": denoiser,
                      "status": step.get("status", ""), "exposure": step.get("exposure", ""), "gain": step.get("tracedGain", 0),
                      "render": f"{step.get('renderWidth', 0)}x{step.get('renderHeight', 0)}", "metrics": data.metrics if data and data.ok else {}}
            entry["steps"].append(record)
            if step.get("error"):
                problems.append(f"{key}/{name}: {step['error']}")
            for applied in step.get("applied", []):
                if applied.startswith("NOT APPLIED"):
                    problems.append(f"{key}/{name}: {applied}")
            if step.get("expectedTraced") != step.get("traced"):
                problems.append(f"{key}/{name}: path tracing is {'on' if step.get('traced') else 'not on'} screen, the step expects it {'on' if step.get('expectedTraced') else 'off'} ({step.get('status', '')})")
            if step.get("traced") and wanted and wanted != denoiser and wanted != "None":
                problems.append(f"{key}/{name}: asked for {wanted}, but {denoiser} denoised")
            if step.get("menuOpen"):
                problems.append(f"{key}/{name}: the game left gameplay during capture (menu, dialogue or loading transition): these frames cannot be used for comparison")
            # The exact straight route is Neural Rendering's comparisons' need (a step with `neural`, or a walk counted in
            # frames). A path tracing walk at a speed keeps the camera over the ground since 2026-10-12 (CameraSlide) and
            # its pictures are taken as it starts: held against the straight route, every one of them was a "problem".
            if step_request.get("walkForward") and step_request.get("walkSpeed") and not step_request.get("neural"):
                pass
            elif step_request.get("walkForward"):
                # A requested walk is motion by design, but it must follow the measured straight route.
                # Validate even cameraMoved=False, which can mean the requested walk never happened.
                from neural_chairs import check_walk
                try:
                    record["walk"] = check_walk(step_request, step.get("frames", []))
                except (ValueError, KeyError, TypeError) as ex:
                    problems.append(f"{key}/{name}: invalid requested camera walk: {ex}")
            elif step.get("cameraMoved") and step.get("frozen") and not step_request.get("slide"):
                problems.append(f"{key}/{name}: the camera moved during the frames (a hand on the mouse?)")
            if data and data.ok and not data.metrics.get("consecutive", True) and step_request.get("stride", 1) == 1:
                problems.append(f"{key}/{name}: the frames are not consecutive")
            perf = step.get("perf", {})
            if perf.get("measured"):
                entry["perf"].append({"name": name, "traced": step.get("traced"), "denoiser": denoiser, **{k: perf.get(k) for k in
                                      ("fps", "lowFps", "frameMs", "medianMs", "p99Ms", "worstMs", "tracerGpuMs", "tracerRenderThreadMs", "unityGpuMs", "unityCpuMs", "stages", "vSyncCount", "targetFrameRate", "frames",
                                       "over50", "over100", "perSecond")}})
        with_frames = [s[3] for s in steps if s[3] is not None and s[3].ok]
        overview(with_frames, folder / "overview.jpg", f"{key}: {about.get('tests', '')}")
        side_by_side(with_frames, folder / "side-by-side.jpg")
        pairs.write_pair(folder, about.get("roi"))
        if with_frames:
            lines += ["", "| step | world | traced | denoiser | luma (region) | darkest 5 %, brightest 5 %, near white | flicker (region) | boil (region) | boil p95 | detail | vs game: luma, boil |",
                      "|---|---|---|---|---|---|---|---|---|---|---|"]
        for d in with_frames:
            m = d.metrics
            ratio = ""
            # The game's own picture in the same kind of world: frozen against frozen, running against running (a
            # running world moves by itself: the camera's idle sway, flames).
            # And at the same size: a step taken at half size (a sliding camera's frames, `scale` 2) is held to the game's
            # take at half size, not to a full one (run 20261007-122527 stopped here).
            game = next((g for g in with_frames if not g.step.get("traced") and bool(g.step.get("frozen")) == bool(d.step.get("frozen"))
                         and g.mean_luma.shape == d.mean_luma.shape), None)
            m["world"] = "frozen" if d.step.get("frozen") else "running"
            if game is not None and d is not game:
                ratio = f"x{m['luma'] / max(game.metrics['luma'], 1e-3):.2f}, x{m['boil'] / max(game.metrics['boil'], 1e-3):.1f}"
                m["luma_vs_game"] = m["luma"] / max(game.metrics["luma"], 1e-3)
                m["boil_vs_game"] = m["boil"] / max(game.metrics["boil"], 1e-3)
                m["boil_roi_vs_game"] = m["boil_roi"] / max(game.metrics["boil_roi"], 1e-3)
                m["detail_vs_game"] = m["detail_roi"] / max(game.metrics["detail_roi"], 1e-3)
                m["difference_from_game"] = float(np.abs(d.mean_luma - game.mean_luma).mean())
            denoiser = "RR" if d.step.get("rayReconstruction") else "RRG" if d.step.get("rayRegeneration") else "NRD" if d.step.get("nrd") else "-"
            lines.append(f"| {d.name} | {m['world']} | {'yes' if d.step.get('traced') else 'no'} | {denoiser if d.step.get('traced') else '-'} | {m['luma']:.1f} ({m['luma_roi']:.1f}) | {m['dark']:.0f}, {m['bright']:.0f}, {m['near_white']:.1f} % | {m['flicker']:.2f} ({m['flicker_roi']:.2f}) | "
                         f"{m['boil']:.2f} ({m['boil_roi']:.2f}) | {m['boil_p95']:.2f} | {m['detail_roi']:.2f} | {ratio} |")
            if d.step.get("traced") and game is not None and stability:
                # Both at once: far more than the game's own picture, and enough levels for an eye to see.
                limit, levels = stability.get("boil_vs_game_max"), stability.get("boil_levels_max", 0.0)
                if limit and m.get("boil_vs_game", 0) > limit and m["boil"] > levels:
                    problems.append(f"{key}/{d.name}: boils {m['boil']:.2f} levels, {m['boil_vs_game']:.1f} times the game's own picture "
                                    f"(flagged above {levels} levels and {limit} times)")
        # Turning path tracing off must give the game's own picture and state back.
        baseline = next(((req, step, sdir, d) for req, step, sdir, d in steps if not step.get("traced") and step.get("audits")), None)
        if baseline is not None:
            if baseline[1].get("framesTraced", 0) != 0 and scenario is request["scenarios"][0]:
                problems.append(f"{key}/{baseline[0]['name']}: the baseline is not untouched (the tracer had traced {baseline[1].get('framesTraced')} frames before it)")
            # The game's picture taken a second time before anything was traced, if the recipe has such a step.
            # Where tracing first was on; in the control recipe (path tracing never on: what the game's picture does by
            # itself over the same time) the step that stands in for it.
            first_traced = next((i for i, s in enumerate(steps) if s[1].get("traced") or s[0].get("control")), len(steps))
            again = next((d for req, step, sdir, d in steps[:first_traced] if step is not baseline[1] and d is not None and d.ok), None)
            # The takes of the game's picture after tracing, in pairs: what differs between two of them moves by itself.
            afters = [d for index, (req, step, sdir, d) in enumerate(steps)
                      if index >= first_traced and not step.get("traced") and step.get("audits") and d is not None and d.ok]
            self_pairs = list(zip(afters, afters[1:]))
            for index, (req, step, sdir, d) in enumerate(steps):
                if step is baseline[1] or step.get("traced") or not step.get("audits") or index < first_traced:
                    continue
                check = {"step": req["name"]}
                if d is not None and d.ok and baseline[3] is not None and baseline[3].ok:
                    check.update(restore_check(baseline[3], d, limits, folder / f"restore-{sdir.name}.jpg", again, self_pairs))
                    if not check["same_picture"]:
                        problems.append(f"{key}/{req['name']}: the game's picture is not back: mean difference {check['difference']:.3f} levels (allowed {check['allowed_mean']:.3f}), "
                                        f"worst 32-pixel block {check['block_max']:.2f} (allowed {check['allowed_block']:.2f}) at {check['worst_at'][0]:.2f}, {check['worst_at'][1]:.2f} of the screen")
                state = audit_check(baseline[2], sdir)
                if state is not None:
                    check["state"] = {k: v for k, v in state.items() if k != "changed"}
                    check["state"]["changed"] = [{"key": c["key"], "what": changed_parts(c)} for c in state["changed"][:200]]
                    check["state"]["changed_count"] = len(state["changed"])
                    if not state["same_state"]:
                        shown = "; ".join(f"{c['key']}: {changed_parts(c)}" for c in state["changed"][:6])
                        problems.append(f"{key}/{req['name']}: HDRP's state is not back: {len(state['changed'])} values differ from before tracing ({shown})")
                    if state["added_other"] or state["removed_other"]:
                        check["state"]["note"] = f"{len(state['added_other'])} keys appeared and {len(state['removed_other'])} vanished that are not materials"
                entry["restore"].append(check)
            if entry["restore"]:
                lines += ["", "| the game, after tracing was turned off | picture | mean difference | worst block (allowed) | state: values compared | changed | changing by themselves |", "|---|---|---|---|---|---|---|"]
                for check in entry["restore"]:
                    state = check.get("state", {})
                    lines.append(f"| {check['step']} | {'same' if check.get('same_picture') else 'DIFFERENT' if 'same_picture' in check else '-'} | {check.get('difference', 0):.3f} | "
                                 f"{check.get('block_max', 0):.2f} ({check.get('allowed_block', 0):.2f}) | {state.get('compared', '-')} | {state.get('changed_count', '-')} | {state.get('volatile', '-')} |")
        entry["neural"], neural_problems = neural_analysis.analyze(steps, folder, limits, restore_check)
        problems.extend(f"{key}/{p}" for p in neural_problems)
        if entry["neural"]:
            lines += ["", "| NR step | actual evaluations | last NR GPU ms | transfer textures MiB |", "|---|---|---|---|"]
            for nr in entry["neural"]:
                lines.append(f"| {nr['name']} | {nr['evaluations']} | {nr['gpu_ms']} | {(nr['transfer_bytes'] or 0) / 1048576:.2f} |")
            lines.append("GPU time covers NR evaluation; texture bytes exclude model allocations. Compare real-frame FPS and adapter memory separately.")
            motion = [n for n in entry["neural"] if "motion_colour" in n]
            if motion:
                lines += ["", "| matched camera motion | mean brightness shift vs Off | brightness shift range | RGB shift ranges |", "|---|---|---|---|"]
                for nr in motion:
                    m = nr["motion_colour"]
                    lines.append(f"| {nr['name']} | {m['mean_luma_shift']:.3f} | {m['luma_shift_range']:.3f} | {', '.join(f'{v:.3f}' for v in m['rgb_shift_range'])} |")
                lines.append("Display levels (0–255), central 80% of the image. Each pose is compared with Off at the same pose. This separates a fixed colour grade from a grade that changes during motion; it is not a judgement of artistic quality.")
                local = [n for n in motion if "local_colour" in n]
                if local:
                    lines += ["", "| local NR changes vs matching Off | largest block change between frames | largest block range while held still | changed block area range (percentage points) |", "|---|---|---|---|"]
                    for nr in local:
                        m = nr["local_colour"]
                        lines.append(f"| {nr['name']} | {m['max_step']:.3f} | {m['hold_max_range']:.3f} | {m['changed_area_range']:.2f} |")
                    lines.append("32-pixel blocks, largest RGB-channel change in 0–255 display levels. The held-still interval starts after the first sharp frame following motion blur. Changed area uses a fixed 2-level threshold: visible differences from Off, not NVIDIA's internal mask. Motion can move surfaces across blocks; use the stationary maps and Off-repeat noise control when judging instability. These are diagnostic measurements, not pass/fail quality thresholds. See nr-local-*.png and nr-local-*.json.")
        if entry["perf"]:
            lines += ["", "| frame rate | fps | 1 % low | frame ms | tracer GPU ms | stages |", "|---|---|---|---|---|---|"]
            for p in entry["perf"]:
                # The tracer's timers hold their last values while it is off: only a traced step's are shown.
                tracer = f"{p['tracerGpuMs']:.2f} | {p.get('stages', '')}" if p["traced"] else "- | -"
                lines.append(f"| {p['name']} ({p['denoiser'] if p['traced'] else 'the game'}) | {p['fps']:.1f} | {p['lowFps']:.1f} | {p['frameMs']:.2f} | {tracer} |")
                if p["traced"] and performance:
                    if performance.get("fps_min") and p["fps"] < performance["fps_min"]:
                        problems.append(f"{key}/{p['name']}: {p['fps']:.1f} fps, target {performance['fps_min']}")
                    if performance.get("tracer_gpu_ms_max") and p["tracerGpuMs"] > performance["tracer_gpu_ms_max"]:
                        problems.append(f"{key}/{p['name']}: the tracer takes {p['tracerGpuMs']:.1f} ms of GPU time, target {performance['tracer_gpu_ms_max']}")
            lines += per_second_tables(entry["perf"])
        lines += ["", f"Pictures: `{folder.name}/game-vs-traced.jpg` (the whole frame, the game's own beside the traced: look here first), `overview.jpg`, `side-by-side.jpg` (the region, the game's own beside the traced), each step's `frame.jpg` and `atlas.jpg`"
                  + (", `restore-*.jpg`" if entry["restore"] else ""), ""]
        if not args.keep_frames:
            for _, step, sdir, _ in steps:
                for frame in step.get("frames", []):
                    try:
                        (sdir / frame["file"]).unlink()
                    except OSError:
                        pass
    summary["problems"] = problems
    summary["saves_to_make"] = to_make
    head = "## " + ("No problems found" if not problems else f"{len(problems)} problems") + "\n" + "\n".join(f"- {p}" for p in problems) + "\n"
    if to_make:
        head += f"\n## {len(to_make)} saves still to make (in the game's save menu, with exactly this name)\n" + "\n".join(f"- {s}" for s in to_make) + "\n"
    lines.insert(2, head)
    (run / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (run / "summary.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")
    print(f"{len(summary['scenarios'])} scenarios, {len(problems)} problems. Report: {run / 'report.md'}")
    for problem in problems[:30]:
        print("  - " + problem)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
