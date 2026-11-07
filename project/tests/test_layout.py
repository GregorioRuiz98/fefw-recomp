"""Regression checks for the runtime address-space contract and this title's modules."""
import json
import os
from pathlib import Path
import re
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[1]
ENGINE = Path(os.environ.get('FEFW_ENGINE', PROJECT.parents[1] / 'work/engine'))
sys.path.insert(0, str(ENGINE / 'recomp'))
sys.path.insert(0, str(PROJECT / 'tools'))
from nxrecomp.config import load_game
from prepare import BASES


class LayoutTests(unittest.TestCase):
    def setUp(self):
        header = (ENGINE / 'runtime/include/nxrt/memory.h').read_text(encoding='utf-8')
        self.constants = {name: int(value, 16) for name, value in re.findall(
            r'constexpr uint64_t (\w+) = (0x[0-9A-Fa-f]+)ull', header)}

    def test_modules_do_not_overlap_reserved_regions_or_each_other(self):
        inventory = json.loads((PROJECT / 'reports/dump-inventory.json').read_text(encoding='utf-8'))
        spans = {m['name']: m['memory_size'] for m in inventory['modules']}
        regions = []
        for stem, length in (('Remote', 'Size'), ('Hle', 'Size'), ('Heap', 'Max'), ('Alias', 'Size'), ('Stack', 'Size'), ('Kernel', 'Size')):
            base, size = self.constants['k' + stem + 'Base'], self.constants['k' + stem + length]
            regions.append((stem, base, base + size))
        for module in load_game('fefw').modules:
            end = module.base + ((spans[module.name] + 0xFFFF) & ~0xFFFF)
            for label, start, stop in regions:
                self.assertTrue(end <= start or module.base >= stop, f'{module.name} overlaps {label}')
            regions.append((module.name, module.base, end))

    def test_alias_region_can_hold_requested_36_gib_virtual_heap(self):
        self.assertGreaterEqual(self.constants['kAliasSize'], 0x900000000 + 0x1000)
        self.assertLessEqual(self.constants['kAliasBase'] + self.constants['kAliasSize'], self.constants['kMainBase'])

    def test_lifter_preparer_and_bootstrap_agree_on_module_bases(self):
        modules = load_game('fefw').modules
        self.assertEqual({m.name: m.base for m in modules}, BASES)
        source = (PROJECT / 'src/fefw_main.cpp').read_text(encoding='utf-8')
        boot_bases = {name: int(base, 16) for name, base in re.findall(
            r'\{"(\w+)", &nxmod_\w+, &nxmod_\w+_abi, (0x[0-9a-f]+)ull\}', source)}
        self.assertEqual(boot_bases, BASES)


if __name__ == '__main__':
    unittest.main()
