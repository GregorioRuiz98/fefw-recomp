"""Offline renderer acquisition regressions: no game files or network required."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('bootstrap', Path(__file__).resolve().parents[1] / 'tools/bootstrap.py')
bootstrap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bootstrap)


def git(folder, *args):
    return subprocess.check_output(['git', '-C', str(folder), *args], text=True, stderr=subprocess.PIPE).strip()


class BootstrapTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.origin = self.root / 'origin'
        self.origin.mkdir()
        git(self.origin, 'init', '-q')
        (self.origin / 'fixture.txt').write_text('original\n')
        (self.origin / 'stability.txt').write_text('original\n')
        git(self.origin, 'add', 'fixture.txt', 'stability.txt')
        git(self.origin, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-qm', 'fixture')
        self.pin = git(self.origin, 'rev-parse', 'HEAD')
        self.work = self.root / 'work'
        self.work.mkdir()
        self.engine = self.work / 'renderer-engine'
        project = self.root / 'project'
        (project / 'patches').mkdir(parents=True)
        (project / 'patches/renderer.patch').write_text(
            'diff --git a/fixture.txt b/fixture.txt\n--- a/fixture.txt\n+++ b/fixture.txt\n'
            '@@ -1 +1 @@\n-original\n+adapted\n')
        (project / 'patches/renderer-stability.patch').write_text(
            'diff --git a/stability.txt b/stability.txt\n--- a/stability.txt\n+++ b/stability.txt\n'
            '@@ -1 +1 @@\n-original\n+safe\n')
        for name, value in [('RUNTIME_PIN', self.pin), ('PROJECT', project)]:
            change = patch.object(bootstrap, name, value)
            change.start()
            self.addCleanup(change.stop)

    def test_missing_origin_falls_back_to_exact_commit_and_applies_patch(self):
        with patch.object(bootstrap, 'RUNTIME_URLS', (str(self.root / 'missing'), str(self.origin))):
            bootstrap.prepare_runtime(self.work)
        self.assertEqual(git(self.engine, 'rev-parse', 'HEAD'), self.pin)
        self.assertEqual((self.engine / 'fixture.txt').read_text(), 'adapted\n')
        self.assertEqual(list(self.work.glob('.renderer-download-*')), [])

    def test_origin_without_pin_is_skipped(self):
        other = self.root / 'other'
        other.mkdir()
        git(other, 'init', '-q')
        with patch.object(bootstrap, 'RUNTIME_URLS', (str(other), str(self.origin))):
            bootstrap.prepare_runtime(self.work)
        self.assertEqual(git(self.engine, 'rev-parse', 'HEAD'), self.pin)

    def test_all_sources_fail_without_partial_install(self):
        with patch.object(bootstrap, 'RUNTIME_URLS', (str(self.root / 'missing'),)):
            with self.assertRaisesRegex(RuntimeError, 'Could not obtain the pinned renderer'):
                bootstrap.prepare_runtime(self.work)
        self.assertEqual(list(self.work.iterdir()), [])

    def test_existing_patched_checkout_and_local_files_are_preserved(self):
        with patch.object(bootstrap, 'RUNTIME_URLS', (str(self.origin),)):
            bootstrap.prepare_runtime(self.work)
        marker = self.engine / 'local-settings.txt'
        marker.write_text('preserve me\n')
        with patch.object(bootstrap, 'clone_runtime', side_effect=AssertionError('must not clone')):
            bootstrap.prepare_runtime(self.work)
        self.assertEqual(marker.read_text(), 'preserve me\n')
        self.assertEqual((self.engine / 'fixture.txt').read_text(), 'adapted\n')

    def test_timed_out_source_does_not_leave_a_partial_checkout(self):
        with patch.object(bootstrap, 'RUNTIME_URLS', ('unused',)), patch.object(
                bootstrap, 'run', side_effect=subprocess.TimeoutExpired('git', 180)):
            with self.assertRaisesRegex(RuntimeError, 'Could not obtain the pinned renderer'):
                bootstrap.prepare_runtime(self.work)
        self.assertEqual(list(self.work.iterdir()), [])

    def test_previously_adapted_renderer_receives_followup_without_reset(self):
        with patch.object(bootstrap, 'RUNTIME_URLS', (str(self.origin),)):
            bootstrap.clone_runtime(self.engine)
        git(self.engine, 'apply', str(bootstrap.PROJECT / 'patches/renderer.patch'))
        (self.engine / 'local.txt').write_text('keep')
        bootstrap.prepare_runtime(self.work)
        self.assertEqual((self.engine / 'fixture.txt').read_text(), 'adapted\n')
        self.assertEqual((self.engine / 'stability.txt').read_text(), 'safe\n')
        self.assertEqual((self.engine / 'local.txt').read_text(), 'keep')

    def test_conflicting_followup_preserves_user_changes(self):
        with patch.object(bootstrap, 'RUNTIME_URLS', (str(self.origin),)):
            bootstrap.clone_runtime(self.engine)
        git(self.engine, 'apply', str(bootstrap.PROJECT / 'patches/renderer.patch'))
        (self.engine / 'stability.txt').write_text('local override\n')
        with self.assertRaisesRegex(RuntimeError, 'conflict with renderer-stability.patch'):
            bootstrap.prepare_runtime(self.work)
        self.assertEqual((self.engine / 'stability.txt').read_text(), 'local override\n')
        self.assertEqual((self.engine / 'fixture.txt').read_text(), 'adapted\n')

    def test_existing_wrong_revision_is_never_reset(self):
        with patch.object(bootstrap, 'RUNTIME_URLS', (str(self.origin),)):
            bootstrap.prepare_runtime(self.work)
        before = (self.engine / 'fixture.txt').read_bytes()
        with patch.object(bootstrap, 'RUNTIME_PIN', '0' * 40):
            with self.assertRaisesRegex(RuntimeError, 'existing checkout was preserved'):
                bootstrap.prepare_runtime(self.work)
        self.assertEqual((self.engine / 'fixture.txt').read_bytes(), before)
        self.assertEqual(git(self.engine, 'rev-parse', 'HEAD'), self.pin)


if __name__ == '__main__':
    unittest.main()
