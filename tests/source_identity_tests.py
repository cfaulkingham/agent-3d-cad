"""Offline production-byte and successful-link identity checks; no Git or SDK needed."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import tarfile

repo = Path(__file__).resolve().parent.parent
checks = 0

def sha(value):
    return hashlib.sha256(value).hexdigest()

def run(args, expected=None, tool='cmake'):
    global checks
    result = subprocess.run([tool, *args], capture_output=True, text=True, timeout=120)
    output = result.stdout + result.stderr
    assert (result.returncode == 0 if expected is None else result.returncode != 0 and expected in output), output
    checks += 1
    return result

with tempfile.TemporaryDirectory(prefix='cad-source-identity-') as temp:
    root = Path(temp); source = root/'source'; source.mkdir()
    for directory in ('src', 'include', 'web', 'cmake', 'third_party'):
        (source/directory).mkdir()
        (source/directory/'input.txt').write_bytes(('Exact '+directory+'\r\n').encode())
    (source/'CMakeLists.txt').write_text('project(fixture)\n')
    (source/'VERSION').write_text('0.1.0-preview.1\n')
    for helper in ('SourceIdentity.cmake', 'StampSource.cmake'):
        shutil.copyfile(repo/'cmake'/helper, source/'cmake'/helper)
    generated = root/'generated'; generated.mkdir()
    manifest = generated/'source-inputs.sha256'; settings = generated/'settings.json'; stamp = generated/'build-source.json'; exe = root/'native'
    exe.write_bytes(b'Linked native fixture bytes, not executed\n')
    collect = root/'collect.cmake'
    collect.write_text(f'include("{source}/cmake/SourceIdentity.cmake")\nagentcad_source_manifest("{source}" content)\nfile(WRITE "{manifest}" "${{content}}")\n')
    run(['-P', str(collect)])
    original = manifest.read_bytes(); digest = sha(original)
    inspection = generated/'inspection.sha256'
    run([f'-DSOURCE_ROOT={source}',f'-DOUTPUT_MANIFEST={inspection}','-P',str(source/'cmake/SourceIdentity.cmake')])
    assert inspection.read_bytes()==original; checks += 1
    records = original.decode().splitlines(); paths = [line[66:] for line in records]
    assert paths == sorted(paths) and len(paths) == len(set(paths)) and all(not path.startswith('/') for path in paths); checks += 1
    for line in records:
        assert sha((source/line[66:]).read_bytes()) == line[:64]; checks += 1
    settings.write_text(json.dumps(dict(format_version=1,source_sha256=digest,cache_identity='fixture-Release',compiler_id='Fixture',compiler_version='1',configuration='Release',system='Darwin',architecture='arm64',osx_architectures='arm64',deployment_target='15.0',cxx_flags='',configuration_flags='')))
    stamp_args = [f'-DSOURCE_ROOT={source}',f'-DINPUT_MANIFEST={manifest}',f'-DSETTINGS_FILE={settings}',f'-DEXECUTABLE={exe}',f'-DOUTPUT_FILE={stamp}','-P',str(source/'cmake/StampSource.cmake')]
    run(stamp_args)
    record = json.loads(stamp.read_text()); assert record['executable_sha256']==sha(exe.read_bytes()) and record['source_sha256']==digest; checks += 1
    verify = root/'verify.cmake'
    verify.write_text(f'include("{source}/cmake/SourceIdentity.cmake")\nagentcad_verify_source_stamp("{source}" "{manifest}" "{digest}" "{stamp}" "{exe}" result)\n')
    run(['-P',str(verify)])
    saved = (source/'src/input.txt').read_bytes(); (source/'src/input.txt').write_bytes(saved+b'changed')
    before = stamp.read_bytes(); run(stamp_args,'Production source changed during build'); assert stamp.read_bytes()==before; checks += 1
    run(['-P',str(verify)],'Production source changed after configure/build')
    (source/'src/input.txt').write_bytes(saved)
    # Reconfiguration updates the input list but cannot bless the old linked binary.
    (source/'src/new.cpp').write_text('new production bytes\n'); run(['-P',str(collect)])
    new_digest=sha(manifest.read_bytes()); text=verify.read_text().replace(digest,new_digest); verify.write_text(text)
    run(['-P',str(verify)],'differs from its successful build/source stamp')
    (source/'src/new.cpp').unlink(); manifest.write_bytes(original); verify.write_text(text.replace(new_digest,digest))
    binary=exe.read_bytes(); exe.write_bytes(b'substituted binary')
    run(['-P',str(verify)],'differs from its successful build/source stamp'); exe.write_bytes(binary)
    manifest.write_bytes(original+b'0'*64+b'  src/fabricated.cpp\n'); run(['-P',str(verify)],'Production source changed after configure/build'); manifest.write_bytes(original)
    # Neither Git metadata nor the absolute root enters the production identity.
    (source/'.git').mkdir(); (source/'.git/HEAD').write_text('irrelevant revision\n'); run(['-P',str(collect)]); assert manifest.read_bytes()==original; checks += 1
    relocated=root/'relocated'; shutil.copytree(source,relocated); collect.write_text(collect.read_text().replace(str(source),str(relocated))); run(['-P',str(collect)]); assert manifest.read_bytes()==original; checks += 1
    run(['-P',str(verify)])

# CPack runs the generated installation in an embedded CMake interpreter. It
# does not reliably set CMAKE_SCRIPT_MODE_FILE, so a -P-only fixture misses it.
# This NONE-language project packages labelled fixture bytes without a compiler,
# native SDK or runtime execution; it uses the actual install verifier and CPack.
with tempfile.TemporaryDirectory(prefix='cad-source-cpack-') as temp:
    root=Path(temp); source=root/'source'; source.mkdir(); build=root/'build'
    (source/'cmake').mkdir(); (source/'src').mkdir()
    (source/'VERSION').write_text('1.0.0\n')
    (source/'src/input.txt').write_text('Immutable production fixture input\n')
    for helper in ('SourceIdentity.cmake','StampSource.cmake'):
        shutil.copyfile(repo/'cmake'/helper,source/'cmake'/helper)
    (source/'cmake/InstallFixture.cmake.in').write_text(r"""
