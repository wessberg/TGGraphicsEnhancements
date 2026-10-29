"""Check live NR/FG cooperation from passive runtime counters, including fault rejection."""
import copy
import json
import sys
from pathlib import Path


def validate(steps):
    expected = [(False, False, 0), (True, False, 0), (True, True, 0), (False, True, 0),
                (True, True, 2), (True, True, 3), (True, False, 0), (False, False, 0)]
    assert len(steps) == len(expected), 'Incomplete FG sequence'
    summary = []
    for step, (nr, fg, backend) in zip(steps, expected):
        assert not step['error'] and not step['menuOpen'], 'Invalid gameplay capture'
        assert (step['neuralEvaluations'] > 0) if nr else (step['neuralEvaluations'] == 0), 'Wrong NR activity'
        frames = step['frames']
        assert len(frames) >= 16, 'Too few frames'
        for frame in frames:
            assert frame['neuralFailed'] == 0 and frame['neuralSkipped'] == 0, 'NR failed or skipped'
            if nr:
                assert frame['neuralBeforeLensEffects'] is True, 'NR is late'
            state = frame['frameGeneration']
            assert state['HaveStatus'] and state['Suspension'] == 'None', 'FG status unavailable or suspended'
            assert 0 <= frame['realtime'] - state['StatusTime'] < .6, 'Stale FG status'
            assert state['RequestedSequence'] == state['AppliedSequence'], 'FG options not applied'
            assert state['LastAcceptedMode'] == int(fg) and state['ActiveBackend'] == backend, 'Wrong active FG backend/mode'
            for key in ('BackendFailedMask', 'BackendResult', 'LastInteropHresult', 'LastSetOptionsResult', 'LastGetStateResult', 'DlssgStatus'):
                assert state[key] == 0, f'FG failure: {key}'
        first, last = [f['frameGeneration'] for f in (frames[0], frames[-1])]
        delta = {k: last[k] - first[k] for k in ('SuccessfulInputDispatches', 'SourcePresents', 'OutputFramesReported')}
        assert delta['SourcePresents'] > 0, 'No presentations'
        assert (delta['SuccessfulInputDispatches'] > 0) if fg else (delta['SuccessfulInputDispatches'] == 0), 'Wrong FG dispatch activity'
        if backend != 2:
            ratio = delta['OutputFramesReported'] / delta['SourcePresents']
            assert abs(ratio - (2 if fg else 1)) < .2, 'Vendor output count does not show requested multiplier'
        # FSR exposes dispatch results but no presented-frame counter. Never infer one from its multiplier.
        summary.append(dict(name=step['name'], nr=step['neuralEvaluations'], **delta,
                            vendorOutputCountAvailable=backend != 2))
    return summary


def main():
    folder = Path(sys.argv[1])
    steps = [json.loads(p.read_text(encoding='utf-8-sig')) for p in sorted(folder.glob('*/step.json'))]
    summary = validate(steps)
    for fault in ('nr-off', 'fg-off', 'backend', 'failure', 'stale', 'output'):
        broken = copy.deepcopy(steps)
        if fault == 'nr-off':
            broken[2]['neuralEvaluations'] = 0
        else:
            state = broken[2]['frames'][-1]['frameGeneration']
            if fault == 'fg-off': state['LastAcceptedMode'] = 0
            if fault == 'backend': state['ActiveBackend'] = 2
            if fault == 'failure': state['BackendFailedMask'] = 2
            if fault == 'stale': state['StatusTime'] = 0
            if fault == 'output': state['OutputFramesReported'] = broken[2]['frames'][0]['frameGeneration']['OutputFramesReported']
        try:
            validate(broken)
        except AssertionError:
            continue
        raise AssertionError(f'Missed corruption: {fault}')
    (folder / 'nr-fg-validation.json').write_text(json.dumps(summary, indent=2) + '\n')
    print('PASS: eight NR/FG states, vendor DLSS/XeSS output counts, FSR dispatches. Rejected six corruptions.')


if __name__ == '__main__':
    main()
