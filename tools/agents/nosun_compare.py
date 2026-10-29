# SPDX-License-Identifier: MIT
"""PT-133: two runs of the same mode of tgpt_gpu_test laid side by side, frame by frame (docs/agents/nosun.md).

    python tools/agents/nosun_compare.py <trace of the 6.6 passes> <trace of the 6.9 passes> [--top 12] [--label text]

Each file is what the test's TGPT_TEST_TRACE=<file> writes: one line a frame (its number, the checks done before it, the
view, the frame's index, options, flags, the sun's w, the camera's place | the composite's mean of each channel, its
largest and smallest value, how many values are not finite, how many pixels are above 1,000, above 30,000 and below
-0.001 | the mean luminance of each block of a 16 x 9 grid).

The two runs trace the same frames with the same random numbers, one with the tracer's Shader Model 6.6 passes
(TGPT_TEST_SM66=1), one with its 6.9 passes. A pass the driver compiles wrongly in one of the sets shows as
  - values that are not finite, or above a bound no light of the scene reaches, in one run and not the other;
  - a window of 16 frames whose mean, or one of whose blocks' means, parts by more than the frames' own noise.
Exit code 0: nothing parts. 1: something does (the lines say what). 2: the two runs did not trace the same frames.
"""
import argparse
import difflib
import math
import sys

BLOCKS = 144
WINDOW = 16


def read(path):
    frames = []
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            parts = line.split("|")
            if len(parts) != 3:
                continue
            head, numbers, blocks = parts[0].split(), parts[1].split(), parts[2].split()
            if len(head) != 10 or len(numbers) != 9 or len(blocks) != BLOCKS:
                continue
            frames.append({
                "n": int(head[0]), "checks": int(head[1]), "view": int(head[2]),
                # (the frame's own number in the run is left out of the key: it is the place in the list)
                "key": (head[2], head[3], head[4], head[5], head[6], head[7], head[8], head[9]),
                "sun": float(head[6]),
                "rgb": [float(v) for v in numbers[0:3]], "largest": float(numbers[3]), "smallest": float(numbers[4]),
                "notFinite": int(numbers[5]), "above1k": int(numbers[6]), "above30k": int(numbers[7]), "below": int(numbers[8]),
                "blocks": [float(v) for v in blocks],
            })
    return frames


def luminance(frame):
    r, g, b = frame["rgb"]
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def align(a, b):
    if len(a) == len(b) and all(x["key"] == y["key"] for x, y in zip(a, b)):
        return list(zip(a, b)), 0
    matcher = difflib.SequenceMatcher(None, [x["key"] for x in a], [y["key"] for y in b], autojunk=False)
    pairs = []
    for block in matcher.get_matching_blocks():
        for i in range(block.size):
            pairs.append((a[block.a + i], b[block.b + i]))
    return pairs, (len(a) - len(pairs)) + (len(b) - len(pairs))