include("@CMAKE_CURRENT_SOURCE_DIR@/cmake/SourceIdentity.cmake")
agentcad_verify_source_stamp("@CMAKE_CURRENT_SOURCE_DIR@"
  "@CMAKE_CURRENT_BINARY_DIR@/source-inputs.sha256" "@source_hash@"
  "@CMAKE_CURRENT_BINARY_DIR@/build-source.json"
  "@CMAKE_CURRENT_BINARY_DIR@/not-a-native-executable.fixture" checked)
file(INSTALL "@CMAKE_CURRENT_BINARY_DIR@/source-inputs.sha256"
  DESTINATION "${CMAKE_INSTALL_PREFIX}/share/source-identity-fixture" TYPE FILE)
""")
    (source/'CMakeLists.txt').write_text(r"""
cmake_minimum_required(VERSION 3.24)
project(SourceIdentityFixture VERSION 1.0.0 LANGUAGES NONE)
include(cmake/SourceIdentity.cmake)
agentcad_source_manifest("${CMAKE_CURRENT_SOURCE_DIR}" inputs TRACK_CONFIGURE)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/source-inputs.sha256" "${inputs}")
string(SHA256 source_hash "${inputs}")
configure_file(cmake/InstallFixture.cmake.in InstallFixture.cmake @ONLY)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/InstallFixture.cmake")
set(CPACK_GENERATOR TGZ)
set(CPACK_PACKAGE_FILE_NAME source-identity-fixture)
include(CPack)
""")
    run(['-S',str(source),'-B',str(build)])
    manifest=build/'source-inputs.sha256'; original=manifest.read_bytes()
    settings=build/'settings.json'; exe=build/'not-a-native-executable.fixture'; stamp=build/'build-source.json'
    settings.write_text(json.dumps(dict(format_version=1,source_sha256=sha(original))))
    exe.write_bytes(b'Test payload only; never a native executable\n')
    run([f'-DSOURCE_ROOT={source}',f'-DINPUT_MANIFEST={manifest}',f'-DSETTINGS_FILE={settings}',f'-DEXECUTABLE={exe}',f'-DOUTPUT_FILE={stamp}','-P',str(source/'cmake/StampSource.cmake')])
    cpack=str(Path(shutil.which('cmake')).with_name('cpack'+('.exe' if Path(shutil.which('cmake')).suffix=='.exe' else '')))
    def package(name, expected=None):
        output=root/name
        run(['--config',str(build/'CPackConfig.cmake'),'-B',str(output)],expected,tool=cpack)
        return output/'source-identity-fixture.tar.gz'
    archive=package('accepted')
    with tarfile.open(archive) as package_file:
        items=[m for m in package_file.getmembers() if m.name.endswith('/source-inputs.sha256')]
        assert len(items)==1 and package_file.extractfile(items[0]).read()==original; checks+=1
    input_file=source/'src/input.txt'; before=input_file.read_bytes(); input_file.write_bytes(before+b'drift\n')
    assert not package('source-drift','differs from its successful build/source stamp').exists(); checks+=1
    input_file.write_bytes(before)
    before=exe.read_bytes(); exe.write_bytes(b'Substituted fixture payload\n')
    assert not package('binary-drift','differs from its successful build/source stamp').exists(); checks+=1
    exe.write_bytes(before)
    # The explicit configure caller still tracks additions and edits. These are
    # generator checks in the NONE-language fixture; no native target is built.
    added=source/'src/added.txt'; added.write_text('New source input\n')
    run(['--build',str(build)])
    updated=manifest.read_bytes(); assert updated!=original and b'  src/added.txt\n' in updated; checks+=1
    added.write_text('Changed source input\n');run(['--build',str(build)])
    assert manifest.read_bytes()!=updated;checks+=1
    assert not package('reconfigured-not-restamped','differs from its successful build/source stamp').exists();checks+=1

print(f'{checks} exact production source/build identity checks passed')
