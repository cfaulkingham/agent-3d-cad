"""Packaging rejects stale native/desktop frontends before copying a bundle."""
import hashlib
import json
import platform
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
from unittest.mock import patch

repo = Path(__file__).resolve().parent.parent
native_html = Path(sys.argv[1]).read_bytes()
start = native_html.index(b'<meta http-equiv="Content-Security-Policy"')
end = native_html.index(b'>', start) + 1
desktop_html = native_html[:start] + native_html[end:]


class AssetsValidated(Exception):
    pass


def check(native_hash, compiled_html, expected_error=None, unavailable=False):
    with tempfile.TemporaryDirectory(prefix='cad-desktop-assets-') as temporary:
        root = Path(temporary).resolve()
        native = root / 'native'
        executable = native / 'bin/agent-3d-cad'
        executable.parent.mkdir(parents=True)
        executable.write_bytes(b'native inventory fixture')
        manifest = native / 'share/agent-3d-cad/provenance.json'
        manifest.parent.mkdir(parents=True)
        manifest.write_text(json.dumps({
            'project_version': (repo / 'VERSION').read_text().strip(),
            'system': platform.system(), 'viewer_app_sha256': native_hash,
            'files': [{'path': 'bin/agent-3d-cad',
                       'sha256': hashlib.sha256(executable.read_bytes()).hexdigest()}]
        }))
        binary = root / 'desktop-binary'
        output = root / 'output'

        def snapshot(command, **options):
            assert command == [str(binary), '--print-viewer-html']
            assert options['capture_output'] and options['check'] and options['timeout'] == 15
            if unavailable:
                raise subprocess.CalledProcessError(1, command)
            return subprocess.CompletedProcess(command, 0, stdout=compiled_html)

        with patch.object(sys, 'argv', ['package-desktop.py', str(native), str(output), str(binary)]), \
                patch('subprocess.run', side_effect=snapshot), \
                patch('shutil.copytree', side_effect=AssetsValidated):
            try:
                runpy.run_path(str(repo / 'packaging/package-desktop.py'))
            except AssetsValidated:
                assert expected_error is None, 'Stale assets reached bundle publication'
            except SystemExit as error:
                assert expected_error is not None, error
                assert expected_error in str(error), error
            else:
                raise AssertionError('Packaging unexpectedly completed')
        assert not output.exists(), 'Asset rejection must precede bundle writes'


current_hash = hashlib.sha256(native_html).hexdigest()
check(current_hash, desktop_html)
check('0' * 64, desktop_html, 'Native bundle viewer differs')
check(current_hash, b'<html>Old desktop UI</html>', 'Desktop and embedded viewers differ')
check(current_hash, b'', 'Cannot verify desktop viewer', unavailable=True)
print('4 desktop asset checks passed: matching UI, stale native, stale desktop, unavailable snapshot.')
