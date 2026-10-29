"""Exact finite-light oracle for the fog's temporal proposal correction (no GPU)."""
import math
import random


def expectation(q, energy, old, represented):
    # Enumerate the marginal distribution of each of eight fresh proposals. A null proposal has zero energy.
    total = 0.0
    for light, (pdf, e) in enumerate(zip(q, energy)):
        denominator = 8 * pdf + (represented if light == old else 0)
        if pdf:
            total += 8 * pdf * e / denominator
    if represented:
        total += represented * energy[old] / (8 * q[old] + represented)
    return total


def main():
    rng = random.Random(118)
    worst = 0.0
    for case in range(1000):
        n = rng.randrange(2, 80)
        q = [10 ** rng.uniform(-6, 3) for _ in range(n)]
        scale = rng.uniform(.1, 1.0) / sum(q)  # far-list duplicates give the proposal a null probability
        q = [v * scale for v in q]
        energy = [10 ** rng.uniform(-6, 6) * (rng.random() > .3) for _ in range(n)]
        # The previous choice is arbitrary: stale, clamped to the view edge, or a previously dark light.
        old = rng.randrange(n)
        for represented in (0, 1, 2, 8, 100, 200):
            actual = expectation(q, energy, old, represented)
            target = sum(energy)  # independent oracle: the exact sum of all current visible lights
            error = abs(actual - target) / max(target, 1e-30)
            worst = max(worst, error)
            assert math.isfinite(actual) and error < 1e-12, (case, represented, error)
    print(f'6000 changing-light/support cases; worst relative mean error {worst:.3g}')


if __name__ == '__main__':
    main()
