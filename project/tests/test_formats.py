"""Synthetic regression tests: never require or embed game data."""
import hashlib
import os
from pathlib import Path
import struct
import sys
import unittest

import lz4.block
import zbic

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / 'tools'))
sys.path.insert(0, str(Path(os.environ.get('FEFW_ENGINE', PROJECT.parents[1] / 'work' / 'engine')) / 'recomp'))
from nxrecomp.nso import nso_info
from prepare import uncompressed_nso
from inspect_dump import read_npdm
from extract_shaders import checked_slice, extract_bnsh


def make_nso(compression):
    header = bytearray(0x100)
    header[:4] = b'NSO0'
    flags = 0x38 | (7 if compression != 'none' else 0) | (0x80 if compression == 'zbic' else 0)
    struct.pack_into('<I', header, 12, flags)
    out = header
    originals = []
    for i in range(3):
        raw = (f'synthetic segment {i}!'.encode() * 128)
        originals.append(raw)
        packed = {'none': lambda x: x, 'lz4': lambda x: lz4.block.compress(x, store_size=False),
                  'zbic': zbic.compress}[compression](raw)
        struct.pack_into('<III', out, 0x10 + i * 16, len(out), i * 0x1000, len(raw))
        struct.pack_into('<I', out, 0x60 + i * 4, len(packed))
        out[0xA0 + 32 * i:0xC0 + 32 * i] = hashlib.sha256(raw).digest()
        out.extend(packed)
    return bytes(out), originals


def make_bnsh():
    data = bytearray(0x800)
    data[:4] = b'BNSH'
    data[8:12] = b'\x01\x04\x02\x00'
    data[12:14] = b'\xff\xfe'
    struct.pack_into('<I', data, 28, len(data))
    data[0x60:0x64] = b'grsc'
    struct.pack_into('<I', data, 0x7C, 1)
    struct.pack_into('<Q', data, 0x80, 0xC0)
    struct.pack_into('<Q', data, 0xD0, 0x100)
    struct.pack_into('<Q', data, 0x108, 0x180)
    struct.pack_into('<QQII', data, 0x188, 0x200, 0x400, 0x100, 0xE0)
    data[0x200:0x208] = b'\x34\x12\x76\x98\xc9\0\0\0'
    struct.pack_into('<I', data, 0x230, 0xE0)
    struct.pack_into('<I', data, 0x244, 0x100)
    return data


class NsoTests(unittest.TestCase):
    def test_supported_compression_and_normalization_preserve_all_segments(self):
        for compression in ('none', 'lz4', 'zbic'):
            with self.subTest(compression=compression):
                data, originals = make_nso(compression)
                self.assertEqual([s['data'] for s in nso_info(data)['segments']], originals)
                normalized = uncompressed_nso(data)
                self.assertEqual(struct.unpack_from('<I', normalized, 12)[0] & 0x87, 0)
                self.assertEqual([s['data'] for s in nso_info(normalized)['segments']], originals)

    def test_bad_hash_is_rejected(self):
        data = bytearray(make_nso('none')[0])
        data[-1] ^= 1
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            nso_info(data)

    def test_truncation_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'outside'):
            nso_info(make_nso('lz4')[0][:-1])

    def test_short_header_is_rejected(self):
        with self.assertRaises(ValueError):
            nso_info(b'NSO0')


class ShaderTests(unittest.TestCase):
    def test_240_layout_with_nvn2_control_is_supported(self):
        data = make_bnsh()
        data[8] = 0
        self.assertEqual(len(list(extract_bnsh(data))), 1)

    def test_240_still_rejects_non_nvn2_control(self):
        data = make_bnsh()
        data[8] = 0
        data[0x200] ^= 1
        with self.assertRaisesRegex(ValueError, 'NVN2 control'):
            list(extract_bnsh(data))

    def test_stage_and_code_are_extracted(self):
        stages = list(extract_bnsh(make_bnsh()))
        self.assertEqual(len(stages), 1)
        self.assertEqual(stages[0][1], 'vs')
        self.assertEqual(len(stages[0][2]), 0xE0)
        self.assertEqual(len(stages[0][3]), 0x100)

    def test_out_of_range_pointer_is_rejected(self):
        data = make_bnsh()
        struct.pack_into('<Q', data, 0x190, 0xFFFF)
        with self.assertRaisesRegex(ValueError, 'outside'):
            list(extract_bnsh(data))

    def test_stage_mismatch_is_rejected(self):
        data = make_bnsh()
        struct.pack_into('<H', data, 0x256, 1)
        with self.assertRaisesRegex(ValueError, 'stage mismatch'):
            list(extract_bnsh(data))

    def test_wrong_version_is_rejected(self):
        data = make_bnsh()
        data[8] = 2
        with self.assertRaisesRegex(ValueError, 'version'):
            list(extract_bnsh(data))

    def test_negative_range_is_rejected(self):
        with self.assertRaises(ValueError):
            checked_slice(b'test', -1, 1)


class NpdmTests(unittest.TestCase):
    def test_npdm_fields_are_read_from_aci0(self):
        data = bytearray(0x180)
        data[:4] = b'META'
        data[14], data[15] = 44, 2
        struct.pack_into('<I', data, 28, 0x200000)
        struct.pack_into('<II', data, 0x70, 0x100, 0x80)
        data[0x100:0x104] = b'ACI0'
        struct.pack_into('<Q', data, 0x110, 0x0400123456789000)
        info = read_npdm(data)
        self.assertEqual(info['program_id'], '0400123456789000')
        self.assertEqual(info['main_thread_core'], 2)
        self.assertEqual(info['main_thread_stack'], 0x200000)

    def test_truncated_npdm_is_rejected(self):
        with self.assertRaises(ValueError):
            read_npdm(b'META')


if __name__ == '__main__':
    unittest.main()
