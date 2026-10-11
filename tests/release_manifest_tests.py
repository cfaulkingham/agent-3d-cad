"""Check future draft-release inventory/checksums using inert fixture bytes.

No fixture is a runnable package, and this suite never creates a tag, release,
network request or host installation. Real universal runtime remains a CI gate.
"""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parent.parent
VERSION = (REPO / 'VERSION').read_text().strip()
# Spell out the published preview contract independently of the generator.
LEGACY = {
    'install.sh', 'install.ps1',
    f'agent-3d-cad-{VERSION}-Darwin-arm64.tar.gz',
    f'agent-3d-cad-{VERSION}-Darwin-x64.tar.gz',
    f'agent-3d-cad-{VERSION}-Linux-arm64.tar.gz',
    f'agent-3d-cad-{VERSION}-Linux-x64.tar.gz',
    f'agent-3d-cad-{VERSION}-Windows-x64.zip',
    f'agent-3d-cad-desktop-{VERSION}-Darwin-arm64.tar.gz',
    f'agent-3d-cad-desktop-{VERSION}-Darwin-x64.tar.gz',
    f'agent-3d-cad-desktop-{VERSION}-Linux-arm64.tar.gz',
    f'agent-3d-cad-desktop-{VERSION}-Linux-x64.tar.gz',
    f'agent-3d-cad-desktop-{VERSION}-Windows-x64.zip',
    f'agent-3d-cad-{VERSION}-Darwin-arm64.mcpb',
    f'agent-3d-cad-{VERSION}-Darwin-x64.mcpb',
    f'agent-3d-cad-{VERSION}-Windows-x64.mcpb',
}
UNIVERSAL = {
    f'agent-3d-cad-{VERSION}-Darwin-universal.tar.gz',
    f'agent-cad-plugin-{VERSION}-Darwin-universal.zip',
    f'agent-cad-claude-{VERSION}-Darwin-universal.mcpb',
}
EXPECTED = LEGACY | UNIVERSAL


class ReleaseManifestTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='cad-release-manifest-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.assets = self.root / 'draft assets with spaces'
        self.assets.mkdir()
        for name in EXPECTED:
            (self.assets / name).write_bytes(b'Inert release fixture\x00\xff\n' + name.encode())

    def invoke(self):
        return subprocess.run([sys.executable, str(REPO / 'packaging/release-manifest.py'), str(self.assets)],
                              text=True, capture_output=True, timeout=15)

    def assert_rejected(self, label):
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0, label)
        self.assertIn(label, result.stderr)
        self.assertFalse((self.assets / 'SHA256SUMS').exists())

    def test_complete_matrix_has_all_exact_checksums(self):
        self.assertEqual((len(LEGACY), len(UNIVERSAL)), (15, 3))
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('18 release assets checksummed', result.stdout)
        expected = ''.join(hashlib.sha256((self.assets / name).read_bytes()).hexdigest() + '  ' + name + '\n'
                           for name in sorted(EXPECTED))
        self.assertEqual((self.assets / 'SHA256SUMS').read_text(), expected)
        self.assertEqual({p.name for p in self.assets.iterdir()}, EXPECTED | {'SHA256SUMS'})

    def test_each_legacy_and_universal_asset_is_required(self):
        for name in sorted(EXPECTED):
            with self.subTest(missing=name):
                path = self.assets / name
                contents = path.read_bytes()
                path.unlink()
                self.assert_rejected(name)
                path.write_bytes(contents)

    def test_unexpected_assets_and_source_manifest_are_rejected(self):
        for name in ('source-inputs.sha256', '.DS_Store', 'agent-3d-cad-wrong-version-Darwin-universal.tar.gz'):
            with self.subTest(extra=name):
                path = self.assets / name
                path.write_bytes(b'Unexpected upload')
                self.assert_rejected(name)
                path.unlink()

    def test_release_glob_cannot_include_a_directory(self):
        # gh receives release/*, so directories cannot silently escape inventory.
        extra = self.assets / 'universal-release-inputs'
        extra.mkdir()
        (extra / 'source-inputs.sha256').write_bytes(b'CI evidence')
        self.assert_rejected(extra.name)

    def test_symlinked_asset_is_not_a_regular_release_file(self):
        name = sorted(UNIVERSAL)[0]
        target = self.root / 'outside-package'
        target.write_bytes(b'External bytes')
        path = self.assets / name
        path.unlink()
        try:
            path.symlink_to(target)
        except OSError as error:
            self.skipTest(f'Host does not permit fixture symlinks: {error}')
        self.assert_rejected(name)

    def test_rechecksums_are_deterministic_and_capture_changed_bytes(self):
        self.assertEqual(self.invoke().returncode, 0)
        checksum = self.assets / 'SHA256SUMS'
        first = checksum.read_bytes()
        self.assertEqual(self.invoke().returncode, 0)
        self.assertEqual(checksum.read_bytes(), first)
        name = sorted(UNIVERSAL)[0]
        (self.assets / name).write_bytes(b'Changed inert archive payload')
        self.assertEqual(self.invoke().returncode, 0)
        self.assertNotEqual(checksum.read_bytes(), first)
        self.assertIn(hashlib.sha256((self.assets / name).read_bytes()).hexdigest() + '  ' + name,
                      checksum.read_text())

    def test_invalid_inventory_preserves_existing_checksum_file(self):
        checksum = self.assets / 'SHA256SUMS'
        checksum.write_bytes(b'Previous checksum evidence\n')
        (self.assets / sorted(UNIVERSAL)[0]).unlink()
        self.assertNotEqual(self.invoke().returncode, 0)
        self.assertEqual(checksum.read_bytes(), b'Previous checksum evidence\n')


if __name__ == '__main__':
    unittest.main()
