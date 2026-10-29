#!/usr/bin/env python3
"""PT-83 bounds census: pair Off/On by identity, preserve unknowns, fail on bad evidence.

python tools/harness/missing.py artifacts/harness/<run>
No image quality claim: projected bounds can include things hidden by other things.
"""
import argparse
import json
import math
from pathlib import Path

SOURCES = {'Unity renderers', 'Drake and Entities Graphics', 'Kandra', 'Leshy',
           'Vegetation Studio', 'Medusa', 'Unity terrain, trees and details',
           'Decal projectors', 'VFX'}
STATUS_ORDER = {'excluded': 0, 'partial': 1, 'pending': 2, 'unknown': 3, 'included': 4}


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def numeric(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def rectangle(value):
    return (isinstance(value, list) and len(value) == 4 and all(numeric(x) and 0 <= x <= 1 for x in value)
            and value[0] < value[2] and value[1] < value[3])


def intersection(a, b):
    return max(0, min(a[2], b[2]) - max(a[0], b[0])) * max(0, min(a[3], b[3]) - max(a[1], b[1]))


def validate(doc, expected, boxes):
    errors = []
    if not isinstance(doc, dict):
        return ['inventory must be an object']
    if doc.get('schema') != 1:
        errors.append('unsupported or missing schema')
    if doc.get('traced') is not expected or doc.get('expectedTraced') is not expected:
        errors.append('actual/expected path tracing state mismatch')
    if doc.get('stepError'):
        errors.append('capture step error: ' + str(doc['stepError']))
    if not numeric(doc.get('frame')) or doc.get('frame', -1) < 0:
        errors.append('missing/invalid frame')
    if doc.get('timeScale') != 0:
        errors.append('world not frozen')
    camera = doc.get('camera', {})
    if not isinstance(camera, dict):
        errors.append('missing camera object')
        camera = {}
    for field in ('position', 'rotation'):
        if not isinstance(camera.get(field), list) or len(camera[field]) != 3 or not all(numeric(v) for v in camera[field]):
            errors.append('invalid camera ' + field)
    for field in ('fov', 'width', 'height'):
        if not numeric(camera.get(field)) or camera[field] <= 0:
            errors.append('invalid camera ' + field)
    declared = doc.get('boxes')
    if declared != boxes:
        errors.append('inventory boxes differ from request')
    by_box = {}
    if not isinstance(boxes, list):
        return errors + ['requested boxes must be a list']
    for box in boxes:
        if not isinstance(box, dict) or not isinstance(box.get('name'), str) or not box['name'] or not rectangle(box.get('rect')):
            errors.append('invalid requested box')
            continue
        if box['name'] in by_box:
            errors.append('duplicate box name')
        by_box[box['name']] = box['rect']
    if not by_box:
        errors.append('no valid requested boxes')
    coverage = doc.get('coverage')
    if not isinstance(coverage, list):
        errors.append('missing coverage census')
        coverage = []
    seen_sources = {x.get('source') for x in coverage if isinstance(x, dict)}
    for source in sorted(SOURCES - seen_sources):
        errors.append('missing source census: ' + source)
    for source in coverage:
        if not isinstance(source, dict) or source.get('ok') is not True:
            errors.append('incomplete source: ' + str(source))
    if expected:
        submission = doc.get('submissions', {})
        if not isinstance(submission, dict):
            errors.append('missing submissions object')
            submission = {}
        for name in ('scene', 'moving', 'skin', 'decal'):
            frame = submission.get(name + 'Frame')
            if not numeric(frame) or frame < 0 or not numeric(doc.get('frame')) or frame > doc['frame']:
                errors.append('missing/invalid ' + name + ' submission frame')
            elif name != 'scene' and doc['frame'] - frame > 2:
                errors.append('stale ' + name + ' submission')
            if submission.get(name + 'Result') != 0:
                errors.append('refused/missing ' + name + ' submission')
    rows = doc.get('rows')
    if not isinstance(rows, list):
        return errors + ['missing row list']
    identities = set()
    for i, row in enumerate(rows):
        prefix = f'row {i}: '
        if not isinstance(row, dict):
            errors.append(prefix + 'not an object')
            continue
        ident = row.get('id')
        if not isinstance(ident, str) or not ident or ident in identities:
            errors.append(prefix + 'missing/duplicate identity')
        identities.add(str(ident))
        for field in ('name', 'path', 'type', 'drawRule'):
            if not isinstance(row.get(field), str):
                errors.append(prefix + 'missing ' + field)
        if not any(row.get('cameraEligible') is v for v in (True, False, None)) or 'cameraEligible' not in row:
            errors.append(prefix + 'missing camera eligibility')
        if not rectangle(row.get('rect')):
            errors.append(prefix + 'invalid projected rectangle')
            continue
        actual = {name: intersection(row['rect'], box) for name, box in by_box.items()}
        actual = {name: area for name, area in actual.items() if area > 0}
        reported = {}
        overlaps = row.get('overlaps')
        if not isinstance(overlaps, list):
            errors.append(prefix + 'missing overlap list')
            overlaps = []
        for overlap in overlaps:
            if not isinstance(overlap, dict) or overlap.get('box') in reported:
                errors.append(prefix + 'bad/duplicate overlap')
                continue
            reported[overlap.get('box')] = overlap.get('area')
        if set(actual) != set(reported) or any(not numeric(reported.get(k)) or abs(reported[k] - v) > 1e-6 for k, v in actual.items()):
            errors.append(prefix + 'overlap/area inconsistent with bounds and requested boxes')
        if not actual:
            errors.append(prefix + 'row does not overlap any requested box')
        triangles = row.get('triangles')
        if triangles is not None and (not numeric(triangles) or triangles < 0 or int(triangles) != triangles):
            errors.append(prefix + 'invalid triangle count')
        mats = row.get('materials')
        if not isinstance(mats, list):
            errors.append(prefix + 'missing material list')
        else:
            for material in mats:
                if (not isinstance(material, dict) or not isinstance(material.get('shader'), str)
                        or not numeric(material.get('queue')) or not isinstance(material.get('keywords'), list)
                        or not isinstance(material.get('rule'), str)):
                    errors.append(prefix + 'material lacks shader/queue/keywords/rule')
        trace = row.get('trace', {})
        if not isinstance(trace, dict):
            errors.append(prefix + 'invalid trace record')
            continue
        status = trace.get('status')
        if status not in STATUS_ORDER or not isinstance(trace.get('rule'), str) or not trace['rule']:
            errors.append(prefix + 'trace status/reason missing')
        instances = trace.get('instances')
        if not isinstance(instances, list):
            errors.append(prefix + 'missing instance list')
        elif status in ('included', 'partial') and not instances:
            errors.append(prefix + 'inclusion without accepted submission evidence')
        else:
            for inst in instances:
                if (not isinstance(inst, dict) or inst.get('route') not in ('SetScene', 'SetMoving', 'SetSkinned', 'SetDecals')
                        or not numeric(inst.get('slot')) or inst['slot'] < 0 or not numeric(inst.get('mesh'))
                        or not numeric(inst.get('frame')) or not numeric(doc.get('frame')) or inst['frame'] > doc['frame']):
                    errors.append(prefix + 'invalid accepted instance evidence')
        if not expected and status in ('included', 'partial', 'excluded', 'pending'):
            errors.append(prefix + 'Off census claims current traced membership')
    return errors


def pair_rows(before, after, boxes):
    before = {r['id']: r for r in before}
    after = {r['id']: r for r in after}
    result = []
    for ident in sorted(before.keys() | after.keys()):
        off, on = before.get(ident), after.get(ident)
        row = dict(on or off)
        row['before'] = off
        row['gameCandidate'] = off.get('cameraEligible') if off else None
        if on is None:
            row['trace'] = {'status': 'unknown', 'rule': 'candidate present Off but absent from On census; not proof that capture rejected it', 'instances': []}
        # Preserve Off candidates even when the tracer changes passes/culling/geometry in On.
        rect = (off or on)['rect']
        row['overlaps'] = [{'box': b['name'], 'area': intersection(rect, b['rect'])} for b in boxes if intersection(rect, b['rect']) > 0]
        row['area'] = max((v['area'] for v in row['overlaps']), default=0)
        result.append(row)
    result.sort(key=lambda r: (r['gameCandidate'] is False, STATUS_ORDER[r['trace']['status']], -r['area'], r['id']))
    return result


def cell(value):
    return str(value).replace('|', '\\|').replace('\r', ' ').replace('\n', ' ')


def scene_report(summary):
    out = [f"# Missing world: {summary['scene']}", '',
           'Bounds overlap is a candidate, not proof of visible pixels. Off supplies game visibility; On supplies accepted tracer inputs.',
           'Left-out and partial candidates come first, then waiting/unknown, then included; each group is largest in the crop first.',
           'An included object may still have wrong materials, emission, alpha, LOD or lighting. This report does not propose a fix.', '',
           f"Save: {cell(summary.get('save', 'unknown'))}. Fallback save: {summary.get('standIn', 'unknown')}.", '']
    for issue in summary['issues']:
        out.append('- INCOMPLETE: ' + cell(issue))
    out += ['', f"Game view: [{summary['offStep']}/missing-view.jpg]({summary['offStep']}/missing-view.jpg). "
            f"Traced view: [{summary['onStep']}/missing-view.jpg]({summary['onStep']}/missing-view.jpg).", '']
    for box in summary['boxes']:
        if not isinstance(box, dict) or not rectangle(box.get('rect')) or not isinstance(box.get('name'), str):
            out += ['Invalid requested box; table withheld.', '']
            continue
        out += [f"## {box['name']} — {box['rect']}", '',
                '| Traced | Game candidate | Crop % | Name / path | Source / mesh / triangles | Materials (slot: shader; queue; keywords) | Rule / accepted input slots |',
                '|---|---|---:|---|---|---|---|']
        rows = [r for r in summary['rows'] if any(x['box'] == box['name'] for x in r['overlaps'])]
        rows.sort(key=lambda r: (r['gameCandidate'] is False, STATUS_ORDER[r['trace']['status']],
                                -next(x['area'] for x in r['overlaps'] if x['box'] == box['name']), r['id']))
        for row in rows:
            area = next(x['area'] for x in row['overlaps'] if x['box'] == box['name']) / ((box['rect'][2]-box['rect'][0])*(box['rect'][3]-box['rect'][1]))
            mesh = row.get('mesh') or {}
            mats = '; '.join(f"{m.get('slot', '?')}: {m.get('name', '')} / {m['shader']}; q{m['queue']}; {','.join(m['keywords'])}; {m['rule']}" for m in row['materials']) or 'none / unknown (see JSON)'
            slots = ', '.join(f"{x['route']}[{x['slot']}] mesh {x['mesh']} frame {x['frame']}" for x in row['trace']['instances'])
            note = row['trace']['rule'] + ('; ' + slots if slots else '')
            if row['gameCandidate'] is False:
                note += '; GAME GATE: ' + row['before']['drawRule']
            if row.get('visibilityNote'):
                note += '; ' + row['visibilityNote']
            values = [row['trace']['status'], str(row['gameCandidate']), f'{100*area:.2f}', row['name']+' / '+row['path'],
                      f"{row['type']} / {mesh.get('name', 'none/unknown')} / {row.get('triangles')}", mats, note]
            out.append('| ' + ' | '.join(cell(x) for x in values) + ' |')
        if not rows:
            out.append('| unknown | unknown | | No candidates; inspect coverage and context image | | | No absence proof |')
        out += ['']
    return '\n'.join(out)


def analyze(run):
    request = read(run / 'request.json')
    summaries, issues = [], []
    for spec in request.get('scenarios', []):
        steps = [s for s in spec.get('steps', []) if s.get('missing')]
        if not steps:
            continue
        key = spec['key']
        if Path(key).name != key or key in ('.', '..'):
            raise ValueError('unsafe scenario key')
        folder = run / key
        boxes = spec.get('missingBoxes', [])
        own = []
        phases = {}
        for step in steps:
            name = step['name']
            if Path(name).name != name or name in ('.', '..'):
                raise ValueError('unsafe step name')
            phase = step.get('traced', False)
            if phase in phases:
                own.append('duplicate Off/On inventory phase')
            try:
                doc = read(folder / name / 'missing.json')
                errs = validate(doc, phase, boxes)
                if not isinstance(doc, dict) or doc.get('scenario') != key:
                    errs.append('inventory scenario differs from request')
                result = read(folder / name / 'step.json')
                if result.get('error') or result.get('menuOpen') or result.get('traced') is not phase:
                    errs.append('step failed, menu open or wrong PT activation')
                if not (folder / name / 'missing-view.jpg').is_file():
                    errs.append('missing context image')
                own.extend(name + ': ' + err for err in errs)
                phases[phase] = (name, doc, not errs)
            except (OSError, ValueError, TypeError, KeyError) as exc:
                own.append(name + ': cannot read inventory/step: ' + str(exc))
        scenario = {}
        try:
            scenario = read(folder / 'scenario.json')
            if scenario.get('error'):
                own.append('scenario error: ' + scenario['error'])
        except (OSError, ValueError):
            own.append('missing/malformed scenario metadata')
        rows = []
        if True not in phases or False not in phases:
            own.append('requires both Off and On inventories')
        elif all(isinstance(phases[p][1], dict) and phases[p][1].get('traced') is p
                 and not any('row ' in e or 'camera ' in e or 'box' in e or 'frame' in e or 'submission' in e
                             for e in validate(phases[p][1], p, boxes)) for p in (False, True)):
            off, on = phases[False][1], phases[True][1]
            a, b = off['camera'], on['camera']
            if any(abs(x-y) > 0.001 for f in ('position', 'rotation') for x, y in zip(a[f], b[f])) or any(a[f] != b[f] for f in ('fov', 'width', 'height')):
                own.append('camera changed between inventories; crop correspondence invalid')
            if on['frame'] <= off['frame']:
                own.append('On inventory does not follow Off')
            rows = pair_rows(off['rows'], on['rows'], boxes)
        summary = {'scene': key, 'save': scenario.get('slotName', spec.get('save')), 'standIn': scenario.get('standIn', 'unknown'),
                   'boxes': boxes, 'rows': rows, 'issues': own, 'offStep': phases.get(False, ('missing',))[0], 'onStep': phases.get(True, ('missing',))[0]}
        folder.mkdir(parents=True, exist_ok=True)
        (folder / 'missing-report.md').write_text(scene_report(summary), encoding='utf-8')
        summaries.append(summary)
        issues.extend(f'{key}: {x}' for x in own)
    if not summaries:
        issues.append('request has no missing inventory steps')
    result = {'schema': 1, 'scenarios': summaries, 'issues': issues, 'ok': not issues}
    (run / 'missing-summary.json').write_text(json.dumps(result, indent=2, allow_nan=False), encoding='utf-8')
    out = ['# Missing world inventory', '', 'One table per scene. These are bounds candidates, not a pixel visibility test.', '']
    out += [f"- [{s['scene']}]({s['scene']}/missing-report.md): {len(s['rows'])} paired candidates; {len(s['issues'])} incomplete-evidence issues." for s in summaries]
    out += ['', 'Detailed Off/On materials, properties, passes, source coverage and native input slots are in each step’s `missing.json`.', '']
    out += ['- INCOMPLETE: ' + x for x in issues]
    (run / 'missing-report.md').write_text('\n'.join(out), encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    args = parser.parse_args()
    result = analyze(args.run)
    print(f"missing-report.md: {len(result['scenarios'])} scenes, {len(result['issues'])} issues")
    return 0 if result['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
