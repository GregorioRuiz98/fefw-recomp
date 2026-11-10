import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location('public_repo_audit', TOOLS / 'audit_public_repo.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PublicRepoTests(unittest.TestCase):
    def test_only_exact_pinned_decoder_may_exceed_source_size_limit(self):
        project = Path(__file__).resolve().parents[1]
        candidates = [project / 'android', project.parent / 'android']
        android = next(path for path in candidates if path.is_dir())
        data = (android / 'runtime/third_party/zbic/zstd.c').read_bytes()
        name = 'android/runtime/third_party/zbic/zstd.c'
        self.assertGreater(len(data), module.MAX_SOURCE_BYTES)
        module.check_content(name, data)
        with self.assertRaises(ValueError):
            module.check_content(name, data + b'\n')
        with self.assertRaises(ValueError):
            module.check_content('android/runtime/src/nso.cpp', data)

    def scratch(self):
        parent = Path.cwd() / 'config-test-results'
        parent.mkdir(exist_ok=True)
        return tempfile.TemporaryDirectory(prefix='public-repo-', dir=parent)

    def run_git(self, folder, *args):
        return subprocess.run(['git', '-C', str(folder), *args], check=True, capture_output=True)

    def create(self, folder):
        self.run_git(folder, 'init', '-b', 'codex/test')
        self.run_git(folder, 'config', 'user.name', 'Synthetic Test')
        self.run_git(folder, 'config', 'user.email', 'synthetic@example.invalid')
        (folder / 'README.md').write_text('Own synthetic source\n', encoding='utf-8')
        self.stage_manifest(folder)

    def stage_manifest(self, folder):
        self.run_git(folder, 'add', '--all')
        module.audit(folder, write_manifest=True)
        self.run_git(folder, 'add', 'source-manifest.json')

    def test_source_snapshot_and_history_are_verified(self):
        with self.scratch() as temp:
            root = Path(temp).resolve(); self.create(root)
            self.run_git(root, 'commit', '-m', 'Synthetic source')
            report = module.audit(root, history=True)
            self.assertEqual(report['verdict'], 'PASS')
            self.assertEqual(report['reachable_commits_checked'], 1)
            self.assertEqual(report['unreadable_count'], 0)

    def test_game_binaries_keys_and_environment_files_are_rejected(self):
        for name in ('fefw.exe', 'fefw_setup.exe', 'project/tools/prod.keys', 'project/.env', 'project/src/c_001.cpp'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                module.check_content(name, b'not public source')
        with self.assertRaises(ValueError):
            module.check_content('project/src/renamed.cpp', b'MZ-disguised-binary')

    def test_secret_like_strings_are_rejected_without_printing_values(self):
        payload = ('value=' + 'ghp_' + 'A' * 40).encode()
        with self.assertRaisesRegex(ValueError, 'Credential-like'):
            module.check_content('project/src/fixture.cpp', payload)

    def test_android_allows_reviewed_source_but_rejects_binaries_builds_and_paths(self):
        for name in ('android/README.md', 'android/build.py', 'android/AndroidManifest.xml',
                     'android/CMakeLists.txt', 'android/native/probe.cpp', 'android/res/values/styles.xml',
                     'android/runtime/README.md', 'android/runtime/include/nxrt/memory.h',
                     'android/runtime/src/memory_android.cpp', 'android/runtime/tests/test_memory.cpp',
                     'android/runtime/tests/test_runner.h', 'android/runtime/tests/test_runner.cpp',
                     'android/runtime/tests/test_framework.h',
                     'android/runtime/include/nxrt/module.h', 'android/runtime/include/nxrecomp/ctx.h',
                     'android/runtime/src/dispatch_android.cpp', 'android/runtime/src/session_android.cpp',
                     'android/runtime/tests/test_nso_android.cpp', 'android/runtime/tests/prepare_fixtures.py',
                     'android/runtime/tests/fixture.c', 'android/runtime/third_party/zbic/LICENSE',
                     'android/java/io/github/gregorioruiz98/fefw/MainActivity.java'):
            module.check_content(name, b'// synthetic original source\n')
        for name in ('android/game.apk', 'android/native/game.so', 'android/native/c_001.cpp',
                     'android/build/classes.dex', 'android/native/../secrets.cpp', 'android/romfs/file.cpp',
                     'android/runtime/romfs/file.cpp', 'android/runtime/src/c_001.cpp',
                      'android/runtime/src/game.so', 'android/runtime/keys/prod.keys',
                      'android/runtime/third_party/zbic/game.bin', 'android/runtime/tests/fixture_data.h',
                     'android/.env', 'android/native/key.p12'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                module.check_content(name, b'not source')
        for data in (b'MZ-disguised', b'\x7fELF\0binary'):
            with self.assertRaises(ValueError):
                module.check_content('android/native/probe.cpp', data)

    def test_changed_staged_source_invalidates_manifest(self):
        with self.scratch() as temp:
            root = Path(temp).resolve(); self.create(root)
            (root / 'README.md').write_text('changed\n', encoding='utf-8')
            self.run_git(root, 'add', 'README.md')
            with self.assertRaisesRegex(ValueError, 'manifest'):
                module.audit(root)

    def test_removed_forbidden_files_still_fail_history_audit(self):
        with self.scratch() as temp:
            root = Path(temp).resolve(); self.create(root)
            (root / 'private.nsp').write_text('synthetic forbidden artifact', encoding='utf-8')
            self.run_git(root, 'add', '--all')
            self.run_git(root, 'commit', '-m', 'Synthetic bad history')
            self.run_git(root, 'rm', 'private.nsp')
            self.stage_manifest(root)
            self.run_git(root, 'commit', '-m', 'Remove forbidden artifact')
            module.audit(root)
            with self.assertRaises(ValueError):
                module.audit(root, history=True)
