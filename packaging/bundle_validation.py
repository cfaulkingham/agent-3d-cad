"""Developer-side native bundle verification shared by plugin packagers.

No runtime dependency: packaged users execute only the native service.
"""
import hashlib
import json
from pathlib import Path
import re
import struct

MANIFEST = Path('share/agent-3d-cad/provenance.json')
# Mach-O constants/layouts are read from the platform SDK's mach-o/fat.h and
# mach-o/loader.h. No external tools or execution are needed for inspection.
_MACH64 = {b'\xcf\xfa\xed\xfe': '<', b'\xfe\xed\xfa\xcf': '>'}
_FAT = {b'\xca\xfe\xba\xbe': ('>', False), b'\xbe\xba\xfe\xca': ('<', False),
        b'\xca\xfe\xba\xbf': ('>', True), b'\xbf\xba\xfe\xca': ('<', True)}
_CPU = {0x01000007: 'x64', 0x0100000C: 'arm64'}


def _read_at(stream, offset, size):
    stream.seek(offset)
    result = stream.read(size)
    if len(result) != size:
        raise ValueError('Truncated Mach-O range')
    return result


def _hash_range(stream, offset, size, digest=None):
    digest = digest or hashlib.sha256()
    stream.seek(offset)
    while size:
        data = stream.read(min(size, 1024 * 1024))
        if not data:
            raise ValueError('Truncated Mach-O payload')
        digest.update(data)
        size -= len(data)
    return digest


def _mac_version(value):
    return f'{value >> 16}.{(value >> 8) & 255}.{value & 255}'


