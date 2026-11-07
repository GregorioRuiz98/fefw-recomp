"""PAC/AUT must reach tier 2 rather than aliasing tier-1 bit operations."""
import sys
import os
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(os.environ.get('FEFW_ENGINE', Path(__file__).resolve().parents[3] / 'work/engine')) / 'recomp'))
from nxrecomp.lift import Lifter


class Env:
    wanted = {}
    jt = {}


class PointerAuthTest(unittest.TestCase):
    def test_register_authentication_is_identity(self):
        # PACIA/PACIB/PACDA/PACDB and AUTIA/AUTIB/AUTDA/AUTDB, XPACI/XPACD.
        for word in (0xdac10020, 0xdac10420, 0xdac10820, 0xdac10c20,
                     0xdac11020, 0xdac11420, 0xdac11820, 0xdac11c20,
                     0xdac143e0, 0xdac147e0, 0xdac107fe, 0xdac117fe):
            with self.subTest(word=hex(word)):
                self.assertEqual(Lifter(0x1000, 0x2000, Env()).lift(word, 0x1000), ';')

    def test_rev16_retains_real_source(self):
        statement = Lifter(0x1000, 0x2000, Env()).lift(0xdac00420, 0x1000)
        self.assertIn('rev16_64(c->x[1])', statement)


if __name__ == '__main__':
    unittest.main()
