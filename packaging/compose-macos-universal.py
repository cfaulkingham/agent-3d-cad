"""Compose two verified native macOS core bundles; developer tool, never a runtime launcher.

Usage: python compose-macos-universal.py ARM64_CORE X64_CORE NEW_OUTPUT_DIRECTORY
Inputs are immutable. Every native image is combined and re-signed, every other
resource must match, and output is published only after independent validation.
"""
import copy
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from bundle_validation import MANIFEST, native_slices, sha, validate_bundle

ARCHES = ('arm64', 'x64')
HLR = Path('share/agent-3d-cad/notices/occt/agentcad-hlr-midpoint-v2.json')
SOURCE = Path('share/agent-3d-cad/source-inputs.sha256')
HEX = re.compile(r'[0-9a-f]{64}\Z')
PINNED = {
    'occt_version': '8.0.1',
    'occt_archive_sha256': '0d6913eae4bcc09a3653ceced6dda1aec11c35a1513d4c06762c9b002092c68a',
    'json_version': '3.12.0',
    'json_archive_sha256': '4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187',
    'freetype_version': '2.14.3',
    'freetype_recipe_archive_sha256': '36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f',
    'occt_modifications': ['agentcad-hlr-midpoint-v2'],
    'original_code_license': 'MIT',
}
PER_SLICE = {'architecture', 'compiler', 'build_identity', 'occt_hlr_sdk_sha256', 'minimum_macos', 'files'}
METADATA = ('filetype', 'cpu_subtype', 'minimum_macos', 'sdk', 'dependencies', 'rpaths', 'install_name', 'dylinker')


def command(*args):
    result = subprocess.run(list(args), text=True, capture_output=True, timeout=120)
    if result.returncode:
        raise ValueError(f'{Path(args[0]).name} failed: {result.stderr.strip()}')
    return result.stdout


def identity(root, provenance):
    expected = {'format_version': 1, 'algorithm': 'sha256-file-list-v1',
                'sha256': sha(root / SOURCE), 'manifest': SOURCE.as_posix()}
    if provenance.get('source_identity') != expected:
        raise ValueError('Core bundle needs an exact production source identity')
    data = (root / SOURCE).read_bytes()
    if len(data) > 8 * 1024 * 1024:
        raise ValueError('Source identity manifest exceeds budget')
    lines = data.decode('utf-8').splitlines(keepends=True)
    paths = []
    for line in lines:
        if not re.fullmatch(r'[0-9a-f]{64}  [^\r\n]+\n', line):
            raise ValueError('Malformed source identity record')
        path = line[66:-1]
        if path.startswith('/') or '\\' in path or any(p in ('', '.', '..') for p in path.split('/')):
            raise ValueError('Unsafe source identity path')
        paths.append(path)
    if not paths or paths != sorted(set(paths)) or not {'CMakeLists.txt', 'VERSION'} <= set(paths):
        raise ValueError('Source identity paths must be complete, sorted and unique')
    if any(not (p in ('CMakeLists.txt', 'VERSION') or p.split('/')[0] in ('src', 'include', 'web', 'third_party', 'cmake')) for p in paths):
        raise ValueError('Unexpected production source identity scope')
    build = provenance.get('build_identity', {})
    if build.get('format_version') != 1 or build.get('source_sha256') != expected['sha256'] or not HEX.fullmatch(str(build.get('executable_sha256', ''))) or not re.fullmatch(r'[0-9a-f]{64}-.+', str(build.get('cache_identity', ''))):
        raise ValueError('Core bundle lacks successful build/source binding')
    return expected


