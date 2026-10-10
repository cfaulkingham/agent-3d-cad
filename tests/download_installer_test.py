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
        'uname': '#!/bin/sh\ncase "$1" in -s) echo "${FIXTURE_SYSTEM:-Linux}";; -m) echo "${FIXTURE_CPU:-x86_64}";; esac\n',
        'getconf': '#!/bin/sh\necho "${FIXTURE_GLIBC:-glibc 2.39}"\n',
        'sw_vers': '#!/bin/sh\necho "${FIXTURE_MACOS:-15.0}"\n',
        'sysctl': '#!/bin/sh\n[ "${FIXTURE_TRANSLATED:-0}" = error ] && exit 1\necho "${FIXTURE_TRANSLATED:-0}"\n',
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
    # Real installer, synthetic downloads: cover all POSIX release architectures
    # and aliases without executing a foreign binary or contacting the network.
    selection_checks = 0
    for system, cpu, translated, expected in [
        ('Linux', 'x86_64', '0', 'x64'), ('Linux', 'amd64', '0', 'x64'),
        ('Linux', 'arm64', '0', 'arm64'), ('Linux', 'aarch64', '0', 'arm64'),
        ('Darwin', 'arm64', '0', 'arm64'), ('Darwin', 'x86_64', '0', 'x64'),
        ('Darwin', 'x86_64', '1', 'arm64'), ('Darwin', 'x86_64', 'error', 'x64')]:
        env.update(FIXTURE_SYSTEM=system, FIXTURE_CPU=cpu, FIXTURE_TRANSLATED=translated)
        for core in (False, True):
            selected = f'agent-3d-cad{"" if core else "-desktop"}-0.1.0-preview.1-{system}-{expected}'
            selected_archive = fixture / (selected + '.tar.gz')
            with tarfile.open(selected_archive, 'w:gz') as tar:
                tar.add(bundle, arcname=selected)
            sums.write_text(f'{hashlib.sha256(selected_archive.read_bytes()).hexdigest()}  {selected_archive.name}\n')
            target = root / f'selection-{selection_checks}'
            result = run('0.1.0-preview.1', '--prefix', str(target), *(['--core'] if core else []))
            assert result.returncode == 0, result.stderr
            assert (target / selected / 'bin/agent-3d-cad').is_file(), result.stdout
            selection_checks += 1
    for values, diagnostic in [
        ({'FIXTURE_SYSTEM':'Linux','FIXTURE_CPU':'riscv64'}, 'Only arm64 and x64'),
        ({'FIXTURE_SYSTEM':'Darwin','FIXTURE_CPU':'arm64','FIXTURE_MACOS':'14.7'}, 'macOS 15'),
        ({'FIXTURE_SYSTEM':'Linux','FIXTURE_CPU':'x86_64','FIXTURE_GLIBC':'glibc 2.38'}, 'glibc 2.39'),
        ({'FIXTURE_SYSTEM':'FreeBSD','FIXTURE_CPU':'x86_64'}, 'unsupported')]:
        for key in ('FIXTURE_SYSTEM','FIXTURE_CPU','FIXTURE_MACOS','FIXTURE_GLIBC','FIXTURE_TRANSLATED'):
            env.pop(key, None)
        env.update(values)
        target = root / f'rejected-{selection_checks}'
        result = run('0.1.0-preview.1', '--prefix', str(target))
        assert result.returncode != 0 and diagnostic in result.stderr, (result.stdout, result.stderr)
        assert not target.exists()
        selection_checks += 1
print(f'Download installer: {selection_checks} architecture/baseline selections plus verified install, paths with spaces, no overwrite, corrupt/duplicate checksum rejection, invalid version and cleanup passed.')
