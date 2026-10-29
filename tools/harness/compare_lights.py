# SPDX-License-Identifier: MIT
"""The game's own lights in two harness audits, compared: which light changed its strength (docs/PT-HARNESS.md "A scene
depends on what was loaded before it", PT-147: the town's lamps read 0.125 or 524,288 by what was loaded before).

  python tools/harness/compare_lights.py <audit-a.txt of run A> <audit-a.txt of run B> [--at X,Y,Z] [--within M]

An audit is written by a step with `"audit": true` (the default recipe's `1-game`): every Light with its position,
strength, range and HDRP's values, the game's own state before tracing changes anything. A light is the same light
in both when its name (the last four parents) and its place agree: its id changes with every start of the game.
Prints how many lights both hold, how many differ in strength, and the ones nearest --at (a camera's position; the
default is each light's own order), with the strengths of both. "Flicker" is a change within 1.5 times.
"""
import argparse
import collections
import re


def lights(path):
    found = collections.OrderedDict()
    for line in open(path, encoding='utf-8', errors='replace'):
        s = line[5:] if line.startswith('info|') else line
        if not s.startswith('light|'):
            continue
        key, _, value = s.rstrip('\n').partition('\t')
        parts = key.split('|')
        found.setdefault(parts[1], {})['|'.join(parts[2:])] = value
    return {(re.sub(r'#-?\d+$', '', name), f.get('position')): f for name, f in found.items()}


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('a'); p.add_argument('b'); p.add_argument('--at', default=''); p.add_argument('--within', type=float, default=30.0)
    o = p.parse_args()
    A, B = lights(o.a), lights(o.b)
    both = [k for k in A if k in B]
    strength = lambda f: float(f.get('intensity', '0') or 0)
    differ = [(k, strength(A[k]), strength(B[k])) for k in both if A[k].get('intensity') != B[k].get('intensity')]
    more = sum(1 for _, a, b in differ if a > b * 1.5); less = sum(1 for _, a, b in differ if b > a * 1.5)
    print('lights: %d in A, %d in B, %d the same light (name and place) in both' % (len(A), len(B), len(both)))
    print('strength differs: %d (A more than 1.5 times B: %d; B more than 1.5 times A: %d; flicker: %d)' % (len(differ), more, less, len(differ) - more - less))
    at = tuple(float(v) for v in o.at.split(',')) if o.at else None
    rows = []
    for k, a, b in differ:
        m = re.match(r'\(([-\d.eE+]+), ([-\d.eE+]+), ([-\d.eE+]+)\)', k[1] or '')
        if not m:
            continue
        place = tuple(float(v) for v in m.groups())
        far = sum((u - v) ** 2 for u, v in zip(place, at)) ** 0.5 if at else 0.0
        if at and far > o.within:
            continue
        if max(a, b) > 1.5 * max(min(a, b), 1e-9):
            rows.append((far, k[0][-44:], a, b, A[k].get('range', '?'), A[k].get('on', '?'), B[k].get('on', '?')))
    for far, name, a, b, reach, on_a, on_b in sorted(rows)[:40]:
        print('%s%-44s A %14.3f (on %s)   B %14.3f (on %s)   range %s' % (('%6.1f m  ' % far) if at else '', name, a, on_a, b, on_b, reach))


if __name__ == '__main__':
    main()
