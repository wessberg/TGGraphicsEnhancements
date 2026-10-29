"""Reject convincing-looking NR evidence taken on the wrong route, or with the wrong quality controls."""
import copy
import csv
import json
from pathlib import Path
import sys
import unittest
import tempfile
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/harness'))
from neural_chairs import check_walk, plan, report


class Chairs(unittest.TestCase):
    def fixture(self, root):
        data = plan(json.loads(Path('tests/ingame/scenarios.json').read_text(encoding='utf-8-sig')))
        scene = data['scenarios'][0]
        scene['steps'] = data['recipes']['nr-chairs']
        for take_index, take in enumerate(scene['steps']):
            take.update(frames=12, motionFrames=8, motionHoldFrames=4)
            folder = root/scene['key']/take['name']; folder.mkdir(parents=True)
            frames = [dict(frame=i+100, neuralFrame=i+100, neuralEvaluated=i, neuralBeforeLensEffects=True,
                           px=min(i, 7)*2/7, py=1, pz=3, rx=0, ry=90, rz=0, fov=60, realtime=i*.1,
                           file=f'f{i:02}.raw') for i in range(12)]
            for frame in frames:
                (folder/frame['file']).write_bytes(np.full((36, 64, 3), 50+10*(take_index == 2), dtype=np.uint8).tobytes())
            (folder/'step.json').write_text(json.dumps(dict(frames=frames, neuralEvaluations=12 if take['neural'] else 0,
                                                           width=64, height=36, format='RGB24', error='', menuOpen=False)), encoding='utf-8')
            if take['neural']:
                stage_dir = folder/'nr-stages'; stage_dir.mkdir()
                rows = []
                for frame in frames:
                    row = dict(frame=frame['frame'], camera=7, flags=9, reset=0, exposurePresent=1, passes=1,
                               model=0, mask=0, strength=0 if take_index == 1 else 1, tone=1, detail=1, skin=1)
                    rows.append(row)
                    header = np.array([0x3144524e, 3, row['frame'], 7, 64, 36, 7, 9], dtype='<u4')
                    stages = np.zeros((7, 36, 64, 4), dtype='<f4')
                    stages[0, ..., :3] = .05; stages[3] = stages[0]
                    stages[1, ..., :3] = .2; stages[2] = stages[1]
                    if take_index == 2:
                        stages[2, ..., :3] += .02
                        stages[3, ..., :3] += .01
                    (stage_dir/f"nr-{row['frame']}.bin").write_bytes(header.tobytes()+stages.tobytes()+np.array([.125,13,0,1], dtype='<f4').tobytes())
                with (stage_dir/'frames.csv').open('w', newline='', encoding='utf-8') as stream:
                    writer = csv.DictWriter(stream, fieldnames=rows[0].keys()); writer.writeheader(); writer.writerows(rows)
        (root/'request.json').write_text(json.dumps(data), encoding='utf-8')
        return scene

    def test_report_rejects_corrupt_evidence(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); scene = self.fixture(root)
            report(root)
            saved = json.loads((root/'nr-chair-validation.json').read_text(encoding='utf-8'))
            self.assertTrue(saved['takes']['02-quality-neutral']['neutral_scene_exact'])
            self.assertTrue((root/'nr-chair-stages.jpg').exists())
            # Mutate captured evidence independently of the reporting implementation.
            stage = root/scene['key']/'02-quality-neutral/nr-stages/nr-100.bin'
            original = stage.read_bytes()
            broken = np.frombuffer(original, dtype='<f4').copy()
            broken[8+3*36*64*4] += .01
            stage.write_bytes(broken.tobytes())
            with self.assertRaisesRegex(ValueError, 'Zero Strength changed'): report(root)
            stage.write_bytes(original)
            metadata = root/scene['key']/'03-quality-full/step.json'
            original = metadata.read_text(encoding='utf-8'); broken = json.loads(original)
            broken['frames'][0]['neuralFrame'] -= 1
            metadata.write_text(json.dumps(broken), encoding='utf-8')
            with self.assertRaises(ValueError): report(root)
            metadata.write_text(original, encoding='utf-8')
            broken = json.loads(original)
            for frame in broken['frames']: frame['py'] += .01
            metadata.write_text(json.dumps(broken), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'routes do not match'): report(root)

    def test_plan_uses_owner_save_and_quality_only(self):
        data = plan(json.loads(Path('tests/ingame/scenarios.json').read_text(encoding='utf-8-sig')))
        scene, = data['scenarios']
        self.assertEqual(scene['save'], 'PT eyebrows-flickering')
        self.assertEqual(scene['yawOffset'], 180)
        steps = data['recipes']['nr-chairs']
        self.assertEqual([s['neural'] for s in steps], [False, True, True, False])
        for step in steps:
            values = {s['key']: s['value'] for s in step['set']}
            self.assertEqual(values['Placement'], 'After Upscaling')
            self.assertEqual((values['Passes'], values['Model'], values['Local Tone']), ('1', 'A', '1'))

    def test_realtime_walk_survives_variable_frame_times(self):
        request = dict(frames=8, motionFrames=4, walkForward=1.2, walkSpeed=1.2)
        times = [1, 1.1, 1.2, 1.5, 1.8, 2.1, 2.3, 2.4]
        positions = [0, 0, .12, .48, .84, 1.2, 1.2, 1.2]
        frames = [dict(px=x, py=1, pz=3, rx=0, ry=90, rz=0, fov=60, realtime=t+.04,
                       cameraMotionTime=t) for t,x in zip(times, positions)]
        self.assertAlmostEqual(check_walk(request, frames)['metres_per_second'], 1.2)
        slow = copy.deepcopy(frames)
        for frame in slow: frame['cameraMotionTime'] *= 2
        with self.assertRaises(ValueError): check_walk(request, slow)

    def test_captured_walk_and_bad_routes(self):
        request = dict(frames=12, motionFrames=8, walkForward=2)
        frames = [dict(px=min(i, 7)*2/7, py=1, pz=3, rx=0, ry=90, rz=0, fov=60, realtime=i*.1) for i in range(12)]
        self.assertAlmostEqual(check_walk(request, frames)['distance_metres'], 2)
        # Faults represent an ignored walk, wrong axis, loop, mid-walk teleport, moving hold and NaNs.
        for fault in ('stationary', 'sideways', 'loop', 'jump', 'hold', 'nan'):
            broken = copy.deepcopy(frames)
            if fault == 'stationary':
                for f in broken: f['px'] = 0
            elif fault == 'sideways':
                for f in broken: f['pz'], f['px'] = f['px']+3, 0
            elif fault == 'loop': broken[9]['px'] = 0
            elif fault == 'jump': broken[3]['px'] = 1.9
            elif fault == 'hold': broken[-1]['px'] += .01
            else: broken[3]['px'] = float('nan')
            with self.assertRaises(ValueError, msg=fault): check_walk(request, broken)


if __name__ == '__main__': unittest.main()
