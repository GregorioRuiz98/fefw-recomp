"""Check both native instruction alternatives and reject unsupported originals."""
import os
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(os.environ.get('FEFW_ENGINE', Path(__file__).resolve().parents[3] / 'work/engine')) / 'recomp'))
from nxrecomp.config import load_game
from nxrecomp.image import Image
from nxrecomp.lift import Lifter
from nxrecomp.program import Env, lift_function, runtime_patch_stmt


class RuntimePatchTests(unittest.TestCase):
    def test_both_variants_are_compiled_and_selected_from_mapped_text(self):
        cfg = load_game('fefw')
        for pc, (old, new) in cfg.runtime_instruction_patches.items():
            with self.subTest(pc=hex(pc)):
                image = Image('synthetic', pc, old.to_bytes(4, 'little'), (0, 4))
                env = Env(image, cfg, full=False)
                env.ends[pc] = pc + 4
                env.starts = [pc]
                text, count, bad, _ = lift_function(env, pc)
                self.assertIn('rd32(0x%xULL)' % pc, text)
                self.assertIn('0x%08xU' % old, text)
                self.assertIn('0x%08xU' % new, text)
                self.assertIn('unsupported runtime patch', text)
                self.assertEqual(count, 1)
                self.assertFalse(bad)

    def test_unexpected_original_is_refused_before_native_generation(self):
        cfg = load_game('fefw')
        pc = next(iter(cfg.runtime_instruction_patches))
        image = Image('synthetic', pc, b'\x00' * 4, (0, 4))
        env = Env(image, cfg, full=False)
        with self.assertRaisesRegex(ValueError, 'unexpected original'):
            runtime_patch_stmt(env, Lifter(pc, pc + 4, env), 0, pc)
