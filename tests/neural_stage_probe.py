"""Check NR's GPU stage readbacks against the smoke probe's known input, independent CPU math."""
import argparse
from pathlib import Path
import numpy as np
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/harness'))
from neural_stages import read_frame


def check(folder):
    if (folder / 'error.txt').exists():
        raise AssertionError((folder / 'error.txt').read_text())
    assert len(list(folder.glob('nr-*.bin'))) == 18, 'Missing probe frames'
    for size in (256, 320):
        header, stages, exposure = read_frame(folder / f'nr-{size+1}.bin')
        assert header[2] == size + 1
        x, y = np.meshgrid(np.arange(size) / size, np.arange(size) / size)
        original = np.stack([x, y, np.full_like(x, .2)], axis=-1)[::-1]
        encoded = np.where(original <= .0031308, original * 12.92,
                           1.055 * original ** (1 / 2.4) - .055)
        for iy in range(36):
            for ix in range(64):
                ys = slice(iy * size // 36, (iy+1) * size // 36)
                xs = slice(ix * size // 64, (ix+1) * size // 64)
                np.testing.assert_allclose(stages[0, iy, ix, :3], original[ys, xs].mean((0, 1)), atol=2e-6)
                np.testing.assert_allclose(stages[1, iy, ix, :3], encoded[ys, xs].mean((0, 1)), atol=2e-6)
                if len(stages) == 9:
                    np.testing.assert_allclose(stages[7, iy, ix], [encoded[ys, xs].min(), encoded[ys, xs].max(), 0, 0], atol=2e-6)
        np.testing.assert_allclose(stages[3, ..., :3], stages[0, ..., :3], atol=.002)
        np.testing.assert_allclose(stages[4, ..., :3], np.broadcast_to([1, 2, np.sqrt(5)], (36, 64, 3)), atol=.003)
        assert len(stages) in (7, 9), 'Signed motion diagnostics missing'
        np.testing.assert_allclose(stages[6, ..., 0], -1, atol=.001)
        np.testing.assert_allclose(stages[6, :18, :, 1], 2, atol=.002)
        np.testing.assert_allclose(stages[6, 18:, :, 1], -2, atol=.002)
        np.testing.assert_allclose(stages[6, ..., 2], np.sqrt(5), atol=.003)
        if len(stages) >= 6:
            np.testing.assert_array_equal(stages[5, ..., 0], .5)
            np.testing.assert_array_equal(stages[5, ..., 1:3], 0)
        assert np.max(stages[:7, ..., 3]) == 0, 'Nonfinite pixels in known inputs'
        np.testing.assert_array_equal(exposure, [.125, 3, 0, 1])
    for name, frame, expected in [('generic-motion', 3002, 512)]:
        _, stages, _ = read_frame(folder / name / f'nr-{frame}.bin')
        np.testing.assert_allclose(stages[4, :, :32, 0], expected, atol=.001)
        np.testing.assert_allclose(stages[4, :, 32:, 0], 2.56, atol=.002)
        np.testing.assert_array_equal(stages[4, ..., 1], 0)
    for frame, scale in [(4001, .25), (4002, 1), (4003, 4), (4004, 1), (4005, 1), (4006, 1)]:
        if frame>4004 and not (folder / f'signal-{frame}').exists(): continue
        _, stages, _ = read_frame(folder / f'signal-{frame}' / f'nr-{frame}.bin')
        x, y = np.meshgrid(np.arange(256) / 32, np.arange(256) / 64)
        original = np.stack([x, y, np.full_like(x, .125)], axis=-1)[::-1]
        linear = original * scale
        linear /= 1 + (linear if frame==4005 else linear.max(axis=-1, keepdims=True))
        encoded = np.where(linear <= .0031308, linear * 12.92, 1.055 * linear ** (1 / 2.4) - .055)
        for iy in range(36):
            for ix in range(64):
                ys = slice(iy * 256 // 36, (iy+1) * 256 // 36)
                xs = slice(ix * 256 // 64, (ix+1) * 256 // 64)
                np.testing.assert_allclose(stages[1, iy, ix, :3], encoded[ys, xs].mean((0, 1)), atol=2e-6,
                                           err_msg=f'Wrong conditioning: {scale}')
        np.testing.assert_allclose(stages[0, ..., :3], stages[3, ..., :3], rtol=.001, atol=2e-6)
    print('PASS: conditioning and restoration, stage orientation, all-pixel block averages, encoding, neutral output, scaled motion and same-frame exposure.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path)
    parser.add_argument('--mutations', action='store_true')
    args = parser.parse_args()
    check(args.folder)
    if args.mutations:
        # Corrupt real GPU readbacks, preserving the originals. The independent expected values must reject each fault.
        import tempfile
        import shutil
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'probe'
            shutil.copytree(args.folder, target)
            path = target / 'nr-257.bin'
            original = path.read_bytes()
            for fault in ('flip-x', 'flip-y', 'wrong-row', 'missing-motion'):
                data = bytearray(original)
                grids = np.frombuffer(data, dtype='<f4', offset=32, count=7*36*64*4).reshape(7, 36, 64, 4)
                if fault == 'flip-x': grids[6, ..., 0] *= -1
                elif fault == 'flip-y': grids[6, ..., 1] *= -1
                elif fault == 'wrong-row': grids[6] = grids[6, ::-1].copy()
                else: grids[6] = 0
                path.write_bytes(data)
                try: check(target)
                except AssertionError: print(f'PASS: rejected signed-motion fault {fault}')
                else: raise AssertionError(f'Signed-motion fault escaped: {fault}')
