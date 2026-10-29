#!/usr/bin/env python3
"""Measure same-frame NR stage changes. Diagnostic readbacks invalidate performance comparisons."""
import argparse
import csv
import json
from pathlib import Path
import numpy as np

STAGES = ('scene_in', 'model_in', 'model_out', 'scene_out', 'motion_pixels', 'depth', 'motion_signed_pixels',
          'model_input_range', 'model_output_range')


def read_frame(path):
    data = path.read_bytes()
    if len(data) < 32 or len(data) % 4:
        raise ValueError(f'Incomplete NR stage frame: {path}')
    header = np.frombuffer(data, dtype='<u4', count=8)
    count = int(header[6])
    if header[0] != 0x3144524e or (header[1], count) not in ((1, 5), (2, 6), (3, 7), (4, 9)) or tuple(header[4:6]) != (64, 36):
        raise ValueError(f'Invalid NR stage header: {path}')
    values = np.frombuffer(data, dtype='<f4', offset=32)
    if values.size != count * 36 * 64 * 4 + 4:
        raise ValueError(f'Incomplete NR stage frame: {path}')
    return header, values[:-4].reshape(count, 36, 64, 4), values[-4:]


def temporal(values, hold):
    if not np.isfinite(values).all() or len(values) <= hold + 1:
        raise ValueError('Insufficient or nonfinite temporal stage data')
    spread = np.ptp(values[hold:], axis=0)
    tail = np.ptp(values[-min(32, len(values)-hold):], axis=0)
    return dict(hold_max_range=float(spread.max()), hold_p95_range=float(np.percentile(spread, 95)),
                tail32_max_range=float(tail.max()),
                hold_max_step=float(np.abs(np.diff(values[hold:], axis=0)).max()),
                mean_curve=values.mean(axis=tuple(range(1, values.ndim))).tolist())