def input_bundle(root, arch, version):
    provenance, system, actual_arch, binary = validate_bundle(root, version)
    if system != 'Darwin' or actual_arch != arch or provenance.get('format_version') != 1:
        raise ValueError(f'Expected a verified thin Darwin {arch} core bundle')
    if len(provenance['files']) > 10000 or sum((root/e['path']).stat().st_size for e in provenance['files']) > 2*1024**3:
        raise ValueError('Core bundle exceeds file/byte budget')
    for key, value in PINNED.items():
        if provenance.get(key) != value:
            raise ValueError(f'Unexpected pinned dependency identity: {key}')
    identity(root, provenance)
    hlr = json.loads((root / HLR).read_text())
    if hlr.get('format_version') != 1 or hlr.get('occt_version') != '8.0.1' or hlr.get('modification') != 'agentcad-hlr-midpoint-v2' or not any(b.get('sha256') == provenance.get('occt_hlr_sdk_sha256') for b in hlr.get('binaries', [])):
        raise ValueError('Original patched SDK HLR identity is missing')
    required = ['LICENSE_LGPL_21.txt', 'OCCT_LGPL_EXCEPTION.txt', 'PatchOcctHlr.cmake', 'OCCT-HLR-PATCH.md',
                *('modified/'+s for s in ('HLRBRep_Intersector.cxx', 'HLRBRep_Intersector.hxx', 'HLRBRep_Data.cxx', 'HLRBRep_Data.hxx', 'HLRBRep_Hider.cxx'))]
    if any(not (root/'share/agent-3d-cad/notices/occt'/p).is_file() for p in required):
        raise ValueError('Original patched SDK notices/modified sources are incomplete')
    native = {}
    for entry in provenance['files']:
        path = entry['path']; file = root/path
        if path == binary or file.suffix == '.dylib':
            slices = native_slices(file)
            if set(slices) != {arch}:
                raise ValueError('Input native image contains an inconsistent slice set')
            if slices[arch]['filetype'] != (2 if path == binary else 6):
                raise ValueError('Native executable/library role differs from bundle path')
            native[path] = slices[arch]
    if not native or len(native) > 512:
        raise ValueError('Native image count exceeds composition budget')
    if provenance['build_identity']['cache_identity'].encode() not in (root/binary).read_bytes():
        raise ValueError('Native executable does not contain its recorded cache identity')
    return provenance, native, hlr


