"""Synthetic end-to-end Ampere texture regressions; no game binaries required."""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
import unittest

WORK = None


def insn(lo, hi):
    return struct.pack('<QQ', lo, hi)


def mov(reg, bits):
    return insn(0x7802 | (7 << 12) | (reg << 16) | (bits << 32), 0xf00)


def load_handle(uniform=False):
    return insn((0xab9 if uniform else 0xb82) | (7 << 12) |
                ((4 if uniform else 8) << 16) | (0 if uniform else 255 << 24) |
                (2 << 54) | (0x20 << 38), 4 << 9)


def texture(kind, scr=False, lod=False, mask=15, resolved=True, literal=42):
    ops = mov(12, 0x3e800000) + mov(13, 0x3f000000) + mov(9, 0x40000000)
    op = {'bound': 0xb60, 'register': 0x361, 'uniform': 0xf60}[kind]
    rb = 9 if lod else 255
    if kind == 'register':
        ops += load_handle() if resolved else mov(8, literal)
        rb = 8
    elif kind == 'uniform':
        ops += load_handle(True)
    # SCR has two Ra values when there are >=3 sources, otherwise one Ra
    # value and one Rb value. A uniform handle is outside these source blocks.
    if scr and not lod and kind != 'register':
        rb = 13
    lo = op | (7 << 12) | (12 << 24) | (rb << 32) | (1 << 61)
    if scr:
        lo |= 1 << 60
    if kind == 'bound':
        lo |= (2 << 54) | (8 << 40)
    elif kind == 'uniform':
        lo |= 4 << 40
    hi = 255 | (mask << 8) | (7 << 17) | ((3 if lod else 1) << 23)
    return ops + insn(lo, hi)


def query(bindless):
    lo = (0x370 if bindless else 0xb6f) | (7 << 12) | ((8 if bindless else 255) << 24)
    if not bindless:
        lo |= (2 << 54) | (8 << 40)
    return (load_handle() + mov(9, 0) if bindless else b'') + insn(lo, 255 | (3 << 8))


def translate(name, words, glsl=False):
    folder = WORK / 'texture-tests' / name
    folder.mkdir(parents=True, exist_ok=True)
    code = bytearray(0x80)
    struct.pack_into('<I', code, 0, (5 << 10) | (4 << 5) | 2)
    struct.pack_into('<I', code, 72, 15)  # four color output components
    code += words + insn(0x794d, 0x000fea0003800000)
    ctrl = bytearray(0xe0)
    struct.pack_into('<II', ctrl, 0, 0x98761234, 201)
    for offset, value in ((0x30, len(ctrl)), (0x38, len(code)), (0x40, len(code)), (0x44, len(code))):
        struct.pack_into('<I', ctrl, offset, value)
    struct.pack_into('<H', ctrl, 0x56, 1)
    (folder / 'code.bin').write_bytes(code)
    (folder / 'ctrl.bin').write_bytes(ctrl)
    output = folder / ('shader.glsl' if glsl else 'shader.spv')
    # A previous successful result must never mask a new rejection.
    if output.exists():
        output.unlink()
    args = [WORK / 'toolchains/dotnet/dotnet.exe',
            WORK / 'engine/tools/shaders/build/bin/Release/net10.0/spirv_harness.dll',
            folder / 'code.bin', folder / 'ctrl.bin', '-o', output]
    if glsl:
        args.append('--glsl')
    result = subprocess.run([str(x) for x in args], capture_output=True, text=True, timeout=30)
    return result, output


