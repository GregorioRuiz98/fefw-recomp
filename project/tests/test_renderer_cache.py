import csv
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import prepare_renderer_shaders as shaders


class RendererCacheTests(unittest.TestCase):
    def setUp(self):
        parent = Path.cwd() / 'config-test-results'
        parent.mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=parent)
        self.addCleanup(self.temp.cleanup)
        self.cache = Path(self.temp.name)
        self.h = '0123456789abcdef'
        self.data = b'original synthetic shader fixture'
        (self.cache / (self.h + '.spv')).write_bytes(self.data)

    def stamp(self, **changes):
        stamp = {'revision': shaders.REVISION, 'runtime_commit': shaders.RUNTIME_PIN,
                 'files': {self.h: hashlib.sha256(self.data).hexdigest()}}
        stamp.update(changes)
        (self.cache / 'renderer-source.json').write_text(json.dumps(stamp), encoding='utf-8')
        (self.cache / 'renderer-source.ini').write_text(shaders.cache_ini(1), encoding='utf-8')

    def test_verified_cache_and_corruption(self):
        self.stamp()
        self.assertTrue(shaders.cache_current(self.cache, {self.h}))
        (self.cache / (self.h + '.spv')).write_bytes(b'changed')
        self.assertFalse(shaders.cache_current(self.cache, {self.h}))

    def test_other_runtime_or_incomplete_corpus_is_rejected(self):
        self.stamp(runtime_commit='old')
        self.assertFalse(shaders.cache_current(self.cache, {self.h}))
        self.stamp()
        self.assertFalse(shaders.cache_current(self.cache, {self.h, 'fedcba9876543210'}))

    def test_empty_corpus_is_rejected(self):
        (self.cache / 'unique.tsv').write_text('code_sha1_16\n', encoding='utf-8')
        with self.assertRaises(ValueError):
            shaders.corpus_hashes(self.cache)

    def test_native_cuda_reconstruction_is_separate_from_nvn_programs(self):
        (self.cache / 'ctrl').mkdir()
        (self.cache / 'ctrl/nvn.bin').write_bytes(b'NVN2 synthetic control')
        (self.cache / 'ctrl/cuda.bin').write_bytes(b'CUDA synthetic control')
        (self.cache / 'unique.tsv').write_text(
            'code_sha1_16\tfirst_ctrl\n' + self.h + '\tnvn\nfedcba9876543210\tcuda\n', encoding='utf-8')
        self.assertEqual(shaders.corpus_hashes(self.cache), {self.h})

    def results(self, status='ok'):
        with (self.cache / 'results.tsv').open('w', newline='', encoding='utf-8') as stream:
            writer = csv.DictWriter(stream, ['hash', 'status', 'error'], delimiter='\t')
            writer.writeheader()
            writer.writerow({'hash': self.h, 'status': status, 'error': 'synthetic failure'})

    def test_missing_result_cannot_mark_cache_ready(self):
        self.results()
        with self.assertRaises(ValueError):
            shaders.verify_outputs(self.cache, {self.h, 'fedcba9876543210'}, Path('validator'))
        self.assertFalse((self.cache / 'renderer-source.json').exists())

    def test_translation_error_cannot_mark_cache_ready(self):
        self.results('exception')
        with self.assertRaises(ValueError):
            shaders.verify_outputs(self.cache, {self.h}, Path('validator'))
        self.assertFalse((self.cache / 'renderer-source.json').exists())

    def test_validator_failure_cannot_mark_cache_ready(self):
        self.results()
        with patch.object(shaders.subprocess, 'run') as run:
            run.return_value.returncode = 1
            run.return_value.stdout = 'invalid synthetic module'
            run.return_value.stderr = ''
            with self.assertRaises(ValueError):
                shaders.verify_outputs(self.cache, {self.h}, Path('validator'))
        self.assertFalse((self.cache / 'renderer-source.json').exists())

    def test_success_records_provenance_and_removes_unused_blocks(self):
        self.results()
        unused = self.cache / 'fedcba9876543210.spv'
        unused.write_bytes(b'unused native reconstruction kernel')
        with patch.object(shaders.subprocess, 'run') as run:
            run.return_value.returncode = 0
            shaders.verify_outputs(self.cache, {self.h}, Path('validator'))
        self.assertTrue(shaders.cache_current(self.cache, {self.h}))
        self.assertFalse(unused.exists())
