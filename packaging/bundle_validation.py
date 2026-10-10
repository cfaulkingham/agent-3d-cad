"""Developer-side native bundle verification shared by plugin packagers.

No runtime dependency: packaged users execute only the native service.
"""
import hashlib
import json
from pathlib import Path
import struct

MANIFEST = Path('share/agent-3d-cad/provenance.json')


def native_identity(path):
    """Read platform/CPU from a bounded native header without executing the file."""
    with Path(path).open('rb') as stream:
        head = stream.read(64)
        if head[:4] == b'\x7fELF':
            if len(head) < 64 or head[4] != 2 or head[5] not in (1, 2):
                raise ValueError(f'Unsupported ELF class/byte order: {path}')
            machine = struct.unpack_from('<H' if head[5] == 1 else '>H', head, 18)[0]
            cpu = {62: 'x64', 183: 'arm64'}.get(machine)
            system = 'Linux'
        elif head[:4] in (b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf'):
            if len(head) < 32:
                raise ValueError(f'Truncated Mach-O header: {path}')
            machine = struct.unpack_from('<I' if head[:4] == b'\xcf\xfa\xed\xfe' else '>I', head, 4)[0]
            cpu = {0x01000007: 'x64', 0x0100000C: 'arm64'}.get(machine)
            system = 'Darwin'
        elif head[:2] == b'MZ':
            if len(head) < 64:
                raise ValueError(f'Truncated PE header: {path}')
            offset = struct.unpack_from('<I', head, 60)[0]
            if offset < 64 or offset > 1024 * 1024:
                raise ValueError(f'Unsupported PE header offset: {path}')
            stream.seek(offset)
            pe = stream.read(26)
            if len(pe) != 26 or pe[:4] != b'PE\0\0' or struct.unpack_from('<H', pe, 24)[0] != 0x20B:
                raise ValueError(f'Expected 64-bit PE image: {path}')
            machine = struct.unpack_from('<H', pe, 4)[0]
            cpu = {0x8664: 'x64', 0xAA64: 'arm64'}.get(machine)
            system = 'Windows'
        else:
            raise ValueError(f'Unsupported native executable header (universal bundles are not implemented): {path}')
        if cpu is None:
            raise ValueError(f'Unsupported native CPU: {path}')
        return system, cpu


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def validate_bundle(root, version):
    root = Path(root).resolve()
    provenance = json.loads((root / MANIFEST).read_text(encoding='utf-8'))
    paths = [entry['path'] for entry in provenance['files']]
    actual = {file.relative_to(root).as_posix() for file in root.rglob('*') if file.is_file()}
    if not paths or len(paths) != len(set(paths)) or actual != set(paths) | {MANIFEST.as_posix()}:
        raise ValueError('Bundle provenance must cover every file exactly once')
    if 'desktop' in provenance or (root / 'desktop').exists():
        raise ValueError('Plugins must use the core native bundle, without a standalone viewer')
    for file in root.rglob('*'):
        if file.is_symlink() and (file.is_dir() or not file.resolve().is_relative_to(root)):
            raise ValueError(f'Unsupported bundle link: {file}')
    for entry in provenance['files']:
        file = (root / entry['path']).resolve()
        if not file.is_relative_to(root) or sha(file) != entry['sha256']:
            raise ValueError(f"Bundle integrity failure: {entry['path']}")
    if provenance['project_version'] != version:
        raise ValueError('Native bundle must match VERSION')
    system = provenance['system']
    arch = {'aarch64': 'arm64', 'arm64': 'arm64', 'x86_64': 'x64', 'amd64': 'x64', 'x64': 'x64'}.get(str(provenance['architecture']).lower())
    if (system, arch) not in {('Darwin', 'arm64'), ('Darwin', 'x64'), ('Linux', 'arm64'), ('Linux', 'x64'), ('Windows', 'x64')}:
        raise ValueError('Unsupported native release platform/architecture')
    binary = 'bin/agent-3d-cad' + ('.exe' if system == 'Windows' else '')
    if not (root / binary).is_file():
        raise ValueError('Native executable is missing')
    candidates = {root / binary}
    for file in root.rglob('*'):
        if file.is_file() and (file.suffix.lower() in ('.dll', '.dylib') or '.so' in file.suffixes or '.so.' in file.name):
            candidates.add(file)
    for file in candidates:
        if native_identity(file) != (system, arch):
            raise ValueError(f'Native platform/architecture differs from provenance: {file.relative_to(root)}')
    return provenance, system, arch, binary
