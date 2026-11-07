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
