# SPDX-License-Identifier: MIT
"""Independent linkage fixtures: reject culling/count/ordering claims unsupported by metadata."""
import copy
import importlib.util
from pathlib import Path
import unittest
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('vfx_capture', ROOT / 'tools/harness/vfx_capture.py')
vfx = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vfx)


def fixture():
    a = {'id': 101, 'dimension': 1, 'address': '500', 'bytes': 144, 'stride': 0}
    # Reuse the address with a different lifetime ID: never the same allocation.
    b = {**a, 'id': 202}
    dispatch = {'kind': 'dispatch', 'sequence': 1, 'cs': 'compute-A', 'groups': [1, 1, 1],
                'bindingsComplete': True, 'CS': {'uavs': [{'slot': 0, 'resource': a}]}}
    draw = {'kind': 'draw', 'sequence': 3, 'candle': True, 'bindingsComplete': True,
            'VS': {'srvs': [{'slot': 0, 'resource': a}]}, 'count': 4, 'instances': 1}
    document = {'schema': 2, 'frame': 20, 'previousBoundary': 19, 'deviceGeneration': 1, 'currentDeviceGeneration': 1,
                'faults': 0, 'hookFailures': [], 'events': [dispatch, {'kind': 'traceBegin', 'sequence': 2}, draw]}
    image = {'frame': 20, 'nativeCompletedAtScreenshot': 20, 'grabbed': True, 'expectedTraced': True, 'clock': {'timeScale': 1}}
    away = copy.deepcopy(document); away.update(frame=21, previousBoundary=20)
    away['events'] = [dispatch, {'kind': 'traceBegin', 'sequence': 2},
                      {**dispatch, 'sequence': 3, 'CS': {'uavs': [{'slot': 0, 'resource': b}]}}]
    away_image = {**image, 'frame': 21, 'nativeCompletedAtScreenshot': 21}
    return [('candle/2-vfx-traced/vfx-live-facing/frame-20.json', document, image),
            ('candle/2-vfx-traced/vfx-live-away/frame-21.json', away, away_image)]


class Evidence(unittest.TestCase):
    def test_screenshot_rows_and_channels(self):
        from PIL import Image
        temporary_root = ROOT / "build" / "vfx"
        temporary_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=temporary_root) as directory:
            root = Path(directory); folder = root / "candle" / "vfx-live-away"; folder.mkdir(parents=True)
            # Unity's first eight rows are the bottom: green. Top rows are red, RGBA input.
            (folder / "frame-1.raw").write_bytes(bytes([0,255,0,255])*128 + bytes([255,0,0,255])*128)
            metadata = {"grabbed": True, "image": "frame-1.raw", "width": 16, "height": 16, "format": "RGBA32"}
            self.assertFalse(vfx.pictures(root, [("candle/vfx-live-away/frame-1.json", {}, metadata)]))
            with Image.open(folder / "frame-1.jpg") as image:
                self.assertGreater(image.getpixel((8,2))[0], 200)
                self.assertGreater(image.getpixel((8,13))[1], 200)
            self.assertTrue((root / "vfx-overview.jpg").exists())


    def test_identity_links_offscreen_work_and_not_reused_address(self):
        r = vfx.summarize(fixture())
        self.assertFalse(r['errors'])
        self.assertEqual(r['attributeBuffers'], [101])
        self.assertEqual(r['attributeWriterHashes'], ['compute-A'])
        self.assertEqual(r['frames'][0]['buffers'][0]['drawReads'][0]['sequence'], 3)
        self.assertTrue(r['frames'][1]['buffers'][0]['submittedWithoutDraw'])
        self.assertEqual(r['unmappedWriterAllocations'][0]['resource']['id'], 202)

    def test_trace_order(self):
        r = vfx.summarize(fixture())
        self.assertIs(r['frames'][0]['buffers'][0]['writes'][0]['beforeTrace'], True)
        frames = fixture(); events = frames[0][1]['events']
        events[0], events[1] = events[1], events[0]
        for i, e in enumerate(events): e['sequence'] = i + 1
        r = vfx.summarize(frames)
        self.assertIs(r['frames'][0]['buffers'][0]['writes'][0]['beforeTrace'], False)

    def test_missing_dispatches_not_success(self):
        frames = fixture()
        for _, d, _ in frames:
            d['events'] = [e for e in d['events'] if not vfx.is_dispatch(e)]
        self.assertTrue(any('No compute UAV writer' in e for e in vfx.summarize(frames)['errors']))

    def test_indirect_counts_remain_unknown(self):
        frames = fixture(); e = frames[0][1]['events'][0]
        e.update(kind='dispatchIndirect', groups=None, indirect={'id': 303, 'dimension': 1}, indirectOffset=16)
        r = vfx.summarize(frames)
        write = r['frames'][0]['buffers'][0]['writes'][0]
        self.assertIsNone(write['groups']); self.assertTrue(write['executionUnknown'])
        self.assertEqual(write['indirectOffset'], 16)

    def test_unknown_or_stale_observation_fails(self):
        for key, value in [('faults', 1), ('hookFailures', ['collision']), ('unknownLists', 1), ('staleLists', 1), ('previousBoundary', 4), ('currentDeviceGeneration', 2)]:
            with self.subTest(key=key):
                frames = fixture(); frames[0][1][key] = value
                self.assertTrue(vfx.summarize(frames)['errors'])

    def test_unmatched_image_and_missing_trace_fail(self):
        frames = fixture(); frames[0][2]['nativeCompletedAtScreenshot'] = 19
        self.assertTrue(vfx.summarize(frames)['errors'])
        frames = fixture(); frames[0][1]['events'].pop(1)
        self.assertTrue(vfx.summarize(frames)['errors'])

    def test_incomplete_deferred_bindings_fail(self):
        frames = fixture(); frames[0][1]['events'][0]['bindingsComplete'] = False
        self.assertTrue(vfx.summarize(frames)['errors'])

    def test_primers_cannot_prove_coverage(self):
        frames = fixture(); frames[0][2]['primer'] = True
        self.assertFalse(vfx.summarize(frames)['attributeBuffers'])

    def test_constant_buffer_only_does_not_identify_attribute_kernel(self):
        frames = fixture(); frames[0][1]['events'][2]['VS'] = {'constantBuffers': [{'slot': 0, 'resource': {'id': 101, 'dimension': 1}}]}
        self.assertFalse(vfx.summarize(frames)['attributeWriterHashes'])


if __name__ == '__main__': unittest.main()
