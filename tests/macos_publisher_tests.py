"""Offline developer-tool tests using real tiny Mach-O fixtures, not publisher proof.

No Developer ID credentials, Apple network, credential creation, CAD runtime,
host installation, or success claim for actual publisher signing/notarization.
"""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import platform
import subprocess
import sys
import tempfile
import uuid
from unittest.mock import patch

repo = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(repo / 'packaging'))
import bundle_validation as validation
spec = importlib.util.spec_from_file_location('publisher', repo / 'packaging/macos-publisher.py')
publisher = importlib.util.module_from_spec(spec); spec.loader.exec_module(publisher)
composer = publisher.composer
checks = 0


def check(value, message):
    global checks
    assert value, message
    checks += 1


def fails(function, fragment):
    try:
        function()
    except (ValueError, KeyError, OSError) as error:
        check(fragment in str(error), (fragment, str(error)))
    else:
        raise AssertionError('Expected failure: ' + fragment)


def run(*args):
    result = subprocess.run(args, capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, result.stderr
    return result.stdout


def digest(data):
    return hashlib.sha256(data).hexdigest()


def fixture(root, arch, source_hash, source_manifest, clang, sdk):
    machine = publisher.MACHINE[arch]
    (root / 'bin').mkdir(parents=True); (root / 'lib').mkdir()
    src = root.parent / (arch + '.c')
    src.write_text('int fixture(void) {return 0;}\n')
    library = root / 'lib/libTKHLR.dylib'
    run(clang, '-isysroot', sdk, '-arch', machine, '-mmacosx-version-min=15.0', '-dynamiclib', str(src),
        '-Wl,-install_name,@rpath/libTKHLR.dylib', '-o', str(library))
    cache = ('a' if arch == 'arm64' else 'b') * 64 + '-Release'
    src.write_text('extern int fixture(void); const char cache[]="' + cache + '"; int main(void){return fixture()+cache[0]-97;}\n')
    executable = root / 'bin/agent-3d-cad'
    run(clang, '-isysroot', sdk, '-arch', machine, '-mmacosx-version-min=15.0', str(src), '-L' + str(root / 'lib'),
        '-lTKHLR', '-Wl,-rpath,@executable_path/../lib', '-o', str(executable))
    for file in (library, executable):
        run('/usr/bin/codesign', '--force', '--sign', '-', '--timestamp=none', str(file))
    (root / composer.SOURCE).parent.mkdir(parents=True)
    (root / composer.SOURCE).write_bytes(source_manifest)
    notices = root / composer.HLR.parent; notices.mkdir(parents=True)
    for name in ['LICENSE_LGPL_21.txt', 'OCCT_LGPL_EXCEPTION.txt', 'PatchOcctHlr.cmake', 'OCCT-HLR-PATCH.md',
                 *('modified/' + s for s in ('HLRBRep_Intersector.cxx', 'HLRBRep_Intersector.hxx', 'HLRBRep_Data.cxx', 'HLRBRep_Data.hxx', 'HLRBRep_Hider.cxx'))]:
        (notices / name).parent.mkdir(parents=True, exist_ok=True)
        (notices / name).write_text('Independent test fixture only: ' + name + '\n')
    library_hash = validation.sha(library)
    publisher.write_json(root / composer.HLR, dict(format_version=1, occt_version='8.0.1', modification='agentcad-hlr-midpoint-v2',
                        binaries=[dict(path='lib/libTKHLR.dylib', sha256=library_hash)]))
    provenance = {**composer.PINNED, 'format_version': 1, 'project_version': publisher.VERSION, 'system': 'Darwin',
        'architecture': arch, 'compiler': 'fixture', 'occt_hlr_sdk_sha256': library_hash, 'minimum_macos': '15.0.0',
        'source_identity': dict(format_version=1, algorithm='sha256-file-list-v1', sha256=source_hash, manifest=composer.SOURCE.as_posix()),
        'build_identity': dict(format_version=1, source_sha256=source_hash, executable_sha256=validation.sha(executable), cache_identity=cache)}
    publisher.refresh_inventory(root, provenance, {})
    return provenance


# Only classification logic is exercised with invented Apple responses. This
# cannot create an acceptance file and is never publisher/notarization evidence.
submission = str(uuid.uuid4()); archive_hash = 'c' * 64
info = {'id': submission, 'status': 'Accepted'}
log = {'jobId': submission.upper(), 'status': 'Accepted', 'sha256': archive_hash, 'issues': None}
publisher.accepted_log(info, log, submission, archive_hash); check(True, 'Consistent response classification')
for altered in [dict(log, jobId=str(uuid.uuid4())), dict(log, status='Invalid'), dict(log, sha256='d' * 64),
                dict(log, issues=[{'severity': 'error'}])]:
    fails(lambda: publisher.accepted_log(info, altered, submission, archive_hash), 'Apple')
fails(lambda: publisher.accepted_log(dict(info, status='In Progress'), log, submission, archive_hash), 'Accepted')
check(publisher.code_identifier('com.example.fixture', 'bin/agent-3d-cad') == 'com.example.fixture.native', 'Explicit owned identifier')
fails(lambda: publisher.code_identifier('unqualified', 'bin/agent-3d-cad'), 'reverse-DNS')

if platform.system() != 'Darwin':
    print(f'{checks} pure publisher contract checks passed; real Mach-O checks require macOS')
    raise SystemExit(0)

with tempfile.TemporaryDirectory(prefix='cad-publisher-fixture-') as temporary:
    root = Path(temporary)
    clang = run('/usr/bin/xcrun', '--find', 'clang').strip()
    sdk = run('/usr/bin/xcrun', '--sdk', 'macosx', '--show-sdk-path').strip()
    source_manifest = ''.join(digest(data) + '  ' + path + '\n' for path, data in
                             [('CMakeLists.txt', b'fixture\n'), ('VERSION', (publisher.VERSION + '\n').encode())]).encode()
    source_hash = digest(source_manifest)
    stage, destination = root / 'atomic-stage', root / 'atomic-output'
    stage.mkdir(); (stage / 'proof').write_text('unchanged')
    publisher.publish_directory(stage, destination)
    check(not stage.exists() and (destination / 'proof').read_text() == 'unchanged', 'Real atomic exclusive publication')
    stage.mkdir(); (stage / 'preserved').write_text('second')
    fails(lambda: publisher.publish_directory(stage, destination), 'exclusively')
    check((stage / 'preserved').exists() and (destination / 'proof').read_text() == 'unchanged', 'Existing output is never replaced')
    empty = root / 'empty-destination'; empty.mkdir()
    fails(lambda: publisher.publish_directory(stage, empty), 'exclusively')
    check(empty.is_dir() and (stage / 'preserved').exists(), 'Empty racing destination is never replaced')
    # Exercise an unknown upload outcome without a network call. No simulated
    # success/Accepted file is produced; exactly one invocation is attempted.
    prepared = root / 'prepared-capture-fixture'; prepared.mkdir()
    original_zip = prepared / 'notary-submission.zip'; original_zip.write_bytes(b'Offline upload-capture fixture')
    approved_hash = validation.sha(original_zip)
    unknown = root / 'unknown-upload'
    timeout = subprocess.TimeoutExpired('notarytool', 300, output=b'partial response', stderr=b'interrupted')
    with patch.object(publisher, 'prepared', return_value={}), patch.object(publisher.subprocess, 'run', side_effect=timeout) as invoked:
        fails(lambda: publisher.submit(prepared, unknown, approved_hash, 'fixture-profile'), 'outcome is unknown')
        check(invoked.call_count == 1, 'Unknown upload never retries automatically')
        check(invoked.call_args.args[0][3] == str(unknown.resolve() / 'notary-submission.zip'), 'Upload uses only the independently captured attempt ZIP')
    check((unknown / 'attempt.json').exists() and not (unknown / 'submission.json').exists() and not (unknown / 'accepted.json').exists(), 'Unknown upload retains attempt without acceptance')
    check((unknown / 'submit.stdout.json').read_bytes() == b'partial response' and (unknown / 'submit.stderr.txt').read_bytes() == b'interrupted', 'Unknown upload retains partial tool evidence')
    check(validation.sha(unknown / 'notary-submission.zip') == approved_hash and not ((unknown / 'notary-submission.zip').stat().st_mode & 0o222), 'Exact captured bytes remain read-only for recovery')
    original_zip.write_bytes(b'changed after validation')
    with patch.object(publisher, 'prepared', return_value={}), patch.object(publisher.subprocess, 'run') as invoked:
        fails(lambda: publisher.submit(prepared, root / 'copy-mismatch', approved_hash, 'fixture-profile'), 'no upload attempted')
        check(invoked.call_count == 0, 'Preflight/copy race never reaches upload')
    original_zip.write_bytes(b'Offline upload-capture fixture')
    def changed_during_upload(argv, **kwargs):
        uploaded = Path(argv[3]); uploaded.chmod(0o644); uploaded.write_bytes(b'mutated captured copy')
        return subprocess.CompletedProcess(argv, 1, stdout='failure fixture', stderr='no network used')
    with patch.object(publisher, 'prepared', return_value={}), patch.object(publisher.subprocess, 'run', side_effect=changed_during_upload) as invoked:
        fails(lambda: publisher.submit(prepared, root / 'post-upload-mutation', approved_hash, 'fixture-profile'), 'changed during upload')
        check(invoked.call_count == 1 and not (root / 'post-upload-mutation/submission.json').exists(), 'Changed upload capture cannot produce a submitted/Accepted record')
    arm, intel = root / 'arm64', root / 'x64'
    fixture(arm, 'arm64', source_hash, source_manifest, clang, sdk)
    fixture(intel, 'x64', source_hash, source_manifest, clang, sdk)
    universal = root / 'universal'; composer.compose(arm, intel, universal, publisher.VERSION)
    for core in (arm, intel, universal):
        provenance, native = publisher.core(core, validation.sha(core / validation.MANIFEST), source_hash)
        check(len(native) == 2, 'Actual thin/fat signed fixture admitted')
        fails(lambda: publisher.verify_developer_id(core / 'bin/agent-3d-cad', 'ABCDEFGHIJ'), 'failed to satisfy')
        before = publisher.snapshot(core)
        manifest_hash = validation.sha(core / validation.MANIFEST)
        fails(lambda: publisher.core(core, '0' * 64, source_hash), 'approved core')
        fails(lambda: publisher.core(core, manifest_hash, '0' * 64), 'approved source')
        fails(lambda: publisher.sign(core, root / 'failed', manifest_hash, source_hash, '0' * 40, 'ABCDEFGHIJ', 'com.example.fixture'), 'not present')
        check(not (root / 'failed').exists() and before == publisher.snapshot(core), 'Missing credential leaves input/output unchanged')
        fails(lambda: publisher.new_destination(core / 'nested', core), 'overlap')
        alias = root / 'alias'; alias.symlink_to(core, target_is_directory=True)
        fails(lambda: publisher.new_destination(alias / 'nested', core), 'overlap'); alias.unlink()
        fails(lambda: publisher.new_destination(core, core), 'new directory')
        result = subprocess.run([sys.executable, str(repo / 'packaging/macos-publisher.py'), 'sign', str(core), str(root / 'blocked'),
            '--input-manifest-sha256', manifest_hash, '--source-sha256', source_hash, '--identity-sha1', '0' * 40,
            '--team-id', 'ABCDEFGHIJ', '--identifier-prefix', 'com.example.fixture'], capture_output=True, text=True)
        check(result.returncode != 0 and '--allow-apple-network' in result.stderr and not (root / 'blocked').exists(), 'Explicit timestamp-network boundary')
        # A real resource can change after the approved input has validated but
        # before the first snapshot. Reject that captured copy before any signer
        # call; an identity-lookup stub is not successful publisher evidence.
        resource = core / composer.HLR.parent / 'OCCT-HLR-PATCH.md'
        resource_bytes = resource.read_bytes()
        original_manifest = (core / validation.MANIFEST).read_bytes()
        normal_core, normal_command = publisher.core, publisher.command
        for rewrite_manifest in ([False, True] if core != universal else [False]):
            mutated, signer_calls = [], []
            def mutate_after_validation(bundle, *args, **kwargs):
                checked = normal_core(bundle, *args, **kwargs)
                if Path(bundle).resolve() == core.resolve() and not mutated:
                    resource.write_bytes(resource_bytes + b'Changed after approved input validation.\n')
                    if rewrite_manifest:
                        changed = json.loads(original_manifest)
                        next(item for item in changed['files'] if item['path'] == resource.relative_to(core).as_posix())['sha256'] = validation.sha(resource)
                        publisher.write_json(core / validation.MANIFEST, changed)
                    mutated.append(True)
                return checked
            def reject_signer(*args, **kwargs):
                if args[:2] == ('/usr/bin/codesign', '--force'):
                    signer_calls.append(args)
                    raise AssertionError('Unapproved captured bytes reached the signer')
                return normal_command(*args, **kwargs)
            try:
                with patch.object(publisher, 'core', side_effect=mutate_after_validation), \
                     patch.object(publisher, 'selected_identity', return_value='offline lookup only'), \
                     patch.object(publisher, 'command', side_effect=reject_signer):
                    fails(lambda: publisher.sign(core, root / 'capture-rejected', manifest_hash, source_hash,
                          '0' * 40, 'ABCDEFGHIJ', 'com.example.fixture'), 'approved core' if rewrite_manifest else 'integrity failure')
                check(mutated == [True] and not signer_calls, 'Changed captured resource rejected before every signer call')
                check(not (root / 'capture-rejected').exists() and not list(root.glob('.publisher-*')), 'Rejected capture publishes nothing and removes staging')
            finally:
                resource.write_bytes(resource_bytes)
                (core / validation.MANIFEST).write_bytes(original_manifest)
            check(publisher.snapshot(core) == before, 'Capture regression restores exact fixture bytes and modes')
        # A real offline ad-hoc replacement is not acceptable publisher signing.
        # Bypass only the preflight identity lookup, never its signature audit.
        normal = publisher.command
        def adhoc_only(*args, **kwargs):
            if args[:2] == ('/usr/bin/codesign', '--force'):
                return normal('/usr/bin/codesign', '--force', '--sign', '-', '--timestamp=none', '--options', 'runtime',
                              '--identifier', args[args.index('--identifier') + 1], args[-1])
            return normal(*args, **kwargs)
        with patch.object(publisher, 'selected_identity', return_value='test-only lookup'), patch.object(publisher, 'command', side_effect=adhoc_only):
            fails(lambda: publisher.sign(core, root / 'adhoc-rejected', manifest_hash, source_hash, '0' * 40, 'ABCDEFGHIJ', 'com.example.fixture'), 'exact selected certificate')
        check(not (root / 'adhoc-rejected').exists() and publisher.snapshot(core) == before, 'Failed actual signature audit publishes nothing and preserves source')
        check(not list(root.glob('.publisher-*')), 'Staging removed after failure')
        # Test payload/loader preservation using actual codesign, independently
        # of any assertion that a Developer ID identity exists on this machine.
        for path, slices in native.items():
            run('/usr/bin/codesign', '--force', '--sign', '-', '--timestamp=none', '--options', 'runtime',
                '--identifier', publisher.code_identifier('com.example.fixture', path), str(core / path))
            after = validation.native_slices(core / path)
            publisher.preserve_native(slices, after, path); check(True, 'Real re-sign preserves unsigned slices and loaders')
            wrong = copy.deepcopy(after); first = next(iter(wrong)); wrong[first]['dependencies'].append({'name': '/tmp/unbundled.dylib'})
            fails(lambda: publisher.preserve_native(slices, wrong, path), 'loader metadata')
            wrong = copy.deepcopy(after); wrong[first]['section_payload_sha256'] = '0' * 64
            fails(lambda: publisher.preserve_native(slices, wrong, path), 'unsigned native')
            native[path] = after
        publisher.refresh_inventory(core, provenance, native)
        after, _ = publisher.core(core, source_hash=source_hash)
        check(after.get('build_identity') == provenance.get('build_identity') and after['source_identity']['sha256'] == source_hash, 'Build/cache/source identity retained after signature-only refresh')
        # Inventory rehashing cannot conceal an altered native cache string.
        executable = core / 'bin/agent-3d-cad'; original = executable.read_bytes()
        manifest_bytes = (core / validation.MANIFEST).read_bytes()
        marker = (b'a' if b'a' * 64 + b'-Release' in original else b'b') * 64 + b'-Release'
        executable.write_bytes(original.replace(marker, b'z' * 64 + b'-Release'))
        fails(lambda: publisher.core(core), 'integrity failure')
        run('/usr/bin/codesign', '--force', '--sign', '-', '--timestamp=none', str(executable))
        changed_native = {p: validation.native_slices(core / p) for p in native}
        publisher.refresh_inventory(core, copy.deepcopy(provenance), changed_native)
        fails(lambda: publisher.core(core), 'unsigned native payload' if core == universal else 'cache identity')
        executable.write_bytes(original); (core / validation.MANIFEST).write_bytes(manifest_bytes)
        publisher.core(core); check(True, 'Valid signature-only fixture remains independently valid')
    # A mutated resource is rejected even when all signatures still verify.
    damaged = arm / composer.SOURCE; damaged.write_bytes(damaged.read_bytes() + b'changed')
    fails(lambda: publisher.core(arm), 'integrity failure')
print(f'{checks} offline publisher preparation checks passed; real ad-hoc Mach-O fixtures, no publisher signing or Apple acceptance evidence')
