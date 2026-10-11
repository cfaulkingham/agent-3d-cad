"""Stage complete native Linux build inputs without Docker or downloads.

Synthetic archives exercise copying only: Docker's pinned SHA-256 verification
remains authoritative before extraction. Optional --native-sdk and --json-source
also configure the staged source offline with already installed dependencies.
"""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--native-sdk', type=Path)
parser.add_argument('--json-source', type=Path)
args = parser.parse_args()
if bool(args.native_sdk) != bool(args.json_source):
    parser.error('--native-sdk and --json-source must be provided together')
repo = Path(__file__).resolve().parents[1]
checks = 0


def check(value, message):
    global checks
    checks += 1
    assert value, message


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


with tempfile.TemporaryDirectory(prefix='cad-linux-stage-') as temporary:
    root = Path(temporary).resolve()
    source = root / 'source checkout with spaces'
    source.mkdir()
    tracked = subprocess.check_output(['git', '-C', str(repo), 'ls-files', '-z'], text=True).split('\0')
    for relative in filter(None, tracked):
        original = repo / relative
        if original.is_file():
            target = source / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(original, target)
    archives = ('OCCT-V8_0_1.tar.gz', 'freetype-2.14.3.tar.xz', 'json-3.12.0.tar.gz')
    for name in archives:
        path = source / '.deps' / name
        path.parent.mkdir(exist_ok=True)
        path.write_bytes(b'Non-buildable archive staging fixture: ' + name.encode())
    for relative in ('.git/private-marker', '.local/private-marker', '.deps/native/private-marker',
                     'build/private-marker', 'untracked-private-marker'):
        path = source / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('Must not enter the Linux source context.\n')
    stage = root / 'build context with spaces'

    def run(target):
        return subprocess.run(['cmake', '-DSOURCE_DIR=' + str(source), '-DSTAGE_DIR=' + str(target),
                               '-P', str(repo / 'packaging/stage-linux-validation.cmake')],
                              text=True, capture_output=True, timeout=30)

    result = run(stage)
    check(result.returncode == 0, result.stderr)
    staged = stage / 'source'
    required = ['VERSION', 'LICENSE', 'NOTICE', 'CMakeLists.txt', 'install.sh', 'install.ps1']
    # Independently inspect actual compiled source paths instead of mirroring
    # the staging script's top-level allowlist.
    required += re.findall(r'\b(?:src|third_party)/[A-Za-z0-9_./-]+\.cpp', (repo / 'CMakeLists.txt').read_text())
    required += ['third_party/tinyxml2/PROVENANCE.json', 'third_party/tinyxml2/tinyxml2.h',
                 'third_party/tinyxml2/LICENSE.txt', 'third_party/tinyxml2/readme.md',
                 'web/viewer.html', 'skills/native-cad/SKILL.md', 'packaging/THIRD_PARTY.md',
                 'cmake/dependencies/PatchOcctHlr.cmake', 'tests/fixtures/authoring-test.ttf',
                 'examples/curved-pipe.create.json']
    for relative in sorted(set(required)):
        check((staged / relative).is_file(), 'Missing native build/test input: ' + relative)
        check(digest(staged / relative) == digest(repo / relative), 'Changed source: ' + relative)
    for name in archives:
        check(digest(stage / 'archives' / name) == digest(source / '.deps' / name), 'Archive bytes changed')
    check(digest(stage / 'Dockerfile') == digest(repo / 'packaging/Dockerfile.linux-validation'),
          'Staging changed pinned archive verification or runtime build commands')
    check(not list(stage.rglob('private-marker')) and not (staged / 'untracked-private-marker').exists(),
          'Host SDK, local state or untracked private files entered the context')
    check(not (staged / '.deps').exists(), 'Host dependencies were copied into Linux source')
    (source / '.deps' / archives[0]).unlink()
    failed = run(root / 'missing archive')
    check(failed.returncode != 0 and 'Missing pinned archive' in failed.stderr,
          'Missing pinned source archive must fail explicitly')
    (source / '.deps' / archives[0]).write_bytes(b'Restored staging fixture')
    (source / 'VERSION').unlink()
    failed = run(root / 'missing version')
    check(failed.returncode != 0 and 'VERSION' in failed.stderr, 'Missing version must fail explicitly')
    if args.native_sdk:
        sdk = args.native_sdk.resolve()
        result = subprocess.run(['cmake', '-S', str(staged), '-B', str(root / 'offline configure'),
            '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_PREFIX_PATH=' + str(sdk),
            '-DOpenCASCADE_DIR=' + str(sdk / 'lib/cmake/opencascade'),
            '-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=' + str(args.json_source.resolve()),
            '-DFETCHCONTENT_FULLY_DISCONNECTED=ON'], text=True, capture_output=True, timeout=120)
        check(result.returncode == 0, result.stdout + result.stderr)
        check((root / 'offline configure/compile_commands.json').is_file(),
              'Offline configure must generate actual native build commands')
print(f'{checks} Linux source-staging checks passed; no Docker, dependency downloads or host SDK copying')
