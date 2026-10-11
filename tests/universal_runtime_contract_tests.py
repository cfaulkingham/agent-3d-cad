"""Nonexecuting adversarial fixtures for the dual-host evidence gate.

These synthetic reports test rejection logic, never platform runtime acceptance.
"""
import argparse
from contextlib import redirect_stdout
import copy
import io
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

import universal_package_runtime as runtime


class EvidenceGateTests(unittest.TestCase):
    def reports(self):
        result = []
        for architecture in ('arm64', 'x64'):
            build = ('a' if architecture == 'arm64' else 'b') * 64 + '-Release'
            process = {'cpu_type': runtime.CPUS[architecture]}
            package = {'executable_sha256': 'c' * 64, 'geometry_cache_keys': [runtime.expected_cache_key(build)],
                       'compiled_cache_identity': build, 'services': [process] * 4, 'workers': [process],
                       'source_identity': {'format_version': 1, 'algorithm': 'sha256-file-list-v1', 'sha256': 'd' * 64,
                                           'manifest': 'share/agent-3d-cad/source-inputs.sha256'}}
            result.append({'architecture': architecture, 'checks': 100, 'system': 'SYNTHETIC fixture',
                           'source_commit': 'e' * 40, 'universal_acceptance': True, 'translated_process': False,
                           'archives': {'native.tar.gz': '1'*64, 'plugin.zip': '2'*64, 'claude.mcpb': '3'*64},
                           'packages': {kind:copy.deepcopy(package) for kind in ('native','plugin','claude')}})
        return result

    def verify(self, reports):
        with tempfile.TemporaryDirectory(prefix='cad-universal-evidence-fixture-') as directory:
            root = Path(directory)
            paths = [root/'arm64.json', root/'x64.json']
            for path, report in zip(paths, reports):
                path.write_text(json.dumps(report))
            args = argparse.Namespace(reports=paths, source='e'*40, report=root/'result.json')
            with redirect_stdout(io.StringIO()):
                runtime.compare(args)
            return json.loads(args.report.read_text())

    def test_original_cmake_architecture_spellings(self):
        for original, expected in [('x86_64', 'x64'), ('AMD64', 'x64'), ('arm64', 'arm64'), ('aarch64', 'arm64')]:
            self.assertEqual(runtime.normalized_architecture(original), expected)
        self.assertIsNone(runtime.normalized_architecture('arm64e'))
        self.assertIsNone(runtime.normalized_architecture('i386'))

    def test_matching_synthetic_gate_fixture(self):
        self.assertEqual(self.verify(self.reports())['status'], 'passed')

    def test_reject_different_archive_bytes(self):
        reports=self.reports();reports[1]['archives']['plugin.zip']='4'*64
        with self.assertRaisesRegex(AssertionError, 'different archives'):
            self.verify(reports)

    def test_reject_thin_development_evidence(self):
        reports=self.reports();reports[0]['universal_acceptance']=False
        with self.assertRaisesRegex(AssertionError, 'Thin/stale fixture'):
            self.verify(reports)

    def test_reject_different_source_bytes(self):
        reports=self.reports();reports[1]['packages']['native']['source_identity']['sha256']='f'*64
        with self.assertRaisesRegex(AssertionError, 'Source bytes differ'):
            self.verify(reports)

    def test_reject_wrong_actual_worker_architecture(self):
        reports=self.reports();reports[0]['packages']['native']['workers']=[{'cpu_type':runtime.CPUS['x64']}]
        with self.assertRaisesRegex(AssertionError, 'Wrong CPU process'):
            self.verify(reports)

    def test_reject_missing_actual_worker(self):
        reports=self.reports();reports[1]['packages']['claude']['workers']=[]
        with self.assertRaisesRegex(AssertionError, 'Missing process observations'):
            self.verify(reports)

    def test_reject_shared_cross_architecture_cache_identity(self):
        reports=self.reports();reports[1]['packages']['plugin']['geometry_cache_keys']=reports[0]['packages']['plugin']['geometry_cache_keys']
        with self.assertRaisesRegex(AssertionError, 'cache identities were not isolated'):
            self.verify(reports)

    def test_reject_second_arm_runner_disguised_as_dual_host(self):
        reports=self.reports();reports[1]['architecture']='arm64'
        with self.assertRaisesRegex(AssertionError, 'both actual CPU reports'):
            self.verify(reports)

    def test_reject_rosetta_instead_of_actual_intel(self):
        reports=self.reports();reports[1]['translated_process']=True
        with self.assertRaisesRegex(AssertionError, 'Translated process'):
            self.verify(reports)

    def test_reject_zip_traversal(self):
        with tempfile.TemporaryDirectory(prefix='cad-universal-archive-fixture-') as directory:
            root=Path(directory);archive=root/'malformed.zip'
            with zipfile.ZipFile(archive,'w') as package:
                package.writestr('../outside','must not be extracted')
            with self.assertRaisesRegex(AssertionError, 'Archive path escaped'):
                runtime.extract(archive,root/'extracted')
            self.assertFalse((root/'outside').exists())


if __name__ == '__main__':
    unittest.main()
