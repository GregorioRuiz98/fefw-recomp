import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('local_preparation', Path(__file__).resolve().parents[1]/'tools/local_preparation.py')
prep = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prep)


class LocalPreparationTests(unittest.TestCase):
    def test_cancelled_translation_is_not_reused_as_complete(self):
        parent = Path.cwd()/'config-test-results'
        parent.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent) as temp:
            work = Path(temp)
            self.assertFalse(prep.lift_ready(work))
            for name in prep.MODULES:
                folder = work/'lift'/name
                folder.mkdir(parents=True)
                for file in ['c_000.cpp', 'module_'+name+'.cpp']:
                    path = folder/file
                    path.write_text('// own synthetic fixture\n')
                    os.utime(path, ns=(1_000_000_000, 1_000_000_000))
                (work/'lift'/(name+'-lift.json')).write_text(json.dumps({'module':name,'files':['c_000.cpp']}))
            self.assertTrue(prep.lift_ready(work))
            # Cancellation after touching a shard but before writing completion.
            shard = work/'lift/main/c_000.cpp'
            os.utime(shard, ns=(8_000_000_000_000_000_000, 8_000_000_000_000_000_000))
            self.assertFalse(prep.lift_ready(work))
            shard.write_text('')
            self.assertFalse(prep.lift_ready(work))
