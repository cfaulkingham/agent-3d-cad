"""Strict universal metadata/header tests. Synthetic native images never execute."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile

repo = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('bundle_validation', repo / 'packaging/bundle_validation.py')
v = importlib.util.module_from_spec(spec)
spec.loader.exec_module(v)
checks = 0


def check(value, label):
    global checks
    assert value, label
    checks += 1


def reject(action, label):
    global checks
    try:
        action()
    except (ValueError, OSError, KeyError):
        checks += 1
    else:
        raise AssertionError('Expected rejection: ' + label)


def image(arch, library=False):
    cpu, subtype = (0x0100000c, 0) if arch == 'arm64' else (0x01000007, 3)
    cache = hashlib.sha256(arch.encode()).hexdigest() + '-Release'
    commands = []
    segment = struct.pack('<II16sQQQQIIII', 0x19, 152, b'__TEXT', 0x100000000, 4096, 0, 1024, 5, 5, 1, 0)
    section = struct.pack('<16s16sQQIIIIIIII', b'__text', b'__TEXT', 0x100000200, 128, 512, 3, 0, 0, 0, 0, 0, 0)
    commands.append(segment + section)
    commands.append(struct.pack('<II16sQQQQIIII', 0x19, 72, b'__LINKEDIT', 0x100001000, 4096, 1024, 32, 1, 1, 0, 0))
    commands.append(struct.pack('<6I', 0x32, 24, 1, 15 << 16, 27 << 16, 0))
    commands.append(struct.pack('<4I', 0x1d, 16, 1040, 16))
    if library:
        name = b'@rpath/runtime.dylib\0'
        length = (24 + len(name) + 7) // 8 * 8
        commands.append(struct.pack('<6I', 0xd, length, 24, 0, 0, 0) + name + bytes(length - 24 - len(name)))
    data = bytearray(1056)
    table = b''.join(commands)
    struct.pack_into('<8I', data, 0, 0xfeedfacf, cpu, subtype, 6 if library else 2, len(commands), len(table), 0, 0)
    data[32:32 + len(table)] = table
    data[512:512 + len(cache)] = cache.encode()
    data[1024:1040] = b'LINKEDIT-PAYLOAD'
    data[1040:1056] = b'fake signature!!'
    return bytes(data), cache


def fat(arm, intel, wide=False, little=False):
    endian = '<' if little else '>'
    stride = 32 if wide else 20
    first, second = 4096, 8192
    raw = bytearray(second + len(intel))
    struct.pack_into(endian + 'II', raw, 0, 0xcafebabf if wide else 0xcafebabe, 2)
    for i, (cpu, subtype, start, payload) in enumerate(((0x0100000c, 0, first, arm), (0x01000007, 3, second, intel))):
        values = (cpu, subtype, start, len(payload), 12, 0) if wide else (cpu, subtype, start, len(payload), 12)
        struct.pack_into(endian + ('IIQQII' if wide else '5I'), raw, 8 + i * stride, *values)
        raw[start:start + len(payload)] = payload
    return bytes(raw)


with tempfile.TemporaryDirectory(prefix='cad-universal-validation-') as directory:
    root = Path(directory)
    arm, _ = image('arm64')
    intel, _ = image('x64')
    file = root / 'image'
    for wide in (False, True):
        for little in (False, True):
            file.write_bytes(fat(arm, intel, wide, little))
            details = v.native_slices(file)
            check(set(details) == {'arm64', 'x64'}, 'All fat byte orders/widths parsed')
            check(v.native_identity(file) == ('Darwin', 'universal'), 'Universal identity normalized')
    invalid = []
    raw = bytearray(fat(arm, intel)); struct.pack_into('>I', raw, 4, 3); invalid.append(raw)
    raw = bytearray(fat(arm, intel)); struct.pack_into('>I', raw, 36, 4096); invalid.append(raw)
    raw = bytearray(fat(arm, intel)); struct.pack_into('>I', raw, 16, 48); invalid.append(raw)
    raw = bytearray(fat(arm, intel)); struct.pack_into('>I', raw, 28, 0x0100000c); invalid.append(raw)
    raw = bytearray(arm); struct.pack_into('<I', raw, 8, 2); invalid.append(raw)
    raw = bytearray(arm); struct.pack_into('<I', raw, 32 + 4, 7); invalid.append(raw)
    raw = bytearray(arm); struct.pack_into('<I', raw, 32 + 152 + 72 + 8, 2); invalid.append(raw)
    raw = bytearray(arm); struct.pack_into('<Q', raw, 32 + 152 + 32, 12345); invalid.append(raw)
    raw = bytearray(arm); struct.pack_into('<I', raw, 32 + 152 + 72 + 24 + 8, 1039); invalid.append(raw)
    invalid.extend((arm[:16], arm[:-1], fat(arm, intel)[:-20]))
    for raw in invalid:
        file.write_bytes(raw); reject(lambda: v.native_slices(file), 'Malformed native image')
    file.write_bytes(arm)
    original = v.native_slices(file)['arm64']
    changed = bytearray(arm); changed[1040:1056] = b'new signature!!!'; file.write_bytes(changed)
    check(v.native_slices(file)['arm64']['signing_payload_sha256'] == original['signing_payload_sha256'], 'Only signature bytes excluded')
    for offset in (28, 400, 512, 1024):
        changed = bytearray(arm); changed[offset] ^= 1; file.write_bytes(changed)
        try:
            check(v.native_slices(file)['arm64']['signing_payload_sha256'] != original['signing_payload_sha256'], 'Unsigned data mutation remains visible')
        except ValueError:
            checks += 1

    bundle = root / 'bundle'
    (bundle / 'bin').mkdir(parents=True); (bundle / 'lib').mkdir()
    source = bundle / 'share/agent-3d-cad/source-inputs.sha256'; source.parent.mkdir(parents=True)
    source.write_text('0' * 64 + '  CMakeLists.txt\n' + '1' * 64 + '  VERSION\n')
    identity = {'format_version': 1, 'algorithm': 'sha256-file-list-v1', 'manifest': source.relative_to(bundle).as_posix(), 'sha256': v.sha(source)}
    native = {'bin/agent-3d-cad': (image('arm64')[0], image('x64')[0]), 'lib/runtime.dylib': (image('arm64', True)[0], image('x64', True)[0])}
    originals, composition, hlrs = {}, {'format_version': 1, 'method': 'lipo', 'inputs': [], 'native_files': []}, {}
    hlr_path = 'share/agent-3d-cad/notices/occt/agentcad-hlr-midpoint-v2.json'
    for arch, index in (('arm64', 0), ('x64', 1)):
        hlr = {'format_version': 1, 'occt_version': '8.0.1', 'modification': 'agentcad-hlr-midpoint-v2', 'binaries': [{'sha256': hashlib.sha256(arch.encode()).hexdigest()}]}
        prefix = f'share/agent-3d-cad/universal-inputs/{arch}/'
        saved = bundle / (prefix + 'agentcad-hlr-midpoint-v2.json'); saved.parent.mkdir(parents=True); saved.write_text(json.dumps(hlr))
        original = {'format_version': 1, 'project_version': 'test', 'system': 'Darwin', 'architecture': arch, 'source_identity': identity,
                    'occt_hlr_sdk_sha256': hlr['binaries'][0]['sha256'],
                    'build_identity': {'format_version': 1, 'source_sha256': identity['sha256'], 'executable_sha256': '2' * 64, 'cache_identity': image(arch)[1]},
                    'files': [{'path': path, 'sha256': hashlib.sha256(payloads[index]).hexdigest()} for path, payloads in native.items()] +
                             [{'path': identity['manifest'], 'sha256': identity['sha256']}, {'path': hlr_path, 'sha256': v.sha(saved)}]}
        manifest = bundle / (prefix + 'provenance.json'); manifest.write_text(json.dumps(original))
        originals[arch], hlrs[arch] = original, hlr
        composition['inputs'].append({'architecture': arch, 'provenance': prefix + 'provenance.json', 'provenance_sha256': v.sha(manifest), 'hlr_manifest': prefix + saved.name, 'hlr_manifest_sha256': v.sha(saved)})
    for path, payloads in native.items():
        target = bundle / path; target.write_bytes(fat(*payloads))
        details = v.native_slices(target)
        record = {'path': path, 'slices': []}
        for arch, index in (('arm64', 0), ('x64', 1)):
            m = details[arch]
            entry = {'architecture': arch, 'input_sha256': hashlib.sha256(payloads[index]).hexdigest(), 'sha256': m['sha256']}
            for key in ('section_payload_sha256', 'signing_payload_sha256'):
                entry[key] = entry['input_' + key] = m[key]
            record['slices'].append(entry)
        composition['native_files'].append(record)
    combined = {k: val for k, val in hlrs['arm64'].items() if k != 'binaries'}
    combined['binaries'] = [{**entry, 'architecture': arch} for arch in ('arm64', 'x64') for entry in hlrs[arch]['binaries']]
    hlr_file = bundle / hlr_path; hlr_file.parent.mkdir(parents=True); hlr_file.write_text(json.dumps(combined))
    provenance = {'format_version': 2, 'project_version': 'test', 'system': 'Darwin', 'architecture': 'universal', 'source_identity': identity, 'composition': composition, 'minimum_macos': '15.0.0'}
    manifest = bundle / v.MANIFEST

    def publish(data):
        data['files'] = [{'path': p.relative_to(bundle).as_posix(), 'sha256': v.sha(p)} for p in sorted(bundle.rglob('*')) if p.is_file() and p != manifest]
        manifest.write_text(json.dumps(data))

    publish(provenance)
    check(v.validate_bundle(bundle, 'test')[1:3] == ('Darwin', 'universal'), 'Complete universal provenance validates')
    executable = v.native_slices(bundle / 'bin/agent-3d-cad')['arm64']
    library = v.native_slices(bundle / 'lib/runtime.dylib')['arm64']
    reject(lambda: v._portable_macho(bundle, 'bin/agent-3d-cad', library, executable), 'A dylib cannot be the service executable')
    reject(lambda: v._portable_macho(bundle, 'lib/runtime.dylib', executable, executable), 'An executable cannot be a dylib')
    altered = copy.deepcopy(executable); altered['rpaths'] = ['/developer/sdk/lib']
    reject(lambda: v._portable_macho(bundle, 'bin/agent-3d-cad', altered, executable), 'Developer runpath rejected')
    altered = copy.deepcopy(executable); altered['rpaths'] = ['@executable_path/missing']
    reject(lambda: v._portable_macho(bundle, 'bin/agent-3d-cad', altered, executable), 'Missing runpath rejected')
    altered = copy.deepcopy(executable); altered['dependencies'] = [{'name': '@executable_path/../../outside.dylib'}]
    reject(lambda: v._portable_macho(bundle, 'bin/agent-3d-cad', altered, executable), 'Escaping dependency rejected')
    for mutate in (
        lambda d: d.update(format_version=1),
        lambda d: d.update(compiler='merged'),
        lambda d: d.update(minimum_macos='14.0.0'),
        lambda d: d['composition']['inputs'].pop(),
        lambda d: d['composition']['inputs'][0].update(provenance_sha256='0' * 64),
        lambda d: d['composition']['native_files'].pop(),
        lambda d: d['composition']['native_files'][0]['slices'].pop(),
        lambda d: d['composition']['native_files'][0]['slices'][0].update(input_sha256='0' * 64),
        lambda d: d['composition']['native_files'][0]['slices'][0].update(input_signing_payload_sha256='0' * 64),
        lambda d: d['source_identity'].update(sha256='0' * 64),
    ):
        broken = copy.deepcopy(provenance); mutate(broken); publish(broken)
        reject(lambda: v.validate_bundle(bundle, 'test'), 'Universal provenance tampering')
    publish(provenance)

print(f'{checks} strict universal header/provenance checks passed; synthetic images not executed')
