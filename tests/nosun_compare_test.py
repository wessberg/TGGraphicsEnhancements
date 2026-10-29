# SPDX-License-Identifier: MIT
"""tools/agents/nosun_compare.py judged on made-up traces (PT-133, docs/agents/nosun.md): python tests/nosun_compare_test.py

Two runs of the same frames that differ by noise alone must pass; a run with values that are not finite, with a value
no light reaches, with one block gone bright in frames without a sun, or with other frames altogether must not.
"""
import os
import random
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
COMPARE = os.path.join(HERE, "..", "tools", "agents", "nosun_compare.py")


def trace(path, seed, frames=96, fault=None):
    """A run's lines as the test's TGPT_TEST_TRACE writes them: the first half with a sun, the second without."""
    rng = random.Random(seed)
    with open(path, "w", encoding="utf-8") as out:
        for n in range(frames):
            sun = 1 if n < frames // 2 else 0
            blocks = [0.2 + 0.1 * ((k * 7) % 13) / 13.0 + rng.gauss(0.0, 0.002) for k in range(144)]
            not_finite = above1k = above30k = below = 0
            largest = 3.0 + rng.random()
            if fault == "block" and not sun:
                blocks[77] *= 1.6
            if fault == "notfinite" and n == 70:
                not_finite = 12
            if fault == "huge" and n >= 60:
                above30k = 1800
                largest = 65504.0
            if fault == "frames" and n >= 10:
                continue
            mean = sum(blocks) / 144.0
            head = f"{n} {n // 16} 0 {n} 4 0 {sun} 0.000 2.500 -5.500"
            numbers = f"{mean:.9g} {mean:.9g} {mean:.9g} {largest:.9g} 0 {not_finite} {above1k} {above30k} {below}"
            out.write(f"{head} | {numbers} | " + " ".join(f"{b:.7g}" for b in blocks) + "\n")


def run(a, b):
    result = subprocess.run([sys.executable, COMPARE, a, b], capture_output=True, text=True)
    return result.returncode, result.stdout


def main():
    failures = 0
    with tempfile.TemporaryDirectory() as folder:
        good = os.path.join(folder, "sm66.txt")
        trace(good, 1)
        cases = [("the same frames, noise alone", dict(seed=2), 0, "nothing parts"),
                 ("a block gone bright without a sun", dict(seed=2, fault="block"), 1, "A BLOCK'S MEAN PARTS"),
                 ("values that are not finite", dict(seed=2, fault="notfinite"), 1, "in ONE set only"),
                 ("values no light reaches", dict(seed=2, fault="huge"), 1, "in ONE set only"),
                 ("other frames", dict(seed=2, fault="frames"), 2, "DID NOT TRACE THE SAME FRAMES")]
        for name, how, code, says in cases:
            other = os.path.join(folder, "sm69.txt")
            trace(other, **how)
            got, text = run(good, other)
            ok = got == code and says in text
            print(("ok  : " if ok else "FAIL: ") + name + f" (exit {got}, expected {code})")
            if not ok:
                print(text)
                failures += 1
    print("FAILED" if failures else "nosun_compare checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