def paired(xs, ys):
    """Mean of each, the relative difference of the means, and how many standard errors the paired difference is."""
    n = len(xs)
    ma, mb = sum(xs) / n, sum(ys) / n
    diffs = [x - y for x, y in zip(xs, ys)]
    mean = sum(diffs) / n
    var = sum((d - mean) ** 2 for d in diffs) / (n - 1) if n > 1 else 0.0
    se = math.sqrt(var / n)
    scale = max(abs(ma), abs(mb))
    rel = abs(ma - mb) / scale if scale > 0 else 0.0
    t = abs(mean) / se if se > 0 else (float("inf") if mean != 0 else 0.0)
    return ma, mb, rel, t


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("sm66")
    parser.add_argument("sm69")
    parser.add_argument("--top", type=int, default=12)
    parser.add_argument("--label", default="")
    # A window parts when its means differ by more than --rel AND by more than --sigma standard errors of the frames'
    # paired differences; a block also needs a mean above --floor of the frame's (dark blocks are all noise).
    parser.add_argument("--rel", type=float, default=0.02)
    parser.add_argument("--block-rel", type=float, default=0.10)
    parser.add_argument("--sigma", type=float, default=5.0)
    parser.add_argument("--floor", type=float, default=0.05)
    args = parser.parse_args()

    a, b = read(args.sm66), read(args.sm69)
    label = (args.label + ": ") if args.label else ""
    if not a or not b:
        print(f"{label}NO FRAMES: {len(a)} in {args.sm66}, {len(b)} in {args.sm69}")
        return 2
    pairs, unmatched = align(a, b)
    noSun = sum(1 for x, _ in pairs if x["sun"] == 0.0)
    print(f"{label}{len(a)} frames (6.6) against {len(b)} (6.9): {len(pairs)} the same frame, {unmatched} without a partner; "
          f"{noSun} of them without a sun, {len(pairs) - noSun} with one")
    if len(pairs) < 0.5 * max(len(a), len(b)):
        print(f"{label}THE RUNS DID NOT TRACE THE SAME FRAMES: nothing to compare")
        return 2

    found = 0
    # 1. Values no light of the scene reaches, and values that are not finite, frame by frame.
    for name, bound in (("not finite", "notFinite"), ("above 30,000", "above30k"), ("above 1,000", "above1k"), ("below -0.001", "below")):
        ta, tb = sum(x[bound] for x, _ in pairs), sum(y[bound] for _, y in pairs)
        fa, fb = sum(1 for x, _ in pairs if x[bound]), sum(1 for _, y in pairs if y[bound])
        line = f"  {name:13s}: 6.6 {ta} values in {fa} frames, 6.9 {tb} in {fb} frames"
        only = [(x, y) for x, y in pairs if (x[bound] == 0) != (y[bound] == 0)]
        if only:
            x, y = only[0]
            line += f"   <-- in ONE set only in {len(only)} frames (first: frame {x['n']}/{y['n']}, view {x['view']}, after check {x['checks']}, sun {x['sun']:g}: {x[bound]} / {y[bound]})"
            found += 1
        print(line)
    largest = max(pairs, key=lambda p: max(p[0]["largest"], p[1]["largest"]))
    print(f"  the largest value: 6.6 {max(x['largest'] for x, _ in pairs):.6g}, 6.9 {max(y['largest'] for _, y in pairs):.6g} "
          f"(frame {largest[0]['n']}, view {largest[0]['view']}, after check {largest[0]['checks']})")

    # 2. Windows of 16 frames (a step of 8): the whole picture's mean, and each block's.
    whole, blocks = [], []
    exact = 0
    for start in range(0, max(len(pairs) - WINDOW + 1, 1), WINDOW // 2):
        chunk = pairs[start:start + WINDOW]
        if len(chunk) < 2:
            continue
        xs, ys = [luminance(x) for x, _ in chunk], [luminance(y) for _, y in chunk]
        ma, mb, rel, t = paired(xs, ys)
        if xs == ys:
            exact += 1
        first = chunk[0][0]
        where = f"frames {chunk[0][0]['n']}-{chunk[-1][0]['n']} (view {first['view']}, after check {first['checks']}, sun {first['sun']:g})"
        whole.append((rel, t, ma, mb, where))
        frameMean = max(ma, mb)
        worst = None
        for k in range(BLOCKS):
            bx, by = [x["blocks"][k] for x, _ in chunk], [y["blocks"][k] for _, y in chunk]
            ba, bb, brel, bt = paired(bx, by)
            if max(ba, bb) < args.floor * frameMean or max(ba, bb) <= 0:
                continue
            if brel > args.block_rel and bt > args.sigma and (worst is None or brel > worst[0]):
                worst = (brel, bt, ba, bb, f"{where}, block {k % 16},{k // 16}")
        if worst:
            blocks.append(worst)
    parted = sorted((w for w in whole if w[0] > args.rel and w[1] > args.sigma), reverse=True)
    rels = sorted(w[0] for w in whole)
    median = rels[len(rels) // 2] if rels else 0.0
    print(f"  windows of {WINDOW} frames: {len(whole)}; the two sets' means part by {median * 100:.3f} % in the middle one, "
          f"{(rels[-1] if rels else 0) * 100:.3f} % at most; {exact} windows the same to the last digit")
    if parted:
        found += 1
        print(f"  THE WHOLE PICTURE'S MEAN PARTS in {len(parted)} windows (more than {args.rel * 100:g} % and {args.sigma:g} standard errors):")
        for rel, t, ma, mb, where in parted[:args.top]:
            print(f"    {rel * 100:8.2f} %  6.6 {ma:.6g}  6.9 {mb:.6g}  ({t:.1f} se)  {where}")
    if blocks:
        found += 1
        blocks.sort(reverse=True)
        print(f"  A BLOCK'S MEAN PARTS in {len(blocks)} windows (more than {args.block_rel * 100:g} % and {args.sigma:g} standard errors; the worst block of each):")
        for rel, t, ma, mb, where in blocks[:args.top]:
            print(f"    {rel * 100:8.2f} %  6.6 {ma:.6g}  6.9 {mb:.6g}  ({t:.1f} se)  {where}")
    print(f"{label}{'SOMETHING PARTS: read the lines above' if found else 'nothing parts'}")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
