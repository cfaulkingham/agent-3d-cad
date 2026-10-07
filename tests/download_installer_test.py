"""Offline installer tests: exercise the real script with fixture downloads."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile

repo = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='cad-download-installer-') as temp:
    root = Path(temp)
    mock = root / 'commands'
    mock.mkdir()
    fixture = root / 'downloads'
    fixture.mkdir()
    name = 'agent-3d-cad-desktop-0.1.0-preview.1-Linux-x64'
    bundle = root / name
    (bundle / 'bin').mkdir(parents=True)
    (bundle / 'bin/agent-3d-cad').write_text('#!/bin/sh\nexit 0\n')
    (bundle / 'bin/agent-3d-cad').chmod(0o755)
    archive = fixture / (name + '.tar.gz')
    with tarfile.open(archive, 'w:gz') as tar:
        tar.add(bundle, arcname=name)
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    sums = fixture / 'SHA256SUMS'
    sums.write_text(f'{checksum}  {archive.name}\n')
    commands = {
        'uname': '#!/bin/sh\ncase "$1" in -s) echo Linux;; -m) echo x86_64;; esac\n',
        'getconf': '#!/bin/sh\necho "glibc 2.39"\n',
        'curl': f'''#!{sys.executable}
import sys,shutil
from pathlib import Path
args=sys.argv[1:]
assert '--fail' in args and '--proto' in args and '--proto-redir' in args
url=next(a for a in args if a.startswith('https://'))
assert url.startswith('https://github.com/cfaulkingham/agent-3d-cad/releases/download/v0.1.0-preview.1/')
shutil.copyfile(Path({str(fixture)!r}) / url.rsplit('/',1)[1], args[args.index('--output')+1])
'''
    }
    for key, content in commands.items():
        (mock / key).write_text(content)
        (mock / key).chmod(0o755)
    env = dict(os.environ, PATH=str(mock) + os.pathsep + os.environ['PATH'])
    prefix = root / 'installed versions'
    def run(*args):
        return subprocess.run(['bash', str(repo / 'install.sh'), *args], env=env, capture_output=True, text=True, errors='replace')
    args = ('0.1.0-preview.1', '--prefix', str(prefix))
    good = run(*args)
    assert good.returncode == 0, good.stderr
    installed = prefix / name / 'bin/agent-3d-cad'
    assert installed.is_file(), (good.stdout, good.stderr, list(prefix.glob('**/*')))
    assert run(*args).returncode != 0, 'must not overwrite an installed version'
    sums.write_text('0' * 64 + f'  {archive.name}\n')
    assert run('0.1.0-preview.1', '--prefix', str(root / 'corrupt')).returncode != 0
    assert not (root / 'corrupt').exists(), 'checksum failure must not publish an installation'
    assert installed.read_text() == '#!/bin/sh\nexit 0\n'
    sums.write_text(f'{checksum}  {archive.name}\n{checksum}  {archive.name}\n')
    assert run('0.1.0-preview.1', '--prefix', str(root / 'duplicate')).returncode != 0
    assert run('../bad').returncode != 0
    assert not (prefix / '.install-lock').exists()
print('Download installer: verified install, paths with spaces, no overwrite, corrupt/duplicate checksum rejection, invalid version, cleanup passed.')
