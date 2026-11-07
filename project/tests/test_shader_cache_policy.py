"""Prevent formally valid shaders from loading unsupported descriptor layouts."""
from pathlib import Path
import contextlib
import csv
import io
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from validate_shaders import main, runtime_rejection


class ShaderCachePolicyTests(unittest.TestCase):
    def test_single_texture_and_image_bindings_are_accepted(self):
        self.assertEqual(runtime_rejection('cb24=s0b57 texTexture2D[c-1:8]=s2b128 imgTexture2D[c0:52]=s3b0'), '')

    def test_descriptor_arrays_are_deferred(self):
        for length in (0, 2, 9, 10, 16, 32, 4096):
            for kind in ('tex', 'img'):
                self.assertIn('descriptor arrays', runtime_rejection(f'{kind}TextureCube[c14:208x{length}]=s2b640'))

    def test_explicit_single_descriptor_is_accepted(self):
        self.assertEqual(runtime_rejection('texTexture2D[c2:8x1]=s2b128'), '')

    def test_only_the_audited_uniform_array_contract_can_be_opted_in(self):
        self.assertEqual(runtime_rejection('texTextureCube[c6:208x32]=s2b640', True), '')
        for resource in ('texTextureCube[c6:0x32]', 'texTextureCube[c14:208x32]',
                         'texTextureCube[c6:208x4096]', 'imgTextureCube[c6:208x32]',
                         'texTexture2D[c6:208x32]', 'texTextureCube[c0:1073741824x4096]'):
            self.assertIn('descriptor arrays', runtime_rejection(resource, True))

    def test_uniform_matrix_accepts_only_tested_shapes_banks_and_bases(self):
        for bank in range(3, 19):
            for shape in ('TextureCube', 'Texture2D'):
                for word in (0, 192, 208):
                    self.assertEqual(runtime_rejection(f'tex{shape}[c{bank}:{word}x32]', allow_uniform_matrix=True), '')
        for token in ('texTextureCube[c2:208x32]', 'texTextureCube[c19:208x32]',
                      'texTextureCube[c14:1x32]', 'texTexture3D[c14:208x32]',
                      'imgTextureCube[c14:208x32]', 'texNone[c0:1073741824x4096]',
                      'texTextureCube[c14:208x33]'):
            self.assertTrue(runtime_rejection(token, allow_uniform_matrix=True), token)

    def test_literal_handle_contract_is_bounded(self):
        for word in (0x40000000, 0x4000401f, 0x43ffffff):
            self.assertEqual(runtime_rejection(f'texTexture2D[c0:{word}]'), '')
        for resource in ('texTexture2D[c0:268435456]', 'texTexture2D[c0:1140850688]',
                         'imgTexture2D[c0:1073741824]', 'texTexture3D[c0:1073741824]'):
            self.assertTrue(runtime_rejection(resource), resource)

    def test_dynamic_pool_contract_requires_explicit_verification(self):
        for token in ('texTexture2D[c0:1073741824x32768]', 'texTextureCube[c0:1073741824x32768]',
                      'texNone[c0:1073741824x512]'):
            self.assertTrue(runtime_rejection(token))
            self.assertEqual(runtime_rejection(token, allow_pool_arrays=True), '')
        for token in ('texTexture2D[c0:1073741824x4096]', 'texNone[c0:1073741824x32768]',
                      'texTexture3D[c0:1073741824x32768]', 'texTexture2D[c2:1073741824x32768]',
                      'imgTexture2D[c0:1073741824x32768]'):
            self.assertTrue(runtime_rejection(token, allow_pool_arrays=True))

    def test_packed_constant_bank_is_deferred(self):
        for bank in (-2, 32, 131076):
            self.assertIn('constant-bank', runtime_rejection(f'texTexture2D[c{bank}:8]=s2b128'))

    def test_a_rejected_translation_removes_its_old_cached_file(self):
        work = Path(__file__).resolve().parents[3] / 'work'
        with tempfile.TemporaryDirectory(prefix='cache-policy-', dir=work) as temp:
            folder = Path(temp)
            source, cache = folder / 'translated', folder / 'cache'
            source.mkdir()
            cache.mkdir()
            (cache / 'old.spv').write_bytes(b'previous generated output')
            with (source / 'results.tsv').open('w', newline='', encoding='utf-8') as stream:
                writer = csv.DictWriter(stream, fieldnames=['hash', 'status'], delimiter='\t')
                writer.writeheader()
                writer.writerow({'hash': 'old', 'status': 'unresolved-resource'})
            args = ['validate_shaders', '--input', str(source), '--out', str(cache),
                    '--validator', str(folder / 'not-called.exe'), '--report', str(folder / 'report.json')]
            with patch.object(sys, 'argv', args), contextlib.redirect_stdout(io.StringIO()):
                main()
            self.assertFalse((cache / 'old.spv').exists())
            with (cache / 'results.tsv').open() as stream:
                self.assertEqual(list(csv.DictReader(stream, delimiter='\t')), [])