def compose(arm64, x64, destination, version):
    roots = {'arm64': Path(arm64).resolve(), 'x64': Path(x64).resolve()}
    requested = Path(destination).absolute()
    if requested.exists() or requested.is_symlink() or roots['arm64'] == roots['x64']:
        raise ValueError('Composition needs distinct inputs and a new output directory')
    destination = requested.resolve(strict=False)
    if any(destination.is_relative_to(r) or r.is_relative_to(destination) for r in roots.values()):
        raise ValueError('Composition output cannot overlap an input')
    inputs = {a: input_bundle(roots[a], a, version) for a in ARCHES}
    first = inputs['arm64'][0]
    common = lambda p: {k: v for k, v in p.items() if k not in PER_SLICE}
    if common(first) != common(inputs['x64'][0]):
        raise ValueError('Thin bundles have inconsistent production source or common provenance')
    paths = {a: {e['path']:e['sha256'] for e in inputs[a][0]['files']} for a in ARCHES}
    native_paths = set(inputs['arm64'][1])
    if set(paths['arm64']) != set(paths['x64']) or native_paths != set(inputs['x64'][1]):
        raise ValueError('Thin bundles have different resource/native image inventories')
    if {k:v for k,v in inputs['arm64'][2].items() if k != 'binaries'} != {k:v for k,v in inputs['x64'][2].items() if k != 'binaries'}:
        raise ValueError('Thin bundles have inconsistent modified SDK metadata')
    for path in paths['arm64']:
        if (roots['arm64']/path).stat().st_mode & 0o777 != (roots['x64']/path).stat().st_mode & 0o777:
            raise ValueError(f'Thin resource permissions differ: {path}')
        if path not in native_paths and path != HLR.as_posix() and paths['arm64'][path] != paths['x64'][path]:
            raise ValueError(f'Architecture-independent resource differs: {path}')
    # Preserve each trusted input and publish no partial output on failure.
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.universal-', dir=destination.parent) as temporary:
        stage = Path(temporary)/'bundle'
        shutil.copytree(roots['arm64'], stage, symlinks=False)
        composition = {'format_version':1, 'method':'lipo', 'inputs':[], 'native_files':[]}
        combined_hlr = {k:copy.deepcopy(v) for k,v in inputs['arm64'][2].items() if k != 'binaries'}
        combined_hlr['binaries'] = []
        for arch in ARCHES:
            saved = Path('share/agent-3d-cad/universal-inputs')/arch
            (stage/saved).mkdir(parents=True)
            for source, name in ((MANIFEST,'provenance.json'),(HLR,HLR.name)):
                shutil.copyfile(roots[arch]/source, stage/saved/name)
            composition['inputs'].append({'architecture':arch,'provenance':(saved/'provenance.json').as_posix(),
                'provenance_sha256':sha(stage/saved/'provenance.json'),'hlr_manifest':(saved/HLR.name).as_posix(),
                'hlr_manifest_sha256':sha(stage/saved/HLR.name)})
            for record in inputs[arch][2]['binaries']:
                combined_hlr['binaries'].append({**record, 'architecture':arch})
        (stage/HLR).write_text(json.dumps(combined_hlr,indent=2)+'\n')
        for path in sorted(native_paths):
            for arch in ARCHES:
                command('/usr/bin/codesign','--verify','--strict',str(roots[arch]/path))
            output = stage/path; output.unlink()
            command('/usr/bin/lipo','-create',str(roots['arm64']/path),str(roots['x64']/path),'-output',str(output))
            output.chmod((roots['arm64']/path).stat().st_mode & 0o777)
            command('/usr/bin/codesign','--force','--sign','-','--timestamp=none',str(output))
            command('/usr/bin/codesign','--verify','--strict',str(output))
            composed = native_slices(output)
            if set(composed) != set(ARCHES):
                raise ValueError('Composed image lacks both expected slices')
            record = {'path':path,'slices':[]}
            for arch in ARCHES:
                before, after = inputs[arch][1][path], composed[arch]
                if any(before[k] != after[k] for k in METADATA):
                    raise ValueError(f'Composition changed native load metadata: {path} {arch}')
                hashes = {}
                for kind in ('section_payload_sha256','signing_payload_sha256'):
                    if before[kind] != after[kind]:
                        raise ValueError(f'Composition changed unsigned native payload: {path} {arch}')
                    hashes['input_'+kind], hashes[kind] = before[kind], after[kind]
                if path == 'bin/agent-3d-cad':
                    with output.open('rb') as stream:
                        stream.seek(after['offset']); data=stream.read(after['size'])
                    if inputs[arch][0]['build_identity']['cache_identity'].encode() not in data:
                        raise ValueError('Composition changed an embedded native cache identity')
                record['slices'].append({'architecture':arch,'input_sha256':paths[arch][path],
                                        'sha256':after['sha256'],**hashes})
            composition['native_files'].append(record)
        provenance = common(first)
        provenance.update(format_version=2,architecture='universal',composition=composition,
            minimum_macos=max((m['minimum_macos'] for a in ARCHES for m in inputs[a][1].values()),key=lambda v:tuple(map(int,v.split('.')))))
        provenance['files'] = [{'path':p.relative_to(stage).as_posix(),'sha256':sha(p)} for p in sorted(stage.rglob('*')) if p.is_file() and p.relative_to(stage)!=MANIFEST]
        (stage/MANIFEST).write_text(json.dumps(provenance,indent=2)+'\n')
        validate_bundle(stage,version)
        os.rename(stage,destination)
    return destination


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    version=(Path(__file__).resolve().parent.parent/'VERSION').read_text().strip()
    try:
        print(compose(*sys.argv[1:],version))
    except (ValueError,OSError,KeyError,subprocess.SubprocessError) as error:
        raise SystemExit(str(error)) from error

if __name__ == '__main__':
    main()
