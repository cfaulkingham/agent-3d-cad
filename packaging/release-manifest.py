"""Require the complete release matrix before generating download checksums."""
import hashlib
from pathlib import Path
import sys

repo = Path(__file__).resolve().parent.parent
version = (repo / 'VERSION').read_text().strip()
directory = Path(sys.argv[1])
expected = {'install.sh', 'install.ps1'}
for system, arch in [('Darwin', 'arm64'), ('Darwin', 'x64'), ('Linux', 'arm64'), ('Linux', 'x64'), ('Windows', 'x64')]:
    extension = 'zip' if system == 'Windows' else 'tar.gz'
    for package in ['agent-3d-cad', 'agent-3d-cad-desktop']:
        expected.add(f'{package}-{version}-{system}-{arch}.{extension}')
    if system != 'Linux':
        expected.add(f'agent-3d-cad-{version}-{system}-{arch}.mcpb')
actual = {p.name for p in directory.iterdir() if p.is_file() and p.name != 'SHA256SUMS'}
if actual != expected:
    raise SystemExit(f'Incomplete/unexpected release assets. Missing: {expected - actual}; extra: {actual - expected}')
lines = []
for name in sorted(expected):
    with (directory / name).open('rb') as file:
        lines.append(f"{hashlib.file_digest(file, 'sha256').hexdigest()}  {name}\n")
(directory / 'SHA256SUMS').write_text(''.join(lines))
print(f'{len(expected)} release assets checksummed for {version}')
