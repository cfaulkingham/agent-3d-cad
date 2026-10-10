"""Offline native-header/provenance tests; fixtures are inspected, never executed."""
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile

repo = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('bundle_validation', repo / 'packaging/bundle_validation.py')
validation = importlib.util.module_from_spec(spec); spec.loader.exec_module(validation)
checks = 0


def check(value, label):
    global checks
    assert value, label
    checks += 1


def fails(function, text):
    global checks
    try:
        function()
    except ValueError as error:
        assert text in str(error), str(error)
        checks += 1
    else:
        raise AssertionError('Expected rejection: ' + text)


def header(system, arch):
    data = bytearray(256)
    if system == 'Linux':
        data[:6] = b'\x7fELF\x02\x01'
        struct.pack_into('<H', data, 18, 62 if arch == 'x64' else 183)
    elif system == 'Darwin':
        struct.pack_into('<II', data, 0, 0xFEEDFACF, 0x01000007 if arch == 'x64' else 0x0100000C)
    else:
        data[:2] = b'MZ'; struct.pack_into('<I', data, 60, 128)
        data[128:132] = b'PE\0\0'; struct.pack_into('<H', data, 132, 0x8664 if arch == 'x64' else 0xAA64)
        struct.pack_into('<H', data, 152, 0x20B)
    return data


with tempfile.TemporaryDirectory(prefix='cad-package-arch-') as directory:
    root = Path(directory)
    for system, arch in [('Darwin','arm64'), ('Darwin','x64'), ('Linux','arm64'), ('Linux','x64'), ('Windows','x64')]:
        bundle = root / (system + arch); (bundle / 'bin').mkdir(parents=True)
        exe = bundle / ('bin/agent-3d-cad.exe' if system == 'Windows' else 'bin/agent-3d-cad')
        exe.write_bytes(header(system, arch))
        manifest = bundle / validation.MANIFEST; manifest.parent.mkdir(parents=True)
        data = {'project_version':'test', 'system':system, 'architecture':arch,
                'files':[{'path':exe.relative_to(bundle).as_posix(), 'sha256':validation.sha(exe)}]}
        def record(): manifest.write_text(json.dumps(data))
        record(); check(validation.validate_bundle(bundle, 'test')[1:3] == (system, arch), 'Release architecture admitted')
        aliases = ['aarch64', 'ARM64'] if arch == 'arm64' else ['AMD64', 'x86_64', 'x64']
        for alias in aliases:
            data['architecture'] = alias; record()
            check(validation.validate_bundle(bundle, 'test')[2] == arch, 'CPU alias normalized')
        data['architecture'] = 'x64' if arch == 'arm64' else 'arm64'; record()
        fails(lambda: validation.validate_bundle(bundle, 'test'), 'Unsupported native release' if system == 'Windows' else 'differs from provenance')
        data['architecture'] = arch; record()
        fails(lambda: validation.validate_bundle(bundle, 'other'), 'match VERSION')
        data['desktop'] = {}; record(); fails(lambda: validation.validate_bundle(bundle, 'test'), 'core native bundle')
        del data['desktop']; record()
        lib = bundle / 'lib'; lib.mkdir()
        dependency = lib / ('runtime.dll' if system == 'Windows' else 'runtime.dylib' if system == 'Darwin' else 'runtime.so.8.0.1')
        dependency.write_bytes(header(system, 'x64' if arch == 'arm64' else 'arm64'))
        data['files'].append({'path':dependency.relative_to(bundle).as_posix(), 'sha256':validation.sha(dependency)}); record()
        fails(lambda: validation.validate_bundle(bundle, 'test'), 'differs from provenance')
    image = root / 'invalid'
    for raw in [b'#!/bin/sh\nexit 0\n', b'MZ', b'\x7fELF\x01\x01'+bytes(58), b'\xca\xfe\xba\xbe'+bytes(60), header('Windows','x64')[:130]]:
        image.write_bytes(raw); fails(lambda: validation.native_identity(image), 'header' if raw[:2] == b'MZ' and len(raw)<64 else 'ELF' if raw[:4]==b'\x7fELF' else 'PE' if raw[:2]==b'MZ' else 'Unsupported native')
print(f'{checks} package architecture/provenance checks passed (synthetic files not executed)')
