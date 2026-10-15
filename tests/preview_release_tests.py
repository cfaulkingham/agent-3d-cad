"""Offline release tests with real isolated Git repositories and inert assets."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('preview_release', REPO / 'packaging/preview_release.py')
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)
from release_manifest_tests import EXPECTED as SOURCE_EXPECTED, VERSION as SOURCE_VERSION

VERSION = "0.1.0-preview.2"
EXPECTED = {name.replace(SOURCE_VERSION, VERSION) for name in SOURCE_EXPECTED}


class GitFixture(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='cad-preview-release-')
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.repo = self.base / 'source'
        self.repo.mkdir()
        self.previous = Path.cwd()
        os.chdir(self.repo)
        self.addCleanup(os.chdir, self.previous)
        release.git('init', '--initial-branch=main')
        release.git('config', 'user.name', 'Release test')
        release.git('config', 'user.email', 'release-test@example.invalid')
        release.git('config', 'commit.gpgsign', 'false')
        Path('VERSION').write_text(VERSION + '\n')
        Path('source.cpp').write_text('inert test source\n')
        Path('packaging').mkdir()
        shutil.copyfile(REPO / 'packaging/release-manifest.py', Path('packaging/release-manifest.py'))
        release.git('add', '.')
        release.git('commit', '-m', 'Fixture')
        self.original = release.git('rev-parse', 'HEAD')
        self.remote = self.base / 'remote.git'
        release.git('init', '--bare', str(self.remote))
        release.git('remote', 'add', 'origin', str(self.remote))
        release.git('push', 'origin', 'main')
        self.bundle = self.base / 'source.bundle'

    def tags(self):
        return release.git('ls-remote', '--refs', 'origin', 'refs/tags/*')


class SourceTests(GitFixture):
    def test_reproducible_source_bundle_restores_in_shallow_checkout(self):
        first = release.prepare('push', 'refs/heads/main', '123', self.bundle)
        self.assertEqual(first['version'], '0.1.0-preview.123')
        self.assertEqual(first['publish'], 'true')
        self.assertEqual(first['parent'], self.original)
        self.assertEqual(release.git('diff', '--name-only', self.original, first['commit']), 'VERSION')
        self.assertEqual(release.git('rev-parse', 'main'), self.original)
        self.assertEqual(self.tags(), '')
        consumer = self.base / 'consumer'
        release.git('clone', '--depth=1', '--branch=main', self.remote.as_uri(), str(consumer))
        release.git('-C', str(consumer), 'bundle', 'verify', str(self.bundle))
        release.git('-C', str(consumer), 'fetch', str(self.bundle), 'HEAD')
        release.git('-C', str(consumer), 'checkout', '--detach', first['commit'])
        self.assertEqual((consumer / 'VERSION').read_text(), '0.1.0-preview.123\n')
        release.git('checkout', '--detach', self.original)
        second = release.prepare('push', 'refs/heads/main', '123', self.bundle)
        self.assertEqual(first, second)
        release.git('checkout', '--detach', self.original)
        third = release.prepare('push', 'refs/heads/main', '124', self.bundle)
        self.assertNotEqual(first['commit'], third['commit'])
        self.assertNotEqual(first['version'], third['version'])

    def test_shallow_ci_producer_can_export_prepared_source(self):
        producer = self.base / 'shallow-producer'
        release.git('clone', '--depth=1', '--branch=main', self.remote.as_uri(), str(producer))
        os.chdir(producer)
        result = release.prepare('push', 'refs/heads/main', '125', self.bundle)
        self.assertEqual(result['version'], '0.1.0-preview.125')
        self.assertEqual(release.git('rev-parse', '--is-shallow-repository'), 'true')
        release.git('bundle', 'verify', str(self.bundle))
        self.assertEqual(self.tags(), '')

    def test_pr_branch_manual_and_wrong_tag_never_publish(self):
        for event, ref in [('pull_request', 'refs/pull/7/merge'), ('push', 'refs/heads/feature'),
                           ('workflow_dispatch', 'refs/heads/main'), ('push', 'refs/tags/v0.1.0-preview.999')]:
            with self.subTest(event=event, ref=ref):
                result = release.prepare(event, ref, '123', self.bundle)
                self.assertEqual(result['publish'], 'false')
                self.assertEqual(result['commit'], self.original)
                self.assertFalse(self.bundle.exists())

    def test_matching_preview_tag_preserves_source(self):
        result = release.prepare('push', 'refs/tags/v' + VERSION, '123', self.bundle)
        self.assertEqual(result['publish'], 'true')
        self.assertEqual(result['commit'], self.original)
        self.assertFalse(self.bundle.exists())

    def test_same_version_needs_no_bundle(self):
        result = release.prepare('push', 'refs/heads/main', VERSION.rsplit('.', 1)[1], self.bundle)
        self.assertEqual(result['commit'], self.original)
        self.assertEqual(result['publish'], 'true')
        self.assertFalse(self.bundle.exists())

    def test_stable_release_is_not_automated(self):
        Path('VERSION').write_text('1.0.0\n')
        release.git('add', 'VERSION')
        release.git('commit', '-m', 'Stable fixture')
        self.assertEqual(release.prepare('push', 'refs/heads/main', '123', self.bundle)['publish'], 'false')
        self.assertFalse(self.bundle.exists())

    def test_dirty_source_and_invalid_counter_rejected(self):
        with self.assertRaisesRegex(ValueError, 'run number'):
            release.prepare('push', 'refs/heads/main', '1\ninjection', self.bundle)
        Path('source.cpp').write_text('dirty\n')
        with self.assertRaisesRegex(ValueError, 'clean'):
            release.prepare('push', 'refs/heads/main', '123', self.bundle)
        self.assertFalse(self.bundle.exists())


class PublicationTests(GitFixture):
    def setUp(self):
        super().setUp()
        self.assets = self.base / 'assets'
        self.assets.mkdir()
        for name in EXPECTED:
            (self.assets / name).write_bytes(b'inert package fixture\n' + name.encode())
        self.acceptance = self.base / 'acceptance.json'
        self.report = {'status': 'passed', 'source_commit': self.original,
            'archives': {p.name: release.digest(p) for p in self.assets.iterdir() if '-universal.' in p.name}}
        self.acceptance.write_text(json.dumps(self.report))
        self.state, self.calls, self.fail_upload_after = None, [], None
        patcher = mock.patch.object(release, 'gh', self.fake_gh)
        patcher.start()
        self.addCleanup(patcher.stop)

    def fake_gh(self, *args):
        self.calls.append(args)
        if args[0] == 'api':
            return json.dumps([[self.state] if self.state else []])
        if args[:2] == ('release', 'create'):
            self.state = {'target_commitish': self.original, 'prerelease': True, 'draft': True,
                          'tag_name': 'v' + VERSION, 'html_url': 'https://example.invalid/release', 'assets': []}
        elif args[:2] == ('release', 'upload'):
            for name in args[3:args.index('--repo')]:
                path = Path(name)
                self.state['assets'].append({'name': path.name, 'state': 'uploaded',
                    'size': path.stat().st_size, 'digest': 'sha256:' + release.digest(path)})
                if self.fail_upload_after and len(self.state['assets']) == self.fail_upload_after:
                    raise RuntimeError('Simulated interrupted upload')
        elif args[:2] == ('release', 'edit'):
            self.assertEqual(len(self.state['assets']), 19)
            self.assertIn('--draft=false', args)
            self.state['draft'] = False
        else:
            self.fail('Unexpected GitHub operation: ' + str(args))
        return ''

    def publish(self):
        release.publish('fixture/repo', self.original, self.original, '1234', self.assets, self.acceptance)

    def test_complete_release_and_repeat_is_read_only(self):
        self.publish()
        self.assertFalse(self.state['draft'])
        self.assertEqual(len(self.state['assets']), 19)
        self.assertIn(self.original, self.tags())
        self.calls.clear()
        self.publish()
        self.assertTrue(all(call[0] == 'api' for call in self.calls))

    def test_partial_upload_resumes_without_replacing_assets(self):
        self.fail_upload_after = 5
        with self.assertRaisesRegex(RuntimeError, 'interrupted upload'):
            self.publish()
        self.assertTrue(self.state['draft'])
        previous = copy.deepcopy(self.state['assets'])
        self.fail_upload_after = None
        self.calls.clear()
        self.publish()
        uploads = [call for call in self.calls if call[:2] == ('release', 'upload')]
        self.assertEqual(len(uploads), 1)
        self.assertEqual(len(uploads[0][3:uploads[0].index('--repo')]), 14)
        self.assertEqual(self.state['assets'][:5], previous)
        self.assertFalse(self.state['draft'])

    def test_missing_native_asset_never_creates_tag(self):
        next(p for p in self.assets.iterdir() if '-Linux-x64.tar.gz' in p.name).unlink()
        with self.assertRaises(subprocess.CalledProcessError):
            self.publish()
        self.assertEqual(self.tags(), '')
        self.assertEqual(self.calls, [])

    def test_wrong_source_or_changed_universal_never_creates_tag(self):
        self.report['source_commit'] = '0' * 40
        self.acceptance.write_text(json.dumps(self.report))
        with self.assertRaisesRegex(ValueError, 'acceptance'):
            self.publish()
        self.report['source_commit'] = self.original
        self.acceptance.write_text(json.dumps(self.report))
        (self.assets / next(iter(self.report['archives']))).write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'accepted bytes'):
            self.publish()
        self.assertEqual(self.tags(), '')
        self.assertEqual(self.calls, [])

    def test_existing_tag_cannot_move(self):
        Path('source.cpp').write_text('different source\n')
        release.git('add', 'source.cpp')
        release.git('commit', '-m', 'Other source')
        other = release.git('rev-parse', 'HEAD')
        release.git('push', 'origin', other + ':refs/tags/v' + VERSION)
        release.git('checkout', '--detach', self.original)
        with self.assertRaisesRegex(ValueError, 'different source'):
            self.publish()
        self.assertIn(other, self.tags())
        self.assertEqual(self.calls, [])

    def test_existing_asset_cannot_be_overwritten(self):
        self.publish()
        self.state['assets'][0]['digest'] = 'sha256:' + '0' * 64
        self.calls.clear()
        with self.assertRaisesRegex(ValueError, 'Uploaded bytes differ'):
            self.publish()
        self.assertTrue(all(call[0] == 'api' for call in self.calls))

    def test_bad_upload_digest_keeps_release_draft(self):
        original_gh = self.fake_gh
        def corrupt_upload(*args):
            result = original_gh(*args)
            if args[:2] == ('release', 'upload'):
                self.state['assets'][0]['digest'] = 'sha256:' + '0' * 64
            return result
        with mock.patch.object(release, 'gh', corrupt_upload):
            with self.assertRaisesRegex(ValueError, 'Uploaded bytes differ'):
                self.publish()
        self.assertTrue(self.state['draft'])
        self.assertFalse(any(call[:2] == ('release', 'edit') for call in self.calls))

    def test_incomplete_published_release_cannot_be_mutated(self):
        self.publish()
        self.state['assets'].pop()
        self.calls.clear()
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            self.publish()
        self.assertTrue(all(call[0] == 'api' for call in self.calls))


class WorkflowTests(unittest.TestCase):
    def test_prepared_source_and_all_release_gates(self):
        workflow = (REPO / '.github/workflows/ci.yml').read_text()
        self.assertEqual(workflow.count('uses: ./.github/actions/restore-ci-source'), 5)
        self.assertEqual(workflow.count('TEST_SOURCE_COMMIT: ' + chr(36) + '{{ needs.source.outputs.commit }}'), 2)
        self.assertNotIn('TEST_SOURCE_COMMIT: ' + chr(36) + '{{ github.sha }}', workflow)
        self.assertIn('needs: [source, native, macos-universal-acceptance]', workflow)
        self.assertIn("if: needs.source.outputs.publish == 'true'", workflow)
        self.assertIn('--acceptance universal-acceptance/universal-acceptance.json', workflow)
        self.assertEqual(workflow.count('contents: write'), 1)


if __name__ == '__main__':
    unittest.main()