def _macho_slice(stream, offset, size, table_cpu=None, table_subtype=None):
    head = _read_at(stream, offset, 32)
    endian = _MACH64.get(head[:4])
    if endian is None:
        raise ValueError('Universal slice must contain a 64-bit Mach-O header')
    _, cpu, subtype, filetype, ncmds, command_size, _, reserved = struct.unpack(endian + '8I', head)
    arch = _CPU.get(cpu)
    baseline = {'arm64': 0, 'x64': 3}
    if arch is None or (subtype & 0xffffff) != baseline[arch] or subtype >> 24 not in (0, 128):
        raise ValueError('Unsupported Mach-O baseline CPU/subtype')
    if table_cpu is not None and (cpu, subtype) != (table_cpu, table_subtype):
        raise ValueError('Universal architecture table differs from embedded Mach-O header')
    if reserved or filetype not in (2, 6):
        raise ValueError('Expected a Mach-O executable or shared library')
    if not 1 <= ncmds <= 4096 or not 8 * ncmds <= command_size <= min(16 * 1024 * 1024, size - 32):
        raise ValueError('Invalid Mach-O load-command bounds')
    commands = _read_at(stream, offset + 32, command_size)
    normalized = bytearray(head + commands)
    dependencies, rpaths, sections, section_ranges = [], [], [], []
    install_name = None
    versions, signature, linkedit, dylinker = [], None, None, None
    position = 0

    def string(command, minimum):
        if len(command) < minimum:
            raise ValueError('Truncated Mach-O string command')
        start = struct.unpack_from(endian + 'I', command, 8)[0]
        if start < minimum or start >= len(command) or b'\0' not in command[start:]:
            raise ValueError('Invalid Mach-O command string offset/terminator')
        raw = command[start:].split(b'\0', 1)[0]
        try:
            value = raw.decode('utf-8')
        except UnicodeDecodeError as error:
            raise ValueError('Invalid Mach-O command string encoding') from error
        if not value or any(ord(character) < 32 for character in value):
            raise ValueError('Invalid Mach-O command string')
        return value

    def fixed_name(raw):
        try:
            return raw.split(b'\0', 1)[0].decode('ascii')
        except UnicodeDecodeError as error:
            raise ValueError('Invalid Mach-O section name') from error

    for _ in range(ncmds):
        if position + 8 > command_size:
            raise ValueError('Truncated Mach-O load command')
        kind, length = struct.unpack_from(endian + 'II', commands, position)
        if length < 8 or length % 8 or position + length > command_size:
            raise ValueError('Invalid Mach-O load-command size')
        command = commands[position:position + length]
        if kind in (0xc, 0xd, 0x80000018, 0x8000001f, 0x20, 0x80000023):
            name = string(command, 24)
            current, compatibility = struct.unpack_from(endian + 'II', command, 16)
            if kind == 0xd:
                if install_name is not None:
                    raise ValueError('Duplicate Mach-O library identity')
                install_name = name
            else:
                dependencies.append({'name': name, 'command': kind, 'current_version': current,
                                     'compatibility_version': compatibility})
        elif kind == 0x8000001c:
            rpaths.append(string(command, 12))
        elif kind == 0xe:
            if dylinker is not None:
                raise ValueError('Duplicate Mach-O dynamic loader')
            dylinker = string(command, 12)
            if dylinker != '/usr/lib/dyld':
                raise ValueError('Unsupported Mach-O dynamic loader')
        elif kind == 0x27:
            raise ValueError('Mach-O loader environment overrides are not portable')
        elif kind == 0x32:
            if length < 24:
                raise ValueError('Truncated Mach-O build version')
            platform, minimum, sdk, count = struct.unpack_from(endian + '4I', command, 8)
            if platform != 1 or length != 24 + 8 * count:
                raise ValueError('Expected a macOS Mach-O build version')
            versions.append((minimum, sdk))
        elif kind in (0x24, 0x25, 0x2f, 0x30):
            if kind != 0x24 or length != 16:
                raise ValueError('Expected a macOS Mach-O minimum version')
            versions.append(struct.unpack_from(endian + 'II', command, 8))
        elif kind == 0x1d:
            if length != 16 or signature is not None:
                raise ValueError('Invalid Mach-O signature command')
            signature = struct.unpack_from(endian + 'II', command, 8)
            if signature[0] < 32 + command_size or not signature[1] or sum(signature) != size:
                raise ValueError('Invalid Mach-O signature payload bounds')
            normalized[32 + position + 8:32 + position + 16] = bytes(8)
        elif kind == 0x19:
            if length < 72:
                raise ValueError('Truncated Mach-O segment')
            name = fixed_name(command[8:24])
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from(endian + '4Q', command, 24)
            count = struct.unpack_from(endian + 'I', command, 64)[0]
            if length != 72 + count * 80 or count > 4096 or fileoff + filesize > size:
                raise ValueError('Invalid Mach-O segment bounds')
            if name == '__LINKEDIT':
                if linkedit is not None:
                    raise ValueError('Duplicate Mach-O linkedit segment')
                linkedit = (fileoff, filesize, vmsize, position)
            for index in range(count):
                section = command[72 + index * 80:152 + index * 80]
                section_name, segment_name = fixed_name(section[:16]), fixed_name(section[16:32])
                address, amount, start, alignment, relocation, relocations, flags = struct.unpack_from(endian + 'QQ5I', section, 32)
                if segment_name != name or address < vmaddr or address + amount > vmaddr + vmsize or alignment > 31 or relocation + relocations * 8 > size:
                    raise ValueError('Invalid Mach-O section bounds')
                descriptor = {'segment': name, 'section': section_name, 'address': address,
                              'size': amount, 'flags': flags, 'alignment': alignment}
                if flags & 255 not in (1, 0xc, 0x12) and amount:
                    if start < fileoff or start + amount > fileoff + filesize or start < 32 + command_size:
                        raise ValueError('Invalid file-backed Mach-O section bounds')
                    section_ranges.append((start, start + amount))
                    descriptor['sha256'] = _hash_range(stream, offset + start, amount).hexdigest()
                sections.append(descriptor)
        position += length
    if position != command_size or len(versions) != 1 or not versions[0][0]:
        raise ValueError('Mach-O needs one complete macOS deployment target')
    if (filetype == 6) != (install_name is not None):
        raise ValueError('Mach-O library identity differs from image type')
    for prior, current in zip(sorted(section_ranges), sorted(section_ranges)[1:]):
        if prior[1] > current[0]:
            raise ValueError('Overlapping Mach-O section payloads')
    if signature and (linkedit is None or linkedit[0] + linkedit[1] != size or signature[0] < linkedit[0]):
        raise ValueError('Mach-O signature must terminate its linkedit segment')
    if signature:
        if any(end > signature[0] for start, end in section_ranges):
            raise ValueError('Mach-O signature overlaps a file-backed section')
        fileoff, filesize, vmsize, command_offset = linkedit
        # codesign adjusts these two sizes when its terminal blob changes. Admit
        # only the native 4K/16K page rounding; arbitrary mapped sizes are not
        # signature metadata and must not disappear from a preserved-payload hash.
        rounded = {((filesize + page - 1) // page) * page for page in (4096, 16384)}
        if vmsize not in rounded:
            raise ValueError('Mach-O signed linkedit virtual size is not page-rounded')
        unsigned_size = signature[0] - fileoff
        struct.pack_into(endian + 'Q', normalized, 32 + command_offset + 32, unsigned_size)
        struct.pack_into(endian + 'Q', normalized, 32 + command_offset + 48, unsigned_size)
    signed_length = signature[0] if signature else size
    content = hashlib.sha256(normalized)
    _hash_range(stream, offset + len(normalized), signed_length - len(normalized), content)
    return arch, {'offset': offset, 'size': size, 'sha256': _hash_range(stream, offset, size).hexdigest(),
                  'filetype': filetype, 'cpu_subtype': subtype, 'minimum_macos': _mac_version(versions[0][0]),
                  'sdk': _mac_version(versions[0][1]), 'dependencies': dependencies, 'rpaths': rpaths,
                  'install_name': install_name, 'dylinker': dylinker,
                  'section_payload_sha256': hashlib.sha256(json.dumps(sections, sort_keys=True, separators=(',', ':')).encode()).hexdigest(),
                  'signing_payload_sha256': content.hexdigest()}


def native_slices(path):
    """Fully inspect thin/universal macOS slices without executing native code.

    Universal files must contain exactly baseline arm64 and x86_64. Digests bind
    every slice, its file-backed sections, and all unsigned payload bytes. The
    latter excludes only the terminal code signature and its load-command/size
    fields changed by re-signing; it does not exclude dyld/linkedit metadata.
    """
    path = Path(path)
    size = path.stat().st_size
    with path.open('rb') as stream:
        magic = stream.read(4)
        if magic in _MACH64:
            arch, details = _macho_slice(stream, 0, size)
            return {arch: details}
        if magic not in _FAT:
            raise ValueError('Unsupported native Mach-O header')
        endian, wide = _FAT[magic]
        count = struct.unpack(endian + 'I', _read_at(stream, 4, 4))[0]
        if count != 2:
            raise ValueError('Unsupported native universal slice count; expected arm64 and x64')
        stride = 32 if wide else 20
        table_end = 8 + stride * count
        entries = []
        for index in range(count):
            record = _read_at(stream, 8 + index * stride, stride)
            if wide:
                cpu, subtype, offset, amount, alignment, reserved = struct.unpack(endian + 'IIQQII', record)
                if reserved:
                    raise ValueError('Unsupported universal architecture flags')
            else:
                cpu, subtype, offset, amount, alignment = struct.unpack(endian + '5I', record)
            if alignment > 30 or offset % (1 << alignment) or offset < table_end or amount < 32 or offset + amount > size:
                raise ValueError('Invalid universal slice bounds or alignment')
            entries.append((offset, amount, cpu, subtype))
        entries.sort()
        if entries[0][0] + entries[0][1] > entries[1][0]:
            raise ValueError('Overlapping universal slices')
        result = {}
        for offset, amount, cpu, subtype in entries:
            arch, details = _macho_slice(stream, offset, amount, cpu, subtype)
            if arch in result:
                raise ValueError('Duplicate universal CPU slice')
            result[arch] = details
        if set(result) != {'arm64', 'x64'}:
            raise ValueError('Universal bundle requires exactly arm64 and x64 slices')
        return result





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
        elif head[:4] in _FAT:
            native_slices(path)
            return 'Darwin', 'universal'
        else:
            raise ValueError(f'Unsupported native executable header: {path}')
        if cpu is None:
            raise ValueError(f'Unsupported native CPU: {path}')
        return system, cpu


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def _source_identity(root, provenance):
    manifest = 'share/agent-3d-cad/source-inputs.sha256'
    identity = provenance.get('source_identity')
    if not isinstance(identity, dict) or identity != {
            'format_version': 1, 'algorithm': 'sha256-file-list-v1',
            'manifest': manifest, 'sha256': sha(root / manifest)}:
        raise ValueError('Bundle source identity is missing or inconsistent')
    data = (root / manifest).read_bytes()
    if len(data) > 8 * 1024 * 1024:
        raise ValueError('Source identity manifest exceeds budget')
    paths = []
    for line in data.decode('utf-8').splitlines(keepends=True):
        if not re.fullmatch(r'[0-9a-f]{64}  [^\r\n]+\n', line):
            raise ValueError('Malformed source identity record')
        path = line[66:-1]
        if path.startswith('/') or '\\' in path or any(p in ('', '.', '..') for p in path.split('/')):
            raise ValueError('Unsafe source identity path')
        if path not in ('CMakeLists.txt', 'VERSION') and path.split('/')[0] not in ('src', 'include', 'web', 'third_party', 'cmake'):
            raise ValueError('Unexpected production source identity scope')
        paths.append(path)
    if not paths or paths != sorted(set(paths)) or not {'CMakeLists.txt', 'VERSION'} <= set(paths):
        raise ValueError('Source identity paths must be sorted, unique and include root inputs')
    return identity


def _portable_macho(root, path, details, executable):
    """Reject developer paths and require every local dependency to be bundled."""
    if details['filetype'] != (2 if path == 'bin/agent-3d-cad' else 6):
        raise ValueError(f'Mach-O executable/library role differs from bundle path: {path}')
    def expand(value):
        for prefix, directory in (('@executable_path', root / 'bin'), ('@loader_path', (root / path).parent)):
            if value == prefix or value.startswith(prefix + '/'):
                result = (directory / value[len(prefix):].lstrip('/')).resolve()
                if result.is_relative_to(root):
                    return result
        raise ValueError(f'Nonportable Mach-O loader path: {path}: {value}')
    rpaths = [expand(value) for value in details['rpaths']]
    # Loaded images inherit the executable's declared runpaths. Do not invent
    # a library directory when the actual executable declares none.
    for value in executable['rpaths']:
        if value == '@loader_path' or value.startswith('@loader_path/'):
            value = '@executable_path' + value[len('@loader_path'):]
        rpaths.append(expand(value))
    if any(not directory.is_dir() for directory in rpaths):
        raise ValueError(f'Mach-O runpath directory is not bundled: {path}')
    for dependency in details['dependencies']:
        name = dependency['name']
        if name.startswith(('/usr/lib/', '/System/Library/')) and '..' not in name.split('/'):
            continue
        if name.startswith('@rpath/'):
            targets = [(directory / name[7:]).resolve() for directory in rpaths]
        else:
            targets = [expand(name)]
        if not any(p.is_relative_to(root) and p.is_file() and p.suffix == '.dylib' for p in targets):
            raise ValueError(f'Mach-O dependency is not bundled: {path}: {name}')
    if details['filetype'] == 6:
        name = details['install_name']
        if name != '@rpath/' + Path(name).name:
            raise ValueError(f'Nonportable Mach-O library identity: {path}')


def _validate_universal(root, provenance, candidates):
    if provenance.get('format_version') != 2 or any(k in provenance for k in ('compiler', 'build_identity', 'occt_hlr_sdk_sha256')):
        raise ValueError('Universal provenance must preserve separate thin build identities')
    identity = _source_identity(root, provenance)
    composition = provenance.get('composition', {})
    if composition.get('format_version') != 1 or composition.get('method') != 'lipo':
        raise ValueError('Universal composition record is missing')
    inputs, inventories, hlrs = {}, {}, {}
    for entry in composition.get('inputs', []):
        arch = entry.get('architecture')
        if arch not in ('arm64', 'x64') or arch in inputs:
            raise ValueError('Universal input CPUs must be unique arm64 and x64')
        prefix = 'share/agent-3d-cad/universal-inputs/' + arch + '/'
        for key, name in (('provenance', 'provenance.json'), ('hlr_manifest', 'agentcad-hlr-midpoint-v2.json')):
            if entry.get(key) != prefix + name or sha(root / entry[key]) != entry.get(key + '_sha256'):
                raise ValueError('Original universal input record is missing or changed')
        original = json.loads((root / entry['provenance']).read_text(encoding='utf-8'))
        normalized = {'arm64': 'arm64', 'aarch64': 'arm64', 'x64': 'x64', 'x86_64': 'x64', 'amd64': 'x64'}.get(str(original.get('architecture')).lower())
        if original.get('format_version') != 1 or original.get('system') != 'Darwin' or normalized != arch or original.get('source_identity') != identity or any(k in original for k in ('composition', 'plugin', 'desktop')):
            raise ValueError('Original universal input identity is inconsistent')
        build = original.get('build_identity', {})
        if build.get('format_version') != 1 or build.get('source_sha256') != identity['sha256'] or not re.fullmatch(r'[0-9a-f]{64}', str(build.get('executable_sha256', ''))) or not re.fullmatch(r'[0-9a-f]{64}-.+', str(build.get('cache_identity', ''))):
            raise ValueError('Original universal input build/source binding is missing')
        files = original.get('files', [])
        inventory = {e['path']: e['sha256'] for e in files}
        if not files or len(files) != len(inventory) or len(files) > 10000 or any(not re.fullmatch(r'[0-9a-f]{64}', str(h)) for h in inventory.values()):
            raise ValueError('Original universal input inventory is invalid')
        hlr = json.loads((root / entry['hlr_manifest']).read_text(encoding='utf-8'))
        if sha(root / entry['hlr_manifest']) != inventory.get('share/agent-3d-cad/notices/occt/agentcad-hlr-midpoint-v2.json') or not any(b.get('sha256') == original.get('occt_hlr_sdk_sha256') for b in hlr.get('binaries', [])):
            raise ValueError('Original universal SDK/HLR identity is inconsistent')
        inputs[arch], inventories[arch], hlrs[arch] = original, inventory, hlr
    if set(inputs) != {'arm64', 'x64'} or set(inventories['arm64']) != set(inventories['x64']):
        raise ValueError('Universal needs both matching original input inventories')
    per_slice = {'architecture', 'compiler', 'build_identity', 'occt_hlr_sdk_sha256', 'minimum_macos', 'files'}
    common = lambda p: {k: v for k, v in p.items() if k not in per_slice}
    if common(inputs['arm64']) != common(inputs['x64']):
        raise ValueError('Universal original source/common provenance differs')
    expected_common = common(inputs['arm64'])
    expected_common['format_version'] = 2
    actual_common = {k: v for k, v in provenance.items() if k not in per_slice | {'composition', 'plugin'}}
    if actual_common != expected_common:
        raise ValueError('Universal common provenance differs from original inputs')
    native_paths = {p.relative_to(root).as_posix() for p in candidates}
    records = {e['path']: e for e in composition.get('native_files', [])}
    if not records or len(records) != len(composition.get('native_files', [])) or set(records) != native_paths or len(records) > 512:
        raise ValueError('Universal native composition inventory differs')
    for arch in inputs:
        original_native = {p for p in inventories[arch] if p == 'bin/agent-3d-cad' or p.endswith('.dylib')}
        if original_native != native_paths:
            raise ValueError('Universal native input inventory differs')
        for path, digest in inventories[arch].items():
            if path not in native_paths and path != 'share/agent-3d-cad/notices/occt/agentcad-hlr-midpoint-v2.json':
                target = (root / path).resolve()
                if not target.is_relative_to(root) or not target.is_file() or sha(target) != digest:
                    raise ValueError('Universal common resource differs from original input')
    minimums = []
    executable = native_slices(root / 'bin/agent-3d-cad')
    for path, record in records.items():
        actual = native_slices(root / path)
        listed = {e['architecture']: e for e in record.get('slices', [])}
        if set(actual) != {'arm64', 'x64'} or set(listed) != set(actual) or len(record.get('slices', [])) != 2:
            raise ValueError('Universal composed image needs both exact CPU slice records')
        for arch, details in actual.items():
            entry = listed[arch]
            if entry.get('input_sha256') != inventories[arch][path] or entry.get('sha256') != details['sha256']:
                raise ValueError('Universal slice hash differs from composition record')
            for key in ('section_payload_sha256', 'signing_payload_sha256'):
                if entry.get(key) != details[key] or entry.get('input_' + key) != details[key]:
                    raise ValueError('Universal unsigned native payload was not preserved')
            _portable_macho(root, path, details, executable[arch])
            minimums.append(details['minimum_macos'])
            if path == 'bin/agent-3d-cad':
                with (root / path).open('rb') as stream:
                    data = _read_at(stream, details['offset'], details['size'])
                if inputs[arch]['build_identity']['cache_identity'].encode() not in data:
                    raise ValueError('Universal slice lost its original cache identity')
    minimum = max(minimums, key=lambda v: tuple(map(int, v.split('.'))))
    if provenance.get('minimum_macos') != minimum:
        raise ValueError('Universal deployment floor differs from native slices')
    expected_hlr = {k: v for k, v in hlrs['arm64'].items() if k != 'binaries'}
    if expected_hlr != {k: v for k, v in hlrs['x64'].items() if k != 'binaries'}:
        raise ValueError('Universal modified SDK sources differ')
    expected_hlr['binaries'] = [{**b, 'architecture': arch} for arch in ('arm64', 'x64') for b in hlrs[arch]['binaries']]
    if json.loads((root / 'share/agent-3d-cad/notices/occt/agentcad-hlr-midpoint-v2.json').read_text()) != expected_hlr:
        raise ValueError('Universal combined HLR record differs from original SDK records')


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
    arch = {'aarch64': 'arm64', 'arm64': 'arm64', 'x86_64': 'x64', 'amd64': 'x64', 'x64': 'x64', 'universal': 'universal'}.get(str(provenance['architecture']).lower())
    if (system, arch) not in {('Darwin', 'arm64'), ('Darwin', 'x64'), ('Darwin', 'universal'), ('Linux', 'arm64'), ('Linux', 'x64'), ('Windows', 'x64')}:
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
    if arch == 'universal':
        _validate_universal(root, provenance, candidates)
    return provenance, system, arch, binary