def analyze_step(folder, requested):
    directory = folder / 'nr-stages'
    if (directory / 'error.txt').exists():
        raise ValueError((directory / 'error.txt').read_text())
    step = json.loads((folder / 'step.json').read_text(encoding='utf-8-sig'))
    if step.get('error') or step.get('menuOpen'):
        raise ValueError(f"Invalid game capture: {step.get('error')} menu={step.get('menuOpen')}")
    with (directory / 'frames.csv').open() as stream:
        rows = list(csv.DictReader(stream))
    metadata = {int(row['frame']): row for row in rows}
    if len(rows) != len(metadata):
        raise ValueError('Duplicate stage frame IDs')
    frames = step['frames']
    if len(frames) != requested['frames']:
        raise ValueError('Incomplete screen capture')
    controls = {s['key']: s['value'] for s in requested.get('set', [])
                if s.get('plugin') == 'com.wessberg.tgneuralrendering'}
    expected = {field: float(controls[key]) for key, field in
                [('Strength', 'strength'), ('Local Tone', 'tone'), ('Detail', 'detail'), ('Skin Detail', 'skin')]
                if key in controls}
    if 'Model' in controls:
        expected['model'] = {'A': 0, 'B': 1, 'C': 2, 'D': 3}[controls['Model']]
    if 'Automatic Mask' in controls:
        expected['mask'] = {'Off': 0, 'On': 1}[controls['Automatic Mask']]
    measurements, exposures, resets, replay_sources, replay_resets, ranges = [], [], [], [], [], []
    for frame in frames:
        number = frame['frame']
        if frame['neuralFrame'] != number:
            raise ValueError('NR and screen frame IDs differ')
        row = metadata[number]
        if int(row.get('passes', 1)) != int(controls.get('Passes', 1)):
            raise ValueError('NR pass count differs from the plan')
        for name, value in expected.items():
            actual = float(row.get(name, 'nan'))
            if not np.isfinite(actual) or abs(actual - value) > 1e-6:
                raise ValueError(f'NR {name} differs from the plan at frame {number}')
        if 'Placement' in controls:
            before = controls['Placement'] == 'Before Upscaling'
            if bool(int(row['flags']) & 16) != before:
                raise ValueError(f'NR placement differs from the plan at frame {number}')
        input_scale = float(row.get('inputScale', 1))
        if not np.isfinite(input_scale) or abs(input_scale - requested.get('neuralInputScale', 1)) > 1e-6:
            raise ValueError('NR input conditioning differs from the plan')
        if int(row.get('toneMap', 0)) != max(0, requested.get('neuralToneMap', -1)):
            raise ValueError('NR tone-map diagnostic differs from the plan')
        if requested.get('neuralPostToneMap', False):
            if int(row['flags']) & 8 or frame.get('neuralPostToneMap') is not True or frame.get('lensEffectsSuppressed') is not True:
                raise ValueError('Tone-mapped comparison did not use uncompressed graded input with lens effects suppressed')
        header, stages, exposure = read_frame(directory / f'nr-{number}.bin')
        if header[2] != number or header[3] != int(row['camera']) or header[7] != int(row['flags']):
            raise ValueError('Stage metadata mismatch')
        if int(row['exposurePresent']) != 1 or not np.isfinite(exposure).all():
            raise ValueError('Same-frame exposure missing or invalid')
        if not np.isfinite(stages).all() or np.any(stages[:7, ..., 3] != 0):
            raise ValueError('Nonfinite pixels in NR inputs/output')
        if len(stages) == 9:
            bounds = stages[7:]
            if np.any(bounds[..., 0] > bounds[..., 1]) or np.any(bounds[..., 2:] < 0) or np.any(bounds[..., 2:] > 1):
                raise ValueError('Invalid model pixel range or fallback fraction')
            ranges.append(bounds)
        measurements.append(stages[..., :3]); exposures.append(exposure); resets.append(int(row['reset']))
        replay_sources.append(int(row.get('replaySourceFrame', 0)))
        replay_resets.append(int(row.get('replayReset', 0)))
    values = np.asarray(measurements, dtype=np.float64)
    exposure = np.asarray(exposures, dtype=np.float64)
    hold = requested.get('motionFrames', 0) + 1
    # Held geometry must actually be held before calling a range temporal instability.
    for key, tolerance in [('px', .001), ('py', .001), ('pz', .001),
                           ('rx', .02), ('ry', .02), ('rz', .02), ('fov', .01)]:
        pose = np.asarray([f[key] for f in frames[hold:]], dtype=float)
        if not np.isfinite(pose).all() or np.ptp(pose) > tolerance:
            raise ValueError(f'Held camera changed: {key}')
    if requested.get('neuralToneMap', -1) >= 0 and resets != [1] + [0]*(len(frames)-1):
        raise ValueError('Tone-map comparison did not use one matched initial history reset')
    result = dict(passes=int(controls.get('Passes', 1)), tone_map=max(0, int(requested.get('neuralToneMap', -1))), input_scale=float(requested.get('neuralInputScale', 1)), frame_ids=[f['frame'] for f in frames], hold_start=hold, frames=len(frames), resets=sum(resets),
                  exposure_min=exposure.min(0).tolist(), exposure_max=exposure.max(0).tolist(),
                  hold_exposure_range=np.ptp(exposure[hold:], axis=0).tolist(),
                  exposure_curve=exposure[:, 0].tolist(), motion_hold_max_mean_pixels=float(values[hold:, 4, ..., 2].max()))
    if ranges:
        if len(ranges) != len(frames):
            raise ValueError('Model pixel range data missing from some frames')
        # Cells can differ in pixel count when the resolution is not divisible by the grid.
        widths = np.diff(np.arange(65, dtype=np.int64) * int(rows[0]['width']) // 64)
        heights = np.diff(np.arange(37, dtype=np.int64) * int(rows[0]['height']) // 36)
        weights = heights[:, None] * widths[None, :]
        bounds = np.asarray(ranges, dtype=np.float64)
        for i, label in enumerate(('model_input_range', 'model_output_range')):
            b = bounds[:, i]
            negative = (b[..., 2] * weights).sum((1, 2)) / weights.sum()
            fallback = (b[..., 3] * weights).sum((1, 2)) / weights.sum()
            result[label] = dict(minimum=float(b[..., 0].min()), maximum=float(b[..., 1].max()),
                negative_fraction_max=float(negative.max()), fallback_fraction_max=float(fallback.max()),
                fallback_fraction_curve=fallback.tolist(), fallback_cell_max=float(b[..., 3].max()))
    for i, name in enumerate(STAGES[:4]):
        # Encoded model colour in 0..255 levels; scene values are pre-exposed linear HDR units.
        result[name] = temporal(values[:, i] * (255 if 'model_' in name else 1), hold)
    result['model_effect'] = temporal((values[:, 2] - values[:, 1]) * 255, hold)
    result['scene_effect'] = temporal(values[:, 3] - values[:, 0], hold)
    result['motion_curve'] = values[:, 4, ..., 2].mean((1, 2)).tolist()
    result['replay'] = bool(requested.get('neuralReplay'))
    if values.shape[1] >= 6:
        result['depth'] = temporal(values[:, 5, ..., 0], hold)
    if result['replay']:
        if values.shape[1] < 6:
            raise ValueError('Replay requires depth measurements')
        expected_source = [0] * hold + [frames[hold]['frame']] * (len(frames)-hold)
        if replay_sources != expected_source:
            raise ValueError('Replay did not retain the requested stationary frame')
        reset = int(bool(requested.get('neuralReplayReset')))
        if replay_resets != [0] * hold + [reset] * (len(frames)-hold):
            raise ValueError('Replay reset request differs from the plan')
        every = int(bool(requested.get('neuralReplayResetEveryFrame')))
        actual_every = [int(metadata[f['frame']].get('replayResetEveryFrame', 0)) for f in frames]
        if every and not reset or actual_every != [0] * hold + [every] * (len(frames)-hold):
            raise ValueError('Repeated-reset mode differs from the plan')
        expected_resets = [reset] * (len(frames)-hold) if every else [reset] + [0] * (len(frames)-hold-1)
        if resets[hold:] != expected_resets:
            raise ValueError('Replay reset sequence differs from the plan')
        if result['model_in']['hold_max_range'] != 0 or result['depth']['hold_max_range'] != 0:
            raise ValueError('Replay colour or depth changed')
        if result['motion_hold_max_mean_pixels'] != 0:
            raise ValueError('Replay motion was not zero')
        result['replay_source_frame'] = frames[hold]['frame']
        result['replay_history_reset'] = bool(reset)
        result['replay_reset_every_frame'] = bool(every)
    elif any(replay_sources):
        raise ValueError('Replay leaked into an ordinary take')
    return result


def report(root):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    request = json.loads((root / 'request.json').read_text(encoding='utf-8-sig'))
    results, errors = {}, []
    text = ['# NR stage measurements', '', 'Synchronous GPU readbacks; frame times are not performance evidence.', '',
             'Model values are encoded SDR RGB levels (0–255). Scene values are linear pre-exposed HDR, or graded linear SDR in post-tone-map diagnostics.',
            'Each grid cell averages every pixel in its region. Coverage is not the internal NR mask.', '',
            'Replay retains colour/depth textures and zeros motion. Its live scene input still changes; judge model output for this test.', '',
            '| Scene/take | Exposure range while held (raw X) | Motion while held (pixels) | Input max range | Model effect max range | Effect final 32 range |',
            '|---|---:|---:|---:|---:|---:|']
    for scene in request['scenarios']:
        for take in scene['steps']:
            if not take.get('neuralStages'): continue
            key = scene['key'] + '/' + take['name']
            try: result = analyze_step(root / key, take)
            except (ValueError, OSError, KeyError) as ex:
                errors.append(f'{key}: {ex}'); continue
            results[key] = result
            text.append(f"| {key} | {result['hold_exposure_range'][0]:.8g} | {result['motion_hold_max_mean_pixels']:.5g} | "
                        f"{result['model_in']['hold_max_range']:.4f} | {result['model_effect']['hold_max_range']:.4f} | {result['model_effect']['tail32_max_range']:.4f} |")
            fig, axes = plt.subplots(3, 1, figsize=(10, 7), sharex=True)
            for name, label in [('model_in', 'Input to NR'), ('model_out', 'Output from NR')]:
                axes[0].plot(result[name]['mean_curve'], label=label)
            axes[0].set_ylabel('Mean RGB level'); axes[0].legend()
            axes[1].plot(result['model_effect']['mean_curve'], color='tab:purple')
            axes[1].set_ylabel('Output − input')
            axes[2].plot(result['exposure_curve'], label='Exposure texture X', color='tab:green')
            axes[2].set_ylabel('Exposure (raw X)')
            motion_axis = axes[2].twinx(); motion_axis.plot(result['motion_curve'], color='tab:orange', alpha=.7)
            motion_axis.set_ylabel('Motion (pixels)', color='tab:orange'); axes[2].set_xlabel('Captured frame index')
            for ax in axes: ax.axvline(result['hold_start'], color='gray', linestyle='--'); ax.grid(alpha=.2)
            fig.suptitle(key); fig.tight_layout()
            fig.savefig(root / key / 'nr-stage-curves.png', dpi=120); plt.close(fig)
    if errors: text += ['', '## Invalid evidence', ''] + ['- '+e for e in errors]
    if any('model_output_range' in result for result in results.values()):
        text += ['', 'Pixel ranges inspect every model pixel, before HDR restoration. Fallback counts the current',
                 'HDR protection condition, including decoded values at or above one. This is separate from the NR mask.', '',
                 '| Scene/take | Stage | Pixel min | Pixel max | Negative pixels (max %) | HDR fallback (max %) | Worst cell fallback (%) |',
                 '|---|---|---:|---:|---:|---:|---:|']
        for key, result in results.items():
            for label in ('model_input_range', 'model_output_range'):
                if label not in result: continue
                bounds = result[label]
                text.append(f"| {key} | {label} | {bounds['minimum']:.7g} | {bounds['maximum']:.7g} | "
                            f"{100*bounds['negative_fraction_max']:.6g} | {100*bounds['fallback_fraction_max']:.6g} | "
                            f"{100*bounds['fallback_cell_max']:.6g} |")
    (root / 'nr-stages.json').write_text(json.dumps(dict(results=results, errors=errors), indent=2)+'\n', encoding='utf-8')
    (root / 'nr-stages.md').write_text('\n'.join(text)+'\n', encoding='utf-8')
    print(f'{len(results)} measured takes; {len(errors)} invalid. {root / "nr-stages.md"}')
    return bool(errors)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    raise SystemExit(report(parser.parse_args().run))
