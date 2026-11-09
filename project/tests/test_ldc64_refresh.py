"""Own synthetic instructions and cache markers; no game code is included."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from refresh_ldc64_shaders import REVISION, cache_current, overlapping_loads


def load(destination=4, index=4, size=5, opcode=0xb82, mode=0):
    return struct.pack('<QQ', opcode | 7 << 12 | destination << 16 | index << 24 | 3 << 54,
                       size << 9 | mode << 14)


class WideConstantLoadTests(unittest.TestCase):
    def test_only_low_destination_alias_needs_refresh(self):
        self.assertEqual(overlapping_loads(load(), 'cs'), [0])
        self.assertEqual(overlapping_loads(load(index=5), 'cs'), [])
        self.assertEqual(overlapping_loads(load(index=8), 'cs'), [])
        self.assertEqual(overlapping_loads(load(index=255), 'cs'), [])

    def test_scalar_and_segmented_loads_are_not_affected(self):
        self.assertEqual(overlapping_loads(load(size=4), 'cs'), [])
        for mode in (1, 2, 3):
            self.assertEqual(overlapping_loads(load(mode=mode), 'cs'), [])

    def test_graphics_header_and_trailing_bytes_are_not_instructions(self):
        self.assertEqual(overlapping_loads(load() * 8 + load() + b'partial', 'vs'), [0x80])
        self.assertEqual(overlapping_loads(load() * 8, 'ps'), [])

    def test_uniform_index_and_zero_register(self):
        self.assertEqual(overlapping_loads(load(opcode=0xabb), 'cs'), [0])
        self.assertEqual(overlapping_loads(load(63, 63, opcode=0xabb), 'cs'), [])

    def test_marker_requires_same_translator_and_unchanged_shader_files(self):
        parent = Path.cwd() / 'config-test-results'
        parent.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent) as temp:
            cache = Path(temp)
            shader = cache / 'own-synthetic.spv'
            shader.write_bytes(b'own test fixture')
            stamp = {'revision': REVISION, 'translator_sha256': 'translator-a',
                     'files': {shader.name: hashlib.sha256(shader.read_bytes()).hexdigest()}}
            self.assertFalse(cache_current(cache, 'translator-a'))
            (cache / 'ldc64-refresh.json').write_text(json.dumps(stamp))
            self.assertTrue(cache_current(cache, 'translator-a'))
            self.assertFalse(cache_current(cache, 'translator-b'))
            empty_stamp = dict(stamp, files={})
            (cache / 'ldc64-refresh.json').write_text(json.dumps(empty_stamp))
            self.assertFalse(cache_current(cache, 'translator-a'))
            (cache / 'ldc64-refresh.json').write_text(json.dumps(stamp))
            shader.write_bytes(b'stale file')
            self.assertFalse(cache_current(cache, 'translator-a'))
            shader.unlink()
            self.assertFalse(cache_current(cache, 'translator-a'))