class TextureRegressions(unittest.TestCase):
    def test_uniform_cube_table_preserves_its_nonzero_base(self):
        # Runtime index from c3, address = 0x340 + 8*index, handle from c6[address].
        words = mov(12, 0x3f800000) + mov(13, 0) + mov(14, 0)
        words += insn(0xb82 | (7 << 12) | (20 << 16) | (255 << 24) | (3 << 54), 4 << 9)
        words += insn(0x811 | (7 << 12) | (8 << 16) | (20 << 24) | (0x340 << 32), 255 | (3 << 11) | (7 << 17))
        words += insn(0xb82 | (7 << 12) | (8 << 16) | (8 << 24) | (6 << 54), 4 << 9)
        words += insn(0x361 | (7 << 12) | (12 << 24) | (8 << 32) | (3 << 61), 255 | (15 << 8) | (7 << 17) | (1 << 23))
        result, output = translate('uniform_array_base', words)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('texTextureCube[c6:208x32]', result.stdout)
        val = subprocess.run([str(WORK / 'build/spirv-tools/tools/spirv-val.exe'), '--target-env', 'vulkan1.3', str(output)], capture_output=True, text=True)
        self.assertEqual(val.returncode, 0, val.stdout + val.stderr)
        result, output = translate('uniform_array_base', words, glsl=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        glsl = output.read_text()
        self.assertIn('[32]', glsl)
        self.assertNotRegex(glsl, r'textureLod\([^;]*\[31\]')

    def assert_valid(self, name, words):
        result, spv = translate(name, words)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('texTexture2D[', result.stdout)
        val = subprocess.run([str(WORK / 'build/spirv-tools/tools/spirv-val.exe'),
                              '--target-env', 'vulkan1.3', str(spv)], capture_output=True, text=True)
        self.assertEqual(val.returncode, 0, val.stdout + val.stderr)
        self.assertNotIn('Failed to find handle source', result.stdout)
        return result.stdout

    def test_bound_register_uniform_and_scr_samples_keep_the_same_resource(self):
        for kind in ('bound', 'register', 'uniform'):
            for scr in (False, True):
                for lod in (False, True):
                    with self.subTest(kind=kind, scr=scr, lod=lod):
                        name = f'{kind}_{scr}_{lod}'
                        words = texture(kind, scr, lod)
                        contract = self.assert_valid(name, words)
                        self.assertRegex(contract, r'texTexture2D\[c(?:-1|2):8\]=s2b128')
                        result, glsl = translate(name, words, glsl=True)
                        self.assertEqual(result.returncode, 0, result.stdout)
                        source = glsl.read_text()
                        self.assertRegex(source, r'textureLod\([^;]+vec2\(0\.25, 0\.5\), (?:0\.0|2\.0)\)')
                        self.assertIn(', 2.0)', source) if lod else self.assertIn(', 0.0)', source)
                        # Rc=RZ must keep components 2 and 3 in Rd+2 / Rd+3.
                        self.assertRegex(source, r'out_attr0\.z = \w+\.z;')
                        self.assertRegex(source, r'out_attr0\.w = \w+\.w;')

    def test_bindless_query_uses_a_handle_then_lod(self):
        for bindless in (False, True):
            with self.subTest(bindless=bindless):
                self.assert_valid(f'query_{bindless}', query(bindless))

    def test_half_precision_texture_results_are_packed_into_register_pairs(self):
        for mask in (1, 15):
            words = bytearray(texture('bound', mask=mask))
            hi = struct.unpack_from('<Q', words, len(words) - 8)[0] | (1 << 15)
            struct.pack_into('<Q', words, len(words) - 8, hi)
            self.assert_valid(f'half_{mask}', words)
            result, output = translate(f'half_{mask}', words, glsl=True)
            self.assertEqual(result.returncode, 0, result.stdout)
            source = output.read_text()
            self.assertEqual(source.count('packHalf2x16('), 1 if mask == 1 else 2)
            if mask == 1:
                self.assertRegex(source, r'packHalf2x16\(vec2\([^\n]+, 0\.0\)\)')
            self.assertIn('out_attr0.z = intBitsToFloat(undef);', source)

    def test_literal_handles_preserve_texture_and_sampler_ids(self):
        for texture_id, sampler_id in ((0, 0), (7, 0), (31, 1), (0x3fff, 0xfff)):
            literal = texture_id | (sampler_id << 20)
            expected = 0x40000000 | texture_id | (sampler_id << 14)
            contract = self.assert_valid(f'literal_{literal:x}', texture('register', resolved=False, literal=literal))
            self.assertIn(f'texTexture2D[c0:{expected}]', contract)

    def test_unresolvable_handle_is_rejected_and_writes_no_spirv(self):
        result, output = translate('unresolved', texture('register', resolved=False, literal=0x4000))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('unresolved-resource', result.stdout)
        self.assertFalse(output.exists())

    def test_extended_integer_comparisons_fold_to_the_expected_boolean(self):
        # These shaders have constant inputs and expose the comparison as the
        # first color component. Check the real IR/optimizer against a separate
        # CPU reference; this does not assert execution on the GPU.
        def iset(cmp, dest, a, b, signed=False, extended=False):
            return insn(0x20c | (7 << 12) | (a << 24) | (b << 32),
                        (dest << 17) | (7 << 20) | (7 << 23) | (cmp << 12) |
                        (int(signed) << 9) | (int(extended) << 8))
        for signed in (False, True):
            for a, b in ((1, 2), (2, 1), (7, 7), (0xffffffff, 0)):
                av = a - (1 << 32) if signed and a >= 1 << 31 else a
                bv = b - (1 << 32) if signed and b >= 1 << 31 else b
                for low in (False, True):
                    eq = av == bv
                    expected = (False, av < bv or (eq and low), eq and low,
                                av < bv or (eq and low), av > bv or (eq and low),
                                not eq or low, av > bv or (eq and low), True)
                    for cmp, answer in enumerate(expected):
                        with self.subTest(signed=signed, a=a, b=b, low=low, comparison=cmp):
                            words = mov(12, a) + mov(13, b) + mov(14, 0x3f800000) + mov(15, 0)
                            words += iset(2 if low else 5, 0, 255, 255)
                            words += iset(cmp, 1, 12, 13, signed, True)
                            words += insn(0x207 | (7 << 12) | (14 << 24) | (15 << 32), 1 << 23)
                            result, output = translate('compare', words, glsl=True)
                            self.assertEqual(result.returncode, 0, result.stdout)
                            self.assertIn(f'out_attr0.x = {int(answer)}.0;', output.read_text())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    WORK = args.work.resolve()
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(TextureRegressions))
    if args.report:
        args.report.write_text(json.dumps({'test_cases': result.testsRun, 'sample_profiles': 12,
                                          'query_profiles': 2, 'rejection_profiles': 1, 'literal_handle_profiles': 4,
                                          'constant_integer_comparison_profiles': 128,
                                          'half_precision_profiles': 2,
                                          'uniform_array_base_profiles': 1,
                                          'passed': result.wasSuccessful(), 'failures': len(result.failures),
                                          'errors': len(result.errors)}, indent=2), encoding='utf-8')
    raise SystemExit(not result.wasSuccessful())
