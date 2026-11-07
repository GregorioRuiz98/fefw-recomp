import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('package_public', Path(__file__).resolve().parents[1] / 'tools/package_public.py')
public = importlib.util.module_from_spec(spec)
spec.loader.exec_module(public)


class PublicPackageTests(unittest.TestCase):
    def scratch(self):
        parent = Path.cwd() / 'config-test-results'
        parent.mkdir(exist_ok=True)
        return tempfile.TemporaryDirectory(prefix='public-package-', dir=parent)

    def test_only_explicit_source_and_independent_configurator_are_included(self):
        with self.scratch() as temp:
            root = Path(temp)
            src = root / 'source'
            (src / 'src').mkdir(parents=True)
            (src / 'src/main.cpp').write_text('// own code\n')
            (src / 'PUBLIC-README.md').write_text('public tools\n')
            (src / 'LICENSE').write_text('Synthetic license fixture\n')
            (src / 'LEGAL.md').write_text('Synthetic publication policy\n')
            (src / 'romfs').mkdir()
            (src / 'romfs/secret.bin').write_bytes(b'private')
            (src / 'reports').mkdir()
            (src / 'reports/screenshot.png').write_bytes(b'private')
            (src / 'src/accidental.spv').write_bytes(b'\x03\x02\x23\x07')
            (src / 'tools').mkdir()
            (src / 'tools/Prepare-Game.ps1').write_text('# preparation entry point\n')
            (src / 'tools/Prepare-Dependencies.ps1').write_text('# dependencies\n')
            (src / 'FireEmblemRecomp.ps1').write_text('# build driver\n')
            (src / 'tools/local-build.json').write_text(json.dumps({'work': 'C:' + '/Users/' + 'private/work'}))
            exe = root / 'setup.exe'
            exe.write_bytes(b'MZ-synthetic-test-placeholder')
            out = root / 'public'
            public.create(src, exe, out)
            manifest = public.verify(out)
            names = {f['path'] for f in manifest['files']}
            self.assertIn('project/src/main.cpp', names)
            self.assertIn('LICENSE', names)
            self.assertIn('LEGAL.md', names)
            self.assertIn('project/LICENSE', names)
            self.assertIn('tools/Prepare-Game.ps1', names)
            self.assertIn('tools/Prepare-Dependencies.ps1', names)
            self.assertIn('project/FireEmblemRecomp.ps1', names)
            self.assertNotIn('project/romfs/secret.bin', names)
            self.assertNotIn('project/src/accidental.spv', names)
            self.assertNotIn('project/tools/local-build.json', names)
            self.assertFalse(manifest['game_content_included'])

    def test_forbidden_release_files_are_rejected(self):
        for name in ['fefw.exe', 'project/romfs/file.txt', 'project/exefs/main', 'project/src/c_001.cpp', '../escape.txt', 'project/tests/video.webm', 'project/src/cache.spv', 'project/tools/prod.keys', 'project/.env']:
            with self.subTest(name=name), self.assertRaises(ValueError):
                public.audit_name(name)

    def test_personal_paths_and_disguised_binary_are_rejected(self):
        for payload in [b'path=C:' + b'/Users/' + b'example/dump', b'NSO0-not-source', b'\x03\x02\x23\x07']:
            with self.subTest(payload=payload), self.assertRaises(ValueError):
                public.audit_payload('project/src/test.cpp', payload)

    def test_audit_detects_added_files_and_changed_content(self):
        with self.scratch() as temp:
            root = Path(temp)
            src = root / 'source'
            src.mkdir()
            (src / 'PUBLIC-README.md').write_text('public tools\n')
            exe = root / 'setup.exe'
            exe.write_bytes(b'MZ-synthetic-test-placeholder')
            out = root / 'public'
            public.create(src, exe, out)
            (out / 'fefw.exe').write_bytes(b'not allowed')
            with self.assertRaises(ValueError):
                public.verify(out)
            (out / 'fefw.exe').unlink()
            (out / 'README.md').write_text('changed')
            with self.assertRaises(ValueError):
                public.verify(out)
