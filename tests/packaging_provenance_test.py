"""Packaging must reject incomplete inventories before creating artifacts."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

repo = Path(__file__).resolve().parent.parent
version = (repo / 'VERSION').read_text().strip()
with tempfile.TemporaryDirectory(prefix='cad-packaging-provenance-') as temp:
    root = Path(temp)
    bundle = root / 'bundle'
    binary = bundle / 'bin/agent-3d-cad'
    binary.parent.mkdir(parents=True)
    binary.write_bytes(b'packaging inventory fixture\n')
    binary.chmod(0o755)
    manifest = bundle / 'share/agent-3d-cad/provenance.json'
    manifest.parent.mkdir(parents=True)
    entry = {'path': 'bin/agent-3d-cad', 'sha256': hashlib.sha256(binary.read_bytes()).hexdigest()}

    def save(files):
        manifest.write_text(json.dumps({'project_version': version, 'system': 'Darwin', 'files': files}))

    def run(helper, output):
        args = [sys.executable, str(repo / 'packaging' / helper), str(bundle), str(output)]
        if helper == 'package-desktop.py':
            args.append(str(binary))
        return subprocess.run(args, capture_output=True, text=True, encoding='utf-8')

    for case, files in [('empty', []), ('duplicate', [entry, entry]),
                        ('missing', [entry, {'path': 'missing', 'sha256': '0' * 64}])]:
        save(files)
        for helper in ['make-mcpb.py', 'package-desktop.py']:
            output = root / (case + helper)
            result = run(helper, output)
            assert result.returncode != 0 and 'cover every file exactly once' in result.stderr, result.stderr
            assert not output.exists()
    save([entry])
    (bundle / 'unlisted').write_text('unlisted bytes')
    for helper in ['make-mcpb.py', 'package-desktop.py']:
        output = root / ('extra-' + helper)
        result = run(helper, output)
        assert result.returncode != 0 and 'cover every file exactly once' in result.stderr, result.stderr
        assert not output.exists()
    (bundle / 'unlisted').unlink()
    output = root / 'valid.mcpb'
    result = run('make-mcpb.py', output)
    assert result.returncode == 0, result.stderr
    with zipfile.ZipFile(output) as archive:
        assert archive.read('bin/agent-3d-cad') == binary.read_bytes()
        assert json.loads(archive.read('share/agent-3d-cad/provenance.json'))['files'] == [entry]

print('Packaging provenance: empty, duplicate, missing and unlisted inventories rejected by both packagers; complete MCPB accepted.')
