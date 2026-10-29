# SPDX-License-Identifier: MIT
"""Two harness runs side by side: what a change to the mod did, scene by scene.

For every scenario both runs have: the numbers that moved (frame rate, the tracer's GPU time, brightness, boiling,
fine detail, whether turning path tracing off gave the game back), and a picture of the scenario's region at full
size, the game's own beside the traced one of the older run and of the newer run.

  python tools/harness/compare.py artifacts/harness/<older> artifacts/harness/<newer> [--step 2-traced-rr]

Writes artifacts/harness/<newer>/compare-<older>/: compare.md and one <scenario>.jpg each.
"""
import argparse
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw


def load(path, default=None):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return default


def safe(name):
    return "".join(c if (c.isalnum() or c in "-_.") else "-" for c in name)


def label(picture, text):
    draw = ImageDraw.Draw(picture)
    box = draw.textbbox((6, 4), text)
    draw.rectangle((box[0] - 4, box[1] - 2, box[2] + 4, box[3] + 2), fill=(0, 0, 0))
    draw.text((6, 4), text, fill=(255, 255, 255))
    return picture


def region(path, roi, limit):
    """The scenario's region out of a step's full-size picture, at most limit pixels a side, around its middle."""
    if not path.exists():
        return None
    image = Image.open(path).convert("RGB")
    w, h = image.size
    x0, y0, x1, y1 = int(roi[0] * w), int(roi[1] * h), int(roi[2] * w), int(roi[3] * h)
    cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
    cw, ch = min(x1 - x0, limit), min(y1 - y0, limit)
    return image.crop((cx - cw // 2, cy - ch // 2, cx - cw // 2 + cw, cy - ch // 2 + ch))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("older")
    parser.add_argument("newer")
    parser.add_argument("--plan", default="")
    parser.add_argument("--step", default="2-traced-none")  # the undenoised take first (the owner, 2026-10-07)
    parser.add_argument("--size", type=int, default=900)
    args = parser.parse_args()
    older, newer = Path(args.older), Path(args.newer)
    plan_path = Path(args.plan) if args.plan else Path(__file__).resolve().parents[2] / "tests" / "ingame" / "scenarios.json"
    rois = {s["key"]: s.get("roi", [0.375, 0.3, 0.625, 0.7]) for s in (load(plan_path, {}) or {}).get("scenarios", [])}
    a, b = load(older / "summary.json"), load(newer / "summary.json")
    if not a or not b:
        print("Both runs need a summary.json (run analyze.py on them).")
        return 2
    out = newer / f"compare-{older.name}"
    out.mkdir(exist_ok=True)
    builds = [load(run / "build.json", {}) or {} for run in (older, newer)]
    lines = [f"# {newer.name} against {older.name}", ""]
    for run, build in zip((older, newer), builds):
        lines.append(f"- {run.name}: " + (", ".join(f"{name} {info.get('sha256', '')[:12]}" for name, info in build.items()) or "build not recorded"))
    lines.append("")
    first = {s["key"]: s for s in a["scenarios"]}
    for scenario in b["scenarios"]:
        key = scenario["key"]
        before = first.get(key)
        if not before or scenario.get("error") or before.get("error"):
            continue
        lines += [f"## {key}", "", "| step | luma | darkest 5 % | boil | detail (region) | vs game: luma |", "|---|---|---|---|---|---|"]
        old_steps = {s["name"]: s for s in before["steps"]}
        for step in scenario["steps"]:
            m, o = step.get("metrics") or {}, (old_steps.get(step["name"]) or {}).get("metrics") or {}
            if not m or not o:
                continue

            def pair(name, digits=1):
                if name not in m or name not in o:
                    return "-"
                change = m[name] - o[name]
                mark = "" if abs(change) < 10 ** -digits else f" ({change:+.{digits}f})"
                return f"{o[name]:.{digits}f} -> {m[name]:.{digits}f}{mark}"
            lines.append(f"| {step['name']} | {pair('luma')} | {pair('dark', 0)} | {pair('boil', 2)} | {pair('detail_roi', 2)} | {pair('luma_vs_game', 2)} |")
        old_perf = {p["name"]: p for p in before.get("perf", [])}
        perf = [p for p in scenario.get("perf", []) if p["name"] in old_perf]
        if perf:
            lines += ["", "| frame rate | fps | 1 % low | tracer GPU ms |", "|---|---|---|---|"]
            for p in perf:
                o = old_perf[p["name"]]
                lines.append(f"| {p['name']} | {o['fps']:.1f} -> {p['fps']:.1f} ({p['fps'] - o['fps']:+.1f}) | {o['lowFps']:.1f} -> {p['lowFps']:.1f} | "
                             + (f"{o['tracerGpuMs']:.2f} -> {p['tracerGpuMs']:.2f}" if p.get("traced") else "-") + " |")
        for name, run_scenario in (("older", before), ("newer", scenario)):
            for check in run_scenario.get("restore", []):
                state = check.get("state", {})
                lines.append(f"- After Off, {name} run, {check['step']}: picture {'same' if check.get('same_picture') else 'DIFFERENT'}, "
                             f"{state.get('changed_count', '?')} of {state.get('compared', '?')} values not back")
        roi = rois.get(key, [0.375, 0.3, 0.625, 0.7])
        tiles = []
        for text, path in ((f"the game's own ({newer.name})", newer / safe(key) / "1-game" / "full.jpg"),
                           (f"{args.step}, {older.name}", older / safe(key) / safe(args.step) / "full.jpg"),
                           (f"{args.step}, {newer.name}", newer / safe(key) / safe(args.step) / "full.jpg")):
            tile = region(path, roi, args.size)
            if tile is not None:
                tiles.append(label(tile, text))
        if len(tiles) >= 2:
            sheet = Image.new("RGB", (sum(t.width for t in tiles), max(t.height for t in tiles)), (20, 20, 20))
            x = 0
            for tile in tiles:
                sheet.paste(tile, (x, 0))
                x += tile.width
            sheet.save(out / f"{safe(key)}.jpg", quality=93)
            lines.append(f"- Picture: `{out.name}/{safe(key)}.jpg`")
        lines.append("")
    (out / "compare.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Written: {out / 'compare.md'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
